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

/// A guard specification that can be serialized.
/// For if/else conditions, tracks the source operation and inversion.
/// For dynamic step done signals, tracks the step name.
struct GuardSpec {
  /// The operation that defines the condition (e.g., ProcIfOp, ProcStaticIfOp).
  /// nullptr means unconditional (always true).
  Operation *sourceOp = nullptr;

  /// Whether the guard should be inverted (for else branches).
  bool inverted = false;

  /// The actual FIRRTL Value for the condition (used during schedule building).
  Value condValue = nullptr;

  /// For dynamic step exits: the step name whose done signal guards the transition.
  /// Empty string means no done signal guard.
  StringRef doneStepName;

  /// Create an unconditional guard.
  static GuardSpec unconditional() { return GuardSpec{}; }

  /// Create a positive guard (condition = true).
  static GuardSpec positive(Operation *op, Value cond) {
    GuardSpec g;
    g.sourceOp = op;
    g.inverted = false;
    g.condValue = cond;
    return g;
  }

  /// Create a negative guard (condition = false / inverted).
  static GuardSpec negative(Operation *op, Value cond) {
    GuardSpec g;
    g.sourceOp = op;
    g.inverted = true;
    g.condValue = cond;
    return g;
  }

  /// Create a done signal guard for dynamic step exits.
  static GuardSpec doneGuard(StringRef stepName) {
    GuardSpec g;
    g.doneStepName = stepName;
    return g;
  }

  bool isUnconditional() const { return sourceOp == nullptr && doneStepName.empty(); }
  bool hasDoneGuard() const { return !doneStepName.empty(); }
};

/// A predecessor edge with state and guard specification.
struct PredEdge {
  uint64_t state;
  GuardSpec guard; // unconditional if guard.isUnconditional()
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

  /// Transition from one state to another with guard specification.
  /// (from_state, to_state, guard_spec)
  SmallVector<std::tuple<uint64_t, uint64_t, GuardSpec>> transitions;

  /// Maximum state ID seen.
  uint64_t maxState = 0;

  void addEnable(uint64_t state, Value dst, Value src, Value guard) {
    enables[state].push_back({dst, src, guard});
    maxState = std::max(maxState, state);
  }

  void addTransition(uint64_t from, uint64_t to, GuardSpec guard) {
    transitions.push_back({from, to, guard});
    maxState = std::max(maxState, std::max(from, to));
  }

