//===- TDCC.cpp - Top-Down Compile Control for procedural rules -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TDCC pass for the Cmt2 dialect.
// It compiles procedural control flow (seq, par, if, while) into FSM-based
// implementations using the schedule-based approach from Calyx's TDCC.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "circt/Dialect/Cmt2/Transforms/Diagnostics.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-tdcc"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_TDCC
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Schedule Data Structures
//===----------------------------------------------------------------------===//

/// A predecessor edge with state and guard.
struct PredEdge {
  uint64_t state;
  Value guard; // null means true guard
};

/// An assignment in the schedule.
struct Assignment {
  Value dst;
  Value src;
  Value guard; // null means unconditional
};

/// The schedule containing enables and transitions.
struct Schedule {
  /// Assignments that should be enabled in a given state.
  DenseMap<uint64_t, SmallVector<Assignment>> enables;

  /// Transition from one state to another when the guard is true.
  /// (from_state, to_state, guard)
  SmallVector<std::tuple<uint64_t, uint64_t, Value>> transitions;

  /// Maximum state ID seen.
  uint64_t maxState = 0;

  void addEnable(uint64_t state, Value dst, Value src, Value guard) {
    enables[state].push_back({dst, src, guard});
    maxState = std::max(maxState, state);
  }

  void addTransition(uint64_t from, uint64_t to, Value guard) {
    transitions.push_back({from, to, guard});
    maxState = std::max(maxState, std::max(from, to));
  }
};

//===----------------------------------------------------------------------===//
// TDCC Pass Implementation
//===----------------------------------------------------------------------===//

struct TDCCPass : public circt::cmt2::impl::TDCCBase<TDCCPass> {

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module, const ConflictMatrixAnalysis &cma);

  /// Process a single procedural rule or method.
  void processProcOp(Operation *procOp, cmt2::ModuleOp module);

  /// Compute unique state IDs for each EnableOp in the control region.
  /// Returns the next available state ID.
  uint64_t computeUniqueIds(Region &control, uint64_t curState);
  uint64_t computeUniqueIdsForOp(Operation *op, uint64_t curState);

  /// Get the exits from a control construct (state, guard pairs).
  void controlExits(Operation *op, SmallVectorImpl<PredEdge> &exits,
                    OpBuilder &builder);

  /// Recursively build the schedule for a control program.
  /// Returns predecessor edges for the next statement.
  SmallVector<PredEdge> calculateStatesRecur(
      Schedule &schedule, Operation *op, SmallVector<PredEdge> preds,
      OpBuilder &builder, cmt2::ModuleOp module);

  /// Generate hardware from the schedule.
  void realizeSchedule(Schedule &schedule, Operation *procOp,
                       cmt2::ModuleOp module, OpBuilder &builder);

  /// Validate static steps for potential backpressure issues.
  void validateStaticSteps(cmt2::ModuleOp module,
                           const ConflictMatrixAnalysis &cma);

  /// Check if a dynamic step can be promoted to static.
  bool canPromoteToStatic(ProcStepOp step, cmt2::ModuleOp module,
                          const ConflictMatrixAnalysis &cma);

  /// Map from EnableOp to its state ID.
  DenseMap<Operation *, uint64_t> stateIds;

  /// Map from step name to step operation.
  DenseMap<StringRef, Operation *> stepMap;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// State Numbering
//===----------------------------------------------------------------------===//

uint64_t TDCCPass::computeUniqueIds(Region &control, uint64_t curState) {
  for (Block &block : control) {
    for (Operation &op : block) {
      curState = computeUniqueIdsForOp(&op, curState);
    }
  }
  return curState;
}

