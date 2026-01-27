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
  /// Next storage index to assign within the current module.
  ///
  /// Note: `dataflow.storage_index` is used by later passes (TokenRTLGen) to
  /// name and wire token storage instances. Indices must be unique across all
  /// proc.dataflow regions lowered into a single module; multiple independent
  /// dataflows may otherwise reuse indices starting from 0, causing unrelated
  /// token channels to alias the same `__tok_N` storage instance.
  unsigned nextStorageIdx_ = 0;

  /// Process a single module - lower all proc.dataflow ops.
  void processModule(cmt2::ModuleOp module);

  /// Lower a single proc.dataflow operation.
  LogicalResult lowerDataflow(ProcDataflowOp dataflow, cmt2::ModuleOp module);

  /// Lower a single dataflow.task to a rule.
  RuleOp lowerTask(DataflowTaskOp task, ProcDataflowOp dataflow,
                   OpBuilder &builder, IRMapping &valueMapping);

  /// Flatten nested dataflows - recursively process dataflows inside tasks.
  LogicalResult flattenNestedDataflows(cmt2::ModuleOp module);
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
  unsigned storageIdx = nextStorageIdx_;

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

      // Annotate each token.create op with its storage index
      // The yield op tells us which position each token goes to
      // The i-th yielded token corresponds to the i-th task output
      LLVM_DEBUG(llvm::dbgs() << "  Annotating token.create ops for @"
                              << task.getSymName() << "\n");
      task.walk([&](DataflowYieldOp yieldOp) {
        auto yieldedTokens = yieldOp.getTokens();
        auto taskOutputs = task.getTokenOutputs();

        LLVM_DEBUG(llvm::dbgs() << "    yieldedTokens.size()=" << yieldedTokens.size()
                                << " taskOutputs.size()=" << taskOutputs.size() << "\n");

        for (size_t i = 0; i < yieldedTokens.size() && i < taskOutputs.size(); ++i) {
          Value yieldedToken = yieldedTokens[i];
          Value taskOutput = taskOutputs[i];
          LLVM_DEBUG(llvm::dbgs() << "    yield " << i << ": ");
          if (auto createOp = yieldedToken.getDefiningOp<TokenCreateOp>()) {
            // The i-th yield position maps to the i-th task output
            auto it = tokenToStorageIdx.find(taskOutput);
            LLVM_DEBUG(llvm::dbgs() << "found token.create, ");
            if (it != tokenToStorageIdx.end()) {
              createOp->setAttr("dataflow.storage_index",
                                builder.getI64IntegerAttr(it->second));
              LLVM_DEBUG(llvm::dbgs() << "annotated with storage_index=" << it->second << "\n");
            } else {
              LLVM_DEBUG(llvm::dbgs() << "taskOutput not found in tokenToStorageIdx!\n");
            }
          } else {
            LLVM_DEBUG(llvm::dbgs() << "not a token.create op\n");
          }
        }
      });
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

  // Keep indices unique across all lowered dataflows in this module.
  nextStorageIdx_ = storageIdx;

  // Erase the dataflow op after successful lowering
  // The rules now handle all the logic - keeping the dataflow op would cause
  // TokenRTLGen to process it again and create duplicate storage instances
  LLVM_DEBUG(llvm::dbgs() << "  Generated " << generatedRules.size()
                          << " rules, erasing dataflow op\n");

  dataflow->erase();

  return success();
}

//===----------------------------------------------------------------------===//
// Nested Dataflow Flattening
//===----------------------------------------------------------------------===//

