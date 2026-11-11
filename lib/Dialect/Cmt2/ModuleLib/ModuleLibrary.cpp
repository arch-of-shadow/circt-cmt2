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
  // Idempotent check - skip if already loaded from same path
  std::string parentPath = llvm::sys::path::parent_path(path).str();
  if (!modules_.empty() && !libraryBasePath_.empty()) {
    // Already loaded - skip re-loading
    return mlir::success();
  }

  libraryBasePath_ = parentPath;

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

  // Use LLVM YAML streaming parser for proper parsing
  llvm::SourceMgr srcMgr;
  srcMgr.AddNewSourceBuffer(std::move(fileOrErr.get()), llvm::SMLoc());

  llvm::yaml::Stream yamlStream(srcMgr.getMemoryBuffer(1)->getBuffer(), srcMgr);

  llvm::yaml::document_iterator docIt = yamlStream.begin();
  if (docIt == yamlStream.end()) {
    llvm::errs() << "Empty YAML document\n";
    return mlir::failure();
  }

  llvm::yaml::Node *root = docIt->getRoot();
  if (!root) {
    llvm::errs() << "Invalid YAML root\n";
    return mlir::failure();
  }

  auto *rootMap = llvm::dyn_cast<llvm::yaml::MappingNode>(root);
  if (!rootMap) {
    llvm::errs() << "YAML root is not a mapping\n";
    return mlir::failure();
  }

  // Helper to get scalar value
  auto getScalar = [](llvm::yaml::Node *node) -> std::string {
    if (auto *scalar = llvm::dyn_cast_or_null<llvm::yaml::ScalarNode>(node)) {
      llvm::SmallString<64> storage;
      return scalar->getValue(storage).str();
    }
    return "";
  };

  // Helper to get boolean
  auto getBool = [&getScalar](llvm::yaml::Node *node) -> bool {
    std::string val = getScalar(node);
    return val == "true" || val == "yes" || val == "1";
  };

  // Helper to get integer (no exceptions allowed in LLVM)
  auto getInt = [&getScalar](llvm::yaml::Node *node) -> std::optional<int64_t> {
    std::string val = getScalar(node);
    if (val.empty()) return std::nullopt;
    char *endPtr = nullptr;
    long long result = std::strtoll(val.c_str(), &endPtr, 10);
    if (endPtr == val.c_str() || *endPtr != '\0')
      return std::nullopt;
    return result;
  };

  // Find modules section
  for (auto &kvp : *rootMap) {
    std::string key = getScalar(kvp.getKey());
    if (key != "modules") continue;

    auto *modulesSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(kvp.getValue());
    if (!modulesSeq) continue;

    // Parse each module
    for (auto &moduleNode : *modulesSeq) {
      auto *moduleMap = llvm::dyn_cast<llvm::yaml::MappingNode>(&moduleNode);
      if (!moduleMap) continue;

      ModuleInfo info;

      for (auto &modKvp : *moduleMap) {
        std::string modKey = getScalar(modKvp.getKey());

        if (modKey == "name") {
          info.name = getScalar(modKvp.getValue());
        } else if (modKey == "type") {
          std::string typeStr = getScalar(modKvp.getValue());
          info.type = (typeStr == "static") ? ModuleInfo::Static : ModuleInfo::Chisel;
        } else if (modKey == "path") {
          info.path = getScalar(modKvp.getValue());
        } else if (modKey == "description") {
          info.description = getScalar(modKvp.getValue());
        } else if (modKey == "ports") {
          // Parse ports map
          if (auto *portsMap = llvm::dyn_cast<llvm::yaml::MappingNode>(modKvp.getValue())) {
            for (auto &portKvp : *portsMap) {
              std::string portName = getScalar(portKvp.getKey());
              std::string portType = getScalar(portKvp.getValue());
              info.ports[portName] = portType;
            }
          }
        } else if (modKey == "parameters") {
          // Parse parameters
          if (auto *paramsSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(modKvp.getValue())) {
            for (auto &paramNode : *paramsSeq) {
              if (auto *paramMap = llvm::dyn_cast<llvm::yaml::MappingNode>(&paramNode)) {
                ModuleInfo::ParamInfo paramInfo;
                for (auto &paramKvp : *paramMap) {
                  std::string paramKey = getScalar(paramKvp.getKey());
                  if (paramKey == "name") {
                    paramInfo.name = getScalar(paramKvp.getValue());
                  } else if (paramKey == "type") {
                    paramInfo.type = getScalar(paramKvp.getValue());
                  } else if (paramKey == "default") {
                    paramInfo.defaultValue = getInt(paramKvp.getValue());
                  } else if (paramKey == "required") {
                    paramInfo.required = getBool(paramKvp.getValue());
                  }
                }
                info.parameters.push_back(paramInfo);
              }
            }
          }
        } else if (modKey == "methods") {
          // Parse methods
          if (auto *methodsSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(modKvp.getValue())) {
            for (auto &methodNode : *methodsSeq) {
              if (auto *methodMap = llvm::dyn_cast<llvm::yaml::MappingNode>(&methodNode)) {
                ModuleInfo::MethodInfo methodInfo;
                methodInfo.hasReady = false;
                methodInfo.hasEnable = false;
                for (auto &methodKvp : *methodMap) {
                  std::string methodKey = getScalar(methodKvp.getKey());
                  if (methodKey == "name") {
                    methodInfo.name = getScalar(methodKvp.getValue());
                  } else if (methodKey == "ready") {
                    methodInfo.hasReady = getBool(methodKvp.getValue());
                  } else if (methodKey == "enable") {
                    methodInfo.hasEnable = getBool(methodKvp.getValue());
                  } else if (methodKey == "inputs") {
                    if (auto *inputsSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(methodKvp.getValue())) {
                      for (auto &inputNode : *inputsSeq) {
                        methodInfo.inputs.push_back(getScalar(&inputNode));
                      }
                    }
                  } else if (methodKey == "outputs") {
                    if (auto *outputsSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(methodKvp.getValue())) {
                      for (auto &outputNode : *outputsSeq) {
                        methodInfo.outputs.push_back(getScalar(&outputNode));
                      }
                    }
                  }
                }
                info.methods.push_back(methodInfo);
              }
            }
          }
        } else if (modKey == "values") {
          // Parse values (similar to methods)
          if (auto *valuesSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(modKvp.getValue())) {
            for (auto &valueNode : *valuesSeq) {
              if (auto *valueMap = llvm::dyn_cast<llvm::yaml::MappingNode>(&valueNode)) {
                ModuleInfo::MethodInfo valueInfo;
                valueInfo.hasReady = false;
                valueInfo.hasEnable = false;
                for (auto &valueKvp : *valueMap) {
                  std::string valueKey = getScalar(valueKvp.getKey());
                  if (valueKey == "name") {
                    valueInfo.name = getScalar(valueKvp.getValue());
                  } else if (valueKey == "ready") {
                    valueInfo.hasReady = getBool(valueKvp.getValue());
                  } else if (valueKey == "enable") {
                    valueInfo.hasEnable = getBool(valueKvp.getValue());
                  } else if (valueKey == "outputs") {
                    if (auto *outputsSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(valueKvp.getValue())) {
                      for (auto &outputNode : *outputsSeq) {
                        valueInfo.outputs.push_back(getScalar(&outputNode));
                      }
                    }
                  }
                }
                info.methods.push_back(valueInfo); // values stored in methods list
              }
            }
          }
        } else if (modKey == "conflict_matrix") {
          // Parse conflict matrix
          if (auto *conflictSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(modKvp.getValue())) {
            for (auto &conflictNode : *conflictSeq) {
              if (auto *conflictRow = llvm::dyn_cast<llvm::yaml::SequenceNode>(&conflictNode)) {
                ModuleInfo::ConflictEntry entry;
                int idx = 0;
                for (auto &elem : *conflictRow) {
                  std::string val = getScalar(&elem);
                  if (idx == 0) entry.func1 = val;
                  else if (idx == 1) entry.func2 = val;
                  else if (idx == 2) {
                    if (val == "Conflict")
                      entry.relation = ModuleInfo::ConflictEntry::Conflict;
                    else if (val == "ConflictFree")
                      entry.relation = ModuleInfo::ConflictEntry::ConflictFree;
                    else if (val == "SequentialBefore")
                      entry.relation = ModuleInfo::ConflictEntry::SequentialBefore;
                  }
                  idx++;
                }
                if (idx >= 3) {
                  info.conflictMatrix.push_back(entry);
                }
              }
            }
          }
        } else if (modKey == "build") {
          // Parse build info
          if (auto *buildMap = llvm::dyn_cast<llvm::yaml::MappingNode>(modKvp.getValue())) {
            ModuleInfo::BuildInfo buildInfo;
            buildInfo.useCache = false;
            for (auto &buildKvp : *buildMap) {
              std::string buildKey = getScalar(buildKvp.getKey());
              if (buildKey == "command") {
                buildInfo.command = getScalar(buildKvp.getValue());
              } else if (buildKey == "output_pattern") {
                buildInfo.outputPattern = getScalar(buildKvp.getValue());
              } else if (buildKey == "cache") {
                buildInfo.useCache = getBool(buildKvp.getValue());
              } else if (buildKey == "args") {
                if (auto *argsSeq = llvm::dyn_cast<llvm::yaml::SequenceNode>(buildKvp.getValue())) {
                  for (auto &argNode : *argsSeq) {
                    buildInfo.args.push_back(getScalar(&argNode));
                  }
                }
              }
            }
            info.buildInfo = buildInfo;
          }
        }
      }

      if (!info.name.empty()) {
        modules_[info.name] = info;
      }
    }
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

mlir::LogicalResult ModuleLibrary::getActualModuleName(
    llvm::StringRef name, const llvm::StringMap<int64_t> &params,
    std::string &actualModuleName) const {

  auto it = modules_.find(name);
  if (it == modules_.end())
    return mlir::failure();

  const ModuleInfo &info = it->second;

  // For static modules, use the module name directly
  if (info.type == ModuleInfo::Static) {
    actualModuleName = name.str();
    return mlir::success();
  }

  // For Chisel modules, extract the module name from the output pattern
  if (!info.buildInfo || info.buildInfo->outputPattern.empty())
    return mlir::failure();

  // Create complete parameter map with defaults
  llvm::StringMap<int64_t> completeParams = params;
  for (const auto &paramInfo : info.parameters) {
    if (completeParams.find(paramInfo.name) == completeParams.end() &&
        paramInfo.defaultValue) {
      completeParams[paramInfo.name] = *paramInfo.defaultValue;
    }
  }

  // Substitute parameters in the output pattern to get the filename
  std::string outputFile = substituteParams(info.buildInfo->outputPattern, completeParams);

  // Extract module name from filename (remove path and .mlir extension)
  llvm::StringRef filename = llvm::sys::path::filename(outputFile);
  if (filename.ends_with(".mlir"))
    filename = filename.drop_back(5);

  actualModuleName = filename.str();
  return mlir::success();
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
      if (firrtlModules_.contains(firrtlMod.getName())) {
        continue;
      } else {
        firrtlModules_.insert_or_assign(firrtlMod.getName(), true);
      }
      builder.clone(op);
    }
  }

  builder.restoreInsertionPoint(savedIP);

  return mlir::success();
}