uint64_t TDCCPass::computeUniqueIdsForOp(Operation *op, uint64_t curState) {
  return llvm::TypeSwitch<Operation *, uint64_t>(op)
      .Case<ProcEnableOp>([&](ProcEnableOp enable) {
        stateIds[enable] = curState;
        LLVM_DEBUG(llvm::dbgs() << "  Enable @" << enable.getGroupName()
                                << " -> state " << curState << "\n");
        return curState + 1;
      })
      .Case<ProcSeqOp>([&](ProcSeqOp seq) {
        // Sequential: states numbered consecutively
        uint64_t cur = curState;
        for (Operation &stmt : seq.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            cur = computeUniqueIdsForOp(&stmt, cur);
        }
        return cur;
      })
      .Case<ProcParOp>([&](ProcParOp par) {
        // Parallel: each branch gets independent state space
        // For now, just assign the par itself a state
        stateIds[par] = curState;
        for (Operation &stmt : par.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            computeUniqueIdsForOp(&stmt, 0); // Reset to 0 for each branch
        }
        return curState + 1;
      })
      .Case<ProcIfOp>([&](ProcIfOp ifOp) {
        // Branches can't get initial state (start at 1 if curState == 0)
        uint64_t cur = (curState == 0) ? 1 : curState;
        stateIds[ifOp] = cur;

        // Process then branch
        uint64_t thenNext = cur;
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            thenNext = computeUniqueIdsForOp(&stmt, thenNext);
        }

        // Process else branch
        uint64_t elseNext = thenNext;
        if (!ifOp.getElseRegion().empty()) {
          for (Operation &stmt : ifOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              elseNext = computeUniqueIdsForOp(&stmt, elseNext);
          }
        }

        return elseNext;
      })
      .Case<ProcWhileOp>([&](ProcWhileOp whileOp) {
        uint64_t cur = (curState == 0) ? 1 : curState;
        stateIds[whileOp] = cur;

        // Process body
        uint64_t bodyNext = cur;
        for (Operation &stmt : whileOp.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            bodyNext = computeUniqueIdsForOp(&stmt, bodyNext);
        }

        return bodyNext;
      })
      .Default([&](Operation *) {
        // Skip control_end and other operations
        return curState;
      });
}

//===----------------------------------------------------------------------===//
// Control Exits
//===----------------------------------------------------------------------===//

void TDCCPass::controlExits(Operation *op, SmallVectorImpl<PredEdge> &exits,
                            OpBuilder &builder) {
  llvm::TypeSwitch<Operation *>(op)
      .Case<ProcEnableOp>([&](ProcEnableOp enable) {
        uint64_t state = stateIds[enable];
        // Exit when step's done signal is true
        // For now, we'll use a placeholder - actual done signal comes from group
        exits.push_back({state, nullptr}); // null = unconditional (done)
      })
      .Case<ProcSeqOp>([&](ProcSeqOp seq) {
        // Only the last statement's exits matter
        Block &block = seq.getBody().front();
        for (auto it = block.rbegin(); it != block.rend(); ++it) {
          if (!isa<ProcControlEndOp>(*it)) {
            controlExits(&*it, exits, builder);
            break;
          }
        }
      })
      .Case<ProcIfOp>([&](ProcIfOp ifOp) {
        // Both branches contribute exits
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            controlExits(&stmt, exits, builder);
        }
        if (!ifOp.getElseRegion().empty()) {
          for (Operation &stmt : ifOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              controlExits(&stmt, exits, builder);
          }
        }
      })
      .Case<ProcWhileOp>([&](ProcWhileOp whileOp) {
        // Loop exits when condition is false
        // This is more complex - for now just add body exits
        for (Operation &stmt : whileOp.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            controlExits(&stmt, exits, builder);
        }
      })
      .Default([](Operation *) {
        // Skip other operations
      });
}

//===----------------------------------------------------------------------===//
// Schedule Building
//===----------------------------------------------------------------------===//