LogicalResult DataflowLoweringPass::flattenNestedDataflows(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Flattening nested dataflows in module @"
                          << module.getSymName() << "\n");

  // Iteratively flatten until no nested dataflows remain. Flattening may expose
  // additional nested structures, so we loop to a fixed point.
  bool changed = true;
  while (changed) {
    changed = false;

    struct NestedInfo {
      ProcDataflowOp nested;
      DataflowTaskOp parentTask;
      ProcDataflowOp parentDataflow;
    };

    SmallVector<NestedInfo> worklist;

    module.walk([&](DataflowTaskOp task) {
      auto parentDataflow = task->getParentOfType<ProcDataflowOp>();
      if (!parentDataflow)
        return;

      task.walk([&](ProcDataflowOp nested) {
        // Only process truly nested dataflows (directly inside a task body).
        auto nestedParentTask = nested->getParentOfType<DataflowTaskOp>();
        if (!nestedParentTask || nestedParentTask != task)
          return;
        LLVM_DEBUG(llvm::dbgs()
                   << "  Found nested dataflow @" << nested.getSymName()
                   << " inside task @" << task.getSymName() << " of @"
                   << parentDataflow.getSymName() << "\n");
        worklist.push_back({nested, task, parentDataflow});
      });
    });

    if (worklist.empty())
      return success();

    OpBuilder builder(module.getContext());

    for (const auto &item : worklist) {
      ProcDataflowOp nested = item.nested;
      DataflowTaskOp parentTask = item.parentTask;
      ProcDataflowOp parentDataflow = item.parentDataflow;

      Block &nestedBlock = nested.getBody().front();
      Block &parentBlock = parentDataflow.getBody().front();

      // Capture parent task token inputs (used to gate inner tasks and to
      // connect inner completion back to the parent task).
      SmallVector<Value> parentTokenIns(parentTask.getTokenInputs().begin(),
                                        parentTask.getTokenInputs().end());
      auto parentTokenInNames = parentTask.getTokenInNames();

      // Collect the inner tasks in order before moving them.
      SmallVector<DataflowTaskOp> innerTasks;
      for (auto &op : nestedBlock) {
        if (auto t = dyn_cast<DataflowTaskOp>(op))
          innerTasks.push_back(t);
      }

      // Identify "completion" tokens from the nested dataflow: tokens that
      // must become valid before the parent task can proceed. We conservatively
      // use the token inputs of tasks that return values in the nested dataflow.
      SmallVector<Value> completionTokens;
      for (auto t : innerTasks) {
        if (isa<DataflowReturnOp>(t.getBody().front().getTerminator())) {
          for (Value tok : t.getTokenInputs())
            completionTokens.push_back(tok);
        }
      }

      // Map nested dataflow block arguments to values available in the parent
      // dataflow:
      //  - Prefer using parent task token inputs (via cmt2.token.data) when the
      //    token carries matching data.
      //  - Otherwise fall back to parent dataflow block arguments by position.
      SmallVector<std::optional<unsigned>> argTokenMap; // nested arg i -> parent token i
      SmallVector<std::optional<unsigned>> argValueMap; // nested arg i -> parent df arg i
      argTokenMap.resize(nestedBlock.getNumArguments());
      argValueMap.resize(nestedBlock.getNumArguments());

      for (unsigned i = 0; i < nestedBlock.getNumArguments(); ++i) {
        Type nestedArgTy = nestedBlock.getArgument(i).getType();

        if (i < parentTokenIns.size()) {
          auto tokTy = dyn_cast<SyncTokenType>(parentTokenIns[i].getType());
          if (tokTy && tokTy.getDataType() && tokTy.getDataType() == nestedArgTy) {
            argTokenMap[i] = i;
            continue;
          }
        }

        if (i < parentBlock.getNumArguments() &&
            parentBlock.getArgument(i).getType() == nestedArgTy) {
          argValueMap[i] = i;
          continue;
        }

        // No mapping found; leave it unmapped for now (will error if used).
        LLVM_DEBUG(llvm::dbgs()
                   << "  Warning: No mapping found for nested arg " << i
                   << " of type ";
                   nestedArgTy.print(llvm::dbgs());
                   llvm::dbgs() << "\n");
      }

      // Move inner tasks into the parent dataflow block, right before the
      // parent task, so any completion tokens dominate the parent task.
      for (auto t : innerTasks) {
        t->moveBefore(parentTask);
      }

      // Rename moved tasks to avoid symbol conflicts in the parent dataflow's
      // symbol table: "{parentTask}_{nestedDf}_{innerTask}".
      llvm::StringSet<> usedTaskNames;
      for (auto &op : parentBlock) {
        if (auto t = dyn_cast<DataflowTaskOp>(op)) {
          usedTaskNames.insert(t.getSymName());
        }
      }

      auto makeUniqueName = [&](StringRef base) -> std::string {
        std::string name = base.str();
        if (!usedTaskNames.contains(name)) {
          usedTaskNames.insert(name);
          return name;
        }
        for (unsigned suffix = 0;; ++suffix) {
          std::string candidate = (base + "_" + std::to_string(suffix)).str();
          if (!usedTaskNames.contains(candidate)) {
            usedTaskNames.insert(candidate);
            return candidate;
          }
        }
      };

      // Timing propagation: offset inner task timing by the parent task's
      // timing start (if present).
      int64_t timingOffset = 0;
      if (auto parentTiming = parentTask.getTiming())
        timingOffset = parentTiming->getStart();

      for (auto t : innerTasks) {
        std::string baseName =
            (parentTask.getSymName() + "_" + nested.getSymName() + "_" +
             t.getSymName()).str();
        auto newName = makeUniqueName(baseName);
        t->setAttr(SymbolTable::getSymbolAttrName(),
                   StringAttr::get(module.getContext(), newName));

        // Adjust task timing (relative nested timings -> parent timeline).
        if (auto timing = t.getTiming()) {
          auto adjusted = TimingIntervalAttr::get(
              module.getContext(), timing->getStart() + timingOffset,
              timing->getEnd() + timingOffset);
          t->setAttr("timing", adjusted);
        }

        // Ensure tasks that depend on nested args can access the parent task's
        // token inputs by appending them to token_inputs (if any).
        if (!parentTokenIns.empty()) {
          SmallVector<Value> newInputs(t.getTokenInputs().begin(),
                                       t.getTokenInputs().end());
          SmallVector<Attribute> newNames;
          for (auto a : t.getTokenInNames())
            newNames.push_back(a);

          // Append parent tokens (avoid duplicates).
          for (unsigned i = 0; i < parentTokenIns.size(); ++i) {
            Value tok = parentTokenIns[i];
            if (llvm::is_contained(newInputs, tok))
              continue;
            newInputs.push_back(tok);
            if (i < parentTokenInNames.size())
              newNames.push_back(parentTokenInNames[i]);
            else
              newNames.push_back(StringAttr::get(module.getContext(), "tok"));
          }

          t.getOperation()->setOperands(newInputs);
          t->setAttr("token_in_names", ArrayAttr::get(module.getContext(), newNames));
        }

        // Replace uses of nested dataflow block arguments inside the task body.
        Block &taskBody = t.getBody().front();
        OpBuilder taskBuilder(&taskBody, taskBody.begin());

        // Lazily create token.data ops per mapped token input in this task.
        llvm::DenseMap<unsigned, Value> tokenDataCache;

        auto getMappedValue = [&](unsigned argIdx) -> Value {
          if (argIdx >= nestedBlock.getNumArguments())
            return nullptr;
          if (argTokenMap[argIdx].has_value()) {
            unsigned tokIdx = *argTokenMap[argIdx];
            if (tokIdx >= parentTokenIns.size())
              return nullptr;
            auto it = tokenDataCache.find(tokIdx);
            if (it != tokenDataCache.end())
              return it->second;
            Value tok = parentTokenIns[tokIdx];
            Value data =
                taskBuilder.create<TokenDataOp>(t.getLoc(),
                                                nestedBlock.getArgument(argIdx).getType(),
                                                tok)
                    .getResult();
            tokenDataCache[tokIdx] = data;
            return data;
          }
          if (argValueMap[argIdx].has_value()) {
            unsigned valIdx = *argValueMap[argIdx];
            if (valIdx >= parentBlock.getNumArguments())
              return nullptr;
            return parentBlock.getArgument(valIdx);
          }
          return nullptr;
        };

        for (unsigned argIdx = 0; argIdx < nestedBlock.getNumArguments(); ++argIdx) {
          Value nestedArg = nestedBlock.getArgument(argIdx);
          Value mapped = getMappedValue(argIdx);
          if (!mapped)
            continue;

          // Replace uses in the task body region.
          t.walk([&](Operation *op) {
            for (OpOperand &operand : op->getOpOperands()) {
              if (operand.get() == nestedArg)
                operand.set(mapped);
            }
          });
        }

        // If this task previously ended the nested dataflow (dataflow.return),
        // convert it to a yield so it doesn't terminate the parent dataflow.
        if (auto ret = dyn_cast<DataflowReturnOp>(taskBody.getTerminator())) {
          OpBuilder termBuilder(ret);
          termBuilder.create<DataflowYieldOp>(ret.getLoc(), ValueRange{});
          ret.erase();
        }
      }

      // Connect inner completion back to the parent task by adding completion
      // tokens as additional parent task inputs.
      if (!completionTokens.empty()) {
        SmallVector<Value> newParentInputs(parentTask.getTokenInputs().begin(),
                                           parentTask.getTokenInputs().end());
        SmallVector<Attribute> newParentNames;
        for (auto a : parentTask.getTokenInNames())
          newParentNames.push_back(a);

        for (Value tok : completionTokens) {
          if (llvm::is_contained(newParentInputs, tok))
            continue;
          newParentInputs.push_back(tok);
          newParentNames.push_back(StringAttr::get(module.getContext(), "nested_done"));
        }

        parentTask.getOperation()->setOperands(newParentInputs);
        parentTask->setAttr("token_in_names",
                            ArrayAttr::get(module.getContext(), newParentNames));
      }

      // Erase the nested dataflow op now that its tasks have been flattened.
      nested.erase();
      changed = true;
    }
  }
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void DataflowLoweringPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Reset module-scoped storage numbering.
  nextStorageIdx_ = 0;

  // Check for nested dataflows and warn/handle them
  if (failed(flattenNestedDataflows(module))) {
    signalPassFailure();
    return;
  }

  // Collect all proc.dataflow ops (at module level)
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
