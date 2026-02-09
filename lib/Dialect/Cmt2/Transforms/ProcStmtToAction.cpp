//===- ProcStmtToAction.cpp - Convert proc statements to actions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ProcStmtToAction pass for the Cmt2 dialect.
// It converts procedural control statements to FSM-based action rules using
// the TDCC metadata to generate FSM register, state-based rules, and transitions.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/Transforms/Diagnostics.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

#define DEBUG_TYPE "cmt2-proc-stmt-to-action"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_PROCSTMTTOACTION
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// ProcStmtToAction pass implementation
struct ProcStmtToActionPass
    : public circt::cmt2::impl::ProcStmtToActionBase<ProcStmtToActionPass> {

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Process a procedural rule and generate FSM-based rules.
  LogicalResult processProcRule(ProcRuleOp rule, cmt2::ModuleOp module);

  /// Generate state-based rules for each enabled step.
  void generateStateRules(ProcRuleOp procRule, cmt2::ModuleOp module,
                          OpBuilder &builder, StringRef fsmInstName,
                          unsigned fsmWidth, ArrayAttr enablesAttr,
                          ArrayAttr transitionsAttr, uint64_t doneState,
                          ArrayAttr complexParsAttr,
                          const DenseMap<StringRef, std::string> &branchFsmInstNames,
                          const DenseMap<StringRef, unsigned> &branchFsmWidths);

  /// Generate idle/running value methods.
  void generateStatusValues(ProcRuleOp procRule, cmt2::ModuleOp module,
                            OpBuilder &builder, StringRef fsmInstName,
                            unsigned fsmWidth, uint64_t doneState);

  /// Find the register module to use for FSM.
  /// Returns nullptr if no suitable module found.
  Operation *findRegisterModule(CircuitOp circuit, unsigned width);

  /// Create an FSM register module with the given width.
  /// This creates a cmt2.module.extern.firrtl with read/write bindings.
  Operation *createFSMRegisterModule(CircuitOp circuit, unsigned width,
                                      OpBuilder &builder);

  /// Generate branch FSM rules for complex par blocks.
  /// Creates tick, enable, and done rules for each branch.
  void generateBranchFsmRules(ProcRuleOp procRule, cmt2::ModuleOp module,
                               OpBuilder &builder, StringRef mainFsmInstName,
                               unsigned fsmWidth, ArrayAttr complexParsAttr,
                               const DenseMap<StringRef, std::string> &branchFsmInstNames,
                               const DenseMap<StringRef, unsigned> &branchFsmWidths);

  /// Process a rule that originated from a dataflow task with proc control.
  /// These rules have tdcc.* attributes and need FSM generation.
  LogicalResult processDataflowTaskRule(RuleOp rule, cmt2::ModuleOp module);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Register Module Finding/Creation
//===----------------------------------------------------------------------===//

/// Create an FSM register module with the given width.
/// This creates a cmt2.module.extern.firrtl with read/write bindings.
/// Uses the same naming convention as the ModuleLibrary (Reg_width${width}_init0)
/// so the existing build infrastructure generates the FIRRTL module.
Operation *ProcStmtToActionPass::createFSMRegisterModule(CircuitOp circuit,
                                                          unsigned width,
                                                          OpBuilder &builder) {
  MLIRContext *ctx = circuit.getContext();
  Location loc = circuit.getLoc();

  // Generate module names matching ModuleLibrary convention
  // This allows the existing Chisel build scripts to generate the FIRRTL
  std::string symName = "__FSMReg_" + std::to_string(width);
  std::string firrtlName = "Reg_width" + std::to_string(width) + "_init0";

  // Check if we already created this module
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(op)) {
      if (extMod.getSymName() == symName)
        return extMod;
    }
  }

  // Types
  auto clockType = firrtl::ClockType::get(ctx);
  auto resetType = firrtl::UIntType::get(ctx, 1);
  auto dataType = firrtl::UIntType::get(ctx, width);

  // Insert at the beginning of the circuit
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(&circuit.getBodyRegion().front());

  // Create the external module
  // ExtModuleFirrtlOp takes: sym_name, ext_module_name, argNames
  SmallVector<Attribute> argNameAttrs = {
      builder.getStringAttr("clk"),
      builder.getStringAttr("rst")};

  auto extMod = builder.create<ExtModuleFirrtlOp>(
      loc,
      builder.getStringAttr(symName),
      FlatSymbolRefAttr::get(ctx, firrtlName),
      builder.getArrayAttr(argNameAttrs));

  // Add body with bindings
  SmallVector<Type> argTypes = {clockType, resetType};
  Block *body = new Block();
  body->addArguments(argTypes, {loc, loc});
  extMod.getBody().push_back(body);

  OpBuilder bodyBuilder(body, body->begin());

  // bind.bare %clk, @clk
  bodyBuilder.create<BindBareOp>(
      loc, body->getArgument(0),
      FlatSymbolRefAttr::get(ctx, "clk"));

  // bind.bare %rst, @rst
  bodyBuilder.create<BindBareOp>(
      loc, body->getArgument(1),
      FlatSymbolRefAttr::get(ctx, "rst"));

  // bind.value @read : () -> !firrtl.uint<width>
  // Arguments: sym_name, function_type, readyName, argNames, bodyResNames,
  //            arg_attrs, res_attrs
  auto readFuncType = builder.getFunctionType({}, {dataType});
  bodyBuilder.create<BindValueOp>(
      loc,
      builder.getStringAttr("read"),                              // sym_name
      TypeAttr::get(readFuncType),                                // function_type
      builder.getStringAttr("read_ready"),                        // readyName
      builder.getArrayAttr({}),                                   // argNames
      builder.getArrayAttr({builder.getStringAttr("read_data")}), // bodyResNames
      ArrayAttr(),                                                // arg_attrs
      ArrayAttr());                                               // res_attrs

  // bind.method @write : (!firrtl.uint<width>) -> ()
  // Arguments: sym_name, function_type, enableName, readyName, argNames, bodyResNames,
  //            arg_attrs, res_attrs
  auto writeFuncType = builder.getFunctionType({dataType}, {});
  bodyBuilder.create<BindMethodOp>(
      loc,
      builder.getStringAttr("write"),                              // sym_name
      TypeAttr::get(writeFuncType),                                // function_type
      builder.getStringAttr("write_enable"),                       // enableName
      builder.getStringAttr("write_ready"),                        // readyName
      builder.getArrayAttr({builder.getStringAttr("write_data")}), // argNames
      builder.getArrayAttr({}),                                    // bodyResNames
      ArrayAttr(),                                                 // arg_attrs
      ArrayAttr());                                                // res_attrs

  // Add sequenceBefore attribute
  SmallVector<Attribute> seqPair = {
      FlatSymbolRefAttr::get(ctx, "read"),
      FlatSymbolRefAttr::get(ctx, "write")};
  extMod->setAttr("sequenceBefore",
      builder.getArrayAttr({builder.getArrayAttr(seqPair)}));

  LLVM_DEBUG(llvm::dbgs() << "Created FSM register module @" << symName
                          << " with width " << width << "\n");
  return extMod;
}

Operation *ProcStmtToActionPass::findRegisterModule(CircuitOp circuit,
                                                     unsigned width) {
  // Look for existing register module in circuit
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(op)) {
      // Look for module with read/write methods of correct width
      bool hasRead = false, hasWrite = false;
      unsigned readWidth = 0;
      for (auto &bodyOp : extMod.getBody().front()) {
        if (auto bindValue = dyn_cast<BindValueOp>(bodyOp)) {
          if (bindValue.getSymName() == "read") {
            hasRead = true;
            // Get the return type width
            auto returnTypes = bindValue.getResultTypes();
            if (returnTypes.size() == 1) {
              if (auto uintType = dyn_cast<firrtl::UIntType>(returnTypes[0])) {
                if (uintType.getWidth().has_value())
                  readWidth = uintType.getWidth().value();
              }
            }
          }
        } else if (auto bindMethod = dyn_cast<BindMethodOp>(bodyOp)) {
          if (bindMethod.getSymName() == "write")
            hasWrite = true;
        }
      }
      // Check if this module has the correct width
      if (hasRead && hasWrite && readWidth == width)
        return extMod;
    }
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// State-Based Rule Generation
//===----------------------------------------------------------------------===//