SmallVector<PredEdge> TDCCPass::calculateStatesRecur(
    Schedule &schedule, Operation *op, SmallVector<PredEdge> preds,
    OpBuilder &builder, cmt2::ModuleOp module) {

  return llvm::TypeSwitch<Operation *, SmallVector<PredEdge>>(op)
      .Case<ProcEnableOp>([&](ProcEnableOp enable) {
        uint64_t curState = stateIds[enable];

        // Add transitions from predecessors
        for (auto &pred : preds) {
          schedule.addTransition(pred.state, curState, pred.guard);
        }

        // Record the enable (will generate go signal later)
        schedule.addEnable(curState, nullptr, nullptr, nullptr);

        // Return exit edge (state, done guard)
        return SmallVector<PredEdge>{{curState, nullptr}};
      })
      .Case<ProcSeqOp>([&](ProcSeqOp seq) {
        SmallVector<PredEdge> prev = preds;
        for (Operation &stmt : seq.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            prev = calculateStatesRecur(schedule, &stmt, prev, builder, module);
        }
        return prev;
      })
      .Case<ProcIfOp>([&](ProcIfOp ifOp) {
        Value cond = ifOp.getCond();

        // True branch: predecessors with condition = true
        SmallVector<PredEdge> truPreds;
        for (auto &p : preds) {
          truPreds.push_back({p.state, cond}); // AND with condition
        }

        SmallVector<PredEdge> truExits;
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            truExits = calculateStatesRecur(schedule, &stmt, truPreds, builder,
                                            module);
        }

        // False branch: predecessors with condition = false
        SmallVector<PredEdge> falExits;
        if (!ifOp.getElseRegion().empty()) {
          SmallVector<PredEdge> falPreds;
          for (auto &p : preds) {
            // Need to create !cond
            falPreds.push_back({p.state, nullptr}); // TODO: proper guard
          }
          for (Operation &stmt : ifOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              falExits = calculateStatesRecur(schedule, &stmt, falPreds,
                                              builder, module);
          }
        }

        // Combine exits
        SmallVector<PredEdge> allExits;
        allExits.append(truExits);
        allExits.append(falExits);
        return allExits;
      })
      .Case<ProcWhileOp>([&](ProcWhileOp whileOp) {
        // Simplified while handling - just process body
        SmallVector<PredEdge> bodyPreds = preds;
        SmallVector<PredEdge> bodyExits;
        for (Operation &stmt : whileOp.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            bodyExits = calculateStatesRecur(schedule, &stmt, bodyPreds,
                                             builder, module);
        }
        return bodyExits;
      })
      .Default([&](Operation *) { return preds; });
}

//===----------------------------------------------------------------------===//
// Schedule Realization
//===----------------------------------------------------------------------===//

