//===- VerifyPrivateFuncsInlined.cpp - Verify private functions inlined -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements verification that all private functions have been
// inlined before conversion to FIRRTL.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-verify-private-funcs-inlined"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_VERIFYPRIVATEFUNCSINLINED
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// Verification pass to check that all private functions have been inlined
struct VerifyPrivateFuncsInlinedPass
    : public circt::cmt2::impl::VerifyPrivateFuncsInlinedBase<
          VerifyPrivateFuncsInlinedPass> {

  void runOnOperation() override;

private:
  /// Check a single function for @this calls
  LogicalResult checkFunction(Cmt2FunctionLike func);
};

} // end anonymous namespace

LogicalResult
VerifyPrivateFuncsInlinedPass::checkFunction(Cmt2FunctionLike func) {
  // Check for @this calls (private functions should be inlined before this pass)
  bool hasThisCall = false;
  CallOp foundCall;

  func.walk([&](CallOp call) {
    if (call.getCallee().getRootReference().getValue() == "this") {
      hasThisCall = true;
      foundCall = call;
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });

  if (hasThisCall) {
    return func.emitError(
        "Function contains @this calls. Run --cmt2-inline-private-funcs "
        "before conversion to FIRRTL.");
  }

  return success();
}

void VerifyPrivateFuncsInlinedPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs()
             << "=== Verify Private Functions Inlined Pass ===\n");

  // Walk through all functions and check for @this calls
  WalkResult result = circuit.walk([&](Cmt2FunctionLike func) {
    LLVM_DEBUG(llvm::dbgs() << "Checking function: " << func.functionName()
                            << "\n");

    if (failed(checkFunction(func))) {
      return WalkResult::interrupt();
    }

    return WalkResult::advance();
  });

  if (result.wasInterrupted()) {
    signalPassFailure();
    return;
  }

  LLVM_DEBUG(llvm::dbgs()
             << "=== Verification Passed: No @this calls found ===\n");
}