void ProcStmtToActionPass::generateStateRules(
    ProcRuleOp procRule, cmt2::ModuleOp module, OpBuilder &builder,
    StringRef fsmInstName, unsigned fsmWidth, ArrayAttr enablesAttr,
    ArrayAttr transitionsAttr, uint64_t doneState,
    ArrayAttr complexParsAttr,
    const DenseMap<StringRef, std::string> &branchFsmInstNames,
    const DenseMap<StringRef, unsigned> &branchFsmWidths) {

  Location loc = procRule.getLoc();
  auto fsmType = firrtl::UIntType::get(builder.getContext(), fsmWidth);
  auto boolType = firrtl::UIntType::get(builder.getContext(), 1);
  StringRef ruleName = procRule.getSymName();

  // Structure to hold step timing information from TDCC
  struct StepTimingInfo {
    uint64_t startState;
    uint64_t endState;
    int64_t latency;
    bool isStatic;
  };

  // Build map from step name to states and timing
  // A step can be enabled in multiple states (e.g., in static_repeat iterations)
  DenseMap<StringRef, SmallVector<uint64_t>> stepToStates;
  // Map from (step name, state) to timing info
  DenseMap<std::pair<StringRef, uint64_t>, StepTimingInfo> stepStateTiming;

  if (enablesAttr) {
    for (auto enableAttr : enablesAttr) {
      auto dict = cast<DictionaryAttr>(enableAttr);
      // TDCC generates enables with "step" attribute, not "group"
      auto stepRef = dict.getAs<FlatSymbolRefAttr>("step");
      auto stateAttr = dict.getAs<IntegerAttr>("state");
      if (stepRef && stateAttr) {
        uint64_t state = stateAttr.getInt();

        // TL2: Extract timing information
        StepTimingInfo timing;
        timing.startState = state;
        timing.endState = state + 1;
        timing.latency = 1;
        timing.isStatic = false;

        if (auto endStateAttr = dict.getAs<IntegerAttr>("end_state"))
          timing.endState = endStateAttr.getInt();
        if (auto latencyAttr = dict.getAs<IntegerAttr>("latency"))
          timing.latency = latencyAttr.getInt();
        if (auto isStaticAttr = dict.getAs<BoolAttr>("is_static"))
          timing.isStatic = isStaticAttr.getValue();

        // For static steps, TDCC provides an enable with an [start, end) window.
        // We expand that window so we can clone scheduled operations in each
        // cycle of the static step, based on `state_assignments` computed by
        // StaticFSMAllocation.
        if (timing.isStatic && timing.endState > timing.startState + 1) {
          for (uint64_t activeState = timing.startState;
               activeState < timing.endState; ++activeState) {
            stepToStates[stepRef.getValue()].push_back(activeState);
            stepStateTiming[{stepRef.getValue(), activeState}] = timing;
          }
        } else {
          stepToStates[stepRef.getValue()].push_back(state);
          stepStateTiming[{stepRef.getValue(), state}] = timing;
        }
      }
    }
  }

  // Structure to hold transition with optional guard
  struct TransitionInfo {
    uint64_t toState;
    int64_t guardOpId;   // -1 means unconditional
    bool guardInverted;  // true for else branches
    SmallVector<std::string> parJoinBranches;  // non-empty means parallel join guard
  };

  // Structure to hold parallel branch info
  struct ParBranchInfo {
    std::string name;
    uint64_t exitState;
    uint64_t firstState;
    uint64_t lastState;
    bool needsSeparateFsm;
  };

  // Structure to hold parallel block info
  struct ParBlockInfo {
    uint64_t forkState;
    uint64_t joinState;
    bool needsPerBranchFsm;
    SmallVector<ParBranchInfo> branches;
  };

  // Parse parallel blocks from attribute
  SmallVector<ParBlockInfo> parBlocks;
  if (auto parBlocksAttr = procRule->getAttrOfType<ArrayAttr>("tdcc.par_blocks")) {
    for (auto blockAttr : parBlocksAttr) {
      auto dict = cast<DictionaryAttr>(blockAttr);
      ParBlockInfo info;
      info.forkState = dict.getAs<IntegerAttr>("fork_state").getInt();
      info.joinState = dict.getAs<IntegerAttr>("join_state").getInt();
      info.needsPerBranchFsm = false;
      if (auto needsFsmAttr = dict.getAs<BoolAttr>("needs_per_branch_fsm"))
        info.needsPerBranchFsm = needsFsmAttr.getValue();

      if (auto branchesAttr = dict.getAs<ArrayAttr>("branches")) {
        for (auto branchAttr : branchesAttr) {
          auto brDict = cast<DictionaryAttr>(branchAttr);
          ParBranchInfo brInfo;
          brInfo.name = brDict.getAs<StringAttr>("name").getValue().str();
          brInfo.exitState = 0;
          if (auto exitStateAttr = brDict.getAs<IntegerAttr>("exit_state"))
            brInfo.exitState = exitStateAttr.getInt();
          brInfo.firstState = 0;
          if (auto firstStateAttr = brDict.getAs<IntegerAttr>("first_state"))
            brInfo.firstState = firstStateAttr.getInt();
          brInfo.lastState = 0;
          if (auto lastStateAttr = brDict.getAs<IntegerAttr>("last_state"))
            brInfo.lastState = lastStateAttr.getInt();
          brInfo.needsSeparateFsm = false;
          if (auto needsFsmAttr = brDict.getAs<BoolAttr>("needs_separate_fsm"))
            brInfo.needsSeparateFsm = needsFsmAttr.getValue();
          info.branches.push_back(brInfo);
        }
      }
      parBlocks.push_back(info);
    }
  }

  // Build fork state to parallel block info map
  DenseMap<uint64_t, ParBlockInfo*> forkStateToParBlock;
  DenseMap<uint64_t, ParBlockInfo*> joinStateToParBlock;
  for (auto &parBlock : parBlocks) {
    forkStateToParBlock[parBlock.forkState] = &parBlock;
    joinStateToParBlock[parBlock.joinState] = &parBlock;
  }

  // Build map from branch name to numStates (for per-branch FSM done state check)
  // State encoding: 0 = idle, 1..N-1 = active, N = done (where N = numStates - 1)
  DenseMap<StringRef, uint64_t> branchNumStates;
  if (complexParsAttr) {
    for (auto cpAttr : complexParsAttr) {
      auto cpDict = cast<DictionaryAttr>(cpAttr);
      if (auto branchFsmsAttr = cpDict.getAs<ArrayAttr>("branch_fsms")) {
        for (auto bfAttr : branchFsmsAttr) {
          auto bfDict = cast<DictionaryAttr>(bfAttr);
          StringRef branchName = bfDict.getAs<StringAttr>("name").getValue();
          uint64_t numStates = bfDict.getAs<IntegerAttr>("num_states").getInt();
          branchNumStates[branchName] = numStates;
        }
      }
    }
  }

  // Build map from state to its outgoing transitions (may have multiple for if/else)
  DenseMap<uint64_t, SmallVector<TransitionInfo>> stateTransitions;
  if (transitionsAttr) {
    for (auto transAttr : transitionsAttr) {
      auto dict = cast<DictionaryAttr>(transAttr);
      auto fromAttr = dict.getAs<IntegerAttr>("from");
      auto toAttr = dict.getAs<IntegerAttr>("to");
      if (!fromAttr || !toAttr)
        continue;

      TransitionInfo trans;
      trans.toState = toAttr.getInt();
      trans.guardOpId = -1;
      trans.guardInverted = false;

      // Check for guard information (if/while condition)
      if (auto guardIdAttr = dict.getAs<IntegerAttr>("guard_op_id")) {
        trans.guardOpId = guardIdAttr.getInt();
        if (auto invertedAttr = dict.getAs<BoolAttr>("guard_inverted")) {
          trans.guardInverted = invertedAttr.getValue();
        }
      }

      // Check for parallel join guard (AND of all branch completions)
      if (auto parJoinAttr = dict.getAs<ArrayAttr>("par_join_branches")) {
        for (auto branchAttr : parJoinAttr) {
          trans.parJoinBranches.push_back(
              cast<StringAttr>(branchAttr).getValue().str());
        }
      }

      stateTransitions[fromAttr.getInt()].push_back(trans);
    }
  }

  // Group steps by state (for parallel blocks, multiple steps share the same state)
  // We store Operation* to handle both ProcStepOp and ProcStaticStepOp
  // With static_repeat, a step can appear in multiple states (one per iteration)
  DenseMap<uint64_t, SmallVector<Operation *>> stateToSteps;
  for (auto &op : module.getBodyRegion().front()) {
    StringRef stepName;
    if (auto step = dyn_cast<ProcStepOp>(op)) {
      stepName = step.getSymName();
    } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(op)) {
      stepName = staticStep.getSymName();
    } else {
      continue;
    }

    auto stateIt = stepToStates.find(stepName);
    if (stateIt == stepToStates.end())
      continue;

    // Add this step to all states it's enabled in
    for (uint64_t state : stateIt->second) {
      stateToSteps[state].push_back(&op);
    }
  }

  // Collect all states that need rules (from both enables and transitions)
  DenseSet<uint64_t> allStates;
  for (auto &[state, _] : stateToSteps) {
    allStates.insert(state);
  }
  for (auto &[fromState, transitions] : stateTransitions) {
    allStates.insert(fromState);
  }

  // Build a map from condition op ID to the actual condition operation in control region
  // We traverse the control region to find if/static_if/while ops and assign them IDs in order
  DenseMap<int64_t, Operation *> condOpIdToOp;
  int64_t nextCondOpId = 0;
  std::function<void(Region *)> collectCondOps = [&](Region *region) {
    if (!region || region->empty())
      return;
    for (auto &op : region->front()) {
      if (isa<ProcIfOp, ProcCondIfOp, ProcStaticIfOp, ProcWhileOp>(&op)) {
        condOpIdToOp[nextCondOpId++] = &op;
      }
      // Recurse into nested regions
      for (auto &nestedRegion : op.getRegions()) {
        collectCondOps(&nestedRegion);
      }
    }
  };
  collectCondOps(&procRule.getControl());

  // For parallel blocks with nested control, identify which states belong to which branch
  // NOTE: For per-branch FSM blocks, branch execution states are NOT in the main FSM.
  //       They have their own separate FSMs handled by generateBranchFsmRules.
  //       The main FSM only has fork/join states, which should NOT be in stateToBranch.
  // This map is only used for simple parallel blocks where branch states are in main FSM.
  DenseMap<uint64_t, const ParBranchInfo*> stateToBranch;
  // For per-branch FSM blocks, we leave stateToBranch empty since branch execution
  // states are not in the main FSM at all.

  // For each state, generate a rule.
  for (uint64_t state : allStates) {
    auto transIt = stateTransitions.find(state);
    auto stepsIt = stateToSteps.find(state);
    bool hasSteps = stepsIt != stateToSteps.end() && !stepsIt->second.empty();
    bool hasTransitions = transIt != stateTransitions.end() && !transIt->second.empty();

    // Check if this is a fork state
    auto forkIt = forkStateToParBlock.find(state);
    bool isForkState = forkIt != forkStateToParBlock.end();

    // Check if this state belongs to a branch with separate FSM
    auto branchIt = stateToBranch.find(state);
    bool isBranchState = branchIt != stateToBranch.end();
    const ParBranchInfo *branchInfo = isBranchState ? branchIt->second : nullptr;
    std::string branchFsmName;
    uint64_t forkStateForBranch = 0;
    if (isBranchState && branchInfo) {
      auto branchFsmIt = branchFsmInstNames.find(branchInfo->name);
      if (branchFsmIt != branchFsmInstNames.end()) {
        branchFsmName = branchFsmIt->second;
      }
      // Find fork state for this branch
      for (auto &parBlock : parBlocks) {
        for (auto &branch : parBlock.branches) {
          if (branch.name == branchInfo->name) {
            forkStateForBranch = parBlock.forkState;
            break;
          }
        }
      }
    }

    // Create rule: @{ruleName}_state{state}
    std::string stateRuleName =
        (ruleName + "_state" + std::to_string(state)).str();

    auto funcType = builder.getFunctionType({}, {});
    auto funcTypeAttr = TypeAttr::get(funcType);

    auto stateRule = builder.create<RuleOp>(
        loc, builder.getStringAttr(stateRuleName), funcTypeAttr,
        builder.getArrayAttr({}), builder.getArrayAttr({}),
        ArrayAttr(), ArrayAttr());

    // Build guard region: fsm.read() == state (or for branch: main_fsm == fork AND branch_fsm == relative_state)
    Block *guardBlock = new Block();
    stateRule.getGuard().push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());

    auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
    Value inState;

    if (isBranchState && !branchFsmName.empty()) {
      // Branch state with per-branch FSM:
      // Guard: main_fsm == fork_state AND branch_fsm == relative_state

      // Look up branch-specific FSM width (may differ from parent FSM)
      unsigned branchWidth = fsmWidth;  // Default to parent width
      auto branchWidthIt = branchFsmWidths.find(branchInfo->name);
      if (branchWidthIt != branchFsmWidths.end()) {
        branchWidth = branchWidthIt->second;
      }
      auto branchFsmType = firrtl::UIntType::get(builder.getContext(), branchWidth);

      auto mainInstanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
      auto mainFsmRead = guardBuilder.create<CallOp>(
          loc, SmallVector<Type>{fsmType}, ValueRange{},
          mainInstanceSym, readSym,
          ArrayAttr(), ArrayAttr());
      auto forkConst = guardBuilder.create<firrtl::ConstantOp>(
          loc, fsmType, llvm::APInt(fsmWidth, forkStateForBranch));
      auto mainAtFork = guardBuilder.create<firrtl::EQPrimOp>(
          loc, mainFsmRead.getResult(0), forkConst.getResult());

      // Read branch FSM and check relative state (using branch-specific type/width)
      auto branchInstanceSym = FlatSymbolRefAttr::get(builder.getContext(), branchFsmName);
      auto branchFsmRead = guardBuilder.create<CallOp>(
          loc, SmallVector<Type>{branchFsmType}, ValueRange{},
          branchInstanceSym, readSym,
          ArrayAttr(), ArrayAttr());
      // Relative state: state - firstState + 1 (state 1 is first active state in branch FSM)
      uint64_t relativeState = state - branchInfo->firstState + 1;
      auto relStateConst = guardBuilder.create<firrtl::ConstantOp>(
          loc, branchFsmType, llvm::APInt(branchWidth, relativeState));
      auto branchAtState = guardBuilder.create<firrtl::EQPrimOp>(
          loc, branchFsmRead.getResult(0), relStateConst.getResult());

      // Combine: main at fork AND branch at relative state
      inState = guardBuilder.create<firrtl::AndPrimOp>(
          loc, mainAtFork.getResult(), branchAtState.getResult()).getResult();
    } else {
      // Normal state: main_fsm == state
      auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
      auto fsmReadCall = guardBuilder.create<CallOp>(
          loc, SmallVector<Type>{fsmType}, ValueRange{},
          instanceSym, readSym,
          ArrayAttr(), ArrayAttr());

      auto stateConst = guardBuilder.create<firrtl::ConstantOp>(
          loc, fsmType, llvm::APInt(fsmWidth, state));
      inState = guardBuilder.create<firrtl::EQPrimOp>(
          loc, fsmReadCall.getResult(0), stateConst.getResult()).getResult();
    }

    // Guard semantics for procedural rules with FSM-based control:
    //
    // The original rule guard controls ENTRY ONLY (state 0), not continuation.
    // This follows GAA (Guarded Atomic Actions) ORAAT semantics:
    //   - Rule guard determines when a rule can fire (start execution)
    //   - Once a rule starts (enters state 1), it's committed to complete
    //   - Continuation states (state > 0) are unconditional on the original guard
    //
    // Why entry-only guard is correct:
    //   1. GAA semantics: "pick a rule, execute it, commit" - guard is for picking
    //   2. Hardware behavior: FSM represents committed control flow, not speculation
    //   3. Scheduling: Other rules can fire between FSM states, but this rule
    //      continues because it already claimed its resources
    //   4. Predictability: Once started, the rule will complete (barring done signals)
    //
    // Example: proc_rule @example with guard %ready {
    //   proc.seq { enable @step_a; enable @step_b; }
    // }
    //   State 0: guard = inState(0) AND %ready  (entry check)
    //   State 1: guard = inState(1)             (step_a, unconditional)
    //   State 2: guard = inState(2)             (step_b, unconditional)
    //
    // If continuation states needed re-checking of the original guard, we would
    // risk deadlock: the FSM is in state 1, but the guard becomes false, leaving
    // the rule stuck mid-execution.
    //
    Value guardResult = inState;
    if (state == 0 && !procRule.getGuard().empty()) {
      // Clone the original guard region content
      IRMapping guardMapping;
      for (auto &guardOp : procRule.getGuard().front()) {
        if (auto retOp = dyn_cast<ReturnOp>(guardOp)) {
          Value origGuard = guardMapping.lookupOrDefault(retOp.getOperand(0));
          guardResult =
              guardBuilder.create<firrtl::AndPrimOp>(loc, inState, origGuard)
                  .getResult();
        } else {
          guardBuilder.clone(guardOp, guardMapping);
        }
      }
    }

    guardBuilder.create<ReturnOp>(loc, ValueRange{guardResult});

    // Build body region: execute step body (if any) + write next state
    Block *bodyBlock = new Block();
    stateRule.getBody().push_back(bodyBlock);
    OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

    // Mapping to track cloned operations (shared across all steps in this state)
    IRMapping bodyMapping;

    if (isForkState) {
      // Fork state: handle parallel block entry
      auto *parBlock = forkIt->second;

      if (parBlock->needsPerBranchFsm) {
        // Per-branch FSM mode: Initialize branch FSMs conditionally
        // Only initialize when branch FSMs are at 0 (not started yet)
        // This prevents re-initialization on subsequent cycles at fork state

        auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
        auto writeSym = FlatSymbolRefAttr::get(builder.getContext(), "write");

        // Check if any branch FSM is at 0 (need initialization)
        Value anyBranchNotStarted = nullptr;

        for (const auto &branch : parBlock->branches) {
          auto branchFsmIt = branchFsmInstNames.find(branch.name);
          if (branchFsmIt != branchFsmInstNames.end()) {
            // Look up branch-specific FSM width
            unsigned branchWidth = fsmWidth;  // Default to parent width
            auto branchWidthIt = branchFsmWidths.find(branch.name);
            if (branchWidthIt != branchFsmWidths.end()) {
              branchWidth = branchWidthIt->second;
            }
            auto branchFsmType = firrtl::UIntType::get(builder.getContext(), branchWidth);
            auto branchZeroConst = bodyBuilder.create<firrtl::ConstantOp>(
                loc, branchFsmType, llvm::APInt(branchWidth, 0)).getResult();

            auto branchInstanceSym = FlatSymbolRefAttr::get(
                builder.getContext(), branchFsmIt->second);
            auto branchFsmRead = bodyBuilder.create<CallOp>(
                loc, SmallVector<Type>{branchFsmType}, ValueRange{},
                branchInstanceSym, readSym,
                ArrayAttr(), ArrayAttr());
            auto branchAtZero = bodyBuilder.create<firrtl::EQPrimOp>(
                loc, branchFsmRead.getResult(0), branchZeroConst).getResult();
            if (!anyBranchNotStarted) {
              anyBranchNotStarted = branchAtZero;
            } else {
              anyBranchNotStarted = bodyBuilder.create<firrtl::OrPrimOp>(
                  loc, anyBranchNotStarted, branchAtZero).getResult();
            }
          }
        }

        // Initialize branch FSMs only if any is at 0 (first entry to fork)
        // Use conditional write: write(anyNotStarted ? 1 : current_value)
        for (const auto &branch : parBlock->branches) {
          auto branchFsmIt = branchFsmInstNames.find(branch.name);
          if (branchFsmIt != branchFsmInstNames.end()) {
            // Look up branch-specific FSM width
            unsigned branchWidth = fsmWidth;  // Default to parent width
            auto branchWidthIt = branchFsmWidths.find(branch.name);
            if (branchWidthIt != branchFsmWidths.end()) {
              branchWidth = branchWidthIt->second;
            }
            auto branchFsmType = firrtl::UIntType::get(builder.getContext(), branchWidth);
            auto branchZeroConst = bodyBuilder.create<firrtl::ConstantOp>(
                loc, branchFsmType, llvm::APInt(branchWidth, 0)).getResult();

            auto branchInstanceSym = FlatSymbolRefAttr::get(
                builder.getContext(), branchFsmIt->second);
            // Read current value
            auto branchFsmRead = bodyBuilder.create<CallOp>(
                loc, SmallVector<Type>{branchFsmType}, ValueRange{},
                branchInstanceSym, readSym,
                ArrayAttr(), ArrayAttr());
            auto initStateConst = bodyBuilder.create<firrtl::ConstantOp>(
                loc, branchFsmType, llvm::APInt(branchWidth, 1)).getResult();
            // Conditional: init if at zero, keep current otherwise
            auto branchAtZero = bodyBuilder.create<firrtl::EQPrimOp>(
                loc, branchFsmRead.getResult(0), branchZeroConst).getResult();
            auto valueToWrite = bodyBuilder.create<firrtl::MuxPrimOp>(
                loc, branchAtZero, initStateConst, branchFsmRead.getResult(0)).getResult();
            bodyBuilder.create<CallOp>(
                loc, SmallVector<Type>{}, ValueRange{valueToWrite},
                branchInstanceSym, writeSym,
                ArrayAttr(), ArrayAttr());
          }
        }
        // Main FSM stays at fork state - transition handled separately
      }
    }

    if (hasSteps) {
      // Clone step bodies for this state.
      for (auto *stepOp : stepsIt->second) {
        Region *bodyRegion = nullptr;
        StringRef stepName;
        if (auto step = dyn_cast<ProcStepOp>(stepOp)) {
          bodyRegion = &step.getBody();
          stepName = step.getSymName();
        } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(stepOp)) {
          bodyRegion = &staticStep.getBody();
          stepName = staticStep.getSymName();
        }

        // Note: stepStateTiming contains timing info from TDCC for this step.
        // However, we do NOT propagate timing to cloned CallOps because:
        // 1. After lowering, timing is implicit in the FSM state structure
        // 2. The CallOp verifier rejects timing outside static_step
        // 3. The FSM state machine already enforces correct timing
        // See docs/Cmt2/features/Lowering.md for lowering overview and pointers.

        if (bodyRegion && !bodyRegion->empty()) {
          if (auto staticStep = dyn_cast<ProcStaticStepOp>(stepOp)) {
            // Static step: clone only operations scheduled for this cycle.
            //
            // StaticFSMAllocation annotates the step with:
            //   state_assignments = [[local_state, call_idx...], ...]
            // where call_idx refers to the i-th CallOp in the step body.
            //
            // We compute local_state = (global_state - step_enable_start_state)
            // using the TDCC timing information expanded above.
            auto timingIt =
                stepStateTiming.find({staticStep.getSymName(), state});

            uint64_t localState = 0;
            if (timingIt != stepStateTiming.end())
              localState = state - timingIt->second.startState;

            DenseMap<int64_t, StringRef> activeCallTypes;
            if (auto assignments =
                    staticStep->getAttrOfType<ArrayAttr>("state_assignments")) {
              for (auto entryAttr : assignments) {
                auto entry = cast<ArrayAttr>(entryAttr);
                if (entry.empty())
                  continue;
                auto stAttr = dyn_cast<IntegerAttr>(entry[0]);
                if (!stAttr)
                  continue;
                if (static_cast<uint64_t>(stAttr.getInt()) != localState)
                  continue;
                for (size_t i = 1; i < entry.size(); ++i) {
                  // Legacy format: integers are treated as "Enable".
                  if (auto idxAttr = dyn_cast<IntegerAttr>(entry[i])) {
                    activeCallTypes[idxAttr.getInt()] = "Enable";
                    continue;
                  }

                  // New format: [call_idx, "Enable"/"GetRes"].
                  if (auto pairAttr = dyn_cast<ArrayAttr>(entry[i])) {
                    if (pairAttr.size() != 2)
                      continue;
                    auto idxAttr = dyn_cast<IntegerAttr>(pairAttr[0]);
                    auto tyAttr = dyn_cast<StringAttr>(pairAttr[1]);
                    if (!idxAttr || !tyAttr)
                      continue;
                    activeCallTypes[idxAttr.getInt()] = tyAttr.getValue();
                  }
                }
                break;
              }
            } else {
              // No scheduling info available: fall back to cloning the full body
              // in the first cycle only.
              if (localState == 0) {
                for (auto &op : bodyRegion->front()) {
                  bodyBuilder.clone(op, bodyMapping);
                }
              }
              continue;
            }

            if (activeCallTypes.empty())
              continue;

            Block &stepBlock = bodyRegion->front();

            // Map each CallOp in the step body to its call index, matching the
            // indexing used by `state_assignments`.
            DenseMap<Operation *, int64_t> callOpToIndex;
            int64_t totalCalls = 0;
            for (auto &op : stepBlock) {
              if (auto call = dyn_cast<CallOp>(op))
                callOpToIndex[call.getOperation()] = totalCalls++;
            }

            auto checkCrossCycleDependencies =
                [&](CallOp consumerCall, int64_t consumerCallIdx,
                    StringRef callTy) -> LogicalResult {
              // For GetRes clones, we intentionally ignore operand dependencies:
              // the per-cycle clone will substitute dummy operands so we don't
              // re-execute operand-producing calls in the capture cycle.
              if (callTy == "GetRes")
                return success();

              // Walk the consumer's operand definitions inside the static_step
              // body. Any dependency on a CallOp that is not scheduled in this
              // local cycle would require cross-cycle storage, which must be
              // made explicit (e.g., via a Reg).
              SmallVector<Value, 8> worklist(consumerCall.getOperands().begin(),
                                             consumerCall.getOperands().end());
              DenseSet<Value> visited;

              while (!worklist.empty()) {
                Value v = worklist.pop_back_val();
                if (!v)
                  continue;
                if (!visited.insert(v).second)
                  continue;
                if (isa<BlockArgument>(v))
                  continue;

                Operation *defOp = v.getDefiningOp();
                if (!defOp)
                  continue;
                if (defOp->getBlock() != &stepBlock)
                  continue;

                if (auto producerCall = dyn_cast<CallOp>(defOp)) {
                  auto it = callOpToIndex.find(defOp);
                  if (it != callOpToIndex.end()) {
                    int64_t producerIdx = it->second;
                    if (!activeCallTypes.contains(producerIdx)) {
                      std::string msg;
                      llvm::raw_string_ostream os(msg);
                      os << "cross-cycle dependency on call result in "
                            "proc.static_step @"
                         << staticStep.getSymName() << ": call #"
                         << consumerCallIdx
                         << " uses value defined by call #" << producerIdx
                         << " which is not scheduled in local cycle "
                         << localState;

                      (void)Cmt2Diagnostic::error(consumerCall.getOperation(),
                                                  os.str())
                          .note(producerCall.getLoc(),
                                "producer call is scheduled in a different "
                                "cycle of this static_step")
                          .hint("insert explicit state (e.g. a Reg) to carry "
                                "the value across cycles, or adjust call "
                                "timing so the producer is active in the same "
                                "cycle as the consumer")
                          .withPythonSource()
                          .emit();
                      return failure();
                    }
                  }
                }

                for (Value operand : defOp->getOperands())
                  worklist.push_back(operand);
              }

              return success();
            };

            // Clone dependencies for operands on-demand before cloning a call.
            std::function<Value(Value)> cloneDef;
            cloneDef = [&](Value v) -> Value {
              if (!v)
                return nullptr;
              if (auto mapped = bodyMapping.lookupOrNull(v))
                return mapped;
              if (auto defOp = v.getDefiningOp()) {
                // Only clone defs that are local to this step body; leave
                // externally-defined values as-is.
                if (defOp->getBlock() != &stepBlock)
                  return v;
                for (Value operand : defOp->getOperands())
                  (void)cloneDef(operand);
                bodyBuilder.clone(*defOp, bodyMapping);
                return bodyMapping.lookup(v);
              }
              // Block argument (shouldn't happen for static_step bodies).
              return v;
            };

            // Clone active calls (and required defs) for this localState.
            int64_t callIdx = 0;
            for (auto &op : stepBlock) {
              auto call = dyn_cast<CallOp>(op);
              if (!call)
                continue;
              auto callTyIt = activeCallTypes.find(callIdx);
              if (callTyIt == activeCallTypes.end()) {
                ++callIdx;
                continue;
              }

              StringRef callTy = callTyIt->second;
              if (failed(checkCrossCycleDependencies(call, callIdx, callTy))) {
                signalPassFailure();
                return;
              }

              if (callTy == "GetRes") {
                // Create a capture-only call clone which does not depend on the
                // original operands. This avoids re-running operand
                // computations (which may contain other calls) in the capture
                // cycle.
                SmallVector<Value> dummyOperands;
                dummyOperands.reserve(call.getNumOperands());
                for (Value operand : call->getOperands()) {
                  dummyOperands.push_back(
                      bodyBuilder.create<firrtl::InvalidValueOp>(
                          loc, operand.getType())
                          .getResult());
                }

                auto clonedCall = bodyBuilder.create<CallOp>(
                    loc, call.getResultTypes(), dummyOperands,
                    call.getCalleeAttr(), call.getMethodOrValueAttr(),
                    call.getArgAttrsAttr(), call.getResAttrsAttr(),
                    /*call_timing=*/nullptr, /*arg_timing=*/nullptr,
                    /*result_timing=*/nullptr, /*call_ty=*/nullptr);
                clonedCall->setAttr("call_ty",
                                    bodyBuilder.getStringAttr(callTy));
                for (auto [origRes, newRes] :
                     llvm::zip(call.getResults(), clonedCall.getResults()))
                  bodyMapping.map(origRes, newRes);
              } else {
                for (Value operand : call->getOperands())
                  (void)cloneDef(operand);
                Operation *cloned = bodyBuilder.clone(op, bodyMapping);
                if (cloned) {
                  OpBuilder attrBuilder(cloned);
                  cloned->setAttr("call_ty", attrBuilder.getStringAttr(callTy));
                }
              }
              ++callIdx;
            }
          } else {
            // Dynamic step: clone full body.
            for (auto &op : bodyRegion->front()) {
              bodyBuilder.clone(op, bodyMapping);
            }
          }
        }
      }
    }

    // Write next state to FSM: cmt2.call @fsmInst @write(%nextState)
    // Handle conditional transitions (if/else branches)
    Value nextStateValue;
    std::string transitionDesc;

    // Special handling for per-branch-FSM fork states: transition when all
    // branch FSMs reach their done state.
    if (isForkState && forkIt->second->needsPerBranchFsm) {
      auto *parBlock = forkIt->second;
      uint64_t joinState = parBlock->joinState;

        // Per-branch FSM mode: check if ALL branch FSMs are at their "done" state
        // Branch state values:
        // - 0 = idle (not started)
        // - 1 to N = active states
        // - N+1 = done (branch completed)
        //
        // Done state for each branch = (lastState - firstState + 1)
        //
        // Join check: all branches at done state -> transition to join
        // State encoding (after fix):
        //   0 = idle (never started)
        //   1..N-1 = active states
        //   N = done (completed), where N = numStates - 1
        // This distinguishes idle from done, preventing the race condition
        // where all branches at 0 on first cycle would incorrectly trigger join.

        auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");

        Value allBranchesDone = nullptr;

        for (const auto &branch : parBlock->branches) {
          auto branchFsmIt = branchFsmInstNames.find(branch.name);
          if (branchFsmIt != branchFsmInstNames.end()) {
            // Look up branch-specific FSM width (may differ from parent FSM)
            unsigned branchWidth = fsmWidth;  // Default to parent width
            auto branchWidthIt = branchFsmWidths.find(branch.name);
            if (branchWidthIt != branchFsmWidths.end()) {
              branchWidth = branchWidthIt->second;
            }
            auto branchFsmType = firrtl::UIntType::get(builder.getContext(), branchWidth);

            auto branchInstanceSym = FlatSymbolRefAttr::get(
                builder.getContext(), branchFsmIt->second);
            auto branchFsmRead = bodyBuilder.create<CallOp>(
                loc, SmallVector<Type>{branchFsmType}, ValueRange{},
                branchInstanceSym, readSym,
                ArrayAttr(), ArrayAttr());

            // Look up done state for this branch (numStates - 1)
            uint64_t doneStateVal = 0;
            auto numStatesIt = branchNumStates.find(branch.name);
            if (numStatesIt != branchNumStates.end()) {
              doneStateVal = numStatesIt->second - 1;  // Done state = numStates - 1
            }

            // Check if at done state (using branch-specific width and type)
            auto doneConst = bodyBuilder.create<firrtl::ConstantOp>(
                loc, branchFsmType, llvm::APInt(branchWidth, doneStateVal)).getResult();
            auto branchAtDone = bodyBuilder.create<firrtl::EQPrimOp>(
                loc, branchFsmRead.getResult(0), doneConst).getResult();

            if (!allBranchesDone) {
              allBranchesDone = branchAtDone;
            } else {
              allBranchesDone = bodyBuilder.create<firrtl::AndPrimOp>(
                  loc, allBranchesDone, branchAtDone).getResult();
            }
          }
        }

        if (!allBranchesDone)
          allBranchesDone = bodyBuilder.create<firrtl::ConstantOp>(
              loc, boolType, llvm::APInt(1, 1))
                               .getResult();

        // Generate: next_state = allBranchesDone ? joinState : forkState
        auto joinConst = bodyBuilder.create<firrtl::ConstantOp>(
            loc, fsmType, llvm::APInt(fsmWidth, joinState)).getResult();
        auto stayConst = bodyBuilder.create<firrtl::ConstantOp>(
            loc, fsmType, llvm::APInt(fsmWidth, state)).getResult();
        nextStateValue = bodyBuilder.create<firrtl::MuxPrimOp>(
            loc, allBranchesDone, joinConst, stayConst).getResult();
        transitionDesc = "all_branches_done ? " + std::to_string(joinState) +
                         " : " + std::to_string(state) + " (per-branch FSM fork-join)";
    } else if (hasTransitions) {
      auto &transitions = transIt->second;

      // Check if single transition with no condition guard
      bool singleUnguarded = (transitions.size() == 1 && transitions[0].guardOpId < 0);
      bool hasParJoinGuard = (transitions.size() == 1 && !transitions[0].parJoinBranches.empty());

      if (singleUnguarded && !hasParJoinGuard) {
        // Single unconditional transition (no condition, no par join)
        uint64_t nextState = transitions[0].toState;
        nextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
            loc, fsmType, llvm::APInt(fsmWidth, nextState)).getResult();
        transitionDesc = std::to_string(nextState);
      } else {
        // Multiple conditional transitions - generate muxed next-state
        // Group transitions by guard condition
        // For if/else: one with guardInverted=false, one with guardInverted=true
        Value condValue = nullptr;
        uint64_t thenState = 0, elseState = 0;
        bool hasThen = false, hasElse = false;

        // Helper lambda to clone the operations that define a value into the body
        // This is needed because the condition is defined in the control region
        // but we need to use it in the generated rule body
        IRMapping condMapping;
        std::function<Value(Value)> cloneConditionDef = [&](Value val) -> Value {
          if (!val)
            return nullptr;

          // Check if already mapped
          if (auto mapped = condMapping.lookupOrNull(val))
            return mapped;

          // If it's a block argument, it should be available in scope
          if (auto blockArg = dyn_cast<BlockArgument>(val)) {
            // Block arguments from proc rule should be available
            return val;
          }

          // Get the defining operation
          Operation *defOp = val.getDefiningOp();
          if (!defOp)
            return val;

          // Recursively clone operands first
          for (Value operand : defOp->getOperands()) {
            cloneConditionDef(operand);
          }

          // Clone the operation
          bodyBuilder.clone(*defOp, condMapping);
          return condMapping.lookup(val);
        };

        for (auto &trans : transitions) {
          if (trans.guardOpId >= 0) {
            // Find the condition operation and get its condition value
            auto condOpIt = condOpIdToOp.find(trans.guardOpId);
            if (condOpIt != condOpIdToOp.end()) {
              Operation *condOp = condOpIt->second;
              Value origCondValue = nullptr;
              if (auto ifOp = dyn_cast<ProcIfOp>(condOp)) {
                origCondValue = ifOp.getCond();
              } else if (auto condIfOp = dyn_cast<ProcCondIfOp>(condOp)) {
                origCondValue = condIfOp.getCond();
              } else if (auto staticIfOp = dyn_cast<ProcStaticIfOp>(condOp)) {
                origCondValue = staticIfOp.getCond();
              } else if (auto whileOp = dyn_cast<ProcWhileOp>(condOp)) {
                origCondValue = whileOp.getCond();
              }

              // Clone the condition computation into the rule body
              if (origCondValue) {
                condValue = cloneConditionDef(origCondValue);
              }
            }

            if (trans.guardInverted) {
              elseState = trans.toState;
              hasElse = true;
            } else {
              thenState = trans.toState;
              hasThen = true;
            }
          } else {
            // Unconditional fallback
            elseState = trans.toState;
            hasElse = true;
          }
        }

        if (condValue && hasThen && hasElse) {
          // Generate: next_state = cond ? thenState : elseState
          auto thenConst = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, thenState)).getResult();
          auto elseConst = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, elseState)).getResult();

          nextStateValue = bodyBuilder.create<firrtl::MuxPrimOp>(
              loc, condValue, thenConst, elseConst).getResult();
          transitionDesc = "cond ? " + std::to_string(thenState) + " : " + std::to_string(elseState);
        } else if (hasThen) {
          // Only then branch (no else)
          nextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, thenState)).getResult();
          transitionDesc = std::to_string(thenState);
        } else if (hasElse) {
          // Only else branch
          nextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, elseState)).getResult();
          transitionDesc = std::to_string(elseState);
        } else {
          // Fallback: stay in same state
          nextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, state)).getResult();
          transitionDesc = std::to_string(state) + " (no transition)";
        }
      }
    } else {
      // No transitions - stay in same state (shouldn't happen normally)
      nextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
          loc, fsmType, llvm::APInt(fsmWidth, state)).getResult();
      transitionDesc = std::to_string(state) + " (no transition)";
    }

    auto writeSym = FlatSymbolRefAttr::get(builder.getContext(), "write");

    // Determine which FSM to write to
    FlatSymbolRefAttr targetFsmSym;
    Value finalNextStateValue = nextStateValue;

    if (isBranchState && !branchFsmName.empty() && branchInfo) {
      // Branch state: write to branch FSM with relative state
      targetFsmSym = FlatSymbolRefAttr::get(builder.getContext(), branchFsmName);

      // Convert absolute next states to relative states for branch FSM
      // If transition goes outside the branch (to join state), set branch FSM to 0 (complete)
      // Otherwise, use relative state: nextState - firstState + 1
      if (hasTransitions) {
        auto &transitions = transIt->second;
        if (transitions.size() == 1 && transitions[0].guardOpId < 0 &&
            transitions[0].parJoinBranches.empty()) {
          // Single unconditional transition
          uint64_t nextAbsState = transitions[0].toState;
          // Done state = max relative state + 1
          uint64_t branchDoneState = branchInfo->lastState - branchInfo->firstState + 1;
          // Check if this transition is within the branch or exits
          uint64_t relNextState;
          if (nextAbsState >= branchInfo->firstState && nextAbsState < branchInfo->lastState) {
            relNextState = nextAbsState - branchInfo->firstState + 1;
          } else {
            // Transition exits the branch (to join state) - mark complete
            relNextState = branchDoneState; // Use done state, not 0
          }
          finalNextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, relNextState)).getResult();
        } else if (transitions.size() >= 1) {
          // Conditional transitions - need to convert each target to relative
          // This handles while loop: cond ? body_state : exit (0)
          Value condValue = nullptr;
          uint64_t thenAbsState = 0, elseAbsState = 0;
          bool hasThen = false, hasElse = false;

          for (auto &trans : transitions) {
            if (trans.guardOpId >= 0) {
              // Conditional transition
              auto condOpIt = condOpIdToOp.find(trans.guardOpId);
              if (condOpIt != condOpIdToOp.end()) {
                Operation *condOp = condOpIt->second;
                Value origCondValue = nullptr;
                if (auto whileOp = dyn_cast<ProcWhileOp>(condOp)) {
                  origCondValue = whileOp.getCond();
                } else if (auto ifOp = dyn_cast<ProcIfOp>(condOp)) {
                  origCondValue = ifOp.getCond();
                } else if (auto condIfOp = dyn_cast<ProcCondIfOp>(condOp)) {
                  origCondValue = condIfOp.getCond();
                }
                if (origCondValue && !condValue) {
                  // Clone condition into body
                  IRMapping condMapping;
                  std::function<Value(Value)> cloneCond = [&](Value val) -> Value {
                    if (auto mapped = condMapping.lookupOrNull(val))
                      return mapped;
                    if (auto defOp = val.getDefiningOp()) {
                      for (Value operand : defOp->getOperands())
                        cloneCond(operand);
                      bodyBuilder.clone(*defOp, condMapping);
                      return condMapping.lookup(val);
                    }
                    return val;
                  };
                  condValue = cloneCond(origCondValue);
                }
              }
              if (trans.guardInverted) {
                elseAbsState = trans.toState;
                hasElse = true;
              } else {
                thenAbsState = trans.toState;
                hasThen = true;
              }
            } else {
              elseAbsState = trans.toState;
              hasElse = true;
            }
          }

          if (condValue && hasThen && hasElse) {
            // Convert both targets to relative
            // Use special "done" state for branch exit instead of 0
            // Done state = max relative state + 1 = (lastState - firstState + 1)
            uint64_t branchDoneState = branchInfo->lastState - branchInfo->firstState + 1;
            auto toRelative = [&](uint64_t absState) -> uint64_t {
              if (absState >= branchInfo->firstState && absState < branchInfo->lastState) {
                return absState - branchInfo->firstState + 1;
              }
              return branchDoneState; // Exit branch -> done state (not 0)
            };
            uint64_t relThen = toRelative(thenAbsState);
            uint64_t relElse = toRelative(elseAbsState);
            auto thenConst = bodyBuilder.create<firrtl::ConstantOp>(
                loc, fsmType, llvm::APInt(fsmWidth, relThen)).getResult();
            auto elseConst = bodyBuilder.create<firrtl::ConstantOp>(
                loc, fsmType, llvm::APInt(fsmWidth, relElse)).getResult();
            finalNextStateValue = bodyBuilder.create<firrtl::MuxPrimOp>(
                loc, condValue, thenConst, elseConst).getResult();
          }
        }
      }
    } else {
      // Main FSM state
      targetFsmSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
    }

    bodyBuilder.create<CallOp>(
        loc, SmallVector<Type>{}, ValueRange{finalNextStateValue},
        targetFsmSym, writeSym,
        ArrayAttr(), ArrayAttr());

    bodyBuilder.create<ReturnOp>(loc);

    LLVM_DEBUG(llvm::dbgs() << "Generated rule @" << stateRuleName
                            << " for state " << state << " -> " << transitionDesc
                            << (hasSteps ? " with steps" : " (wait state)")
                            << "\n");
  }

  // Generate reset rule: when in done state, go back to idle
  std::string resetRuleName = (ruleName + "_reset").str();
  auto funcType = builder.getFunctionType({}, {});
  auto funcTypeAttr = TypeAttr::get(funcType);

  auto resetRule = builder.create<RuleOp>(
      loc, builder.getStringAttr(resetRuleName), funcTypeAttr,
      builder.getArrayAttr({}), builder.getArrayAttr({}),
      ArrayAttr(), ArrayAttr());

  // Guard: fsm.read() == doneState
  Block *guardBlock = new Block();
  resetRule.getGuard().push_back(guardBlock);
  OpBuilder guardBuilder(guardBlock, guardBlock->begin());

  auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
  auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
  auto fsmReadCall = guardBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());

  auto doneStateConst = guardBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, doneState));
  auto inDoneState = guardBuilder.create<firrtl::EQPrimOp>(
      loc, fsmReadCall.getResult(0), doneStateConst.getResult());

  guardBuilder.create<ReturnOp>(loc, ValueRange{inDoneState.getResult()});

  // Body: write 0 to FSM
  Block *bodyBlock = new Block();
  resetRule.getBody().push_back(bodyBlock);
  OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

  auto zeroConst = bodyBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, 0));
  auto writeSym = FlatSymbolRefAttr::get(builder.getContext(), "write");
  bodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{}, ValueRange{zeroConst.getResult()},
      instanceSym, writeSym,
      ArrayAttr(), ArrayAttr());

  bodyBuilder.create<ReturnOp>(loc);
}