  /// Convenience overload for unconditional transitions.
  void addTransition(uint64_t from, uint64_t to) {
    addTransition(from, to, GuardSpec::unconditional());
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
  /// For operations inside static_repeat, we use (op, iteration) as key
  /// via the iterationOffsets map.
  DenseMap<Operation *, uint64_t> stateIds;

  /// For static_repeat: map from (op, iteration_index) to state ID.
  /// This allows different iterations to have different state IDs.
  DenseMap<std::pair<Operation *, int64_t>, uint64_t> iterStateIds;

  /// Current iteration index for static_repeat unrolling.
  /// -1 means not inside a static_repeat.
  int64_t currentIteration = -1;

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
        // Look up the step to get its latency
        int64_t latency = 1; // Default latency for dynamic steps
        StringRef stepName = enable.getStepName();
        auto it = stepMap.find(stepName);
        if (it != stepMap.end()) {
          if (auto staticStep = dyn_cast<ProcStaticStepOp>(it->second)) {
            latency = staticStep.getLatency();
            LLVM_DEBUG(llvm::dbgs() << "  Enable @" << stepName
                                    << " has static latency " << latency << "\n");
          }
        }

        // Use iteration-aware state ID if inside static_repeat
        if (currentIteration >= 0) {
          iterStateIds[{enable.getOperation(), currentIteration}] = curState;
          LLVM_DEBUG(llvm::dbgs() << "  Enable @" << enable.getGroupName()
                                  << " iter " << currentIteration
                                  << " -> state " << curState << "\n");
        } else {
          stateIds[enable] = curState;
          LLVM_DEBUG(llvm::dbgs() << "  Enable @" << enable.getGroupName()
                                  << " -> state " << curState << "\n");
        }
        // Allocate 'latency' states for this step
        return curState + latency;
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
        // Parallel: fork-join pattern
        // - Fork state (state N): All branches are enabled
        // - Join: Wait for all branches to complete
        //
        // State allocation strategy:
        // - Assign fork state to the par op
        // - Each branch gets states starting from fork+1
        // - Track max end state across all branches
        // - Next available state is max_end + 1
        uint64_t forkState = (curState == 0) ? 1 : curState;
        stateIds[par] = forkState;
        LLVM_DEBUG(llvm::dbgs() << "  Par fork -> state " << forkState << "\n");

        // Process each branch, tracking max end state
        uint64_t maxBranchEnd = forkState;
        for (Operation &stmt : par.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt)) {
            // Each branch starts at forkState + 1
            // (branches execute concurrently, so they share state space
            //  but in terms of FSM, they're all enabled from fork state)
            uint64_t branchEnd = computeUniqueIdsForOp(&stmt, forkState + 1);
            maxBranchEnd = std::max(maxBranchEnd, branchEnd);
            LLVM_DEBUG(llvm::dbgs() << "    Branch ends at state " << branchEnd << "\n");
          }
        }

        // Return next state after all branches complete
        return maxBranchEnd;
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
        // While loop needs:
        // - Header state for condition checking (state N)
        // - Body states starting at N+1
        uint64_t headerState = (curState == 0) ? 1 : curState;
        stateIds[whileOp] = headerState;
        LLVM_DEBUG(llvm::dbgs() << "  While header -> state " << headerState << "\n");

        // Process body starting at headerState + 1
        uint64_t bodyNext = headerState + 1;
        for (Operation &stmt : whileOp.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            bodyNext = computeUniqueIdsForOp(&stmt, bodyNext);
        }

        return bodyNext;
      })
      .Case<ProcStaticRepeatOp>([&](ProcStaticRepeatOp repeatOp) {
        // Static repeat: unroll state allocation for each iteration
        // Total states = count * body_states
        uint64_t cur = (curState == 0) ? 1 : curState;
        stateIds[repeatOp] = cur;
        LLVM_DEBUG(llvm::dbgs() << "  StaticRepeat (count=" << repeatOp.getCount()
                                << ") -> state " << cur << "\n");

        // Process body states for each iteration with iteration-aware state IDs
        int64_t count = repeatOp.getCount();
        int64_t savedIteration = currentIteration;
        for (int64_t i = 0; i < count; ++i) {
          currentIteration = i;
          for (Operation &stmt : repeatOp.getBody().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              cur = computeUniqueIdsForOp(&stmt, cur);
          }
        }
        currentIteration = savedIteration;

        return cur;
      })
      .Case<ProcStaticIfOp>([&](ProcStaticIfOp staticIf) {
        // Static if: similar to regular if but with known latencies
        uint64_t cur = (curState == 0) ? 1 : curState;
        stateIds[staticIf] = cur;
        LLVM_DEBUG(llvm::dbgs() << "  StaticIf -> state " << cur << "\n");

        // Process then branch
        uint64_t thenNext = cur;
        for (Operation &stmt : staticIf.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            thenNext = computeUniqueIdsForOp(&stmt, thenNext);
        }

        // Process else branch
        uint64_t elseNext = thenNext;
        if (!staticIf.getElseRegion().empty()) {
          for (Operation &stmt : staticIf.getElseRegion().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              elseNext = computeUniqueIdsForOp(&stmt, elseNext);
          }
        }

        return elseNext;
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
        exits.push_back({state, GuardSpec::unconditional()});
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
        // Loop exits from header state when condition is false
        // The header state has guard=!cond for exit
        uint64_t headerState = stateIds[whileOp];
        exits.push_back({headerState, GuardSpec::negative(whileOp, whileOp.getCond())});
      })
      .Case<ProcStaticRepeatOp>([&](ProcStaticRepeatOp repeatOp) {
        // Static repeat exits after all iterations complete
        // The exit is from the last statement of the last iteration
        Block &block = repeatOp.getBody().front();
        for (auto it = block.rbegin(); it != block.rend(); ++it) {
          if (!isa<ProcControlEndOp>(*it)) {
            controlExits(&*it, exits, builder);
            break;
          }
        }
      })
      .Case<ProcStaticIfOp>([&](ProcStaticIfOp staticIf) {
        // Static if: both branches contribute exits (similar to dynamic if)
        for (Operation &stmt : staticIf.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            controlExits(&stmt, exits, builder);
        }
        if (!staticIf.getElseRegion().empty()) {
          for (Operation &stmt : staticIf.getElseRegion().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              controlExits(&stmt, exits, builder);
          }
        }
      })
      .Case<ProcParOp>([&](ProcParOp par) {
        // Parallel: all branches contribute exits
        // Join happens when ALL branches complete
        // Each branch's exit contributes to the overall parallel exit
        for (Operation &stmt : par.getBody().front()) {
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
        // Get state ID - use iteration-aware if inside static_repeat
        uint64_t curState;
        if (currentIteration >= 0) {
          auto it = iterStateIds.find({enable.getOperation(), currentIteration});
          if (it != iterStateIds.end()) {
            curState = it->second;
          } else {
            // Fallback to stateIds if not found
            curState = stateIds[enable];
          }
        } else {
          curState = stateIds[enable];
        }

        // Look up the step to get its latency and type
        int64_t latency = 1;
        bool isStaticStep = false;
        StringRef stepName = enable.getStepName();
        auto it = stepMap.find(stepName);
        if (it != stepMap.end()) {
          if (auto staticStep = dyn_cast<ProcStaticStepOp>(it->second)) {
            latency = staticStep.getLatency();
            isStaticStep = true;
          }
          // Dynamic steps (ProcStepOp) have latency=1 but need done signal
        }

        // Add transitions from predecessors to current state
        for (auto &pred : preds) {
          schedule.addTransition(pred.state, curState, pred.guard);
        }

        // For multi-cycle static steps, add internal transitions (unconditional)
        for (int64_t i = 0; i < latency - 1; ++i) {
          schedule.addTransition(curState + i, curState + i + 1);
        }

        // Record the enable (will generate go signal later)
        schedule.addEnable(curState, nullptr, nullptr, nullptr);

        // Return exit edge from the last state of this step
        // - Static steps: unconditional (FSM counts cycles)
        // - Dynamic steps: guarded by done signal
        uint64_t exitState = curState + latency - 1;
        if (isStaticStep) {
          return SmallVector<PredEdge>{{exitState, GuardSpec::unconditional()}};
        } else {
          // Dynamic step: exit is guarded by step's done signal
          return SmallVector<PredEdge>{{exitState, GuardSpec::doneGuard(stepName)}};
        }
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

        // True branch: predecessors with positive condition guard
        SmallVector<PredEdge> truPreds;
        for (auto &p : preds) {
          // Create guard: cond = true (positive guard)
          truPreds.push_back({p.state, GuardSpec::positive(ifOp, cond)});
        }

        SmallVector<PredEdge> truExits;
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            truExits = calculateStatesRecur(schedule, &stmt, truPreds, builder,
                                            module);
        }

        // False branch: predecessors with negative condition guard (!cond)
        SmallVector<PredEdge> falExits;
        if (!ifOp.getElseRegion().empty()) {
          SmallVector<PredEdge> falPreds;
          for (auto &p : preds) {
            // Create guard: cond = false (negative/inverted guard)
            falPreds.push_back({p.state, GuardSpec::negative(ifOp, cond)});
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
        // While loop FSM structure:
        // - Header state: condition is checked here
        // - Body entry: transition from header when cond=true
        // - Body exit -> header: back-edge to re-check condition
        // - Header -> exit: transition when cond=false

        Value cond = whileOp.getCond();
        uint64_t headerState = stateIds[whileOp];

        // Add transitions from predecessors to header state
        for (auto &pred : preds) {
          schedule.addTransition(pred.state, headerState, pred.guard);
        }

        // Process body with header as predecessor (guarded by cond=true)
        SmallVector<PredEdge> bodyPreds;
        bodyPreds.push_back({headerState, GuardSpec::positive(whileOp, cond)});

        SmallVector<PredEdge> bodyExits;
        for (Operation &stmt : whileOp.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            bodyExits = calculateStatesRecur(schedule, &stmt, bodyPreds,
                                             builder, module);
        }

        // Add back-edge: body exit -> header (to re-check condition)
        for (auto &exitPred : bodyExits) {
          schedule.addTransition(exitPred.state, headerState,
                                 GuardSpec::unconditional());
        }

        // Return exit edge: header with guard=!cond (loop termination)
        // The actual exit transitions will be added by the caller
        return SmallVector<PredEdge>{{headerState, GuardSpec::negative(whileOp, cond)}};
      })
      .Case<ProcStaticRepeatOp>([&](ProcStaticRepeatOp repeatOp) {
        // Static repeat: process body 'count' times sequentially
        // Each iteration's exit becomes the next iteration's predecessor
        SmallVector<PredEdge> prev = preds;
        int64_t count = repeatOp.getCount();

        int64_t savedIteration = currentIteration;
        for (int64_t i = 0; i < count; ++i) {
          currentIteration = i;
          for (Operation &stmt : repeatOp.getBody().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              prev = calculateStatesRecur(schedule, &stmt, prev, builder, module);
          }
        }
        currentIteration = savedIteration;

        return prev;
      })
      .Case<ProcStaticIfOp>([&](ProcStaticIfOp staticIf) {
        // Static if: similar to dynamic if but with known branch latencies
        Value cond = staticIf.getCond();

        // True branch: predecessors with positive condition guard
        SmallVector<PredEdge> truPreds;
        for (auto &p : preds) {
          // Create guard: cond = true (positive guard)
          truPreds.push_back({p.state, GuardSpec::positive(staticIf, cond)});
        }

        SmallVector<PredEdge> truExits;
        for (Operation &stmt : staticIf.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            truExits = calculateStatesRecur(schedule, &stmt, truPreds, builder,
                                            module);
        }

        // False branch: predecessors with negative condition guard (!cond)
        SmallVector<PredEdge> falExits;
        if (!staticIf.getElseRegion().empty()) {
          SmallVector<PredEdge> falPreds;
          for (auto &p : preds) {
            // Create guard: cond = false (negative/inverted guard)
            falPreds.push_back({p.state, GuardSpec::negative(staticIf, cond)});
          }
          for (Operation &stmt : staticIf.getElseRegion().front()) {
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
      .Case<ProcParOp>([&](ProcParOp par) {
        // Parallel: fork-join pattern
        // - Fork: single state enables all branches
        // - Join: wait for all branches to complete
        //
        // FSM structure:
        // - Predecessors transition to fork state
        // - Fork state enables all branches simultaneously
        // - Each branch may have different latencies
        // - Exit when ALL branches are done (for now: when slowest completes)

        uint64_t forkState = stateIds[par];

        // Add transitions from predecessors to fork state
        for (auto &pred : preds) {
          schedule.addTransition(pred.state, forkState, pred.guard);
        }

        // Process all branches from fork state (they all start simultaneously)
        // Each branch gets the fork state as predecessor
        SmallVector<PredEdge> forkPreds;
        forkPreds.push_back({forkState, GuardSpec::unconditional()});

        // Collect exits from all branches
        SmallVector<PredEdge> allBranchExits;
        for (Operation &stmt : par.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt)) {
            SmallVector<PredEdge> branchExits =
                calculateStatesRecur(schedule, &stmt, forkPreds, builder, module);
            allBranchExits.append(branchExits);
          }
        }

        // For fork-join: all branches must complete
        // The parallel block exits from the latest-completing branch
        // In terms of FSM edges, we return all branch exits
        // The caller will handle joining them appropriately
        //
        // TODO: For proper join semantics with dynamic done signals,
        // we would need to AND all branch done signals. For now, we assume
        // static steps where latency determines completion.
        return allBranchExits;
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
      llvm::dbgs() << "    " << from << " -> " << to;
      if (!guard.isUnconditional()) {
        llvm::dbgs() << " [guard: " << (guard.inverted ? "NOT " : "");
        if (isa<ProcIfOp>(guard.sourceOp))
          llvm::dbgs() << "if";
        else if (isa<ProcStaticIfOp>(guard.sourceOp))
          llvm::dbgs() << "static_if";
        llvm::dbgs() << "]";
      }
      llvm::dbgs() << "\n";
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
  // First add non-iteration state assignments
  for (auto &[enableOp, state] : stateIds) {
    if (auto enable = dyn_cast<ProcEnableOp>(enableOp)) {
      auto entry = builder.getDictionaryAttr({
        builder.getNamedAttr("step", enable.getStepNameAttr()),
        builder.getNamedAttr("state", builder.getI64IntegerAttr(state))
      });
      stateAssigns.push_back(entry);
    }
  }
  // Then add iteration-aware state assignments from static_repeat
  for (auto &[key, state] : iterStateIds) {
    auto [enableOp, iteration] = key;
    if (auto enable = dyn_cast<ProcEnableOp>(enableOp)) {
      auto entry = builder.getDictionaryAttr({
        builder.getNamedAttr("step", enable.getStepNameAttr()),
        builder.getNamedAttr("state", builder.getI64IntegerAttr(state)),
        builder.getNamedAttr("iteration", builder.getI64IntegerAttr(iteration))
      });
      stateAssigns.push_back(entry);
    }
  }
  procOp->setAttr("tdcc.enables", builder.getArrayAttr(stateAssigns));

  // Build a map from if/static_if operations to unique IDs for guard serialization
  DenseMap<Operation *, int64_t> condOpIds;
  SmallVector<Attribute> condOpAttrs;
  int64_t nextCondOpId = 0;

  // First pass: identify all unique condition operations from transitions
  for (auto &[from, to, guard] : schedule.transitions) {
    if (!guard.isUnconditional() && guard.sourceOp) {
      if (condOpIds.find(guard.sourceOp) == condOpIds.end()) {
        condOpIds[guard.sourceOp] = nextCondOpId++;
      }
    }
  }

  // Store condition operation info (for ProcStmtToAction to look up)
  // Each condition op is identified by its operation type and location in control region
  for (auto &[condOp, id] : condOpIds) {
    SmallVector<NamedAttribute> attrs;
    attrs.push_back(builder.getNamedAttr("id", builder.getI64IntegerAttr(id)));

    // Store operation type for identification
    if (isa<ProcIfOp>(condOp)) {
      attrs.push_back(builder.getNamedAttr("type", builder.getStringAttr("if")));
    } else if (isa<ProcStaticIfOp>(condOp)) {
      attrs.push_back(builder.getNamedAttr("type", builder.getStringAttr("static_if")));
    } else if (isa<ProcWhileOp>(condOp)) {
      attrs.push_back(builder.getNamedAttr("type", builder.getStringAttr("while")));
    }

    condOpAttrs.push_back(builder.getDictionaryAttr(attrs));
  }
  if (!condOpAttrs.empty()) {
    procOp->setAttr("tdcc.cond_ops", builder.getArrayAttr(condOpAttrs));
  }

  // Store transitions with guard information
  SmallVector<Attribute> transAttrs;
  for (auto &[from, to, guard] : schedule.transitions) {
    SmallVector<NamedAttribute> attrs;
    attrs.push_back(builder.getNamedAttr("from", builder.getI64IntegerAttr(from)));
    attrs.push_back(builder.getNamedAttr("to", builder.getI64IntegerAttr(to)));

    // Serialize guard information
    if (!guard.isUnconditional()) {
      // Store reference to the condition operation (for if/while guards)
      if (guard.sourceOp) {
        auto it = condOpIds.find(guard.sourceOp);
        if (it != condOpIds.end()) {
          attrs.push_back(builder.getNamedAttr("guard_op_id",
                                               builder.getI64IntegerAttr(it->second)));
          attrs.push_back(builder.getNamedAttr("guard_inverted",
                                               builder.getBoolAttr(guard.inverted)));
        }
      }
      // Store done signal guard (for dynamic step exits)
      if (guard.hasDoneGuard()) {
        attrs.push_back(builder.getNamedAttr("done_step",
                                             builder.getStringAttr(guard.doneStepName)));
      }
    }

    transAttrs.push_back(builder.getDictionaryAttr(attrs));
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
  iterStateIds.clear();
  currentIteration = -1;

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

  SmallVector<PredEdge> initPreds = {{0, GuardSpec::unconditional()}}; // Start at state 0

  for (Operation &op : controlRegion->front()) {
    if (!isa<ProcControlEndOp>(op))
      initPreds = calculateStatesRecur(schedule, &op, initPreds, builder, module);
  }

  // Step 3: Add transitions from final exits to done state
  // The done state is maxState + 1, representing completion of the procedural block
  // NOTE: We push directly to transitions instead of using addTransition() to avoid
  // updating maxState, since doneState is calculated as maxState + 1 in realizeSchedule
  uint64_t doneState = schedule.maxState + 1;
  for (auto &exitPred : initPreds) {
    schedule.transitions.push_back({exitPred.state, doneState, exitPred.guard});
    LLVM_DEBUG(llvm::dbgs() << "  Added final transition: " << exitPred.state
                            << " -> " << doneState << " (done state)\n");
  }

  // Step 4: Realize the schedule as hardware
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
