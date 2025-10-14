//===- InlinePrivateFuncs.cpp - Inline private functions --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements inlining of private functions for the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/PrivateFuncAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-inline-private-funcs"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_INLINEPRIVATEFUNCS
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// Inline private functions pass implementation
struct InlinePrivateFuncsPass
    : public circt::cmt2::impl::InlinePrivateFuncsBase<InlinePrivateFuncsPass> {

  void runOnOperation() override;

private:
  /// Inline a private function at all its call sites
  LogicalResult inlinePrivateFunc(mlir::Operation *privateFunc);

  /// Inline a single call to a private function
  void inlineCall(CallOp call, mlir::Operation *targetFunc);
};

} // end anonymous namespace

void InlinePrivateFuncsPass::inlineCall(CallOp call,
                                         mlir::Operation *targetFunc) {
  OpBuilder builder(call);

  LLVM_DEBUG(llvm::dbgs() << "Inlining call to private function\n");

  // Get the body region of the target function
  Region *bodyRegion = nullptr;
  if (auto methodOp = dyn_cast<MethodOp>(targetFunc))
    bodyRegion = &methodOp.getBody();
  else if (auto valueOp = dyn_cast<ValueOp>(targetFunc))
    bodyRegion = &valueOp.getBody();

  if (!bodyRegion || bodyRegion->empty()) {
    LLVM_DEBUG(llvm::dbgs() << "  Skipping - no body region\n");
    return;
  }

  // Create a mapping from function arguments to call inputs
  IRMapping mapper;
  Block &entryBlock = bodyRegion->front();
  for (auto [arg, input] :
       llvm::zip(entryBlock.getArguments(), call.getInputs())) {
    mapper.map(arg, input);
  }

  // Clone the operations from the body
  SmallVector<Value> clonedResults;
  for (Operation &op : entryBlock) {
    if (auto returnOp = dyn_cast<ReturnOp>(op)) {
      // Collect the results
      for (Value result : returnOp.getOperands()) {
        clonedResults.push_back(mapper.lookupOrDefault(result));
      }
      break;
    }
    builder.clone(op, mapper);
  }

  // Replace the call's results
  if (!clonedResults.empty())
    call.replaceAllUsesWith(clonedResults);

  // Erase the call
  call.erase();
}

LogicalResult
InlinePrivateFuncsPass::inlinePrivateFunc(mlir::Operation *privateFunc) {
  // Get the function name
  mlir::StringAttr funcName;
  if (auto methodOp = dyn_cast<MethodOp>(privateFunc))
    funcName = methodOp.getSymNameAttr();
  else if (auto valueOp = dyn_cast<ValueOp>(privateFunc))
    funcName = valueOp.getSymNameAttr();
  else
    return failure();

  if (!funcName)
    return failure();

  auto module = privateFunc->getParentOfType<Cmt2ModuleLike>();
  if (!module)
    return failure();

  LLVM_DEBUG(llvm::dbgs() << "Inlining private function: " << funcName.getValue()
                          << " in module " << module.moduleName() << "\n");

  // Find all calls to this function in the same module
  SmallVector<CallOp> callsToInline;
  module->walk([&](CallOp call) {
    // Check if this call targets our function
    if (call.getMethodOrValueAttr().getLeafReference() != funcName)
      return;

    // Check if it's a @this call
    if (call.getCalleeAttr().getLeafReference().getValue() != "this")
      return;

    callsToInline.push_back(call);
  });

  LLVM_DEBUG(llvm::dbgs() << "  Found " << callsToInline.size() << " calls to inline\n");

  // Inline each call
  for (auto call : callsToInline) {
    inlineCall(call, privateFunc);
  }

  // Remove the private function
  privateFunc->erase();

  return success();
}

void InlinePrivateFuncsPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs() << "=== Inline Private Functions Pass ===\n");

  // Build the private function analysis
  PrivateFuncAnalysis analysis(circuit);

  LLVM_DEBUG(analysis.print(llvm::dbgs()));

  // Process each module
  circuit.walk([&](Cmt2ModuleLike module) {
    auto privateFuncs = analysis.getPrivateFuncs(module);

    LLVM_DEBUG(llvm::dbgs() << "Processing module: " << module.moduleName()
                            << " with " << privateFuncs.size()
                            << " private functions\n");

    // Collect all private functions to inline (we can't modify during walk)
    SmallVector<mlir::Operation *> funcsToInline;
    for (auto *func : privateFuncs) {
      funcsToInline.push_back(func);
    }

    // Inline each private function
    for (auto *func : funcsToInline) {
      if (failed(inlinePrivateFunc(func))) {
        signalPassFailure();
        return;
      }
    }
  });

  LLVM_DEBUG(llvm::dbgs() << "=== Inline Private Functions Pass Complete ===\n");
}