//===----------------------------------------------------------------------===//
// Branch FSM Rule Generation (for complex par)
//===----------------------------------------------------------------------===//

void ProcStmtToActionPass::generateBranchFsmRules(
    ProcRuleOp procRule, cmt2::ModuleOp module, OpBuilder &builder,
    StringRef mainFsmInstName, unsigned fsmWidth, ArrayAttr complexParsAttr,
    const DenseMap<StringRef, std::string> &branchFsmInstNames,
    const DenseMap<StringRef, unsigned> &branchFsmWidths) {

  if (!complexParsAttr)
    return;

  Location loc = procRule.getLoc();
  MLIRContext *ctx = builder.getContext();
  auto mainFsmType = firrtl::UIntType::get(ctx, fsmWidth);
  auto boolType = firrtl::UIntType::get(ctx, 1);
  StringRef procName = procRule.getSymName();

  auto readSym = FlatSymbolRefAttr::get(ctx, "read");
  auto writeSym = FlatSymbolRefAttr::get(ctx, "write");
  auto mainInstanceSym = FlatSymbolRefAttr::get(ctx, mainFsmInstName);

  // Build step name to step operation map
  DenseMap<StringRef, Operation *> stepMap;
  for (auto &op : module.getBodyRegion().front()) {
    if (auto step = dyn_cast<ProcStepOp>(op)) {
      stepMap[step.getSymName()] = step;
    } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(op)) {
      stepMap[staticStep.getSymName()] = staticStep;
    }
  }

  // Process each complex par block
  for (auto cpAttr : complexParsAttr) {
    auto cpDict = cast<DictionaryAttr>(cpAttr);
    uint64_t parId = cpDict.getAs<IntegerAttr>("par_id").getInt();
    uint64_t forkState = cpDict.getAs<IntegerAttr>("fork_state").getInt();
    (void)parId; // Suppress unused warning

    auto branchFsmsAttr = cpDict.getAs<ArrayAttr>("branch_fsms");
    if (!branchFsmsAttr)
      continue;

    // Generate rules for each branch FSM
    for (auto bfAttr : branchFsmsAttr) {
      auto bfDict = cast<DictionaryAttr>(bfAttr);
      std::string branchName = bfDict.getAs<StringAttr>("name").getValue().str();
      uint64_t numStates = bfDict.getAs<IntegerAttr>("num_states").getInt();
      auto enablesAttr = bfDict.getAs<ArrayAttr>("enables");
      auto transitionsAttr = bfDict.getAs<ArrayAttr>("transitions");

      // Find the branch FSM instance name
      auto branchFsmIt = branchFsmInstNames.find(branchName);
      if (branchFsmIt == branchFsmInstNames.end()) {
        LLVM_DEBUG(llvm::dbgs() << "Warning: No FSM instance for branch "
                                << branchName << "\n");
        continue;
      }
      auto branchInstanceSym = FlatSymbolRefAttr::get(ctx, branchFsmIt->second);

      // Look up branch-specific FSM width (may differ from main FSM width)
      unsigned branchWidth = fsmWidth;  // Default to main FSM width
      auto branchWidthIt = branchFsmWidths.find(branchName);
      if (branchWidthIt != branchFsmWidths.end()) {
        branchWidth = branchWidthIt->second;
      }
      auto branchFsmType = firrtl::UIntType::get(ctx, branchWidth);

      // Build transition map: from_state -> to_state
      // For unconditional transitions (enables), prefer back-edges (lower state)
      // over forward transitions. This ensures while body states loop back
      // to the header instead of going to the done state.
      DenseMap<uint64_t, uint64_t> transitionMap;
      if (transitionsAttr) {
        for (auto tAttr : transitionsAttr) {
          auto tDict = cast<DictionaryAttr>(tAttr);
          uint64_t fromState = tDict.getAs<IntegerAttr>("from").getInt();
          uint64_t toState = tDict.getAs<IntegerAttr>("to").getInt();
          // Skip conditional transitions - they're handled by condition rules
          if (tDict.get("guard_while_idx"))
            continue;
          // Prefer back-edges (toState < fromState) for loop continuation
          auto it = transitionMap.find(fromState);
          if (it == transitionMap.end()) {
            transitionMap[fromState] = toState;
          } else if (toState < fromState && it->second >= fromState) {
            // New is a back-edge and existing is forward - prefer back-edge
            transitionMap[fromState] = toState;
          }
        }
      }

      // Generate enable rules for each branch state (1 to numStates-1)
      // State 0 is idle/done, not active
      if (enablesAttr) {
        for (auto eAttr : enablesAttr) {
          auto eDict = cast<DictionaryAttr>(eAttr);
          uint64_t state = eDict.getAs<IntegerAttr>("state").getInt();
          StringRef stepName = eDict.getAs<StringAttr>("step").getValue();
          bool isStatic = eDict.getAs<BoolAttr>("is_static").getValue();
          int64_t latency = eDict.getAs<IntegerAttr>("latency").getInt();

          // Create enable rule: @{procName}_{branchName}_state{state}
          std::string stateRuleName =
              (procName + "_" + branchName + "_state" + std::to_string(state)).str();

          auto funcType = builder.getFunctionType({}, {});
          auto funcTypeAttr = TypeAttr::get(funcType);

          auto stateRule = builder.create<RuleOp>(
              loc, builder.getStringAttr(stateRuleName), funcTypeAttr,
              builder.getArrayAttr({}), builder.getArrayAttr({}),
              ArrayAttr(), ArrayAttr());

          // Build guard: main_fsm == fork_state AND branch_fsm == state
          Block *guardBlock = new Block();
          stateRule.getGuard().push_back(guardBlock);
          OpBuilder guardBuilder(guardBlock, guardBlock->begin());

          // Read main FSM
          auto mainFsmRead = guardBuilder.create<CallOp>(
              loc, SmallVector<Type>{mainFsmType}, ValueRange{},
              mainInstanceSym, readSym, ArrayAttr(), ArrayAttr());
          auto forkConst = guardBuilder.create<firrtl::ConstantOp>(
              loc, mainFsmType, llvm::APInt(fsmWidth, forkState));
          auto mainAtFork = guardBuilder.create<firrtl::EQPrimOp>(
              loc, mainFsmRead.getResult(0), forkConst.getResult());

          // Read branch FSM (using branch-specific type/width)
          auto branchFsmRead = guardBuilder.create<CallOp>(
              loc, SmallVector<Type>{branchFsmType}, ValueRange{},
              branchInstanceSym, readSym, ArrayAttr(), ArrayAttr());
          auto stateConst = guardBuilder.create<firrtl::ConstantOp>(
              loc, branchFsmType, llvm::APInt(branchWidth, state));
          auto branchAtState = guardBuilder.create<firrtl::EQPrimOp>(
              loc, branchFsmRead.getResult(0), stateConst.getResult());

          // Combine guards
          auto guardResult = guardBuilder.create<firrtl::AndPrimOp>(
              loc, mainAtFork.getResult(), branchAtState.getResult());
          guardBuilder.create<ReturnOp>(loc, ValueRange{guardResult.getResult()});

          // Build body: execute step body + write next state to branch FSM
          Block *bodyBlock = new Block();
          stateRule.getBody().push_back(bodyBlock);
          OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

          // Clone step body.
          auto stepIt = stepMap.find(stepName);
          if (stepIt != stepMap.end()) {
            Region *bodyRegion = nullptr;
            if (auto step = dyn_cast<ProcStepOp>(stepIt->second)) {
              bodyRegion = &step.getBody();
            } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(stepIt->second)) {
              bodyRegion = &staticStep.getBody();
            }

            if (bodyRegion && !bodyRegion->empty()) {
              IRMapping bodyMapping;
              for (auto &op : bodyRegion->front()) {
                bodyBuilder.clone(op, bodyMapping);
              }
            }
          }

          // Write next state to branch FSM
          // For static steps with latency > 1, use internal state transitions
          // State encoding: 0=idle, 1..N-2=active, N-1=done (where N=numStates)
          uint64_t doneStateVal = numStates - 1;
          uint64_t nextState = doneStateVal;  // Default to done state
          if (isStatic && latency > 1) {
            // Multi-cycle static step: advance within step states
            // The transition from last cycle goes to next state or done
            nextState = state + 1;
            if (state + latency - 1 >= numStates - 2) {
              nextState = doneStateVal; // Go to done state
            }
          } else {
            // Single-cycle or dynamic: use transition map
            auto transIt = transitionMap.find(state);
            if (transIt != transitionMap.end()) {
              nextState = transIt->second;
            } else {
              // Default: go to done state
              nextState = doneStateVal;
            }
          }

          auto nextStateConst = bodyBuilder.create<firrtl::ConstantOp>(
              loc, branchFsmType, llvm::APInt(branchWidth, nextState));
          bodyBuilder.create<CallOp>(
              loc, SmallVector<Type>{}, ValueRange{nextStateConst.getResult()},
              branchInstanceSym, writeSym, ArrayAttr(), ArrayAttr());

          bodyBuilder.create<ReturnOp>(loc);

          LLVM_DEBUG(llvm::dbgs() << "  Generated branch state rule: "
                                  << stateRuleName << " (state " << state
                                  << " -> " << nextState << ")\n");
        }
      }

      // Collect while ops from the proc rule's control region
      SmallVector<ProcWhileOp> whileOps;
      std::function<void(Region *)> collectWhileOps = [&](Region *region) {
        if (!region || region->empty())
          return;
        for (auto &op : region->front()) {
          if (auto whileOp = dyn_cast<ProcWhileOp>(op)) {
            whileOps.push_back(whileOp);
          }
          for (auto &nestedRegion : op.getRegions()) {
            collectWhileOps(&nestedRegion);
          }
        }
      };
      collectWhileOps(&procRule.getControl());

      // Build set of states that have enables (so we know which states need condition-only rules)
      DenseSet<uint64_t> statesWithEnables;
      if (enablesAttr) {
        for (auto eAttr : enablesAttr) {
          auto eDict = cast<DictionaryAttr>(eAttr);
          uint64_t state = eDict.getAs<IntegerAttr>("state").getInt();
          statesWithEnables.insert(state);
        }
      }

      // Find states that have conditional transitions (while header states)
      // These states need rules that evaluate the condition and advance the FSM
      DenseMap<uint64_t, SmallVector<std::tuple<uint64_t, int64_t, bool>>> condTransitions;
      if (transitionsAttr) {
        for (auto tAttr : transitionsAttr) {
          auto tDict = cast<DictionaryAttr>(tAttr);
          if (auto whileIdxAttr = tDict.getAs<IntegerAttr>("guard_while_idx")) {
            uint64_t fromState = tDict.getAs<IntegerAttr>("from").getInt();
            uint64_t toState = tDict.getAs<IntegerAttr>("to").getInt();
            int64_t whileIdx = whileIdxAttr.getInt();
            bool inverted = false;
            if (auto invertedAttr = tDict.getAs<BoolAttr>("guard_inverted")) {
              inverted = invertedAttr.getValue();
            }
            condTransitions[fromState].push_back({toState, whileIdx, inverted});
          }
        }
      }

      // Generate rules for while header states (states with conditional transitions but no enables)
      for (auto &[headerState, transitions] : condTransitions) {
        if (statesWithEnables.contains(headerState))
          continue;  // Skip if this state already has an enable rule

        // Find the while op for this state (use the first transition's while index)
        if (transitions.empty())
          continue;
        auto [_, whileIdx, __] = transitions[0];
        if (whileIdx < 0 || static_cast<size_t>(whileIdx) >= whileOps.size())
          continue;

        ProcWhileOp whileOp = whileOps[whileIdx];

        // Create condition check rule: @{procName}_{branchName}_cond{state}
        std::string condRuleName =
            (procName + "_" + branchName + "_cond" + std::to_string(headerState)).str();

        auto funcType = builder.getFunctionType({}, {});
        auto funcTypeAttr = TypeAttr::get(funcType);

        auto condRule = builder.create<RuleOp>(
            loc, builder.getStringAttr(condRuleName), funcTypeAttr,
            builder.getArrayAttr({}), builder.getArrayAttr({}),
            ArrayAttr(), ArrayAttr());

        // Build guard: main_fsm == fork_state AND branch_fsm == headerState
        Block *guardBlock = new Block();
        condRule.getGuard().push_back(guardBlock);
        OpBuilder guardBuilder(guardBlock, guardBlock->begin());

        // Read main FSM
        auto mainFsmRead = guardBuilder.create<CallOp>(
            loc, SmallVector<Type>{mainFsmType}, ValueRange{},
            mainInstanceSym, readSym, ArrayAttr(), ArrayAttr());
        auto forkConst = guardBuilder.create<firrtl::ConstantOp>(
            loc, mainFsmType, llvm::APInt(fsmWidth, forkState));
        auto mainAtFork = guardBuilder.create<firrtl::EQPrimOp>(
            loc, mainFsmRead.getResult(0), forkConst.getResult());

        // Read branch FSM (using branch-specific type/width)
        auto branchFsmRead = guardBuilder.create<CallOp>(
            loc, SmallVector<Type>{branchFsmType}, ValueRange{},
            branchInstanceSym, readSym, ArrayAttr(), ArrayAttr());
        auto stateConst = guardBuilder.create<firrtl::ConstantOp>(
            loc, branchFsmType, llvm::APInt(branchWidth, headerState));
        auto branchAtState = guardBuilder.create<firrtl::EQPrimOp>(
            loc, branchFsmRead.getResult(0), stateConst.getResult());

        // Combine guards
        auto guardResult = guardBuilder.create<firrtl::AndPrimOp>(
            loc, mainAtFork.getResult(), branchAtState.getResult());
        guardBuilder.create<ReturnOp>(loc, ValueRange{guardResult.getResult()});

        // Build body: evaluate while condition, write next state
        Block *bodyBlock = new Block();
        condRule.getBody().push_back(bodyBlock);
        OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

        // Clone the while condition from the while op's condition region
        IRMapping condMapping;
        Value condValue;
        for (auto &op : whileOp.getCondRegion().front()) {
          if (auto yieldOp = dyn_cast<ProcWhileCondYieldOp>(op)) {
            // Get the yielded condition value
            condValue = condMapping.lookupOrDefault(yieldOp.getCond());
          } else {
            bodyBuilder.clone(op, condMapping);
          }
        }

        if (!condValue) {
          // Fallback: use constant true if condition couldn't be cloned
          condValue = bodyBuilder.create<firrtl::ConstantOp>(
              loc, boolType, llvm::APInt(1, 1)).getResult();
        }

        // Find the target states for condition true and false
        uint64_t trueTarget = headerState;  // Default: stay at header
        uint64_t falseTarget = numStates - 1;  // Default: done state
        for (auto &[toState, wIdx, inverted] : transitions) {
          if (wIdx != whileIdx)
            continue;
          if (!inverted)
            trueTarget = toState;
          else
            falseTarget = toState;
        }

        // Write next state: mux(cond, trueTarget, falseTarget) using branch-specific type/width
        auto trueConst = bodyBuilder.create<firrtl::ConstantOp>(
            loc, branchFsmType, llvm::APInt(branchWidth, trueTarget));
        auto falseConst = bodyBuilder.create<firrtl::ConstantOp>(
            loc, branchFsmType, llvm::APInt(branchWidth, falseTarget));
        auto nextState = bodyBuilder.create<firrtl::MuxPrimOp>(
            loc, condValue, trueConst.getResult(), falseConst.getResult());

        bodyBuilder.create<CallOp>(
            loc, SmallVector<Type>{}, ValueRange{nextState.getResult()},
            branchInstanceSym, writeSym, ArrayAttr(), ArrayAttr());

        bodyBuilder.create<ReturnOp>(loc);

        LLVM_DEBUG(llvm::dbgs() << "  Generated while condition rule: "
                                << condRuleName << " (state " << headerState
                                << " -> " << trueTarget << "/" << falseTarget << ")\n");
      }

      // Generate done value: @{procName}_{branchName}_done
      // Done state = numStates - 1 (state encoding: 0=idle, 1..N-1=active, N=done)
      uint64_t branchDoneState = numStates - 1;
      std::string doneValueName = (procName + "_" + branchName + "_done").str();
      {
        auto funcType = builder.getFunctionType({}, {boolType});
        auto funcTypeAttr = TypeAttr::get(funcType);

        auto doneValue = builder.create<ValueOp>(
            loc, builder.getStringAttr(doneValueName), funcTypeAttr,
            builder.getArrayAttr({}), builder.getArrayAttr({}),
            ArrayAttr(), ArrayAttr());

        // Guard (empty for values)
        Block *guardBlock = new Block();
        doneValue.getGuard().push_back(guardBlock);
        OpBuilder guardBuilder(guardBlock, guardBlock->begin());
        guardBuilder.create<ReturnOp>(loc);

        // Body: return branch_fsm == doneState (using branch-specific type/width)
        Block *bodyBlock = new Block();
        doneValue.getBody().push_back(bodyBlock);
        OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());

        auto branchFsmRead = bodyBuilder.create<CallOp>(
            loc, SmallVector<Type>{branchFsmType}, ValueRange{},
            branchInstanceSym, readSym, ArrayAttr(), ArrayAttr());
        auto doneStateConst = bodyBuilder.create<firrtl::ConstantOp>(
            loc, branchFsmType, llvm::APInt(branchWidth, branchDoneState));
        auto isDone = bodyBuilder.create<firrtl::EQPrimOp>(
            loc, branchFsmRead.getResult(0), doneStateConst.getResult());
        bodyBuilder.create<ReturnOp>(loc, isDone.getResult());

        LLVM_DEBUG(llvm::dbgs() << "  Generated branch done value: "
                                << doneValueName << " (done state = "
                                << branchDoneState << ")\n");
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Status Value Generation
//===----------------------------------------------------------------------===//

void ProcStmtToActionPass::generateStatusValues(
    ProcRuleOp procRule, cmt2::ModuleOp module, OpBuilder &builder,
    StringRef fsmInstName, unsigned fsmWidth, uint64_t doneState) {

  Location loc = procRule.getLoc();
  auto fsmType = firrtl::UIntType::get(builder.getContext(), fsmWidth);
  auto boolType = firrtl::UIntType::get(builder.getContext(), 1);
  StringRef ruleName = procRule.getSymName();

  // Generate @{ruleName}__idle value
  std::string idleValueName = (ruleName + "__idle").str();
  auto idleFuncType = builder.getFunctionType({}, {boolType});
  auto idleFuncTypeAttr = TypeAttr::get(idleFuncType);

  auto idleValue = builder.create<ValueOp>(
      loc, builder.getStringAttr(idleValueName), idleFuncTypeAttr,
      builder.getArrayAttr({}), builder.getArrayAttr({}),
      ArrayAttr(), ArrayAttr());

  // Guard: always true
  Block *idleGuardBlock = new Block();
  idleValue.getGuard().push_back(idleGuardBlock);
  OpBuilder idleGuardBuilder(idleGuardBlock, idleGuardBlock->begin());
  auto trueConst = idleGuardBuilder.create<firrtl::ConstantOp>(
      loc, boolType, llvm::APInt(1, 1));
  idleGuardBuilder.create<ReturnOp>(loc, ValueRange{trueConst.getResult()});

  // Body: return fsm.read() == 0
  Block *idleBodyBlock = new Block();
  idleValue.getBody().push_back(idleBodyBlock);
  OpBuilder idleBodyBuilder(idleBodyBlock, idleBodyBlock->begin());

  auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
  auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
  auto fsmReadCall = idleBodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());

  auto zeroConst = idleBodyBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, 0));
  auto isIdle = idleBodyBuilder.create<firrtl::EQPrimOp>(
      loc, fsmReadCall.getResult(0), zeroConst.getResult());
  idleBodyBuilder.create<ReturnOp>(loc, ValueRange{isIdle.getResult()});

  // Generate @{ruleName}__running value
  std::string runningValueName = (ruleName + "__running").str();
  auto runningValue = builder.create<ValueOp>(
      loc, builder.getStringAttr(runningValueName), idleFuncTypeAttr,
      builder.getArrayAttr({}), builder.getArrayAttr({}),
      ArrayAttr(), ArrayAttr());

  // Guard: always true
  Block *runGuardBlock = new Block();
  runningValue.getGuard().push_back(runGuardBlock);
  OpBuilder runGuardBuilder(runGuardBlock, runGuardBlock->begin());
  auto trueConst2 = runGuardBuilder.create<firrtl::ConstantOp>(
      loc, boolType, llvm::APInt(1, 1));
  runGuardBuilder.create<ReturnOp>(loc, ValueRange{trueConst2.getResult()});

  // Body: return fsm.read() != 0
  Block *runBodyBlock = new Block();
  runningValue.getBody().push_back(runBodyBlock);
  OpBuilder runBodyBuilder(runBodyBlock, runBodyBlock->begin());

  auto fsmReadCall2 = runBodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());

  auto zeroConst2 = runBodyBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, llvm::APInt(fsmWidth, 0));
  auto notZero = runBodyBuilder.create<firrtl::NEQPrimOp>(
      loc, fsmReadCall2.getResult(0), zeroConst2.getResult());
  runBodyBuilder.create<ReturnOp>(loc, ValueRange{notZero.getResult()});

  LLVM_DEBUG(llvm::dbgs() << "Generated status values @" << idleValueName
                          << " and @" << runningValueName << "\n");
}

