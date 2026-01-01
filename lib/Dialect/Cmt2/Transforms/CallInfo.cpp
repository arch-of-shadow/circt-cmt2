//===- CallInfo.cpp - Call information analysis for Cmt2 -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CallInfo analysis for the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "mlir/IR/BuiltinOps.h"

using namespace circt;
using namespace cmt2;

CallInfoView::CallInfoView(CircuitOp circuit) : circuit(circuit) {
  // Iterate through all modules in the circuit
  for (auto &op : circuit.getOps()) {
    if (auto module = llvm::dyn_cast<ModuleOp>(op)) {
      buildModuleCallInfo(module);
    }
  }
}

void CallInfoView::buildModuleCallInfo(cmt2::ModuleOp module) {
  ModuleCallInfo moduleInfo;
  llvm::StringRef moduleName = module.getSymName();

  // Process all rules, methods, and values in the module
  for (auto &op : module.getOps()) {
    mlir::SymbolRefAttr entityName = nullptr;

    if (auto rule = llvm::dyn_cast<RuleOp>(op)) {
      entityName = mlir::SymbolRefAttr::get(rule.getSymNameAttr());
      processEntity(rule, entityName, module, moduleInfo);
    } else if (auto method = llvm::dyn_cast<MethodOp>(op)) {
      entityName = mlir::SymbolRefAttr::get(method.getSymNameAttr());
      processEntity(method, entityName, module, moduleInfo);
    } else if (auto value = llvm::dyn_cast<ValueOp>(op)) {
      entityName = mlir::SymbolRefAttr::get(value.getSymNameAttr());
      processEntity(value, entityName, module, moduleInfo);
    }
  }

  // Store the module's call info
  callInfoMap[moduleName] = std::move(moduleInfo);
}

void CallInfoView::processEntity(mlir::Operation *entity,
                                   mlir::SymbolRefAttr entityName,
                                   ModuleOp currentModule,
                                   ModuleCallInfo &moduleInfo) {
  EntityCallInfo calls;

  // Walk through all operations in the entity's regions
  entity->walk([&](CallOp callOp) {
    // Get the callee and method/value
    mlir::SymbolRefAttr calleeAttr = callOp.getCalleeAttr();
    mlir::SymbolRefAttr methodOrValueAttr = callOp.getMethodOrValueAttr();

    // Check if this is an interface call by looking up InterfaceDefOp
    if (auto interfaceDef = mlir::SymbolTable::lookupNearestSymbolFrom<InterfaceDefOp>(
            currentModule, calleeAttr)) {
      // This is an interface call - resolve it to actual instance.method
      // InterfaceDefOp has methods attribute: [[@inst, @instMethod, @ifaceMethod], ...]
      auto methodsAttr = interfaceDef.getMethods();
      for (auto methodEntry : methodsAttr) {
        auto arrayAttr = llvm::cast<mlir::ArrayAttr>(methodEntry);
        if (arrayAttr.size() >= 3) {
          // Format: [@instance, @instanceMethod, @interfaceMethod]
          auto ifaceMethodRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[2]);
          if (ifaceMethodRef.getLeafReference() == methodOrValueAttr.getLeafReference()) {
            // Found the mapping - use the actual instance and method
            auto instanceRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[0]);
            auto instanceMethodRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[1]);

            // Determine the call type using the resolved instance and method
            CallType callType = determineCalleeType(callOp, currentModule, instanceRef, instanceMethodRef);
            CallInfo info(instanceRef, instanceMethodRef, callType);
            calls.push_back(info);
            return; // Found and processed the interface call
          }
        }
      }
    }

    // Not an interface call - process normally
    CallType callType = determineCalleeType(callOp, currentModule);
    CallInfo info(calleeAttr, methodOrValueAttr, callType);
    calls.push_back(info);
  });

  // Store the calls for this entity
  if (!calls.empty()) {
    moduleInfo[entityName] = std::move(calls);
  }
}

CallType CallInfoView::determineCalleeType(CallOp callOp, ModuleOp currentModule,
                                             mlir::SymbolRefAttr calleeInstance,
                                             mlir::SymbolRefAttr calleeEntity) {

  // Find the target module where the callee entity is defined
  Cmt2ModuleLike targetModule = nullptr;

  if (calleeInstance.getLeafReference().getValue() == "this") {
    // Calling within the same module
    targetModule = currentModule;
  } else {
    // Find the instance and resolve to its target module
    auto instance = mlir::SymbolTable::lookupNearestSymbolFrom<InstanceOp>(
        currentModule, calleeInstance);
    if (instance) {
      // Get the module being instantiated
      auto moduleNameAttr = instance.getModuleNameAttr().getAttr();
      // Try ModuleOp first
      if (auto module = mlir::SymbolTable::lookupNearestSymbolFrom<ModuleOp>(
              circuit, moduleNameAttr)) {
        targetModule = module;
      }
      // Then try ExtModuleFirrtlOp
      else if (auto extModule = mlir::SymbolTable::lookupNearestSymbolFrom<ExtModuleFirrtlOp>(
              circuit, moduleNameAttr)) {
        targetModule = extModule;
      }
    }
  }

  // Look up the callee entity in the target module using the interface
  if (targetModule) {
    Cmt2FunctionLike function = targetModule.lookupFunctionLike(calleeEntity.getLeafReference());
    if (function) {
      if (function.getFunctionKind() == FunctionKind::Method) {
        return CallType::MethodCall;
      } else if (function.getFunctionKind() == FunctionKind::Value) {
        return CallType::ValueCall;
      }
    }
  }

  // Default to MethodCall if we can't determine
  return CallType::MethodCall;
}

