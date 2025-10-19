//===- Circuit.cpp - ECMT2 Circuit Implementation ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace circt;
using namespace cmt2::ecmt2;

//===----------------------------------------------------------------------===//
// Circuit
//===----------------------------------------------------------------------===//

Circuit::Circuit(llvm::StringRef topModule, mlir::MLIRContext &context)
    : context_(context), builder_(&context), loc_(builder_.getUnknownLoc()),
      topModule_(topModule.str()) {

  // Create MLIR module
  mlirModule_ = mlir::ModuleOp::create(loc_);

  // Set insertion point to module body
  builder_.setInsertionPointToEnd(mlirModule_->getBody());

  // Create circuit operation (CircuitOp has no explicit arguments, just body region)
  circuitOp_ = builder_.create<cmt2::CircuitOp>(loc_);

  // Set insertion point inside circuit
  auto *block = new mlir::Block();
  circuitOp_.getBody().push_back(block);
  builder_.setInsertionPointToEnd(block);
}

Circuit::~Circuit() = default;

Module *Circuit::addModule(llvm::StringRef name) {
  // Set insertion point to circuit body for module creation
  builder_.setInsertionPointToEnd(&circuitOp_.getBody().front());

  auto module = std::make_unique<Module>(name, builder_, loc_);
  auto *ptr = module.get();
  modules_.push_back(std::move(module));

  // Note: insertion point is now inside the new module's body
  // (set by Module constructor)
  return ptr;
}

ExternalModule *Circuit::addExternalModule(llvm::StringRef firrtlModule,
                                           llvm::StringRef name) {
  llvm::StringMap<int64_t> emptyParams;
  return addExternalModule(firrtlModule, emptyParams, name);
}

ExternalModule *Circuit::addExternalModule(
    llvm::StringRef firrtlModule,
    const llvm::StringMap<int64_t> &params,
    llvm::StringRef name) {

  // Check library for the FIRRTL module
  auto &library = ModuleLibrary::getInstance();
  if (library.hasModule(firrtlModule)) {
    // Get module info for conflict matrix, etc.
    auto moduleInfo = library.getModuleInfo(firrtlModule);

    // First, compute the actual module name that will be generated
    std::string actualModuleName;
    if (mlir::failed(library.getActualModuleName(firrtlModule, params, actualModuleName))) {
      llvm::errs() << "Warning: Failed to get actual module name for: "
                   << firrtlModule << "\n";
    }

    // Check if we already have an external module with this FIRRTL module name
    auto it = externalModuleMap_.find(actualModuleName);
    if (it != externalModuleMap_.end()) {
      // Module already exists, return the existing one
      return it->second;
    }

    // Save current insertion point
    auto savedIP = builder_.saveInsertionPoint();

    // Insert the FIRRTL module into the MLIR module (at module level, not inside cmt2.circuit)
    // Set insertion point to the mlirModule body (same level as cmt2.circuit)
    builder_.setInsertionPointToEnd(mlirModule_->getBody());

    // Load and insert the FIRRTL module
    std::string insertedModuleName;
    if (mlir::failed(library.insertModuleIntoCircuit(firrtlModule, params, builder_, loc_, insertedModuleName))) {
      llvm::errs() << "Warning: Failed to insert module from library: "
                   << firrtlModule << "\n";
    }

    // Use provided name if given, otherwise use the actual FIRRTL module name
    std::string cmt2ModuleName = name.empty() ? actualModuleName : name.str();

    // Create ExternalModule at circuit level
    // Save the current insertion point and ensure we create at circuit level
    auto savedIPForExtMod = builder_.saveInsertionPoint();
    builder_.setInsertionPointToEnd(&circuitOp_.getBody().front());

    // Create ExternalModule wrapper with metadata using the actual parameterized module name
    auto extModule =
        std::make_unique<ExternalModule>(cmt2ModuleName, actualModuleName, builder_, loc_);

    // Restore insertion point immediately after creating the ExternalModule op
    builder_.restoreInsertionPoint(savedIPForExtMod);

    // Apply conflict matrix from library
    // (these methods modify attributes, not insertion point)
    if (moduleInfo) {
      for (auto &entry : moduleInfo->conflictMatrix) {
        switch (entry.relation) {
        case ModuleLibrary::ModuleInfo::ConflictEntry::Conflict:
          extModule->addConflict(entry.func1, entry.func2);
          break;
        case ModuleLibrary::ModuleInfo::ConflictEntry::ConflictFree:
          extModule->addConflictFree(entry.func1, entry.func2);
          break;
        case ModuleLibrary::ModuleInfo::ConflictEntry::SequentialBefore:
          extModule->addSequenceBefore(entry.func1, entry.func2);
          break;
        }
      }
    }

    auto *ptr = extModule.get();
    externalModuleMap_[actualModuleName] = ptr;  // Track this external module
    externalModules_.push_back(std::move(extModule));

    // Restore original insertion point
    builder_.restoreInsertionPoint(savedIP);

    return ptr;
  }

  // Module not in library - create basic external reference
  std::string firrtlModuleStr = firrtlModule.str();

  // Check if already exists
  auto it = externalModuleMap_.find(firrtlModuleStr);
  if (it != externalModuleMap_.end()) {
    return it->second;
  }

  // Save current insertion point
  auto savedIP = builder_.saveInsertionPoint();

  // Set insertion point to circuit body for creating the external module declaration
  builder_.setInsertionPointToEnd(&circuitOp_.getBody().front());

  auto extModule =
      std::make_unique<ExternalModule>(name, firrtlModule, builder_, loc_);
  auto *ptr = extModule.get();
  externalModuleMap_[firrtlModuleStr] = ptr;  // Track this external module
  externalModules_.push_back(std::move(extModule));

  // Restore insertion point
  builder_.restoreInsertionPoint(savedIP);

  return ptr;
}

