//===- ModuleLibrary.cpp - ECMT2 Module Library Implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/YAMLTraits.h"
#include <fstream>
#include <sstream>

using namespace circt;
using namespace cmt2::ecmt2;

ModuleLibrary &ModuleLibrary::getInstance() {
  static ModuleLibrary instance;
  return instance;
}

bool ModuleLibrary::CacheKey::operator<(const CacheKey &other) const {
  if (moduleName != other.moduleName)
    return moduleName < other.moduleName;
  return params < other.params;
}

mlir::LogicalResult ModuleLibrary::loadManifest(llvm::StringRef path) {
  libraryBasePath_ = llvm::sys::path::parent_path(path).str();

  // Convert to absolute path if needed
  if (!llvm::sys::path::is_absolute(libraryBasePath_)) {
    llvm::SmallString<256> absPath;
    if (std::error_code ec = llvm::sys::fs::current_path(absPath)) {
      llvm::errs() << "Failed to get current path: " << ec.message() << "\n";
    } else {
      llvm::sys::path::append(absPath, libraryBasePath_);
      libraryBasePath_ = absPath.str().str();
    }
  }

  auto fileOrErr = llvm::MemoryBuffer::getFile(path);
  if (!fileOrErr) {
    llvm::errs() << "Failed to open manifest file: " << path << "\n";
    return mlir::failure();
  }

  // Simple YAML parsing - for production, use llvm::yaml::Input
  std::string content = fileOrErr.get()->getBuffer().str();

  // This is a simplified parser for the manifest format
  // For production code, use llvm::yaml::Input with proper traits

  // Parse "modules:" section
  size_t pos = content.find("modules:");
  if (pos == std::string::npos) {
    llvm::errs() << "No 'modules:' section found in manifest\n";
    return mlir::failure();
  }

  // Very basic parsing - just enough to get the reg module working
  // Look for "- name:" entries
  pos = content.find("- name:", pos);
  while (pos != std::string::npos) {
    ModuleInfo info;

    // Extract name
    size_t nameStart = content.find("\"", pos) + 1;
    size_t nameEnd = content.find("\"", nameStart);
    info.name = content.substr(nameStart, nameEnd - nameStart);

    // Extract type
    size_t typePos = content.find("type:", pos);
    size_t typeStart = content.find("\"", typePos) + 1;
    size_t typeEnd = content.find("\"", typeStart);
    std::string typeStr = content.substr(typeStart, typeEnd - typeStart);
    info.type = (typeStr == "static") ? ModuleInfo::Static : ModuleInfo::Chisel;

    // Extract path
    size_t pathPos = content.find("path:", typePos);
    size_t pathStart = content.find("\"", pathPos) + 1;
    size_t pathEnd = content.find("\"", pathStart);
    info.path = content.substr(pathStart, pathEnd - pathStart);

    // Extract description (optional)
    size_t descPos = content.find("description:", pathPos);
    if (descPos != std::string::npos && descPos < content.find("- name:", pos + 1)) {
      size_t descStart = content.find("\"", descPos) + 1;
      size_t descEnd = content.find("\"", descStart);
      info.description = content.substr(descStart, descEnd - descStart);
    }

    // Parse parameters section (for Chisel modules)
    size_t paramsPos = content.find("parameters:", pathPos);
    if (paramsPos != std::string::npos && paramsPos < content.find("- name:", pos + 1)) {
      // Find all parameter entries
      size_t paramPos = content.find("- name:", paramsPos);
      size_t nextModulePos = content.find("- name:", pos + 1);
      size_t buildSectionPos = content.find("build:", paramsPos);

      while (paramPos != std::string::npos && paramPos < buildSectionPos) {
        ModuleInfo::ParamInfo paramInfo;

        // Extract parameter name
        size_t pnameStart = content.find("\"", paramPos) + 1;
        size_t pnameEnd = content.find("\"", pnameStart);
        paramInfo.name = content.substr(pnameStart, pnameEnd - pnameStart);

        // Extract default value (optional)
        size_t defaultPos = content.find("default:", paramPos);
        if (defaultPos != std::string::npos && defaultPos < content.find("- name:", paramPos + 1)) {
          size_t defaultStart = defaultPos + 8; // Skip "default:"
          while (defaultStart < content.size() && std::isspace(content[defaultStart])) defaultStart++;
          size_t defaultEnd = defaultStart;
          while (defaultEnd < content.size() && std::isdigit(content[defaultEnd])) defaultEnd++;
          if (defaultEnd > defaultStart) {
            paramInfo.defaultValue = std::stoll(content.substr(defaultStart, defaultEnd - defaultStart));
          }
        }

        // Extract required flag (optional)
        size_t requiredPos = content.find("required:", paramPos);
        if (requiredPos != std::string::npos && requiredPos < content.find("- name:", paramPos + 1)) {
          paramInfo.required = content.find("true", requiredPos) != std::string::npos;
        } else {
          paramInfo.required = false;
        }

        info.parameters.push_back(paramInfo);

        // Find next parameter
        paramPos = content.find("- name:", pnameEnd);
        if (paramPos >= buildSectionPos) break;
      }
    }

    // For Chisel modules, extract build info
    if (info.type == ModuleInfo::Chisel) {
      ModuleInfo::BuildInfo buildInfo;

      size_t buildPos = content.find("build:", pathPos);
      if (buildPos != std::string::npos) {
        size_t cmdPos = content.find("command:", buildPos);
        size_t cmdStart = content.find("\"", cmdPos) + 1;
        size_t cmdEnd = content.find("\"", cmdStart);
        buildInfo.command = content.substr(cmdStart, cmdEnd - cmdStart);

        size_t outPos = content.find("output_pattern:", buildPos);
        size_t outStart = content.find("\"", outPos) + 1;
        size_t outEnd = content.find("\"", outStart);
        buildInfo.outputPattern = content.substr(outStart, outEnd - outStart);

        buildInfo.useCache = content.find("cache: true", buildPos) != std::string::npos;

        info.buildInfo = buildInfo;
      }
    }

    modules_[info.name] = info;

    // Find next module
    pos = content.find("- name:", nameEnd);
  }

  return mlir::success();
}

