//===- ModuleInterpreterRegistry.cpp - Module Interpreter Registry -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the registry for module interpreters in CMT2.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Interpreter/ModuleInterpreterRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"

#define DEBUG_TYPE "cmt2-module-registry"

using namespace circt;
using namespace cmt2;
using namespace interp;

//===----------------------------------------------------------------------===//
// ModuleInterpreterRegistry Implementation
//===----------------------------------------------------------------------===//

ModuleInterpreterRegistry::ModuleInterpreterRegistry() {
  registerBuiltins();
}

ModuleInterpreterRegistry::~ModuleInterpreterRegistry() = default;

void ModuleInterpreterRegistry::registerBuiltins() {
  // Register built-in interpreters
  registerInterpreter(std::make_unique<RegInterpreter>());
  registerInterpreter(std::make_unique<WireInterpreter>());
  registerInterpreter(std::make_unique<FIFOInterpreter>());
  // MemoryInterpreter removed - will use MLIR-based behavioral models
  // See docs/Dialects/Cmt2/tmp/InterpreterModularization-Design.md

  LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: registered "
                          << interpreters_.size() << " built-in interpreters\n");
}

void ModuleInterpreterRegistry::registerInterpreter(
    std::unique_ptr<ModuleInterpreter> interp) {
  llvm::StringRef typeName = interp->getModuleType();
  interpreters_[typeName] = std::move(interp);
  LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: registered '"
                          << typeName << "'\n");
}

ModuleInterpreter *
ModuleInterpreterRegistry::getInterpreter(llvm::StringRef typeName) {
  auto it = interpreters_.find(typeName);
  if (it != interpreters_.end())
    return it->second.get();
  return nullptr;
}

ModuleInterpreter *
ModuleInterpreterRegistry::resolveInterpreter(ExtModuleFirrtlOp extModule) {
  llvm::StringRef moduleName = extModule.getModuleName();

  // Strategy 1: Check for interpreter_class attribute
  if (auto classAttr = extModule->getAttrOfType<mlir::StringAttr>("interpreter_class")) {
    llvm::StringRef className = classAttr.getValue();
    if (auto *interp = getInterpreter(className)) {
      LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: resolved '"
                              << moduleName << "' via interpreter_class='"
                              << className << "'\n");
      return interp;
    }
  }

  // Strategy 2: Pattern matching on module name
  if (auto *interp = resolveByName(moduleName))
    return interp;

  // Strategy 3: Check config mapping
  auto configIt = moduleConfig_.find(moduleName);
  if (configIt != moduleConfig_.end()) {
    if (auto *interp = getInterpreter(configIt->second)) {
      LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: resolved '"
                              << moduleName << "' via config mapping\n");
      return interp;
    }
  }

  // Not found - add to unresolved list
  bool alreadyUnresolved = false;
  for (const auto &name : unresolvedModules_) {
    if (name == moduleName) {
      alreadyUnresolved = true;
      break;
    }
  }
  if (!alreadyUnresolved) {
    unresolvedModules_.push_back(moduleName.str());
    LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: no interpreter for '"
                            << moduleName << "'\n");
  }

  return nullptr;
}

ModuleInterpreter *
ModuleInterpreterRegistry::resolveByName(llvm::StringRef moduleName) {
  // Try each interpreter's canHandle method
  for (auto &entry : interpreters_) {
    if (entry.second->canHandle(moduleName)) {
      LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: resolved '"
                              << moduleName << "' via pattern matching to '"
                              << entry.first() << "'\n");
      return entry.second.get();
    }
  }
  return nullptr;
}

bool ModuleInterpreterRegistry::initializeInstance(
    llvm::StringRef instanceName, ExtModuleFirrtlOp extModule,
    llvm::ArrayRef<mlir::NamedAttribute> params) {

  ModuleInterpreter *interp = resolveInterpreter(extModule);
  if (!interp)
    return false;

  interp->initializeInstance(instanceName, params);
  instanceToInterpreter_[instanceName] = interp;
  return true;
}

