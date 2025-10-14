//===- PrivateFuncAnalysis.cpp - Private Function Analysis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the private function analysis for the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/PrivateFuncAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2OpInterfaces.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-private-func-analysis"

using namespace circt;
using namespace cmt2;

PrivateFuncAnalysis::PrivateFuncAnalysis(Operation *operation) {
  // Cast to CircuitOp
  auto circuit = dyn_cast<CircuitOp>(operation);
  if (!circuit)
    return;

  // We need to build CallInfo first
  CallInfoView callInfo(circuit);

  buildPrivateFuncSets(operation, callInfo);
}

void PrivateFuncAnalysis::buildPrivateFuncSets(Operation *operation,
                                                CallInfoView &callInfo) {
  // Walk through all modules
  operation->walk([&](Cmt2ModuleLike module) {
    llvm::DenseSet<mlir::Operation *> privateFuncsInModule;

    // For each function in the module, check if it's private
    module->walk([&](mlir::Operation *op) {
      // Check if this is a method or value operation
      if (!isMethodOp(op) && !isValueOp(op))
        return;

      // Get the function name
      mlir::StringAttr funcName;
      if (auto methodOp = dyn_cast<MethodOp>(op))
        funcName = methodOp.getSymNameAttr();
      else if (auto valueOp = dyn_cast<ValueOp>(op))
        funcName = valueOp.getSymNameAttr();
      else
        return;

      if (!funcName)
        return;

      // Check all calls to this function across all modules
      bool isPrivate = true;
      bool hasCalls = false;

      operation->walk([&](CallOp call) {
        // Get the called method/value name
        mlir::SymbolRefAttr methodOrValue = call.getMethodOrValueAttr();
        if (methodOrValue.getLeafReference() != funcName)
          return;

        hasCalls = true;

        // Check if the callee instance is @this
        mlir::SymbolRefAttr calleeInstance = call.getCalleeAttr();
        if (calleeInstance.getLeafReference().getValue() != "this") {
          // This function is called from outside the module
          isPrivate = false;
          return;
        }

        // Check if the call is from a function in the same module
        mlir::Operation *parentFunc = nullptr;
        if (auto methodOp = call->getParentOfType<MethodOp>())
          parentFunc = methodOp.getOperation();
        else if (auto valueOp = call->getParentOfType<ValueOp>())
          parentFunc = valueOp.getOperation();
        else if (auto ruleOp = call->getParentOfType<RuleOp>())
          parentFunc = ruleOp.getOperation();

        if (!parentFunc) {
          isPrivate = false;
          return;
        }

        // Check if the parent function is in the same module
        auto parentModule = parentFunc->getParentOfType<Cmt2ModuleLike>();
        if (parentModule != module) {
          isPrivate = false;
          return;
        }
      });

      // A function is private if it has calls and all calls are via @this
      // from the same module
      if (hasCalls && isPrivate) {
        privateFuncsInModule.insert(op);
        LLVM_DEBUG(llvm::dbgs() << "Found private function: " << funcName.getValue()
                                << " in module " << module.moduleName() << "\n");
      }
    });

    privateFuncs[module] = std::move(privateFuncsInModule);
  });
}

bool PrivateFuncAnalysis::isPrivateFunc(mlir::Operation *func) const {
  // Find the module containing this function
  auto module = func->getParentOfType<Cmt2ModuleLike>();
  if (!module)
    return false;

  auto it = privateFuncs.find(module);
  if (it == privateFuncs.end())
    return false;

  return it->second.contains(func);
}

llvm::DenseSet<mlir::Operation *>
PrivateFuncAnalysis::getPrivateFuncs(Cmt2ModuleLike module) const {
  auto it = privateFuncs.find(module);
  if (it == privateFuncs.end())
    return {};
  return it->second;
}

void PrivateFuncAnalysis::print(llvm::raw_ostream &os) const {
  os << "=== Private Function Analysis ===\n";
  for (const auto &entry : privateFuncs) {
    Cmt2ModuleLike module = entry.first;
    const auto &funcs = entry.second;
    os << "Module: " << module.moduleName() << "\n";
    os << "  Private functions: " << funcs.size() << "\n";
    for (auto *func : funcs) {
      if (auto methodOp = dyn_cast<MethodOp>(func)) {
        os << "    - " << methodOp.getSymNameAttr().getValue() << "\n";
      } else if (auto valueOp = dyn_cast<ValueOp>(func)) {
        os << "    - " << valueOp.getSymNameAttr().getValue() << "\n";
      }
    }
  }
}