bool ModuleLibrary::hasModule(llvm::StringRef name) const {
  return modules_.find(name) != modules_.end();
}

std::optional<ModuleLibrary::ModuleInfo>
ModuleLibrary::getModuleInfo(llvm::StringRef name) const {
  auto it = modules_.find(name);
  if (it == modules_.end())
    return std::nullopt;
  return it->second;
}

std::string ModuleLibrary::substituteParams(
    const std::string &pattern,
    const llvm::StringMap<int64_t> &params) const {
  std::string result = pattern;

  // Replace ${param} with actual values
  for (const auto &param : params) {
    std::string placeholder = "${" + param.getKey().str() + "}";
    std::string value = std::to_string(param.getValue());

    size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
      result.replace(pos, placeholder.length(), value);
      pos += value.length();
    }
  }

  return result;
}

std::string ModuleLibrary::getCachePath(
    const std::string &moduleName,
    const llvm::StringMap<int64_t> &params) const {

  llvm::SmallString<256> cachePath(libraryBasePath_);
  llvm::sys::path::append(cachePath, "cache");

  // Build filename from module name and params
  std::string filename = moduleName;
  for (const auto &param : params) {
    filename += "_" + param.getKey().str() + std::to_string(param.getValue());
  }
  filename += ".mlir";

  llvm::sys::path::append(cachePath, filename);
  return cachePath.str().str();
}

mlir::LogicalResult ModuleLibrary::loadStaticModule(
    const ModuleInfo &info, mlir::MLIRContext &context,
    mlir::OwningOpRef<mlir::ModuleOp> &result) {

  llvm::SmallString<256> modulePath(libraryBasePath_);
  llvm::sys::path::append(modulePath, info.path);

  auto fileOrErr = llvm::MemoryBuffer::getFile(modulePath);
  if (!fileOrErr) {
    llvm::errs() << "Failed to open module file: " << modulePath << "\n";
    return mlir::failure();
  }

  // Ensure FIRRTL dialect is loaded
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  result = mlir::parseSourceString<mlir::ModuleOp>(
      fileOrErr.get()->getBuffer(), &context);

  if (!result) {
    llvm::errs() << "Failed to parse module: " << modulePath << "\n";
    return mlir::failure();
  }

  return mlir::success();
}

