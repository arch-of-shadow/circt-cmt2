//===- CompileInvoke.cpp - Compile proc.invoke to steps --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CompileInvoke pass for the Cmt2 dialect.
// It converts cmt2.proc.invoke operations to cmt2.proc.enable + generated steps.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-compile-invoke"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_COMPILEINVOKE
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// CompileInvoke pass implementation
struct CompileInvokePass
    : public circt::cmt2::impl::CompileInvokeBase<CompileInvokePass> {

  void runOnOperation() override;

private:
  /// Process a single module
  void processModule(cmt2::ModuleOp module);

  /// Convert a ProcInvokeOp to a generated step and ProcEnableOp
  void compileInvoke(ProcInvokeOp invoke, cmt2::ModuleOp module, unsigned &stepCounter);
};

} // end anonymous namespace

void CompileInvokePass::compileInvoke(ProcInvokeOp invoke, cmt2::ModuleOp module,
                                       unsigned &stepCounter) {
  OpBuilder builder(invoke);
  Location loc = invoke.getLoc();

  // Generate unique step name
  std::string stepName = "__invoke_group_" + std::to_string(stepCounter++);

  // Create the step at module level (before any proc operations)
  Block *moduleBody = &module.getBody().front();
  OpBuilder moduleBuilder(moduleBody, moduleBody->begin());

  // Find insertion point - after instances and before proc operations
  Operation *insertBefore = nullptr;
  for (auto &op : moduleBody->getOperations()) {
    if (isa<ProcStepOp, ProcStaticStepOp, ProcRuleOp, ProcMethodOp>(op)) {
      insertBefore = &op;
      break;
    }
  }

  if (insertBefore)
    moduleBuilder.setInsertionPoint(insertBefore);
  else
    moduleBuilder.setInsertionPointToEnd(moduleBody);

  // Create the group
  auto stepOp = moduleBuilder.create<ProcStepOp>(
      loc, builder.getStringAttr(stepName));

  // Add the body to the group
  Block *stepBody = new Block();
  stepOp.getBody().push_back(stepBody);
  OpBuilder stepBuilder(stepBody, stepBody->begin());

  // Clone the operand-producing operations into the step body
  // This handles values defined in the control region that need to be used
  // in the step (which is at module level)
  IRMapping valueMap;
  SmallVector<Value> clonedInputs;
  for (Value input : invoke.getInputs()) {
    if (Operation *defOp = input.getDefiningOp()) {
      // Clone the operation into the step body
      Operation *clonedOp = stepBuilder.clone(*defOp, valueMap);
      // Get the corresponding result from the cloned op
      unsigned resultIdx = cast<OpResult>(input).getResultNumber();
      clonedInputs.push_back(clonedOp->getResult(resultIdx));
    } else {
      // Block argument - this shouldn't happen for proc.invoke inputs
      // but handle it just in case
      clonedInputs.push_back(input);
    }
  }

  // Create the cmt2.call inside the group
  // CallOp::build(builder, state, resultTypes, inputs, callee, methodOrValue, arg_attrs, res_attrs)
  auto callOp = stepBuilder.create<CallOp>(
      loc, invoke.getResultTypes(), clonedInputs,
      SymbolRefAttr::get(builder.getContext(), invoke.getInstance()),
      SymbolRefAttr::get(builder.getContext(), invoke.getMethod()),
      /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

  // Store the call results for later use
  SmallVector<Value> callResults(callOp.getResults());

  // Replace the invoke with an enable
  builder.create<ProcEnableOp>(loc, FlatSymbolRefAttr::get(
      builder.getContext(), stepName));

  // Replace uses of invoke results with call results
  // Note: For now, we don't handle result values from invoke
  // as they would need to be stored in registers for multi-cycle methods.
  // This will be handled by the TDCC pass.

  // Erase the original invoke
  invoke.erase();

  LLVM_DEBUG(llvm::dbgs() << "Compiled invoke to step @" << stepName << "\n");
}

void CompileInvokePass::processModule(cmt2::ModuleOp module) {
  unsigned stepCounter = 0;

  // Collect all ProcInvokeOps first to avoid iterator invalidation
  SmallVector<ProcInvokeOp> invokes;
  module.walk([&](ProcInvokeOp invoke) {
    invokes.push_back(invoke);
  });

  // Process each invoke
  for (auto invoke : invokes) {
    compileInvoke(invoke, module, stepCounter);
  }

  LLVM_DEBUG(llvm::dbgs() << "Compiled " << stepCounter << " invokes in module @"
                           << module.getSymName() << "\n");
}

void CompileInvokePass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Process each module in the circuit
  for (auto &op : circuit.getBodyRegion().front().getOperations()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