CallType CallInfoView::determineCalleeType(CallOp callOp, ModuleOp currentModule) {
  return determineCalleeType(callOp, currentModule, callOp.getCalleeAttr(), callOp.getMethodOrValueAttr());
}

const ModuleCallInfo *
CallInfoView::getModuleCallInfo(mlir::StringRef moduleName) const {
  auto it = callInfoMap.find(moduleName);
  if (it != callInfoMap.end()) {
    return &it->second;
  }
  return nullptr;
}

llvm::SmallVector<mlir::StringRef, 4> CallInfoView::getModuleNames() const {
  llvm::SmallVector<mlir::StringRef, 4> names;
  for (const auto &entry : callInfoMap) {
    names.push_back(entry.first);
  }
  return names;
}

void CallInfoView::print(llvm::raw_ostream &os) const {
  os << "CallInfoView:\n";

  for (const auto &moduleEntry : callInfoMap) {
    os << "  Module: " << moduleEntry.first << "\n";

    for (const auto &entityEntry : moduleEntry.second) {
      os << "    Entity: " << entityEntry.first << "\n";

      for (const auto &call : entityEntry.second) {
        os << "      Call: " << call.calleeInstance
           << " -> " << call.calleeEntity;

        os << " (type: ";
        switch (call.callType) {
        case CallType::MethodCall:
          os << "Method";
          break;
        case CallType::ValueCall:
          os << "Value";
          break;
        }
        os << ")\n";
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Step Call Collection Utilities
//===----------------------------------------------------------------------===//

llvm::SmallVector<CallInfo, 4> cmt2::collectStepCalls(mlir::Operation *step,
                                                        cmt2::ModuleOp module,
                                                        CircuitOp circuit) {
  llvm::SmallVector<CallInfo, 4> calls;

  // Walk all operations in the step's body region
  step->walk([&](CallOp callOp) {
    mlir::SymbolRefAttr calleeAttr = callOp.getCalleeAttr();
    mlir::SymbolRefAttr methodOrValueAttr = callOp.getMethodOrValueAttr();

    // Determine call type
    CallType callType = CallType::MethodCall; // Default

    // Find the target module to determine call type
    Cmt2ModuleLike targetModule = nullptr;

    if (calleeAttr.getLeafReference().getValue() == "this") {
      targetModule = module;
    } else {
      // Find instance and resolve to its module
      auto instance = mlir::SymbolTable::lookupNearestSymbolFrom<InstanceOp>(
          module, calleeAttr);
      if (instance) {
        auto moduleNameAttr = instance.getModuleNameAttr().getAttr();
        if (auto mod = mlir::SymbolTable::lookupNearestSymbolFrom<cmt2::ModuleOp>(
                circuit, moduleNameAttr)) {
          targetModule = mod;
        } else if (auto extMod =
                       mlir::SymbolTable::lookupNearestSymbolFrom<ExtModuleFirrtlOp>(
                           circuit, moduleNameAttr)) {
          targetModule = extMod;
        }
      }
    }

    // Look up callee to determine type
    if (targetModule) {
      Cmt2FunctionLike function =
          targetModule.lookupFunctionLike(methodOrValueAttr.getLeafReference());
      if (function) {
        if (function.getFunctionKind() == FunctionKind::Value) {
          callType = CallType::ValueCall;
        }
      }
    }

    calls.emplace_back(calleeAttr, methodOrValueAttr, callType);
  });

  return calls;
}

bool cmt2::stepHasConflictingCalls(mlir::Operation *step, cmt2::ModuleOp module,
                                   const ModuleConflictMatrix &conflictMatrix) {
  auto circuit = module->getParentOfType<CircuitOp>();
  auto calls = collectStepCalls(step, module, circuit);

  // Check if any method call has conflicts with ANY function in the module
  for (const auto &call : calls) {
    if (call.callType != CallType::MethodCall)
      continue;

    // Get all relationships for this call's method
    auto ctx = module.getContext();
    for (const auto &[pair, rel] : conflictMatrix.getRelationships()) {
      // Check if this method is involved in any conflict relationship
      if (pair.first == call.calleeEntity.getLeafReference() ||
          pair.second == call.calleeEntity.getLeafReference()) {
        if (rel == Relationship::Conflict) {
          return true;
        }
      }
    }
  }

  return false;
}

llvm::SmallVector<mlir::StringAttr, 8>
cmt2::getConcurrentFunctions(mlir::Operation *step, cmt2::ModuleOp module) {
  llvm::SmallVector<mlir::StringAttr, 8> concurrent;

  // Get all rules and methods in the module
  for (auto &op : module.getBodyRegion().front()) {
    if (auto rule = llvm::dyn_cast<RuleOp>(op)) {
      concurrent.push_back(rule.getSymNameAttr());
    } else if (auto method = llvm::dyn_cast<MethodOp>(op)) {
      concurrent.push_back(method.getSymNameAttr());
    }
  }

  // Note: A more sophisticated analysis would:
  // 1. Find which proc rule contains this step
  // 2. Determine which other functions could fire when this step is active
  // 3. Filter out functions in strict sequential relationship
  // For now, we conservatively assume all functions could be concurrent

  return concurrent;
}