void TDCCPass::realizeSchedule(Schedule &schedule, Operation *procOp,
                               cmt2::ModuleOp module, OpBuilder &builder) {
  Location loc = procOp->getLoc();

  LLVM_DEBUG({
    llvm::dbgs() << "Schedule for " << procOp->getName() << ":\n";
    llvm::dbgs() << "  Max state: " << schedule.maxState << "\n";
    llvm::dbgs() << "  Enables:\n";
    for (auto &[state, assigns] : schedule.enables) {
      llvm::dbgs() << "    State " << state << ": " << assigns.size()
                   << " assignments\n";
    }
    llvm::dbgs() << "  Transitions:\n";
    for (auto &[from, to, guard] : schedule.transitions) {
      llvm::dbgs() << "    " << from << " -> " << to << "\n";
    }
  });

  // Get proc op name for generating unique names
  StringRef procName;
  if (auto rule = dyn_cast<ProcRuleOp>(procOp))
    procName = rule.getSymName();
  else if (auto method = dyn_cast<ProcMethodOp>(procOp))
    procName = method.getSymName();

  // Compute FSM width: log2(maxState + 2) to hold states 0..maxState+1 (done state)
  uint64_t numStates = schedule.maxState + 2; // +1 for done state
  unsigned fsmWidth = llvm::Log2_64_Ceil(numStates);
  if (fsmWidth == 0) fsmWidth = 1; // At least 1 bit

  // Add FSM metadata as attributes on the proc op
  // This will be used by ProcToGAA to generate the actual FSM
  auto ctx = builder.getContext();
  procOp->setAttr("tdcc.num_states", builder.getI64IntegerAttr(numStates));
  procOp->setAttr("tdcc.fsm_width", builder.getI64IntegerAttr(fsmWidth));
  procOp->setAttr("tdcc.done_state", builder.getI64IntegerAttr(schedule.maxState + 1));

  // Store state assignments for each enable
  SmallVector<Attribute> stateAssigns;
  for (auto &[enableOp, state] : stateIds) {
    if (auto enable = dyn_cast<ProcEnableOp>(enableOp)) {
      auto entry = builder.getDictionaryAttr({
        builder.getNamedAttr("step", enable.getStepNameAttr()),
        builder.getNamedAttr("state", builder.getI64IntegerAttr(state))
      });
      stateAssigns.push_back(entry);
    }
  }
  procOp->setAttr("tdcc.enables", builder.getArrayAttr(stateAssigns));

  // Store transitions
  SmallVector<Attribute> transAttrs;
  for (auto &[from, to, guard] : schedule.transitions) {
    auto entry = builder.getDictionaryAttr({
      builder.getNamedAttr("from", builder.getI64IntegerAttr(from)),
      builder.getNamedAttr("to", builder.getI64IntegerAttr(to))
    });
    transAttrs.push_back(entry);
  }
  procOp->setAttr("tdcc.transitions", builder.getArrayAttr(transAttrs));

  LLVM_DEBUG(llvm::dbgs() << "Added TDCC attributes to " << procName << "\n");
}

//===----------------------------------------------------------------------===//
// Static Step Validation
//===----------------------------------------------------------------------===//

void TDCCPass::validateStaticSteps(cmt2::ModuleOp module,
                                    const ConflictMatrixAnalysis &cma) {
  auto circuit = module->getParentOfType<CircuitOp>();
  auto *moduleMatrix = cma.getModuleMatrix(module.getSymNameAttr());

  // Validate each static step
  module.walk([&](ProcStaticStepOp staticStep) {
    auto calls = collectStepCalls(staticStep, module, circuit);

    // Check for conflicts between methods called within the same step
    for (size_t i = 0; i < calls.size(); ++i) {
      for (size_t j = i + 1; j < calls.size(); ++j) {
        if (calls[i].callType != CallType::MethodCall ||
            calls[j].callType != CallType::MethodCall)
          continue;

        // Check if these methods conflict
        if (moduleMatrix) {
          auto rel = moduleMatrix->getRelationship(
              calls[i].calleeEntity.getLeafReference(),
              calls[j].calleeEntity.getLeafReference());
          if (rel == Relationship::Conflict) {
            std::string method1 =
                (calls[i].calleeInstance.getLeafReference().getValue() + "." +
                 calls[i].calleeEntity.getLeafReference().getValue())
                    .str();
            std::string method2 =
                (calls[j].calleeInstance.getLeafReference().getValue() + "." +
                 calls[j].calleeEntity.getLeafReference().getValue())
                    .str();
            (void)reportSchedulingConflict(staticStep, method1, method2,
                                     "in static step")
                .note("latency guarantee may be violated due to backpressure")
                .hint("consider using dynamic step or explicit sequencing")
                .emit();
          }
        }
      }
    }

    // Check for conflicts with concurrently firing functions
    auto concurrentFuncs = getConcurrentFunctions(staticStep, module);
    for (const auto &call : calls) {
      if (call.callType != CallType::MethodCall)
        continue;

      for (auto concFunc : concurrentFuncs) {
        if (moduleMatrix) {
          auto rel = moduleMatrix->getRelationship(
              call.calleeEntity.getLeafReference(),
              concFunc);
          if (rel == Relationship::Conflict) {
            std::string method =
                (call.calleeInstance.getLeafReference().getValue() + "." +
                 call.calleeEntity.getLeafReference().getValue())
                    .str();
            (void)reportSchedulingConflict(staticStep, method, concFunc.getValue(),
                                     "static step may conflict with concurrent rule/method")
                .note("latency guarantee may be violated")
                .hint("ensure concurrent operations don't conflict")
                .emit();
          }
        }
      }
    }
  });
}