mlir::LogicalResult ModuleLibrary::buildChiselModule(
    const ModuleInfo &info, const llvm::StringMap<int64_t> &params,
    mlir::MLIRContext &context,
    mlir::OwningOpRef<mlir::ModuleOp> &result) {

  if (!info.buildInfo) {
    llvm::errs() << "No build info for Chisel module: " << info.name << "\n";
    return mlir::failure();
  }

  // Check cache first
  std::string cachePath = getCachePath(info.name, params);
  if (info.buildInfo->useCache && llvm::sys::fs::exists(cachePath)) {
    auto fileOrErr = llvm::MemoryBuffer::getFile(cachePath);
    if (fileOrErr) {
      result = mlir::parseSourceString<mlir::ModuleOp>(
          fileOrErr.get()->getBuffer(), &context);
      if (result)
        return mlir::success();
    }
  }

  // Build the module
  llvm::SmallString<256> buildDir(libraryBasePath_);
  llvm::sys::path::append(buildDir, info.path);

  // Construct build command - use bash to execute in the correct directory
  llvm::SmallString<256> buildScript(buildDir);
  llvm::sys::path::append(buildScript, info.buildInfo->command);

  // Create command to cd to directory and run script
  std::string bashCmd = "cd " + buildDir.str().str() + " && " + buildScript.str().str();

  // Debug: Print module info
  llvm::errs() << "DEBUG: Building module " << info.name << "\n";
  llvm::errs() << "DEBUG: Number of parameters in manifest: " << info.parameters.size() << "\n";
  llvm::errs() << "DEBUG: Number of parameters provided: " << params.size() << "\n";
  for (const auto &p : params) {
    llvm::errs() << "DEBUG: Provided param: " << p.getKey() << " = " << p.getValue() << "\n";
  }

  // Add parameters as named arguments (key=value style)
  for (const auto &paramInfo : info.parameters) {
    llvm::errs() << "DEBUG: Processing manifest param: " << paramInfo.name << "\n";

    auto it = params.find(paramInfo.name);
    int64_t value;

    if (it != params.end()) {
      // Use provided value
      value = it->getValue();
      llvm::errs() << "DEBUG: Using provided value: " << value << "\n";
    } else if (paramInfo.defaultValue) {
      // Use default value
      value = *paramInfo.defaultValue;
      llvm::errs() << "DEBUG: Using default value: " << value << "\n";
    } else if (paramInfo.required) {
      // Required parameter not provided
      llvm::errs() << "Required parameter " << paramInfo.name << " not provided\n";
      return mlir::failure();
    } else {
      // No value and not required - skip
      llvm::errs() << "DEBUG: Skipping optional param with no value\n";
      continue;
    }

    bashCmd += " " + paramInfo.name + "=" + std::to_string(value);
    llvm::errs() << "DEBUG: Added to command: " << paramInfo.name << "=" << value << "\n";
  }

  llvm::errs() << "DEBUG: Full command: " << bashCmd << "\n";

  llvm::SmallVector<llvm::StringRef, 8> args;
  args.push_back("/bin/bash");
  args.push_back("-c");
  args.push_back(bashCmd);

  // Execute build script
  std::string errMsg;
  int returnCode = llvm::sys::ExecuteAndWait(
      "/bin/bash", args, std::nullopt, {}, 0, 0, &errMsg);

  if (returnCode != 0) {
    llvm::errs() << "Build failed for " << info.name << ": " << errMsg << "\n";
    return mlir::failure();
  }

  // Determine output file path
  // Create complete parameter map (merge provided params with defaults)
  llvm::StringMap<int64_t> completeParams = params;
  for (const auto &paramInfo : info.parameters) {
    if (completeParams.find(paramInfo.name) == completeParams.end() && paramInfo.defaultValue) {
      completeParams[paramInfo.name] = *paramInfo.defaultValue;
    }
  }

  std::string outputPattern = substituteParams(info.buildInfo->outputPattern, completeParams);
  llvm::SmallString<256> outputFile(buildDir);
  llvm::sys::path::append(outputFile, outputPattern);

  // Load the generated module
  auto fileOrErr = llvm::MemoryBuffer::getFile(outputFile);
  if (!fileOrErr) {
    llvm::errs() << "Failed to read build output: " << outputFile << "\n";
    return mlir::failure();
  }

  // Ensure FIRRTL dialect is loaded
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  result = mlir::parseSourceString<mlir::ModuleOp>(
      fileOrErr.get()->getBuffer(), &context);

  if (!result) {
    llvm::errs() << "Failed to parse build output: " << outputFile << "\n";
    return mlir::failure();
  }

  // Cache the result
  if (info.buildInfo->useCache) {
    llvm::SmallString<256> cacheDir(libraryBasePath_);
    llvm::sys::path::append(cacheDir, "cache");
    llvm::sys::fs::create_directories(cacheDir);

    std::error_code ec = llvm::sys::fs::copy_file(outputFile, cachePath);
    if (ec) {
      llvm::errs() << "Warning: Failed to cache module: " << ec.message() << "\n";
    }
  }

  return mlir::success();
}

mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
ModuleLibrary::loadModule(llvm::StringRef name,
                          const llvm::StringMap<int64_t> &params,
                          mlir::MLIRContext &context) {

  auto infoOpt = getModuleInfo(name);
  if (!infoOpt) {
    llvm::errs() << "Module not found in library: " << name << "\n";
    return mlir::failure();
  }

  mlir::OwningOpRef<mlir::ModuleOp> result;

  if (infoOpt->type == ModuleInfo::Static) {
    if (mlir::failed(loadStaticModule(*infoOpt, context, result)))
      return mlir::failure();
  } else {
    if (mlir::failed(buildChiselModule(*infoOpt, params, context, result)))
      return mlir::failure();
  }

  return result;
}

mlir::LogicalResult ModuleLibrary::insertModuleIntoCircuit(
    llvm::StringRef name, const llvm::StringMap<int64_t> &params,
    mlir::OpBuilder &builder, mlir::Location loc,
    std::string &actualModuleName) {

  auto moduleOpResult = loadModule(name, params, *builder.getContext());
  if (mlir::failed(moduleOpResult))
    return mlir::failure();

  auto &loadedModule = *moduleOpResult;

  // Extract the firrtl.circuit from the loaded module
  circt::firrtl::CircuitOp loadedCircuit;
  loadedModule->walk([&](circt::firrtl::CircuitOp circuit) {
    loadedCircuit = circuit;
    return mlir::WalkResult::interrupt();
  });

  if (!loadedCircuit) {
    llvm::errs() << "No firrtl.circuit found in loaded module\n";
    return mlir::failure();
  }

  // Extract the actual FIRRTL module name (with parameters)
  // This will be returned to the caller
  for (auto &op : loadedCircuit.getBodyBlock()->getOperations()) {
    if (auto firrtlMod = mlir::dyn_cast<circt::firrtl::FModuleOp>(op)) {
      actualModuleName = firrtlMod.getModuleName().str();
      break;
    }
  }

  // Find or create a firrtl.circuit to insert the modules into
  // The builder's insertion point should be at the top-level module body
  mlir::Operation *insertPoint = builder.getInsertionBlock()->getParentOp();
  circt::firrtl::CircuitOp targetCircuit;

  // Walk the parent to find an existing firrtl.circuit
  if (auto parentModule = mlir::dyn_cast<mlir::ModuleOp>(insertPoint)) {
    parentModule.walk([&](circt::firrtl::CircuitOp circuit) {
      if (!targetCircuit) {
        targetCircuit = circuit;
        return mlir::WalkResult::interrupt();
      }
      return mlir::WalkResult::advance();
    });
  }

  // If no circuit exists, create one with a placeholder name
  // (the Cmt2ToFIRRTL pass will create/update the circuit later)
  if (!targetCircuit) {
    auto savedIP = builder.saveInsertionPoint();
    // Get the first FModuleOp name from the loaded circuit as the circuit name
    llvm::StringRef circuitName = "Library";
    for (auto &op : loadedCircuit.getBodyBlock()->getOperations()) {
      if (auto firrtlMod = mlir::dyn_cast<circt::firrtl::FModuleOp>(op)) {
        circuitName = firrtlMod.getModuleName();
        break;
      }
    }

    targetCircuit = builder.create<circt::firrtl::CircuitOp>(
        loc, builder.getStringAttr(circuitName));
    builder.restoreInsertionPoint(savedIP);
  }

  // Now insert the firrtl.module operations into the target circuit
  auto savedIP = builder.saveInsertionPoint();
  builder.setInsertionPointToEnd(targetCircuit.getBodyBlock());

  for (auto &op : loadedCircuit.getBodyBlock()->getOperations()) {
    if (auto firrtlMod = mlir::dyn_cast<circt::firrtl::FModuleOp>(op)) {
      // Clone the module and insert it into the firrtl.circuit
      builder.clone(op);
    }
  }

  builder.restoreInsertionPoint(savedIP);

  return mlir::success();
}
