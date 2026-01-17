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

  // Collect external values used in the task body (dataflow arguments)
  // These need to be captured and passed to the rule
  SmallVector<Value> capturedValues;
  SmallVector<Type> capturedTypes;
  SmallVector<std::string> capturedNames;
  llvm::DenseSet<Value> seenValues;

  Block &dataflowBlock = dataflow.getBody().front();
  Block &taskBlock = task.getBody().front();

  // Check each operation in the task body for uses of external values
  taskBlock.walk([&](Operation *op) {
    for (Value operand : op->getOperands()) {
      // Skip if already captured
      if (seenValues.contains(operand))
        continue;

      // Check if this is a dataflow block argument (external to task)
      if (auto blockArg = dyn_cast<BlockArgument>(operand)) {
        if (blockArg.getOwner() == &dataflowBlock) {
          // This is a dataflow argument - need to capture it
          LLVM_DEBUG(llvm::dbgs() << "    Capturing dataflow arg: " << operand << "\n");
          capturedValues.push_back(operand);
          capturedTypes.push_back(operand.getType());

          // Get name from dataflow arg names if available
          unsigned argIdx = blockArg.getArgNumber();
          auto argNames = dataflow.getArgNames();
          if (argIdx < argNames.size()) {
            capturedNames.push_back(cast<StringAttr>(argNames[argIdx]).strref().str());
          } else {
            capturedNames.push_back("captured_" + std::to_string(argIdx));
          }
          seenValues.insert(operand);
        }
      }
    }
  });

  LLVM_DEBUG(llvm::dbgs() << "    Captured " << capturedValues.size()
                          << " external values\n");

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

  // Check if this is the final task (terminates with DataflowReturnOp)
  // If so, the rule should return the dataflow's result types
  bool isFinalTask = isa<DataflowReturnOp>(taskBlock.getTerminator());
  SmallVector<Type> resultTypes;
  SmallVector<std::string> resultNames;
  if (isFinalTask) {
    // Get return types from the dataflow
    auto dataflowType = cast<FunctionType>(dataflow.getFunctionType());
    for (auto [idx, resType] : llvm::enumerate(dataflowType.getResults())) {
      resultTypes.push_back(resType);
      resultNames.push_back("result_" + std::to_string(idx));
    }
    LLVM_DEBUG(llvm::dbgs() << "    Final task with " << resultTypes.size()
                            << " return types\n");
  }

  // Create the rule with captured values as regular arguments
  SmallVector<StringRef> argNameRefs;
  for (const auto &name : capturedNames)
    argNameRefs.push_back(name);
  auto funcType = builder.getFunctionType(capturedTypes, resultTypes);

  // Build result name attributes
  SmallVector<Attribute> resultNameAttrs;
  for (const auto &name : resultNames)
    resultNameAttrs.push_back(builder.getStringAttr(name));

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
      /*argNames=*/builder.getStrArrayAttr(argNameRefs),
      /*bodyResNames=*/builder.getArrayAttr(resultNameAttrs),
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

  // C2: Propagate TDCC attributes for tasks with proc control
  // This allows downstream passes to generate FSM logic and connect
  // the FSM done state to token production
  if (task->hasAttr("tdcc.has_proc_control")) {
    LLVM_DEBUG(llvm::dbgs() << "    Task has proc control, propagating TDCC attributes\n");

    // Copy all tdcc.* attributes from task to rule
    for (auto attr : task->getAttrs()) {
      if (attr.getName().getValue().starts_with("tdcc.")) {
        rule->setAttr(attr.getName(), attr.getValue());
      }
    }

    // Mark rule as originating from a dataflow task for special handling
    rule->setAttr("dataflow.from_task", builder.getUnitAttr());
    rule->setAttr("dataflow.task_name", builder.getStringAttr(task.getSymName()));
  }

  // Get the rule's regions
  Region &guardRegion = rule.getGuard();
  Region &bodyRegion = rule.getBody();

  // Create blocks for guard and body
  Block *guardBlock = new Block();
  Block *bodyBlock = new Block();

  // Add captured value arguments first (regular rule arguments)
  // These match the function signature
  for (auto capturedType : capturedTypes) {
    guardBlock->addArgument(capturedType, loc);
    bodyBlock->addArgument(capturedType, loc);
  }

  // Then add token arguments
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

    // Token arguments start after captured values
    unsigned tokenArgOffset = capturedTypes.size();
    for (auto [idx, tokenType] : llvm::enumerate(tokenInTypes)) {
      Value tokenArg = guardBlock->getArgument(tokenArgOffset + idx);
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

  // Build the body: clone task body with proper argument mapping
  OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

  // Map captured dataflow arguments to body block arguments
  for (auto [idx, capturedVal] : llvm::enumerate(capturedValues)) {
    valueMapping.map(capturedVal, bodyBlock->getArgument(idx));
  }

  // Map task's token inputs to body block arguments (after captured values)
  unsigned tokenArgOffset = capturedTypes.size();
  for (auto [idx, taskToken] : llvm::enumerate(task.getTokenInputs())) {
    valueMapping.map(taskToken, bodyBlock->getArgument(tokenArgOffset + idx));
  }

  // Clone operations from task body to rule body
  // For tasks with proc control, skip cloning proc control ops (they will be
  // expanded into FSM logic by downstream passes based on TDCC attributes)
  bool hasProcControl = task->hasAttr("tdcc.has_proc_control");

  for (auto &op : taskBlock.without_terminator()) {
    // Skip proc control operations for tasks with proc control
    if (hasProcControl &&
        isa<ProcSeqOp, ProcParOp, ProcIfOp, ProcWhileOp,
            ProcStaticRepeatOp, ProcStaticIfOp, ProcEnableOp>(&op)) {
      LLVM_DEBUG(llvm::dbgs() << "    Skipping proc control op: "
                              << op.getName() << "\n");
      continue;
    }
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
    // Clone the return values and create a return with them
    SmallVector<Value> returnValues;
    for (Value operand : returnOp->getOperands()) {
      Value mappedValue = valueMapping.lookupOrDefault(operand);
      returnValues.push_back(mappedValue);
    }
    bodyBuilder.create<ReturnOp>(loc, returnValues);

    LLVM_DEBUG(llvm::dbgs() << "    Final task returns " << returnValues.size()
                            << " values\n");
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

  // Collect all tasks and build token-to-storage-index mapping
  // Each task that produces tokens gets storage indices assigned in order
  SmallVector<DataflowTaskOp> tasks;
  llvm::DenseMap<Value, unsigned> tokenToStorageIdx;
  unsigned storageIdx = 0;

  for (auto &op : dataflowBlock) {
    if (auto task = dyn_cast<DataflowTaskOp>(op)) {
      tasks.push_back(task);
      // Assign storage indices to this task's output tokens
      for (Value result : task.getTokenOutputs()) {
        tokenToStorageIdx[result] = storageIdx++;
        LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                                << " output mapped to storage __tok_"
                                << (storageIdx - 1) << "\n");
      }
    }
  }

  SmallVector<RuleOp> generatedRules;
  for (auto task : tasks) {
    // Build the list of storage indices for this task's inputs
    SmallVector<unsigned> inputStorageIndices;
    for (Value inputToken : task.getTokenInputs()) {
      auto it = tokenToStorageIdx.find(inputToken);
      if (it != tokenToStorageIdx.end()) {
        inputStorageIndices.push_back(it->second);
        LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                                << " input from storage __tok_" << it->second
                                << "\n");
      } else {
        LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                                << " input not found in mapping!\n");
        inputStorageIndices.push_back(0); // Fallback
      }
    }

    RuleOp rule = lowerTask(task, dataflow, builder, valueMapping);

    // Add attribute with input storage indices for TokenRTLGen to use
    if (!inputStorageIndices.empty()) {
      SmallVector<Attribute> indexAttrs;
      for (unsigned idx : inputStorageIndices) {
        indexAttrs.push_back(builder.getI64IntegerAttr(idx));
      }
      rule->setAttr("dataflow.input_storage_indices",
                    builder.getArrayAttr(indexAttrs));
    }

    generatedRules.push_back(rule);
  }

  // Erase the dataflow op after successful lowering
  // The rules now handle all the logic - keeping the dataflow op would cause
  // TokenRTLGen to process it again and create duplicate storage instances
  LLVM_DEBUG(llvm::dbgs() << "  Generated " << generatedRules.size()
                          << " rules, erasing dataflow op\n");

  dataflow->erase();

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