bool TDCCPass::canPromoteToStatic(ProcStepOp step, cmt2::ModuleOp module,
                                   const ConflictMatrixAnalysis &cma) {
  auto circuit = module->getParentOfType<CircuitOp>();
  auto *moduleMatrix = cma.getModuleMatrix(module.getSymNameAttr());

  auto calls = collectStepCalls(step, module, circuit);

  for (const auto &call : calls) {
    if (call.callType != CallType::MethodCall)
      continue;

    // Check if this method has ANY potential conflicts
    if (moduleMatrix &&
        stepHasConflictingCalls(step, module, *moduleMatrix)) {
      std::string method =
          (call.calleeInstance.getLeafReference().getValue() + "." +
           call.calleeEntity.getLeafReference().getValue())
              .str();
      (void)Cmt2Diagnostic::info(step, "cannot promote step to static")
          .note("method '" + method + "' has potential conflicts")
          .note("backpressure could occur")
          .hint("step will remain dynamic to handle backpressure correctly")
          .withPythonSource()
          .emit();
      return false;
    }
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Process Procedural Operations
//===----------------------------------------------------------------------===//

void TDCCPass::processProcOp(Operation *procOp, cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing proc op: " << procOp->getName()
                          << "\n");

  // Clear state from previous proc op
  stateIds.clear();

  // Get the control region
  Region *controlRegion = nullptr;
  if (auto rule = dyn_cast<ProcRuleOp>(procOp)) {
    controlRegion = &rule.getControl();
  } else if (auto method = dyn_cast<ProcMethodOp>(procOp)) {
    controlRegion = &method.getControl();
  }

  if (!controlRegion || controlRegion->empty())
    return;

  // Step 1: Compute unique state IDs
  LLVM_DEBUG(llvm::dbgs() << "Computing state IDs...\n");
  uint64_t numStates = computeUniqueIds(*controlRegion, 0);
  LLVM_DEBUG(llvm::dbgs() << "  Total states: " << numStates << "\n");

  if (numStates == 0)
    return;

  // Step 2: Build the schedule
  OpBuilder builder(procOp);
  Schedule schedule;

  SmallVector<PredEdge> initPreds = {{0, nullptr}}; // Start at state 0

  for (Operation &op : controlRegion->front()) {
    if (!isa<ProcControlEndOp>(op))
      initPreds = calculateStatesRecur(schedule, &op, initPreds, builder, module);
  }

  // Step 3: Realize the schedule as hardware
  realizeSchedule(schedule, procOp, module, builder);
}

void TDCCPass::processModule(cmt2::ModuleOp module,
                             const ConflictMatrixAnalysis &cma) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Build step map
  stepMap.clear();
  module.walk([&](Operation *op) {
    if (auto step = dyn_cast<ProcStepOp>(op)) {
      stepMap[step.getSymName()] = step;
    } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(op)) {
      stepMap[staticStep.getSymName()] = staticStep;
    }
  });

  // Validate static steps for backpressure issues
  validateStaticSteps(module, cma);

  // Process all procedural rules and methods
  SmallVector<Operation *> procOps;
  module.walk([&](Operation *op) {
    if (isa<ProcRuleOp, ProcMethodOp>(op))
      procOps.push_back(op);
  });

  for (auto *procOp : procOps) {
    processProcOp(procOp, module);
  }
}

void TDCCPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Build conflict matrix analysis for backpressure validation
  ConflictMatrixAnalysis cma(circuit);

  // Process each module in the circuit
  for (auto &op : circuit.getBodyRegion().front().getOperations()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module, cma);
    }
  }
}
