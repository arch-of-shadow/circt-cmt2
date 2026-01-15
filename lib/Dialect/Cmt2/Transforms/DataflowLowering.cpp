//===- DataflowLowering.cpp - Lower dataflow to GAA rules -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the DataflowLowering pass for the Cmt2 dialect.
// It converts proc.dataflow and dataflow.task operations to regular GAA
// rules with token-based synchronization.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-dataflow-lowering"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_DATAFLOWLOWERING
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// DataflowLowering pass implementation
struct DataflowLoweringPass
    : public circt::cmt2::impl::DataflowLoweringBase<DataflowLoweringPass> {

  void runOnOperation() override;

private:
  /// Process a single module - lower all proc.dataflow ops.
  void processModule(cmt2::ModuleOp module);

  /// Lower a single proc.dataflow operation.
  LogicalResult lowerDataflow(ProcDataflowOp dataflow, cmt2::ModuleOp module);

  /// Lower a single dataflow.task to a rule.
  RuleOp lowerTask(DataflowTaskOp task, ProcDataflowOp dataflow,
                   OpBuilder &builder, IRMapping &valueMapping);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Task Lowering
//===----------------------------------------------------------------------===//

RuleOp DataflowLoweringPass::lowerTask(DataflowTaskOp task,
                                        ProcDataflowOp dataflow,
                                        OpBuilder &builder,
                                        IRMapping &valueMapping) {
  Location loc = task.getLoc();
  std::string ruleName =
      (dataflow.getSymName() + "_" + task.getSymName()).str();

  LLVM_DEBUG(llvm::dbgs() << "  Lowering task @" << task.getSymName()
                          << " to rule @" << ruleName << "\n");

  // Collect token input types and names
  SmallVector<Type> tokenInTypes;
  SmallVector<StringRef> tokenInNames;
  for (auto [idx, token] : llvm::enumerate(task.getTokenInputs())) {
    tokenInTypes.push_back(token.getType());
    auto names = task.getTokenInNames();
    if (idx < names.size())
      tokenInNames.push_back(cast<StringAttr>(names[idx]).getValue());
    else
      tokenInNames.push_back("tok");
  }

  // Collect token output types
  SmallVector<Type> tokenOutTypes;
  for (auto result : task.getTokenOutputs())
    tokenOutTypes.push_back(result.getType());

  // Create the rule with empty regions first
  // Rule signature: no regular args, only token args
  auto funcType = builder.getFunctionType({}, {});

  // Build token type arrays for attributes
  SmallVector<Attribute> tokenInTypeAttrs;
  for (auto t : tokenInTypes)
    tokenInTypeAttrs.push_back(TypeAttr::get(t));

  SmallVector<Attribute> tokenOutTypeAttrs;
  for (auto t : tokenOutTypes)
    tokenOutTypeAttrs.push_back(TypeAttr::get(t));

  auto rule = builder.create<RuleOp>(
      loc,
      /*sym_name=*/builder.getStringAttr(ruleName),
      /*function_type=*/TypeAttr::get(funcType),
      /*argNames=*/builder.getStrArrayAttr({}),
      /*bodyResNames=*/builder.getArrayAttr({}),
      /*arg_attrs=*/nullptr,
      /*res_attrs=*/nullptr,
      /*token_in_types=*/
      tokenInTypeAttrs.empty() ? nullptr
                               : builder.getArrayAttr(tokenInTypeAttrs),
      /*token_in_names=*/
      tokenInNames.empty() ? nullptr
                           : builder.getStrArrayAttr(tokenInNames),
      /*token_out_types=*/
      tokenOutTypeAttrs.empty() ? nullptr
                                : builder.getArrayAttr(tokenOutTypeAttrs));

  // Copy timing attribute if present
  if (auto timing = task.getTiming())
    rule->setAttr("timing", *timing);

  // Get the rule's regions
  Region &guardRegion = rule.getGuard();
  Region &bodyRegion = rule.getBody();

  // Create blocks for guard and body with token arguments
  Block *guardBlock = new Block();
  Block *bodyBlock = new Block();

  // Add token arguments to both blocks
  for (auto tokenType : tokenInTypes) {
    guardBlock->addArgument(tokenType, loc);
    bodyBlock->addArgument(tokenType, loc);
  }

  guardRegion.push_back(guardBlock);
  bodyRegion.push_back(bodyBlock);

  // Build the guard: AND of all token.valid checks
  OpBuilder guardBuilder(guardBlock, guardBlock->begin());
  if (!tokenInTypes.empty()) {
    auto boolType = firrtl::UIntType::get(builder.getContext(), 1);
    Value guardCond = nullptr;

    for (auto [idx, tokenType] : llvm::enumerate(tokenInTypes)) {
      Value tokenArg = guardBlock->getArgument(idx);
      auto validOp =
          guardBuilder.create<TokenValidOp>(loc, boolType, tokenArg);

      if (!guardCond) {
        guardCond = validOp.getValid();
      } else {
        guardCond = guardBuilder.create<firrtl::AndPrimOp>(loc, guardCond,
                                                           validOp.getValid());
      }
    }

    // Add guard terminator with condition
    // Note: RuleOp guard region uses cmt2.return, condition is implicit
    // The guard condition would be used in scheduling
    // For now, just create the return
  }
  guardBuilder.create<ReturnOp>(loc);

  // Build the body: clone task body with token argument mapping
  OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

  // Create mapping from task's token inputs to body block arguments
  // Task body accesses tokens via the operands (not block args since not isolated)
  for (auto [idx, taskToken] : llvm::enumerate(task.getTokenInputs())) {
    valueMapping.map(taskToken, bodyBlock->getArgument(idx));
  }

  // Clone operations from task body to rule body
  Block &taskBlock = task.getBody().front();
  for (auto &op : taskBlock.without_terminator()) {
    bodyBuilder.clone(op, valueMapping);
  }

  // Handle terminator
  Operation *terminator = taskBlock.getTerminator();
  if (auto yieldOp = dyn_cast<DataflowYieldOp>(terminator)) {
    // Map yielded tokens to rule results
    // For now, just create a return
    bodyBuilder.create<ReturnOp>(loc);

    // Map task's results to the yielded tokens (for downstream tasks)
    for (auto [taskResult, yieldedToken] :
         llvm::zip(task.getTokenOutputs(), yieldOp.getTokens())) {
      Value mappedToken = valueMapping.lookupOrDefault(yieldedToken);
      valueMapping.map(taskResult, mappedToken);
    }
  } else if (auto returnOp = dyn_cast<DataflowReturnOp>(terminator)) {
    // Final task - results go to dataflow output
    // For now, just create a return
    bodyBuilder.create<ReturnOp>(loc);

    // Store the return values for dataflow output wiring
    // This would need module-level handling
  }

  LLVM_DEBUG(llvm::dbgs() << "    Created rule with " << tokenInTypes.size()
                          << " token inputs, " << tokenOutTypes.size()
                          << " token outputs\n");

  return rule;
}

//===----------------------------------------------------------------------===//
// Dataflow Lowering
//===----------------------------------------------------------------------===//

LogicalResult DataflowLoweringPass::lowerDataflow(ProcDataflowOp dataflow,
                                                   cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Lowering dataflow @" << dataflow.getSymName()
                          << "\n");

  OpBuilder builder(dataflow);
  IRMapping valueMapping;

  // Map dataflow arguments to module-level values
  // (In a real implementation, these would be wired appropriately)
  Block &dataflowBlock = dataflow.getBody().front();
  for (auto arg : dataflowBlock.getArguments()) {
    // For now, create placeholder - in real impl would wire to module ports
    LLVM_DEBUG(llvm::dbgs() << "  Dataflow arg: " << arg << "\n");
  }

  // Lower each task to a rule
  SmallVector<DataflowTaskOp> tasks;
  for (auto &op : dataflowBlock) {
    if (auto task = dyn_cast<DataflowTaskOp>(op))
      tasks.push_back(task);
  }

  SmallVector<RuleOp> generatedRules;
  for (auto task : tasks) {
    RuleOp rule = lowerTask(task, dataflow, builder, valueMapping);
    generatedRules.push_back(rule);
  }

  // Mark the dataflow as lowered
  dataflow->setAttr("dataflow.lowered", builder.getUnitAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Generated " << generatedRules.size()
                          << " rules\n");

  return success();
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void DataflowLoweringPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Collect all proc.dataflow ops
  SmallVector<ProcDataflowOp> dataflows;
  for (auto &op : module.getBodyRegion().front()) {
    if (auto dataflow = dyn_cast<ProcDataflowOp>(op))
      dataflows.push_back(dataflow);
  }

  // Lower each dataflow
  for (auto dataflow : dataflows) {
    if (failed(lowerDataflow(dataflow, module))) {
      signalPassFailure();
      return;
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "  Processed " << dataflows.size()
                          << " dataflows\n");
}

void DataflowLoweringPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Process each module in the circuit
  for (auto &op : circuit.getBodyRegion().front().getOperations()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