//===----------------------------------------------------------------------===//
// Procedural Rule Processing
//===----------------------------------------------------------------------===//

LogicalResult ProcStmtToActionPass::processProcRule(ProcRuleOp rule,
                                                     cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing proc rule @" << rule.getSymName()
                          << "\n");

  // Check for TDCC metadata
  auto numStatesAttr = rule->getAttrOfType<IntegerAttr>("tdcc.num_states");
  auto fsmWidthAttr = rule->getAttrOfType<IntegerAttr>("tdcc.fsm_width");
  auto doneStateAttr = rule->getAttrOfType<IntegerAttr>("tdcc.done_state");
  auto enablesAttr = rule->getAttrOfType<ArrayAttr>("tdcc.enables");
  auto transitionsAttr = rule->getAttrOfType<ArrayAttr>("tdcc.transitions");

  if (!numStatesAttr || !fsmWidthAttr || !doneStateAttr) {
    LLVM_DEBUG(llvm::dbgs() << "  Skipping - no TDCC metadata\n");
    return success();
  }

  unsigned fsmWidth = fsmWidthAttr.getInt();
  uint64_t doneState = doneStateAttr.getInt();

  // Find or create register module for FSM
  auto circuit = module->getParentOfType<CircuitOp>();
  OpBuilder circuitBuilder(circuit);
  auto regMod = findRegisterModule(circuit, fsmWidth);
  if (!regMod) {
    // Create the FSM register module automatically
    regMod = createFSMRegisterModule(circuit, fsmWidth, circuitBuilder);
    if (!regMod) {
      return reportConversionError(rule, "failed to create FSM register module")
          .note("could not create register module for FSM width " +
                std::to_string(fsmWidth))
          .emit();
    }
  }

  // Get clock and reset from module arguments
  Value clock, reset;
  for (auto arg : module.getBody().getArguments()) {
    if (isa<firrtl::ClockType>(arg.getType())) {
      clock = arg;
    } else if (auto uintType = dyn_cast<firrtl::UIntType>(arg.getType())) {
      if (uintType.getWidth() == 1 && !reset) {
        reset = arg;
      }
    }
  }

  if (!clock || !reset) {
    return reportConversionError(rule, "could not find clock/reset for FSM")
        .note("procedural rules require clock and reset signals")
        .hint("ensure module has clock and reset arguments")
        .emit();
  }

  // Create FSM instance
  OpBuilder builder(rule);
  builder.setInsertionPointAfter(rule);

  Location loc = rule.getLoc();
  std::string fsmInstName = ("__fsm_" + rule.getSymName()).str();

  // Get the module name from the register module
  StringRef regModName;
  if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(regMod)) {
    regModName = extMod.getSymName();
  }

  // Create main FSM instance
  builder.create<InstanceOp>(
      loc, builder.getStringAttr(fsmInstName),
      ValueRange{clock, reset},
      FlatSymbolRefAttr::get(builder.getContext(), regModName),
      ArrayAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Created FSM instance @" << fsmInstName << "\n");

  // Create per-branch FSM instances for parallel blocks that need them
  // Map from branch name to FSM instance name and width
  DenseMap<StringRef, std::string> branchFsmInstNames;
  DenseMap<StringRef, unsigned> branchFsmWidths;

  // Parse tdcc.complex_pars for detailed branch FSM info (new per-branch FSM approach)
  auto complexParsAttr = rule->getAttrOfType<ArrayAttr>("tdcc.complex_pars");
  if (complexParsAttr) {
    for (auto cpAttr : complexParsAttr) {
      auto cpDict = cast<DictionaryAttr>(cpAttr);
      if (auto branchFsmsAttr = cpDict.getAs<ArrayAttr>("branch_fsms")) {
        for (auto bfAttr : branchFsmsAttr) {
          auto bfDict = cast<DictionaryAttr>(bfAttr);
          auto branchName = bfDict.getAs<StringAttr>("name").getValue();
          uint64_t branchNumStates = bfDict.getAs<IntegerAttr>("num_states").getInt();

          // Calculate branch FSM width from num_states
          unsigned branchWidth = llvm::Log2_64_Ceil(branchNumStates);
          if (branchWidth == 0) branchWidth = 1;  // Minimum 1 bit
          branchFsmWidths[branchName] = branchWidth;

          // Find or create register module with branch-specific width
          auto branchRegMod = findRegisterModule(circuit, branchWidth);
          if (!branchRegMod) {
            branchRegMod = createFSMRegisterModule(circuit, branchWidth, circuitBuilder);
          }
          StringRef branchRegModName;
          if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(branchRegMod)) {
            branchRegModName = extMod.getSymName();
          }

          // Create FSM instance for this branch with branch-specific register module
          std::string branchFsmName = (fsmInstName + "_" + branchName).str();
          builder.create<InstanceOp>(
              loc, builder.getStringAttr(branchFsmName),
              ValueRange{clock, reset},
              FlatSymbolRefAttr::get(builder.getContext(), branchRegModName),
              ArrayAttr());

          branchFsmInstNames[branchName] = branchFsmName;
          LLVM_DEBUG(llvm::dbgs() << "  Created branch FSM instance @"
                                  << branchFsmName << " (width=" << branchWidth
                                  << ", numStates=" << branchNumStates
                                  << ", from complex_pars)\n");
        }
      }
    }
  }

  // Fallback: also check tdcc.par_blocks for backwards compatibility
  if (auto parBlocksAttr = rule->getAttrOfType<ArrayAttr>("tdcc.par_blocks")) {
    for (auto blockAttr : parBlocksAttr) {
      auto dict = cast<DictionaryAttr>(blockAttr);
      bool needsPerBranchFsm = false;
      if (auto needsFsmAttr = dict.getAs<BoolAttr>("needs_per_branch_fsm"))
        needsPerBranchFsm = needsFsmAttr.getValue();

      if (needsPerBranchFsm) {
        if (auto branchesAttr = dict.getAs<ArrayAttr>("branches")) {
          for (auto branchAttr : branchesAttr) {
            auto brDict = cast<DictionaryAttr>(branchAttr);
            auto branchName = brDict.getAs<StringAttr>("name").getValue();

            // Skip if already created from complex_pars
            if (branchFsmInstNames.count(branchName))
              continue;

            // For par_blocks fallback, use main FSM width (less precise but backwards compatible)
            branchFsmWidths[branchName] = fsmWidth;

            // Create FSM instance for this branch
            std::string branchFsmName = (fsmInstName + "_" + branchName).str();
            builder.create<InstanceOp>(
                loc, builder.getStringAttr(branchFsmName),
                ValueRange{clock, reset},
                FlatSymbolRefAttr::get(builder.getContext(), regModName),
                ArrayAttr());

            branchFsmInstNames[branchName] = branchFsmName;
            LLVM_DEBUG(llvm::dbgs() << "  Created branch FSM instance @"
                                    << branchFsmName << " (from par_blocks)\n");
          }
        }
      }
    }
  }

  // Generate state-based rules
  generateStateRules(rule, module, builder, fsmInstName, fsmWidth, enablesAttr,
                     transitionsAttr, doneState, complexParsAttr,
                     branchFsmInstNames, branchFsmWidths);

  // Generate branch FSM rules for complex par (new per-branch FSM approach)
  generateBranchFsmRules(rule, module, builder, fsmInstName, fsmWidth,
                         complexParsAttr, branchFsmInstNames, branchFsmWidths);

  // Generate idle/running value methods
  generateStatusValues(rule, module, builder, fsmInstName, fsmWidth, doneState);

  // Update module precedence to include generated state rules
  // Principle: Later states in control flow have HIGHER precedence (pipeline semantics)
  // This ensures downstream stages drain before upstream produces more.
  {
    SmallVector<Attribute> newPrecedenceChains;

    // Get existing precedence
    if (auto existingPrec = module->getAttrOfType<ArrayAttr>("precedence")) {
      for (auto chain : existingPrec) {
        newPrecedenceChains.push_back(chain);
      }
    }

    // Build precedence chain using state sequence order from TDCC
    // Later states (higher seq_idx) have higher precedence, so they go first in chain
    StringRef ruleName = rule.getSymName();
    SmallVector<Attribute> stateChain;

    // Read state order from TDCC
    auto stateOrderAttr = rule->getAttrOfType<ArrayAttr>("tdcc.state_order");
    if (stateOrderAttr) {
      // Parse state order: [{state: N, seq_idx: M}, ...]
      SmallVector<std::pair<uint64_t, uint64_t>> stateSeqPairs; // (state, seq_idx)
      for (auto attr : stateOrderAttr) {
        auto dict = cast<DictionaryAttr>(attr);
        uint64_t state = dict.getAs<IntegerAttr>("state").getInt();
        uint64_t seqIdx = dict.getAs<IntegerAttr>("seq_idx").getInt();
        stateSeqPairs.push_back({state, seqIdx});
      }

      // Sort by sequence index DESCENDING (later states first = higher precedence)
      llvm::sort(stateSeqPairs, [](auto &a, auto &b) {
        return a.second > b.second;
      });

      // Build precedence chain for main FSM states
      for (auto &[state, seqIdx] : stateSeqPairs) {
        std::string stateName = (ruleName + "_state" + std::to_string(state)).str();
        stateChain.push_back(FlatSymbolRefAttr::get(builder.getContext(), stateName));
      }

      LLVM_DEBUG(llvm::dbgs() << "  Generated precedence chain with "
                              << stateSeqPairs.size() << " states (by seq_idx desc)\n");
    } else {
      // Fallback: Use old par_blocks-based approach if no state_order
      SmallVector<uint64_t> forkJoinStates;
      SmallVector<uint64_t> branchHeaderStates;
      SmallVector<uint64_t> branchBodyStates;

      if (auto parBlocksAttr = rule->getAttrOfType<ArrayAttr>("tdcc.par_blocks")) {
        for (auto blockAttr : parBlocksAttr) {
          auto dict = cast<DictionaryAttr>(blockAttr);
          uint64_t forkState = dict.getAs<IntegerAttr>("fork_state").getInt();
          uint64_t joinState = dict.getAs<IntegerAttr>("join_state").getInt();
          forkJoinStates.push_back(forkState);
          forkJoinStates.push_back(joinState);

          if (auto needsFsmAttr = dict.getAs<BoolAttr>("needs_per_branch_fsm")) {
            if (needsFsmAttr.getValue()) {
              if (auto branchesAttr = dict.getAs<ArrayAttr>("branches")) {
                for (auto branchAttr : branchesAttr) {
                  auto brDict = cast<DictionaryAttr>(branchAttr);
                  uint64_t firstState = brDict.getAs<IntegerAttr>("first_state").getInt();
                  uint64_t lastState = brDict.getAs<IntegerAttr>("last_state").getInt();
                  branchHeaderStates.push_back(firstState);
                  for (uint64_t s = firstState + 1; s < lastState; ++s) {
                    branchBodyStates.push_back(s);
                  }
                }
              }
            }
          }
        }
      }

      // Build precedence: body states >> header states >> fork/join
      for (uint64_t s : branchBodyStates) {
        std::string stateName = (ruleName + "_state" + std::to_string(s)).str();
        stateChain.push_back(FlatSymbolRefAttr::get(builder.getContext(), stateName));
      }
      for (uint64_t s : branchHeaderStates) {
        std::string stateName = (ruleName + "_state" + std::to_string(s)).str();
        stateChain.push_back(FlatSymbolRefAttr::get(builder.getContext(), stateName));
      }
      for (uint64_t s : forkJoinStates) {
        std::string stateName = (ruleName + "_state" + std::to_string(s)).str();
        stateChain.push_back(FlatSymbolRefAttr::get(builder.getContext(), stateName));
      }
    }

    // Finally reset rule (lowest precedence)
    std::string resetName = (ruleName + "_reset").str();
    stateChain.push_back(FlatSymbolRefAttr::get(builder.getContext(), resetName));

    if (!stateChain.empty()) {
      newPrecedenceChains.push_back(ArrayAttr::get(builder.getContext(), stateChain));
    }

    module->setAttr("precedence", ArrayAttr::get(builder.getContext(), newPrecedenceChains));
  }

  // Mark as converted
  rule->setAttr("proc.stmt_converted", builder.getUnitAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Conversion complete\n");

  return success();
}