Interface *Circuit::addInterface(llvm::StringRef name) {
  // Save insertion point
  auto savedIP = builder_.saveInsertionPoint();

  // Set insertion point inside circuit for interface creation
  builder_.setInsertionPointToEnd(&circuitOp_.getBody().front());

  // Create the Interface
  auto interface = std::make_unique<Interface>(name, this);
  auto *ptr = interface.get();
  interfaces_.push_back(std::move(interface));

  // Restore insertion point
  builder_.restoreInsertionPoint(savedIP);

  return ptr;
}

mlir::OwningOpRef<mlir::ModuleOp> Circuit::generateMLIR() {
  // The MLIR is already constructed in-place
  // Just return a copy
  return mlir::OwningOpRef<mlir::ModuleOp>(
      mlir::cast<mlir::ModuleOp>(mlirModule_->clone()));
}

std::string Circuit::emitMLIRString() {
  std::string output;
  llvm::raw_string_ostream os(output);

  // Use OpPrintingFlags to enable nice format printing
  mlir::OpPrintingFlags flags;
  flags.elideLargeElementsAttrs();
  flags.enableDebugInfo(false);
  flags.printGenericOpForm(false);  // Explicitly disable generic format

  // Create AsmState to properly handle the printing
  mlir::AsmState state(mlirModule_.get(), flags);

  // Print the module with proper formatting
  mlirModule_->print(os, state);
  os.flush();
  return output;
}

mlir::LogicalResult Circuit::runCmt2ToFIRRTLPipeline() {
  // Create pass manager for ModuleOp
  // Note: Some passes run on CircuitOp (nested automatically),
  // while LowerCmt2ToFIRRTL runs on ModuleOp
  mlir::PassManager pm(&context_);

  // Populate with Cmt2 to FIRRTL conversion pipeline
  cmt2::populateCmt2ToFIRRTLPipeline(pm);

  // Run on the ModuleOp - passes for CircuitOp will be automatically nested
  return pm.run(mlirModule_.get());
}

std::string Circuit::emitFIRRTL() {
  // Run conversion first
  if (mlir::failed(runCmt2ToFIRRTLPipeline())) {
    return "";
  }

  // Emit FIRRTL (placeholder - would use CIRCT's FIRRTL emitter)
  std::string output;
  llvm::raw_string_ostream os(output);
  mlirModule_->print(os);
  os.flush();
  return output;
}

std::string Circuit::emitVerilog() {
  // Run conversion first
  if (mlir::failed(runCmt2ToFIRRTLPipeline())) {
    return "";
  }

  // Emit Verilog (placeholder - would use firtool or CIRCT's Verilog emitter)
  std::string output;
  llvm::raw_string_ostream os(output);
  mlirModule_->print(os);
  os.flush();
  return output;
}

mlir::LogicalResult Circuit::saveToFile(llvm::StringRef filename,
                                        FileFormat format) {
  std::string output;

  switch (format) {
  case FileFormat::MLIR:
    output = emitMLIRString();
    break;
  case FileFormat::FIRRTL:
    output = emitFIRRTL();
    if (output.empty())
      return mlir::failure();
    break;
  case FileFormat::Verilog:
    output = emitVerilog();
    if (output.empty())
      return mlir::failure();
    break;
  }

  std::ofstream file(filename.str());
  if (!file.is_open())
    return mlir::failure();

  file << output;
  file.close();

  return mlir::success();
}
