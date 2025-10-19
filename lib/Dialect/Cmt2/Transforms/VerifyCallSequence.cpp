//===- VerifyCallSequence.cpp - Verify call sequence ordering -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements verification that call sequences respect conflict
// matrix constraints.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-verify-call-sequence"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_VERIFYCALLSEQUENCE
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// Verification pass to check that call sequences respect conflict matrix
struct VerifyCallSequencePass
    : public circt::cmt2::impl::VerifyCallSequenceBase<
          VerifyCallSequencePass> {

  void runOnOperation() override;

private:
  /// Validate call sequence for a single function
  LogicalResult validateCallSequence(const SmallVector<CallInfo> &calls,
                                       const ModuleConflictMatrix *conflictMatrix,
                                       Cmt2FunctionLike func);
};

} // end anonymous namespace

LogicalResult VerifyCallSequencePass::validateCallSequence(
    const SmallVector<CallInfo> &calls,
    const ModuleConflictMatrix *conflictMatrix, Cmt2FunctionLike func) {

  if (!conflictMatrix || calls.size() < 2)
    return success();

  LLVM_DEBUG({
    llvm::dbgs() << "  Validating call sequence for function: "
                 << func.functionName() << " with " << calls.size()
                 << " calls\n";
    for (size_t i = 0; i < calls.size(); ++i) {
      llvm::dbgs() << "    [" << i << "] " << calls[i].calleeInstance << " @ "
                   << calls[i].calleeEntity << "\n";
    }
  });

  // Check for sequential before violations (calling in wrong order)
  for (size_t i = 0; i < calls.size(); ++i) {
    for (size_t j = i + 1; j < calls.size(); ++j) {
      auto rel = conflictMatrix->getRelationship(
          calls[j].calleeEntity.getLeafReference(),
          calls[i].calleeEntity.getLeafReference());

      if (rel == Relationship::SequentialBefore) {
        // calls[j] < calls[i], but calls[i] appears before calls[j] -
        // violation!
        return func.emitError("Sequential before violation: ")
               << calls[j].calleeEntity << " should be called before "
               << calls[i].calleeEntity;
      }
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "  Call sequence validation passed\n");
  return success();
}

void VerifyCallSequencePass::runOnOperation() {
  CircuitOp circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs() << "=== Verify Call Sequence Pass ===\n");

  // Run analyses
  ConflictMatrixAnalysis conflictAnalysis(circuit);
  CallInfoView callInfo(circuit);

  // Walk through all functions and check each function's call sequence
  WalkResult result = circuit.walk([&](Cmt2FunctionLike func) {
    // Get the module containing this function
    auto module = func->getParentOfType<Cmt2ModuleLike>();
    if (!module)
      return WalkResult::advance();

    StringAttr moduleName = module.moduleNameAttr();
    LLVM_DEBUG(llvm::dbgs() << "Checking function: " << func.functionName()
                            << " in module: " << moduleName << "\n");

    const ModuleConflictMatrix *conflictMatrix =
        conflictAnalysis.getModuleMatrix(moduleName);
    const ModuleCallInfo *moduleCallInfo =
        callInfo.getModuleCallInfo(moduleName.getValue());

    if (!conflictMatrix) {
      LLVM_DEBUG(llvm::dbgs() << "  No conflict matrix found, skipping\n");
      return WalkResult::advance();
    }

    // Get calls for this function
    SmallVector<CallInfo> calls;
    if (moduleCallInfo) {
      auto it =
          moduleCallInfo->find(SymbolRefAttr::get(func.functionNameAttr()));
      if (it != moduleCallInfo->end()) {
        calls.assign(it->second.begin(), it->second.end());
      }
    }

    // Validate the call sequence
    if (failed(validateCallSequence(calls, conflictMatrix, func))) {
      return WalkResult::interrupt();
    }

    return WalkResult::advance();
  });

  if (result.wasInterrupted()) {
    signalPassFailure();
    return;
  }

  LLVM_DEBUG(llvm::dbgs()
             << "=== Verification Passed: All call sequences valid ===\n");
}