bool ModuleInterpreterRegistry::initializeInstance(
    llvm::StringRef instanceName, llvm::StringRef moduleName,
    llvm::ArrayRef<mlir::NamedAttribute> params) {

  ModuleInterpreter *interp = resolveByName(moduleName);
  if (!interp)
    return false;

  interp->initializeInstance(instanceName, params);
  instanceToInterpreter_[instanceName] = interp;
  return true;
}

void ModuleInterpreterRegistry::resetAllInstances() {
  for (auto &entry : instanceToInterpreter_) {
    entry.second->resetInstance(entry.first());
  }
}

void ModuleInterpreterRegistry::tickAllInstances() {
  for (auto &entry : instanceToInterpreter_) {
    entry.second->tick(entry.first());
  }
}

void ModuleInterpreterRegistry::commitAllInstances() {
  for (auto &entry : instanceToInterpreter_) {
    entry.second->commitCycle(entry.first());
  }
}

bool ModuleInterpreterRegistry::checkMethodGuard(
    llvm::StringRef instanceName, llvm::StringRef methodName,
    llvm::ArrayRef<InterpValue> args) {
  auto it = instanceToInterpreter_.find(instanceName);
  if (it == instanceToInterpreter_.end())
    return false;
  return it->second->checkMethodGuard(instanceName, methodName, args);
}

std::optional<std::vector<InterpValue>>
ModuleInterpreterRegistry::callMethod(llvm::StringRef instanceName,
                                      llvm::StringRef methodName,
                                      llvm::ArrayRef<InterpValue> args) {
  auto it = instanceToInterpreter_.find(instanceName);
  if (it == instanceToInterpreter_.end())
    return std::nullopt;
  return it->second->callMethod(instanceName, methodName, args);
}

llvm::json::Value
ModuleInterpreterRegistry::getInstanceState(llvm::StringRef instanceName) const {
  auto it = instanceToInterpreter_.find(instanceName);
  if (it == instanceToInterpreter_.end())
    return llvm::json::Object{{"error", "instance not found"}};
  return it->second->getInstanceState(instanceName);
}

std::vector<std::string> ModuleInterpreterRegistry::getAllInstanceNames() const {
  std::vector<std::string> names;
  for (const auto &entry : instanceToInterpreter_)
    names.push_back(entry.first().str());
  return names;
}

bool ModuleInterpreterRegistry::hasInstance(llvm::StringRef instanceName) const {
  return instanceToInterpreter_.count(instanceName) > 0;
}

mlir::LogicalResult
ModuleInterpreterRegistry::loadConfig(llvm::StringRef jsonPath) {
  auto bufferOrErr = llvm::MemoryBuffer::getFile(jsonPath);
  if (!bufferOrErr) {
    LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: failed to open config '"
                            << jsonPath << "'\n");
    return mlir::failure();
  }

  auto json = llvm::json::parse(bufferOrErr.get()->getBuffer());
  if (!json) {
    LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: failed to parse JSON\n");
    return mlir::failure();
  }

  auto *root = json->getAsObject();
  if (!root) {
    LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: JSON root is not object\n");
    return mlir::failure();
  }

  auto *modules = root->getObject("modules");
  if (!modules) {
    LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: no 'modules' object\n");
    return mlir::failure();
  }

  for (auto &entry : *modules) {
    llvm::StringRef moduleName = entry.first;
    auto *config = entry.second.getAsObject();
    if (!config)
      continue;

    if (auto classStr = config->getString("class")) {
      moduleConfig_[moduleName] = classStr->str();
      LLVM_DEBUG(llvm::dbgs() << "ModuleInterpreterRegistry: config '"
                              << moduleName << "' -> '" << *classStr << "'\n");
    }
  }

  return mlir::success();
}