//===----------------------------------------------------------------------===//
// Dataflow Task Rule Processing (Phase 7 C2)
//===----------------------------------------------------------------------===//

LogicalResult ProcStmtToActionPass::processDataflowTaskRule(RuleOp rule,
                                                             cmt2::ModuleOp module) {
  // Only process rules that originated from dataflow tasks with proc control
  if (!rule->hasAttr("dataflow.from_task") || !rule->hasAttr("tdcc.has_proc_control"))
    return success();

  LLVM_DEBUG(llvm::dbgs() << "Processing dataflow task rule @" << rule.getSymName() << "\n");

  // Get TDCC metadata
  auto numStatesAttr = rule->getAttrOfType<IntegerAttr>("tdcc.num_states");
  auto fsmWidthAttr = rule->getAttrOfType<IntegerAttr>("tdcc.fsm_width");
  auto doneStateAttr = rule->getAttrOfType<IntegerAttr>("tdcc.done_state");
  auto enablesAttr = rule->getAttrOfType<ArrayAttr>("tdcc.enables");
  auto transitionsAttr = rule->getAttrOfType<ArrayAttr>("tdcc.transitions");

  if (!numStatesAttr || !fsmWidthAttr || !doneStateAttr) {
    LLVM_DEBUG(llvm::dbgs() << "  Missing TDCC metadata, skipping\n");
    return success();
  }

  unsigned fsmWidth = fsmWidthAttr.getInt();
  uint64_t doneState = doneStateAttr.getInt();

  // Find or create register module for FSM
  auto circuit = module->getParentOfType<CircuitOp>();
  OpBuilder circuitBuilder(circuit);
  auto regMod = findRegisterModule(circuit, fsmWidth);
  if (!regMod) {
    regMod = createFSMRegisterModule(circuit, fsmWidth, circuitBuilder);
    if (!regMod) {
      rule.emitError("failed to create FSM register module for dataflow task");
      return failure();
    }
  }

  // Get clock and reset from module arguments
  Value clock, reset;
  for (auto arg : module.getBody().getArguments()) {
    if (isa<firrtl::ClockType>(arg.getType())) {
      clock = arg;
    } else if (auto uintType = dyn_cast<firrtl::UIntType>(arg.getType())) {
      if (uintType.getWidth() == 1 && !reset) {
        reset = arg;
      }
    }
  }

  if (!clock || !reset) {
    rule.emitError("could not find clock/reset for dataflow task FSM");
    return failure();
  }

  // Create FSM instance
  OpBuilder builder(rule);
  builder.setInsertionPointAfter(rule);

  Location loc = rule.getLoc();
  auto taskNameAttr = rule->getAttrOfType<StringAttr>("dataflow.task_name");
  std::string taskName = taskNameAttr ? taskNameAttr.getValue().str() : rule.getSymName().str();
  std::string fsmInstName = "__fsm_df_" + taskName;

  // Get the module name from the register module
  StringRef regModName;
  if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(regMod)) {
    regModName = extMod.getSymName();
  }

  // Create FSM instance
  builder.create<InstanceOp>(
      loc, builder.getStringAttr(fsmInstName),
      ValueRange{clock, reset},
      FlatSymbolRefAttr::get(builder.getContext(), regModName),
      ArrayAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Created FSM instance @" << fsmInstName << "\n");

  // Create tick rule for FSM transitions
  std::string tickRuleName = rule.getSymName().str() + "_tick";
  auto tickFuncType = builder.getFunctionType({}, {});

  auto tickRule = builder.create<RuleOp>(
      loc,
      builder.getStringAttr(tickRuleName),
      TypeAttr::get(tickFuncType),
      builder.getStrArrayAttr({}),
      builder.getArrayAttr({}),
      nullptr, nullptr, nullptr, nullptr, nullptr);

  // Mark tick rule with FSM metadata
  tickRule->setAttr("dataflow.fsm_tick", builder.getUnitAttr());
  tickRule->setAttr("dataflow.fsm_inst", builder.getStringAttr(fsmInstName));
  tickRule->setAttr("dataflow.task_name", builder.getStringAttr(taskName));

  // C4: Check if module needs stall gating and mark tick rule accordingly
  if (module->hasAttr("stall.controller")) {
    tickRule->setAttr("stall.gated", builder.getUnitAttr());
    LLVM_DEBUG(llvm::dbgs() << "  Tick rule marked for stall gating\n");
  }

  // Build tick rule guard: always enabled (unconditional)
  Region &tickGuardRegion = tickRule.getGuard();
  Block *tickGuardBlock = new Block();
  tickGuardRegion.push_back(tickGuardBlock);
  OpBuilder tickGuardBuilder(tickGuardBlock, tickGuardBlock->begin());

  // Create constant true for guard
  auto boolType = firrtl::UIntType::get(builder.getContext(), 1);
  auto trueConst = tickGuardBuilder.create<firrtl::ConstantOp>(
      loc, boolType, APInt(1, 1));
  tickGuardBuilder.create<ReturnOp>(loc, ValueRange{trueConst});

  // Build tick rule body: FSM state transitions based on TDCC transitions
  Region &tickBodyRegion = tickRule.getBody();
  Block *tickBodyBlock = new Block();
  tickBodyRegion.push_back(tickBodyBlock);
  OpBuilder tickBodyBuilder(tickBodyBlock, tickBodyBlock->begin());

  // Read current FSM state
  auto fsmType = firrtl::UIntType::get(builder.getContext(), fsmWidth);
  auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
  auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
  auto writeSym = FlatSymbolRefAttr::get(builder.getContext(), "write");

  auto readOp = tickBodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{fsmType}, ValueRange{},
      instanceSym, readSym,
      ArrayAttr(), ArrayAttr());
  Value currentState = readOp.getResult(0);

  // Build next state logic based on transitions
  // For now, simple sequential: state + 1 until done
  Value nextState = currentState;

  // Check if at done state, if so stay at done
  auto doneStateConst = tickBodyBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, APInt(fsmWidth, doneState));
  auto atDone = tickBodyBuilder.create<firrtl::EQPrimOp>(
      loc, boolType, currentState, doneStateConst);

  // Simple increment for next state (will be refined based on transitions)
  auto oneConst = tickBodyBuilder.create<firrtl::ConstantOp>(
      loc, fsmType, APInt(fsmWidth, 1));
  auto incrementedWide = tickBodyBuilder.create<firrtl::AddPrimOp>(
      loc, currentState, oneConst);
  auto incrementedState = tickBodyBuilder.create<firrtl::BitsPrimOp>(
      loc, incrementedWide, fsmWidth - 1, 0);

  // Mux: if at done, stay at done; else increment
  nextState = tickBodyBuilder.create<firrtl::MuxPrimOp>(
      loc, atDone, doneStateConst, incrementedState);

  // Write next state
  tickBodyBuilder.create<CallOp>(
      loc, SmallVector<Type>{}, ValueRange{nextState},
      instanceSym, writeSym,
      ArrayAttr(), ArrayAttr());

  tickBodyBuilder.create<ReturnOp>(loc);

  LLVM_DEBUG(llvm::dbgs() << "  Created tick rule @" << tickRuleName << "\n");

  // Create step enable rules for each step in enables
  if (enablesAttr) {
    for (auto enableAttr : enablesAttr) {
      auto enableDict = cast<DictionaryAttr>(enableAttr);
      auto stepAttr = enableDict.getAs<FlatSymbolRefAttr>("step");
      auto stateAttr = enableDict.getAs<IntegerAttr>("state");

      if (!stepAttr || !stateAttr)
        continue;

      StringRef stepName = stepAttr.getValue();
      uint64_t enableState = stateAttr.getInt();

      // Create enable rule: guards on FSM state == enableState
      // Include state in name for uniqueness when same step is enabled at different states
      std::string enableRuleName = rule.getSymName().str() + "_enable_" +
                                    stepName.str() + "_s" + std::to_string(enableState);

      auto enableRule = builder.create<RuleOp>(
          loc,
          builder.getStringAttr(enableRuleName),
          TypeAttr::get(tickFuncType),
          builder.getStrArrayAttr({}),
          builder.getArrayAttr({}),
          nullptr, nullptr, nullptr, nullptr, nullptr);

      // Guard: FSM state == enableState
      Region &enableGuardRegion = enableRule.getGuard();
      Block *enableGuardBlock = new Block();
      enableGuardRegion.push_back(enableGuardBlock);
      OpBuilder enableGuardBuilder(enableGuardBlock, enableGuardBlock->begin());

      auto enableStateConst = enableGuardBuilder.create<firrtl::ConstantOp>(
          loc, fsmType, APInt(fsmWidth, enableState));
      auto stateReadOp = enableGuardBuilder.create<CallOp>(
          loc, SmallVector<Type>{fsmType}, ValueRange{},
          instanceSym, readSym,
          ArrayAttr(), ArrayAttr());
      auto atEnableState = enableGuardBuilder.create<firrtl::EQPrimOp>(
          loc, boolType, stateReadOp.getResult(0), enableStateConst);
      enableGuardBuilder.create<ReturnOp>(loc, ValueRange{atEnableState});

      // Body: The step's body logic is activated when this rule fires
      // For static steps, the step body is already defined separately
      // This enable rule just gates when the step can execute
      Region &enableBodyRegion = enableRule.getBody();
      Block *enableBodyBlock = new Block();
      enableBodyRegion.push_back(enableBodyBlock);
      OpBuilder enableBodyBuilder(enableBodyBlock, enableBodyBlock->begin());

      // Mark which step this rule enables via attribute
      enableRule->setAttr("enables.step", builder.getStringAttr(stepName));
      enableRule->setAttr("enables.state", builder.getI64IntegerAttr(enableState));
      enableRule->setAttr("dataflow.fsm_inst", builder.getStringAttr(fsmInstName));

      // C4: Mark enable rules for stall gating if module needs it
      if (module->hasAttr("stall.controller")) {
        enableRule->setAttr("stall.gated", builder.getUnitAttr());
      }

      enableBodyBuilder.create<ReturnOp>(loc);

      LLVM_DEBUG(llvm::dbgs() << "  Created enable rule @" << enableRuleName
                              << " for step @" << stepName << " at state " << enableState << "\n");
    }
  }

  // Modify the original rule's guard to include FSM done state check
  // The rule should only fire when FSM reaches done state
  Region &guardRegion = rule.getGuard();
  if (!guardRegion.empty()) {
    Block &guardBlock = guardRegion.front();

    // Find the return op
    Operation *terminator = guardBlock.getTerminator();
    if (auto returnOp = dyn_cast<ReturnOp>(terminator)) {
      OpBuilder guardBuilder(returnOp);

      // Read FSM state and check if at done
      auto fsmReadOp = guardBuilder.create<CallOp>(
          loc, SmallVector<Type>{fsmType}, ValueRange{},
          instanceSym, readSym,
          ArrayAttr(), ArrayAttr());
      auto doneConst = guardBuilder.create<firrtl::ConstantOp>(
          loc, fsmType, APInt(fsmWidth, doneState));
      auto atDoneState = guardBuilder.create<firrtl::EQPrimOp>(
          loc, boolType, fsmReadOp.getResult(0), doneConst);

      // If there was an existing guard condition, AND it with done check
      if (returnOp.getNumOperands() > 0) {
        Value existingGuard = returnOp.getOperand(0);
        auto combinedGuard = guardBuilder.create<firrtl::AndPrimOp>(
            loc, existingGuard, atDoneState);
        returnOp->setOperand(0, combinedGuard);
      } else {
        // Replace return with one that has the done check
        guardBuilder.create<ReturnOp>(loc, ValueRange{atDoneState});
        returnOp.erase();
      }
    }
  }

  // Mark rule as processed
  rule->setAttr("dataflow.fsm_generated", builder.getUnitAttr());
  rule->setAttr("dataflow.fsm_inst", builder.getStringAttr(fsmInstName));

  LLVM_DEBUG(llvm::dbgs() << "  Added FSM done guard to rule\n");

  return success();
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void ProcStmtToActionPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Collect all proc rules
  SmallVector<ProcRuleOp> procRules;
  for (auto &op : module.getBodyRegion().front()) {
    if (auto rule = dyn_cast<ProcRuleOp>(op))
      procRules.push_back(rule);
  }

  // Process each proc rule
  for (auto rule : procRules) {
    if (failed(processProcRule(rule, module))) {
      signalPassFailure();
      return;
    }
  }

  // Collect and process dataflow task rules (Phase 7 C2)
  SmallVector<RuleOp> dataflowRules;
  for (auto &op : module.getBodyRegion().front()) {
    if (auto rule = dyn_cast<RuleOp>(op)) {
      if (rule->hasAttr("dataflow.from_task"))
        dataflowRules.push_back(rule);
    }
  }

  for (auto rule : dataflowRules) {
    if (failed(processDataflowTaskRule(rule, module))) {
      signalPassFailure();
      return;
    }
  }
}

void ProcStmtToActionPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Process each module in the circuit
  for (auto &op : circuit.getBodyRegion().front().getOperations()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
