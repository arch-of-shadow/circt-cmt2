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
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"

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
                          ArrayAttr transitionsAttr, uint64_t doneState);

  /// Generate idle/running value methods.
  void generateStatusValues(ProcRuleOp procRule, cmt2::ModuleOp module,
                            OpBuilder &builder, StringRef fsmInstName,
                            unsigned fsmWidth, uint64_t doneState);

  /// Find the register module to use for FSM.
  /// Returns nullptr if no suitable module found.
  Operation *findRegisterModule(CircuitOp circuit, unsigned width);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Register Module Finding
//===----------------------------------------------------------------------===//

Operation *ProcStmtToActionPass::findRegisterModule(CircuitOp circuit,
                                                     unsigned width) {
  // Look for existing register module in circuit
  // We expect tests to provide a @reg module or similar
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto extMod = dyn_cast<ExtModuleFirrtlOp>(op)) {
      // Look for module with read/write methods
      bool hasRead = false, hasWrite = false;
      for (auto &bodyOp : extMod.getBody().front()) {
        if (auto bindValue = dyn_cast<BindValueOp>(bodyOp)) {
          if (bindValue.getSymName() == "read")
            hasRead = true;
        } else if (auto bindMethod = dyn_cast<BindMethodOp>(bodyOp)) {
          if (bindMethod.getSymName() == "write")
            hasWrite = true;
        }
      }
      if (hasRead && hasWrite)
        return extMod;
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "Warning: No suitable register module found\n");
  return nullptr;
}

//===----------------------------------------------------------------------===//
// State-Based Rule Generation
//===----------------------------------------------------------------------===//

void ProcStmtToActionPass::generateStateRules(
    ProcRuleOp procRule, cmt2::ModuleOp module, OpBuilder &builder,
    StringRef fsmInstName, unsigned fsmWidth, ArrayAttr enablesAttr,
    ArrayAttr transitionsAttr, uint64_t doneState) {

  Location loc = procRule.getLoc();
  auto fsmType = firrtl::UIntType::get(builder.getContext(), fsmWidth);
  auto boolType = firrtl::UIntType::get(builder.getContext(), 1);
  StringRef ruleName = procRule.getSymName();

  // Build map from step name to states
  // A step can be enabled in multiple states (e.g., in static_repeat iterations)
  DenseMap<StringRef, SmallVector<uint64_t>> stepToStates;
  if (enablesAttr) {
    for (auto enableAttr : enablesAttr) {
      auto dict = cast<DictionaryAttr>(enableAttr);
      // TDCC generates enables with "step" attribute, not "group"
      auto stepRef = dict.getAs<FlatSymbolRefAttr>("step");
      auto stateAttr = dict.getAs<IntegerAttr>("state");
      if (stepRef && stateAttr) {
        stepToStates[stepRef.getValue()].push_back(stateAttr.getInt());
      }
    }
  }

  // Structure to hold transition with optional guard
  struct TransitionInfo {
    uint64_t toState;
    int64_t guardOpId;   // -1 means unconditional
    bool guardInverted;  // true for else branches
    StringRef doneStep;  // non-empty means guarded by step's done signal
  };

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

      // Check for done signal guard (dynamic step exits)
      if (auto doneStepAttr = dict.getAs<StringAttr>("done_step")) {
        trans.doneStep = doneStepAttr.getValue();
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
      if (isa<ProcIfOp, ProcStaticIfOp, ProcWhileOp>(&op)) {
        condOpIdToOp[nextCondOpId++] = &op;
      }
      // Recurse into nested regions
      for (auto &nestedRegion : op.getRegions()) {
        collectCondOps(&nestedRegion);
      }
    }
  };
  collectCondOps(&procRule.getControl());

  // For each state, generate a rule
  for (uint64_t state : allStates) {
    auto transIt = stateTransitions.find(state);
    auto stepsIt = stateToSteps.find(state);
    bool hasSteps = stepsIt != stateToSteps.end() && !stepsIt->second.empty();
    bool hasTransitions = transIt != stateTransitions.end() && !transIt->second.empty();

    // Create rule: @{ruleName}_state{state}
    std::string stateRuleName =
        (ruleName + "_state" + std::to_string(state)).str();

    auto funcType = builder.getFunctionType({}, {});
    auto funcTypeAttr = TypeAttr::get(funcType);

    auto stateRule = builder.create<RuleOp>(
        loc, builder.getStringAttr(stateRuleName), funcTypeAttr,
        builder.getArrayAttr({}), builder.getArrayAttr({}),
        ArrayAttr(), ArrayAttr());

    // Build guard region: fsm.read() == state
    Block *guardBlock = new Block();
    stateRule.getGuard().push_back(guardBlock);
    OpBuilder guardBuilder(guardBlock, guardBlock->begin());

    // Read FSM state: cmt2.call @fsmInst @read() -> fsmType
    auto instanceSym = FlatSymbolRefAttr::get(builder.getContext(), fsmInstName);
    auto readSym = FlatSymbolRefAttr::get(builder.getContext(), "read");
    auto fsmReadCall = guardBuilder.create<CallOp>(
        loc, SmallVector<Type>{fsmType}, ValueRange{},
        instanceSym, readSym,
        ArrayAttr(), ArrayAttr());

    auto stateConst = guardBuilder.create<firrtl::ConstantOp>(
        loc, fsmType, llvm::APInt(fsmWidth, state));
    auto inState = guardBuilder.create<firrtl::EQPrimOp>(
        loc, fsmReadCall.getResult(0), stateConst.getResult());

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
    Value guardResult = inState.getResult();
    if (state == 0 && !procRule.getGuard().empty()) {
      // Clone the original guard region content
      IRMapping guardMapping;
      for (auto &guardOp : procRule.getGuard().front()) {
        if (auto retOp = dyn_cast<ReturnOp>(guardOp)) {
          Value origGuard = guardMapping.lookupOrDefault(retOp.getOperand(0));
          guardResult =
              guardBuilder.create<firrtl::AndPrimOp>(loc, inState.getResult(), origGuard)
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

    // Clone step bodies only for states that have steps
    if (hasSteps) {
      for (auto *stepOp : stepsIt->second) {
        Region *bodyRegion = nullptr;
        if (auto step = dyn_cast<ProcStepOp>(stepOp)) {
          bodyRegion = &step.getBody();
        } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(stepOp)) {
          bodyRegion = &staticStep.getBody();
        }

        if (bodyRegion && !bodyRegion->empty()) {
          for (auto &op : bodyRegion->front()) {
            if (!isa<ProcStepDoneOp>(op)) {
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

    if (hasTransitions) {
      auto &transitions = transIt->second;

      // Check if single transition with no condition guard
      bool singleUnguarded = (transitions.size() == 1 && transitions[0].guardOpId < 0);
      bool hasDoneGuard = (transitions.size() == 1 && !transitions[0].doneStep.empty());

      if (singleUnguarded && !hasDoneGuard) {
        // Single unconditional transition (no condition, no done signal)
        uint64_t nextState = transitions[0].toState;
        nextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
            loc, fsmType, llvm::APInt(fsmWidth, nextState)).getResult();
        transitionDesc = std::to_string(nextState);
      } else if (hasDoneGuard) {
        // Dynamic step exit - guard transition by done signal
        // next_state = done ? nextState : currentState (stay until done)
        StringRef doneStepName = transitions[0].doneStep;
        uint64_t nextState = transitions[0].toState;

        // Find the step and extract its done signal
        // The step body was already cloned into bodyMapping (excluding ProcStepDoneOp)
        // We need to find the done value in the original step
        Value doneValue = nullptr;
        if (hasSteps) {
          for (auto *stepOp : stepsIt->second) {
            if (auto step = dyn_cast<ProcStepOp>(stepOp)) {
              if (step.getSymName() == doneStepName) {
                // Find ProcStepDoneOp in the step body
                for (auto &op : step.getBody().front()) {
                  if (auto stepDone = dyn_cast<ProcStepDoneOp>(op)) {
                    // The done signal should be in bodyMapping from step body cloning
                    doneValue = bodyMapping.lookupOrNull(stepDone.getDone());
                    if (!doneValue) {
                      // If not in mapping, clone the done signal computation
                      // (This handles cases where done is computed inline)
                      Value origDone = stepDone.getDone();
                      if (origDone.getDefiningOp()) {
                        // Clone the defining operation chain
                        std::function<Value(Value)> cloneDef;
                        cloneDef = [&](Value v) -> Value {
                          if (!v) return nullptr;
                          if (auto mapped = bodyMapping.lookupOrNull(v))
                            return mapped;
                          if (auto defOp = v.getDefiningOp()) {
                            for (Value operand : defOp->getOperands())
                              cloneDef(operand);
                            bodyBuilder.clone(*defOp, bodyMapping);
                            return bodyMapping.lookup(v);
                          }
                          return v;
                        };
                        doneValue = cloneDef(origDone);
                      }
                    }
                    break;
                  }
                }
                break;
              }
            }
          }
        }

        if (doneValue) {
          // Generate: next_state = done ? nextState : currentState
          auto nextConst = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, nextState)).getResult();
          auto stayConst = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, state)).getResult();
          nextStateValue = bodyBuilder.create<firrtl::MuxPrimOp>(
              loc, doneValue, nextConst, stayConst).getResult();
          transitionDesc = "done ? " + std::to_string(nextState) + " : " + std::to_string(state);
        } else {
          // Fallback: unconditional (done signal not found)
          nextStateValue = bodyBuilder.create<firrtl::ConstantOp>(
              loc, fsmType, llvm::APInt(fsmWidth, nextState)).getResult();
          transitionDesc = std::to_string(nextState) + " (done signal not found)";
        }
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
          Operation *cloned = bodyBuilder.clone(*defOp, condMapping);
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
    bodyBuilder.create<CallOp>(
        loc, SmallVector<Type>{}, ValueRange{nextStateValue},
        instanceSym, writeSym,
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

  // Find register module for FSM
  auto circuit = module->getParentOfType<CircuitOp>();
  auto regMod = findRegisterModule(circuit, fsmWidth);
  if (!regMod) {
    // For now, just add metadata and skip actual conversion
    // The FIRRTL-level conversion will handle this
    OpBuilder builder(rule);
    std::string fsmName = ("__fsm_" + rule.getSymName()).str();
    rule->setAttr("proc.fsm_name", builder.getStringAttr(fsmName));
    rule->setAttr("proc.stmt_converted", builder.getUnitAttr());
    return success();
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

  auto fsmInst = builder.create<InstanceOp>(
      loc, builder.getStringAttr(fsmInstName),
      ValueRange{clock, reset},
      FlatSymbolRefAttr::get(builder.getContext(), regModName),
      ArrayAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Created FSM instance @" << fsmInstName << "\n");

  // Generate state-based rules
  generateStateRules(rule, module, builder, fsmInstName, fsmWidth, enablesAttr,
                     transitionsAttr, doneState);

  // Generate idle/running value methods
  generateStatusValues(rule, module, builder, fsmInstName, fsmWidth, doneState);

  // Mark as converted
  rule->setAttr("proc.stmt_converted", builder.getUnitAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Conversion complete\n");

  return success();
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void ProcStmtToActionPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Collect all proc rules
  SmallVector<ProcRuleOp> rules;
  for (auto &op : module.getBodyRegion().front()) {
    if (auto rule = dyn_cast<ProcRuleOp>(op))
      rules.push_back(rule);
  }

  // Process each rule
  for (auto rule : rules) {
    if (failed(processProcRule(rule, module))) {
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
