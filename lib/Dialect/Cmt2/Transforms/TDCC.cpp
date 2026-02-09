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
struct GuardSpec {
  /// The operation that defines the condition (e.g., ProcIfOp, ProcStaticIfOp).
  /// nullptr means unconditional (always true).
  Operation *sourceOp = nullptr;

  /// Whether the guard should be inverted (for else branches).
  bool inverted = false;

  /// The actual FIRRTL Value for the condition (used during schedule building).
  Value condValue = nullptr;

  /// For parallel join: list of branch completion register names.
  /// The join transition fires when ALL of these are true.
  SmallVector<std::string> parJoinBranches;

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

  /// Create a parallel join guard (AND of all branch completions).
  static GuardSpec parJoin(SmallVector<std::string> branchNames) {
    GuardSpec g;
    g.parJoinBranches = std::move(branchNames);
    return g;
  }

  bool isUnconditional() const {
    return sourceOp == nullptr && parJoinBranches.empty();
  }
  bool hasParJoinGuard() const { return !parJoinBranches.empty(); }
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

/// Info about a parallel branch for fork-join tracking.
struct ParBranchInfo {
  std::string name;       // Unique name for the branch
  uint64_t exitState;     // State where branch completes
  GuardSpec exitGuard;    // Guard for branch completion
  uint64_t firstState;    // First state of this branch (for per-branch FSM)
  uint64_t lastState;     // Last state used by this branch
  bool needsSeparateFsm;  // True if branch has nested control (while, if, etc.)
};

/// Info about a parallel (fork-join) block.
struct ParBlockInfo {
  uint64_t forkState;                   // State where fork happens
  uint64_t joinState;                   // State where join happens
  SmallVector<ParBranchInfo> branches;  // Info about each branch
  bool needsPerBranchFsm;               // True if any branch has nested control
};

//===----------------------------------------------------------------------===//
// Per-Branch FSM Data Structures (for complex par)
//===----------------------------------------------------------------------===//

/// A transition within a branch FSM.
struct BranchTransition {
  uint64_t fromState;     // Source state (0 = idle/done, 1+ = active)
  uint64_t toState;       // Destination state
  GuardSpec guard;        // Transition guard (done signal, condition, etc.)
};

/// An enable within a branch (step activation at a specific state).
struct BranchEnable {
  uint64_t state;         // Branch FSM state where this enable fires
  StringRef stepName;     // Name of the step to enable
  bool isStatic;          // True if static step
  int64_t latency;        // Step latency (for static steps)
};

/// Complete FSM info for a single branch of a par block.
struct BranchFsmInfo {
  std::string name;                       // Branch FSM name (e.g., "__par_0_branch_0")
  uint64_t parId = 0;                     // Parent par block ID
  uint64_t branchIdx = 0;                 // Branch index within the par
  uint64_t numStates = 0;                 // Number of states (including state 0 = idle/done)
  std::vector<BranchTransition> transitions; // State transitions
  std::vector<BranchEnable> enables;       // Step enables per state
  Operation *branchOp = nullptr;          // The root operation of this branch

  /// Get the FSM register name
  std::string getFsmRegName() const {
    return name + "_fsm";
  }

  /// Get the done value name
  std::string getDoneValueName() const {
    return name + "_done";
  }

  /// Get the tick rule name
  std::string getTickRuleName() const {
    return name + "_tick";
  }
};

/// Extended info for a complex par block with per-branch FSMs.
struct ComplexParInfo {
  uint64_t parId = 0;                     // Unique ID for this par block
  ProcParOp parOp;                        // The par operation
  uint64_t forkState = 0;                 // Main FSM state for fork
  uint64_t joinState = 0;                 // Main FSM state for join
  std::vector<BranchFsmInfo> branchFsms;  // FSM info for each branch

  /// Get the fork rule name
  std::string getForkRuleName() const {
    return "__par_" + std::to_string(parId) + "_fork";
  }
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

  /// Parallel blocks for fork-join tracking.
  SmallVector<ParBlockInfo> parBlocks;

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

  /// Complex par blocks that need per-branch FSM generation.
  std::vector<ComplexParInfo> complexParBlocks;

  /// Counter for generating unique par block IDs.
  uint64_t nextParId = 0;

  /// State sequence order tracking for precedence generation.
  /// Maps state ID to its sequence index (order in which states are encountered).
  /// Later states (higher seq_idx) have higher precedence in pipeline semantics.
  DenseMap<uint64_t, uint64_t> stateSequenceIndex;
  uint64_t nextSequenceIndex = 0;

  /// Record a state in the sequence order (called when state is first allocated).
  void recordStateOrder(uint64_t state) {
    if (stateSequenceIndex.find(state) == stateSequenceIndex.end()) {
      stateSequenceIndex[state] = nextSequenceIndex++;
      LLVM_DEBUG(llvm::dbgs() << "    State " << state << " -> seq_idx "
                              << stateSequenceIndex[state] << "\n");
    }
  }

  /// Analyze the control flow in a branch and compute the states needed.
  /// Returns the number of states required (1+ for active states, 0 is reserved for idle/done).
  uint64_t analyzeBranchControl(Operation *branchOp, BranchFsmInfo &branchFsm);

  /// Check if a region contains proc control operations.
  static bool containsProcControl(Region &region);

  /// Process a DataflowTaskOp that contains proc control operations.
  void processDataflowTask(DataflowTaskOp task, cmt2::ModuleOp module);
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
        bool isStatic = false;
        StringRef stepName = enable.getStepName();
        auto it = stepMap.find(stepName);
        if (it != stepMap.end()) {
          if (auto staticStep = dyn_cast<ProcStaticStepOp>(it->second)) {
            latency = staticStep.getLatency();
            isStatic = true;
            LLVM_DEBUG(llvm::dbgs() << "  Enable @" << stepName
                                    << " has static latency " << latency << "\n");
          }
        }

        // Use iteration-aware state ID if inside static_repeat
        if (currentIteration >= 0) {
          iterStateIds[{enable.getOperation(), currentIteration}] = curState;
          LLVM_DEBUG(llvm::dbgs() << "  Enable @" << enable.getStepName()
                                  << " iter " << currentIteration
                                  << " -> state " << curState << "\n");
        } else {
          stateIds[enable] = curState;
          LLVM_DEBUG(llvm::dbgs() << "  Enable @" << enable.getStepName()
                                  << " -> state " << curState << "\n");
        }

        // TL2: Annotate enable with execution timing for downstream passes
        // This allows ProcStmtToAction to propagate timing to cloned CallOps
        OpBuilder attrBuilder(enable.getContext());
        enable->setAttr("tdcc.start_state",
                        attrBuilder.getI64IntegerAttr(curState));
        enable->setAttr("tdcc.end_state",
                        attrBuilder.getI64IntegerAttr(curState + latency));
        enable->setAttr("tdcc.latency",
                        attrBuilder.getI64IntegerAttr(latency));
        enable->setAttr("tdcc.is_static",
                        attrBuilder.getBoolAttr(isStatic));

        // Record state order for precedence generation
        // For multi-cycle steps, record all intermediate states
        for (int64_t i = 0; i < latency; ++i) {
          recordStateOrder(curState + i);
        }

        // Allocate 'latency' states for this step
        return curState + latency;
      })
      .Case<ProcSeqOp>([&](ProcSeqOp seq) {
        // Sequential: states numbered consecutively
        uint64_t cur = curState;
        for (Operation &stmt : seq.getBody().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            cur = computeUniqueIdsForOp(&stmt, cur);
        }
        return cur;
      })
      .Case<ProcParOp>([&](ProcParOp par) {
        // Parallel: fork-join pattern
        // Two modes based on branch complexity:
        //
        // 1. Simple par (all branches are single enables):
        //    - All enables share the SAME state (true hardware parallelism)
        //    - State allocation: fork -> execState -> join
        //    - Max latency determines state increment
        //
        // 2. Complex par (branches have nested control):
        //    - Each branch gets non-overlapping state ranges (sequential)
        //    - Requires per-branch FSM (future enhancement)

        uint64_t forkState = (curState == 0) ? 1 : curState;
        stateIds[par] = forkState;
        recordStateOrder(forkState);  // Record fork state in sequence
        LLVM_DEBUG(llvm::dbgs() << "  Par fork -> state " << forkState << "\n");

        // Check if this is a simple par (all children are enables)
        bool isSimplePar = true;
        SmallVector<ProcEnableOp> enableOps;
        for (Operation &stmt : par.getBody().front()) {
          if (isa<ProcControlEndOp, ProcYieldOp>(stmt))
            continue;
          if (auto enable = dyn_cast<ProcEnableOp>(stmt)) {
            enableOps.push_back(enable);
          } else {
            isSimplePar = false;
            break;
          }
        }

        if (isSimplePar && !enableOps.empty()) {
          // Simple par: all enables get the same state
          // Compute max latency across all branches
          int64_t maxLatency = 1;
          uint64_t execState = forkState + 1;

          for (auto enable : enableOps) {
            // Assign same state to all enables
            if (currentIteration >= 0) {
              iterStateIds[{enable.getOperation(), currentIteration}] = execState;
            } else {
              stateIds[enable] = execState;
            }
            LLVM_DEBUG(llvm::dbgs() << "    Par enable @" << enable.getStepName()
                                    << " -> state " << execState << " (shared)\n");

            // Look up latency for this step
            int64_t latency = 1;
            bool isStatic = false;
            StringRef stepName = enable.getStepName();
            auto it = stepMap.find(stepName);
            if (it != stepMap.end()) {
              if (auto staticStep = dyn_cast<ProcStaticStepOp>(it->second)) {
                latency = staticStep.getLatency();
                isStatic = true;
              }
            }
            maxLatency = std::max(maxLatency, latency);

            // Annotate enable with timing
            OpBuilder attrBuilder(enable.getContext());
            enable->setAttr("tdcc.start_state",
                            attrBuilder.getI64IntegerAttr(execState));
            enable->setAttr("tdcc.end_state",
                            attrBuilder.getI64IntegerAttr(execState + latency));
            enable->setAttr("tdcc.latency",
                            attrBuilder.getI64IntegerAttr(latency));
            enable->setAttr("tdcc.is_static",
                            attrBuilder.getBoolAttr(isStatic));
            enable->setAttr("tdcc.in_simple_par",
                            attrBuilder.getBoolAttr(true));
          }

          // Record exec state and all intermediate states for multi-cycle enables
          for (int64_t i = 0; i < maxLatency; ++i) {
            recordStateOrder(execState + i);
          }

          // Join state is after max latency
          uint64_t joinState = execState + maxLatency;
          recordStateOrder(joinState);  // Record join state in sequence
          LLVM_DEBUG(llvm::dbgs() << "  Simple par: exec state " << execState
                                  << ", max latency " << maxLatency
                                  << ", join -> state " << joinState << "\n");
          return joinState;
        }

        // Complex par: each branch gets its own FSM
        // Main FSM only has fork state and join state.
        // Each branch FSM tracks its own control flow independently.

        // Create ComplexParInfo for this par block
        ComplexParInfo complexPar;
        complexPar.parId = nextParId++;
        complexPar.parOp = par;
        complexPar.forkState = forkState;

        // Join state is immediately after fork state in main FSM
        uint64_t joinState = forkState + 1;
        complexPar.joinState = joinState;
        recordStateOrder(joinState);  // Record join state in sequence

        LLVM_DEBUG(llvm::dbgs() << "  Complex par " << complexPar.parId
                                << ": fork=" << forkState << ", join=" << joinState << "\n");

        // Analyze each branch and create its FSM info
        size_t branchIdx = 0;
        for (Operation &stmt : par.getBody().front()) {
          if (isa<ProcControlEndOp, ProcYieldOp>(stmt))
            continue;

          // Create BranchFsmInfo for this branch
          BranchFsmInfo branchFsm;
          branchFsm.name = "__par_" + std::to_string(complexPar.parId) +
                          "_branch_" + std::to_string(branchIdx);
          branchFsm.parId = complexPar.parId;
          branchFsm.branchIdx = branchIdx;

          // Analyze the branch control flow to compute states
          analyzeBranchControl(&stmt, branchFsm);

          LLVM_DEBUG(llvm::dbgs() << "    Branch " << branchIdx
                                  << " (" << branchFsm.name << "): "
                                  << branchFsm.numStates << " states, "
                                  << branchFsm.enables.size() << " enables, "
                                  << branchFsm.transitions.size() << " transitions\n");

          complexPar.branchFsms.push_back(std::move(branchFsm));
          branchIdx++;
        }

        // Store the complex par info for later processing in calculateStatesRecur
        complexParBlocks.push_back(std::move(complexPar));

        LLVM_DEBUG(llvm::dbgs() << "  Complex par complete, main FSM uses states "
                                << forkState << "-" << joinState << "\n");

        // Main FSM only uses fork and join states
        // Return state after join
        return joinState + 1;
      })
      .Case<ProcIfOp>([&](ProcIfOp ifOp) {
        // Branches can't get initial state (start at 1 if curState == 0)
        uint64_t cur = (curState == 0) ? 1 : curState;
        stateIds[ifOp] = cur;
        recordStateOrder(cur);  // Record if header state

        // Process then branch
        uint64_t thenNext = cur;
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            thenNext = computeUniqueIdsForOp(&stmt, thenNext);
        }

        // Process else branch
        uint64_t elseNext = thenNext;
        if (!ifOp.getElseRegion().empty()) {
          for (Operation &stmt : ifOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              elseNext = computeUniqueIdsForOp(&stmt, elseNext);
          }
        }

        return elseNext;
      })
      .Case<ProcCondIfOp>([&](ProcCondIfOp condIfOp) {
        // Similar to ProcIfOp but condition comes from condition region
        uint64_t cur = (curState == 0) ? 1 : curState;
        stateIds[condIfOp] = cur;
        recordStateOrder(cur);  // Record cond_if header state

        // Process then branch
        uint64_t thenNext = cur;
        for (Operation &stmt : condIfOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            thenNext = computeUniqueIdsForOp(&stmt, thenNext);
        }

        // Process else branch
        uint64_t elseNext = thenNext;
        if (!condIfOp.getElseRegion().empty()) {
          for (Operation &stmt : condIfOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
        recordStateOrder(headerState);  // Record while header state
        LLVM_DEBUG(llvm::dbgs() << "  While header -> state " << headerState << "\n");

        // Process body starting at headerState + 1
        uint64_t bodyNext = headerState + 1;
        for (Operation &stmt : whileOp.getBody().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            bodyNext = computeUniqueIdsForOp(&stmt, bodyNext);
        }

        return bodyNext;
      })
      .Case<ProcStaticRepeatOp>([&](ProcStaticRepeatOp repeatOp) {
        // Static repeat: unroll state allocation for each iteration
        // Total states = count * body_states
        uint64_t cur = (curState == 0) ? 1 : curState;
        stateIds[repeatOp] = cur;
        recordStateOrder(cur);  // Record static_repeat header state
        LLVM_DEBUG(llvm::dbgs() << "  StaticRepeat (count=" << repeatOp.getCount()
                                << ") -> state " << cur << "\n");

        // Process body states for each iteration with iteration-aware state IDs
        int64_t count = repeatOp.getCount();
        int64_t savedIteration = currentIteration;
        for (int64_t i = 0; i < count; ++i) {
          currentIteration = i;
          for (Operation &stmt : repeatOp.getBody().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
        recordStateOrder(cur);  // Record static_if header state
        LLVM_DEBUG(llvm::dbgs() << "  StaticIf -> state " << cur << "\n");

        // Process then branch
        uint64_t thenNext = cur;
        for (Operation &stmt : staticIf.getThenRegion().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            thenNext = computeUniqueIdsForOp(&stmt, thenNext);
        }

        // Process else branch
        uint64_t elseNext = thenNext;
        if (!staticIf.getElseRegion().empty()) {
          for (Operation &stmt : staticIf.getElseRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
// Branch FSM Analysis (for complex par)
//===----------------------------------------------------------------------===//

uint64_t TDCCPass::analyzeBranchControl(Operation *branchOp,
                                         BranchFsmInfo &branchFsm) {
  // Recursively analyze the control structure and compute branch FSM info.
  // Branch FSM state 0 = idle/done, state 1+ = active execution states.
  //
  // For each control construct in the branch:
  // - ProcEnableOp: one state per latency cycle
  // - ProcSeqOp: sequential states
  // - ProcIfOp/ProcStaticIfOp: states for then/else branches (non-overlapping)
  // - ProcWhileOp: header state + body states with back-edge
  // - ProcStaticRepeatOp: unrolled states for each iteration
  //
  // Returns the total number of states (including state 0 for idle/done).

  // Helper to recursively compute states for an operation
  std::function<uint64_t(Operation *, uint64_t)> computeBranchStates =
      [&](Operation *op, uint64_t curState) -> uint64_t {
    return llvm::TypeSwitch<Operation *, uint64_t>(op)
        .Case<ProcEnableOp>([&](ProcEnableOp enable) {
          // Look up step latency
          int64_t latency = 1;
          bool isStatic = false;
          StringRef stepName = enable.getStepName();
          auto it = stepMap.find(stepName);
          if (it != stepMap.end()) {
            if (auto staticStep = dyn_cast<ProcStaticStepOp>(it->second)) {
              latency = staticStep.getLatency();
              isStatic = true;
            }
          }

          // Add enable for this state
          BranchEnable brEnable;
          brEnable.state = curState;
          brEnable.stepName = stepName;
          brEnable.isStatic = isStatic;
          brEnable.latency = latency;
          branchFsm.enables.push_back(brEnable);

          // Add transitions for multi-cycle steps (internal cycles)
          for (int64_t i = 0; i < latency - 1; ++i) {
            BranchTransition trans;
            trans.fromState = curState + i;
            trans.toState = curState + i + 1;
            trans.guard = GuardSpec::unconditional();
            branchFsm.transitions.push_back(trans);
          }

          // Add transition from last cycle of this enable to the next state
          // This is needed for sequential control flow between enables
          uint64_t lastCycle = curState + latency - 1;
          uint64_t nextState = curState + latency;
          {
            BranchTransition trans;
            trans.fromState = lastCycle;
            trans.toState = nextState;
            trans.guard = GuardSpec::unconditional();
            branchFsm.transitions.push_back(trans);
          }

          return curState + latency;
        })
        .Case<ProcSeqOp>([&](ProcSeqOp seq) {
          uint64_t cur = curState;
          for (Operation &stmt : seq.getBody().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              cur = computeBranchStates(&stmt, cur);
          }
          return cur;
        })
        .Case<ProcIfOp>([&](ProcIfOp ifOp) {
          uint64_t cur = curState;

          // Process then branch
          uint64_t thenNext = cur;
          for (Operation &stmt : ifOp.getThenRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              thenNext = computeBranchStates(&stmt, thenNext);
          }

          // Process else branch (states continue after then)
          uint64_t elseNext = thenNext;
          if (!ifOp.getElseRegion().empty()) {
            for (Operation &stmt : ifOp.getElseRegion().front()) {
              if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
                elseNext = computeBranchStates(&stmt, elseNext);
            }
          }

          return elseNext;
        })
        .Case<ProcCondIfOp>([&](ProcCondIfOp condIfOp) {
          uint64_t cur = curState;

          // Process then branch
          uint64_t thenNext = cur;
          for (Operation &stmt : condIfOp.getThenRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              thenNext = computeBranchStates(&stmt, thenNext);
          }

          // Process else branch (states continue after then)
          uint64_t elseNext = thenNext;
          if (!condIfOp.getElseRegion().empty()) {
            for (Operation &stmt : condIfOp.getElseRegion().front()) {
              if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
                elseNext = computeBranchStates(&stmt, elseNext);
            }
          }

          return elseNext;
        })
        .Case<ProcStaticIfOp>([&](ProcStaticIfOp staticIf) {
          uint64_t cur = curState;

          // Process then branch
          uint64_t thenNext = cur;
          for (Operation &stmt : staticIf.getThenRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              thenNext = computeBranchStates(&stmt, thenNext);
          }

          // Process else branch
          uint64_t elseNext = thenNext;
          if (!staticIf.getElseRegion().empty()) {
            for (Operation &stmt : staticIf.getElseRegion().front()) {
              if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
                elseNext = computeBranchStates(&stmt, elseNext);
            }
          }

          return elseNext;
        })
        .Case<ProcWhileOp>([&](ProcWhileOp whileOp) {
          // While loop structure:
          //   headerState: condition check
          //   headerState+1 to bodyNext-1: body execution
          //   bodyNext: exit state (after loop)
          //
          // Transitions:
          //   header -> body start (when condition is TRUE)
          //   header -> exit (when condition is FALSE)
          //   body end -> header (back-edge, unconditional)

          uint64_t headerState = curState;
          uint64_t bodyStartState = headerState + 1;

          // Process body starting at headerState + 1
          uint64_t bodyNext = bodyStartState;
          for (Operation &stmt : whileOp.getBody().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              bodyNext = computeBranchStates(&stmt, bodyNext);
          }

          uint64_t exitState = bodyNext;

          // Transition: header -> body start (when condition is TRUE)
          {
            BranchTransition enterBody;
            enterBody.fromState = headerState;
            enterBody.toState = bodyStartState;
            enterBody.guard = GuardSpec::positive(whileOp, whileOp.getCond());
            branchFsm.transitions.push_back(enterBody);
          }

          // Transition: header -> exit (when condition is FALSE)
          {
            BranchTransition exitLoop;
            exitLoop.fromState = headerState;
            exitLoop.toState = exitState;
            exitLoop.guard = GuardSpec::negative(whileOp, whileOp.getCond());
            branchFsm.transitions.push_back(exitLoop);
          }

          // Back-edge: body end -> header (unconditional)
          {
            BranchTransition backEdge;
            backEdge.fromState = bodyNext - 1;
            backEdge.toState = headerState;
            backEdge.guard = GuardSpec::unconditional();
            branchFsm.transitions.push_back(backEdge);
          }

          return bodyNext;
        })
        .Case<ProcStaticRepeatOp>([&](ProcStaticRepeatOp repeatOp) {
          // Unroll states for each iteration
          uint64_t cur = curState;
          int64_t count = repeatOp.getCount();
          for (int64_t i = 0; i < count; ++i) {
            for (Operation &stmt : repeatOp.getBody().front()) {
              if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
                cur = computeBranchStates(&stmt, cur);
            }
          }
          return cur;
        })
        .Default([&](Operation *) { return curState; });
  };

  // Start computing from state 1 (state 0 is idle/done)
  uint64_t finalState = computeBranchStates(branchOp, 1);

  // Add final transition to done state
  // State encoding:
  //   0 = idle (never started)
  //   1 to finalState-1 = active execution states
  //   finalState = done (completed)
  // This allows distinguishing between "idle" and "done" for join checks.
  if (finalState > 1) {
    BranchTransition finalTrans;
    finalTrans.fromState = finalState - 1;
    finalTrans.toState = finalState;  // Transition to done state, not 0
    finalTrans.guard = GuardSpec::unconditional();
    branchFsm.transitions.push_back(finalTrans);
  }

  branchFsm.numStates = finalState + 1; // States: 0 (idle), 1..finalState-1 (active), finalState (done)
  branchFsm.branchOp = branchOp;

  return finalState;
}

//===----------------------------------------------------------------------===//
// Dataflow Task Processing (C1 - Dataflow-Proc Compatibility)
//===----------------------------------------------------------------------===//

bool TDCCPass::containsProcControl(Region &region) {
  // Check if any operation in the region is a proc control operation
  for (Block &block : region) {
    for (Operation &op : block) {
      if (isa<ProcSeqOp, ProcParOp, ProcIfOp, ProcCondIfOp, ProcWhileOp,
              ProcStaticRepeatOp, ProcStaticIfOp, ProcEnableOp>(&op))
        return true;
      // Recursively check nested regions
      for (Region &nestedRegion : op.getRegions()) {
        if (containsProcControl(nestedRegion))
          return true;
      }
    }
  }
  return false;
}

void TDCCPass::processDataflowTask(DataflowTaskOp task, cmt2::ModuleOp module) {
  Region &body = task.getBody();
  if (body.empty())
    return;

  // Check if task body contains proc control operations
  if (!containsProcControl(body)) {
    LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                            << " has no proc control, skipping TDCC\n");
    return;
  }

  LLVM_DEBUG(llvm::dbgs() << "Processing dataflow task @" << task.getSymName()
                          << " with proc control\n");

  // Clear state from previous processing
  stateIds.clear();
  iterStateIds.clear();
  stateSequenceIndex.clear();
  nextSequenceIndex = 0;
  currentIteration = -1;
  complexParBlocks.clear();
  nextParId = 0;

  // Step 1: Compute unique state IDs for proc control ops in task body
  // Unlike ProcRuleOp/ProcMethodOp, task body is a flat region with mixed ops
  LLVM_DEBUG(llvm::dbgs() << "Computing state IDs for task body...\n");
  uint64_t numStates = 0;
  for (Operation &op : body.front()) {
    // Skip non-control operations (firrtl ops, token ops, etc.)
    if (isa<ProcSeqOp, ProcParOp, ProcIfOp, ProcWhileOp,
            ProcStaticRepeatOp, ProcStaticIfOp, ProcEnableOp>(&op)) {
      numStates = computeUniqueIdsForOp(&op, numStates);
    }
  }
  LLVM_DEBUG(llvm::dbgs() << "  Total states: " << numStates << "\n");

  if (numStates == 0)
    return;

  // Step 2: Build the schedule
  OpBuilder builder(task);
  Schedule schedule;

  SmallVector<PredEdge> initPreds = {{0, GuardSpec::unconditional()}};

  for (Operation &op : body.front()) {
    // Only process proc control operations for schedule building
    if (isa<ProcSeqOp, ProcParOp, ProcIfOp, ProcWhileOp,
            ProcStaticRepeatOp, ProcStaticIfOp, ProcEnableOp>(&op)) {
      initPreds = calculateStatesRecur(schedule, &op, initPreds, builder, module);
    }
  }

  // Step 3: Add transitions from final exits to done state
  uint64_t doneState = schedule.maxState + 1;
  for (auto &exitPred : initPreds) {
    schedule.transitions.push_back({exitPred.state, doneState, exitPred.guard});
    LLVM_DEBUG(llvm::dbgs() << "  Added final transition: " << exitPred.state
                            << " -> " << doneState << " (done state)\n");
  }

  // Step 4: Realize the schedule as attributes on the task
  // This is similar to realizeSchedule but stores on DataflowTaskOp
  LLVM_DEBUG({
    llvm::dbgs() << "Schedule for task @" << task.getSymName() << ":\n";
    llvm::dbgs() << "  Max state: " << schedule.maxState << "\n";
    llvm::dbgs() << "  Enables: " << schedule.enables.size() << " states\n";
    llvm::dbgs() << "  Transitions: " << schedule.transitions.size() << "\n";
  });

  // Compute FSM width
  uint64_t totalStates = schedule.maxState + 2; // +1 for done state
  unsigned fsmWidth = llvm::Log2_64_Ceil(totalStates);
  if (fsmWidth == 0) fsmWidth = 1;

  // Add FSM metadata as attributes on the task
  task->setAttr("tdcc.num_states", builder.getI64IntegerAttr(totalStates));
  task->setAttr("tdcc.fsm_width", builder.getI64IntegerAttr(fsmWidth));
  task->setAttr("tdcc.done_state", builder.getI64IntegerAttr(schedule.maxState + 1));
  task->setAttr("tdcc.has_proc_control", builder.getUnitAttr());

  // C5: Infer LS/LI mode from task body analysis
  // If task contains dynamic control (while loops), tokens should be LI
  // Otherwise (static control only), tokens can be LS
  bool hasDynamicControl = false;
  body.walk([&](ProcWhileOp whileOp) {
    hasDynamicControl = true;
  });
  if (hasDynamicControl) {
    task->setAttr("tdcc.requires_li_mode", builder.getUnitAttr());
    LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                            << " requires LI mode (has while loops)\n");
  } else {
    task->setAttr("tdcc.static_timing", builder.getUnitAttr());
    LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                            << " can use LS mode (static control only)\n");
  }

  // Store state assignments for enables
  SmallVector<Attribute> stateAssigns;
  for (auto &[enableOp, state] : stateIds) {
    if (auto enable = dyn_cast<ProcEnableOp>(enableOp)) {
      int64_t endState = state + 1;
      int64_t latency = 1;
      bool isStatic = false;

      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.end_state"))
        endState = attr.getInt();
      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.latency"))
        latency = attr.getInt();
      if (auto attr = enable->getAttrOfType<BoolAttr>("tdcc.is_static"))
        isStatic = attr.getValue();

      auto entry = builder.getDictionaryAttr({
        builder.getNamedAttr("step", enable.getStepNameAttr()),
        builder.getNamedAttr("state", builder.getI64IntegerAttr(state)),
        builder.getNamedAttr("end_state", builder.getI64IntegerAttr(endState)),
        builder.getNamedAttr("latency", builder.getI64IntegerAttr(latency)),
        builder.getNamedAttr("is_static", builder.getBoolAttr(isStatic))
      });
      stateAssigns.push_back(entry);
    }
  }
  // Add iteration-aware state assignments from static_repeat
  for (auto &[key, state] : iterStateIds) {
    auto [enableOp, iteration] = key;
    if (auto enable = dyn_cast<ProcEnableOp>(enableOp)) {
      int64_t endState = state + 1;
      int64_t latency = 1;
      bool isStatic = false;

      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.end_state"))
        endState = attr.getInt();
      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.latency"))
        latency = attr.getInt();
      if (auto attr = enable->getAttrOfType<BoolAttr>("tdcc.is_static"))
        isStatic = attr.getValue();

      auto entry = builder.getDictionaryAttr({
        builder.getNamedAttr("step", enable.getStepNameAttr()),
        builder.getNamedAttr("state", builder.getI64IntegerAttr(state)),
        builder.getNamedAttr("end_state", builder.getI64IntegerAttr(endState)),
        builder.getNamedAttr("latency", builder.getI64IntegerAttr(latency)),
        builder.getNamedAttr("is_static", builder.getBoolAttr(isStatic)),
        builder.getNamedAttr("iteration", builder.getI64IntegerAttr(iteration))
      });
      stateAssigns.push_back(entry);
    }
  }
  if (!stateAssigns.empty())
    task->setAttr("tdcc.enables", builder.getArrayAttr(stateAssigns));

  // Store transitions with guard information
  DenseMap<Operation *, int64_t> condOpIds;
  int64_t nextCondOpId = 0;
  for (auto &[from, to, guard] : schedule.transitions) {
    if (!guard.isUnconditional() && guard.sourceOp) {
      if (condOpIds.find(guard.sourceOp) == condOpIds.end())
        condOpIds[guard.sourceOp] = nextCondOpId++;
    }
  }

  SmallVector<Attribute> transAttrs;
  for (auto &[from, to, guard] : schedule.transitions) {
    SmallVector<NamedAttribute> attrs;
    attrs.push_back(builder.getNamedAttr("from", builder.getI64IntegerAttr(from)));
    attrs.push_back(builder.getNamedAttr("to", builder.getI64IntegerAttr(to)));

    if (!guard.isUnconditional()) {
      if (guard.sourceOp) {
        auto it = condOpIds.find(guard.sourceOp);
        if (it != condOpIds.end()) {
          attrs.push_back(builder.getNamedAttr("guard_op_id",
                                               builder.getI64IntegerAttr(it->second)));
          attrs.push_back(builder.getNamedAttr("guard_inverted",
                                               builder.getBoolAttr(guard.inverted)));
        }
      }
      if (guard.hasParJoinGuard()) {
        SmallVector<Attribute> branchAttrs;
        for (const auto &branchName : guard.parJoinBranches)
          branchAttrs.push_back(builder.getStringAttr(branchName));
        attrs.push_back(builder.getNamedAttr("par_join_branches",
                                             builder.getArrayAttr(branchAttrs)));
      }
    }
    transAttrs.push_back(builder.getDictionaryAttr(attrs));
  }
  if (!transAttrs.empty())
    task->setAttr("tdcc.transitions", builder.getArrayAttr(transAttrs));

  // Store complex par info if any
  if (!complexParBlocks.empty()) {
    SmallVector<Attribute> complexParAttrs;
    for (const auto &complexPar : complexParBlocks) {
      SmallVector<NamedAttribute> cpAttrs;
      cpAttrs.push_back(builder.getNamedAttr("par_id",
                                             builder.getI64IntegerAttr(complexPar.parId)));
      cpAttrs.push_back(builder.getNamedAttr("fork_state",
                                             builder.getI64IntegerAttr(complexPar.forkState)));
      cpAttrs.push_back(builder.getNamedAttr("join_state",
                                             builder.getI64IntegerAttr(complexPar.joinState)));

      SmallVector<Attribute> branchFsmAttrs;
      for (const auto &branchFsm : complexPar.branchFsms) {
        SmallVector<NamedAttribute> bfAttrs;
        bfAttrs.push_back(builder.getNamedAttr("name",
                                               builder.getStringAttr(branchFsm.name)));
        bfAttrs.push_back(builder.getNamedAttr("num_states",
                                               builder.getI64IntegerAttr(branchFsm.numStates)));
        branchFsmAttrs.push_back(builder.getDictionaryAttr(bfAttrs));
      }
      cpAttrs.push_back(builder.getNamedAttr("branch_fsms",
                                             builder.getArrayAttr(branchFsmAttrs)));
      complexParAttrs.push_back(builder.getDictionaryAttr(cpAttrs));
    }
    task->setAttr("tdcc.complex_pars", builder.getArrayAttr(complexParAttrs));
  }

  LLVM_DEBUG(llvm::dbgs() << "Added TDCC attributes to task @" << task.getSymName() << "\n");
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
          if (!isa<ProcControlEndOp, ProcYieldOp>(*it)) {
            controlExits(&*it, exits, builder);
            break;
          }
        }
      })
      .Case<ProcIfOp>([&](ProcIfOp ifOp) {
        // Both branches contribute exits
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            controlExits(&stmt, exits, builder);
        }
        if (!ifOp.getElseRegion().empty()) {
          for (Operation &stmt : ifOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              controlExits(&stmt, exits, builder);
          }
        }
      })
      .Case<ProcCondIfOp>([&](ProcCondIfOp condIfOp) {
        // Both branches contribute exits
        for (Operation &stmt : condIfOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            controlExits(&stmt, exits, builder);
        }
        if (!condIfOp.getElseRegion().empty()) {
          for (Operation &stmt : condIfOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
          if (!isa<ProcControlEndOp, ProcYieldOp>(*it)) {
            controlExits(&*it, exits, builder);
            break;
          }
        }
      })
      .Case<ProcStaticIfOp>([&](ProcStaticIfOp staticIf) {
        // Static if: both branches contribute exits (similar to dynamic if)
        for (Operation &stmt : staticIf.getThenRegion().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            controlExits(&stmt, exits, builder);
        }
        if (!staticIf.getElseRegion().empty()) {
          for (Operation &stmt : staticIf.getElseRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              controlExits(&stmt, exits, builder);
          }
        }
      })
      .Case<ProcParOp>([&](ProcParOp par) {
        // Parallel: all branches contribute exits
        // Join happens when ALL branches complete
        // Each branch's exit contributes to the overall parallel exit
        for (Operation &stmt : par.getBody().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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

        // Return exit edge from the last state of this step.
        // Both dynamic steps and static steps advance when the corresponding
        // state rule fires; multi-cycle static steps allocate internal
        // transition states.
        uint64_t exitState = curState + latency - 1;
        (void)isStaticStep;
        (void)stepName;
        return SmallVector<PredEdge>{{exitState, GuardSpec::unconditional()}};
      })
      .Case<ProcSeqOp>([&](ProcSeqOp seq) {
        SmallVector<PredEdge> prev = preds;
        for (Operation &stmt : seq.getBody().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              falExits = calculateStatesRecur(schedule, &stmt, falPreds,
                                              builder, module);
          }
        } else {
          // No else branch: false condition skips the if entirely
          // Pass through predecessors with negative guard
          for (auto &p : preds) {
            falExits.push_back({p.state, GuardSpec::negative(ifOp, cond)});
          }
        }

        // Combine exits
        SmallVector<PredEdge> allExits;
        allExits.append(truExits);
        allExits.append(falExits);
        return allExits;
      })
      .Case<ProcCondIfOp>([&](ProcCondIfOp condIfOp) {
        // Same as ProcIfOp but condition comes from condition region
        Value cond = condIfOp.getCond();

        // True branch: predecessors with positive condition guard
        SmallVector<PredEdge> truPreds;
        for (auto &p : preds) {
          truPreds.push_back({p.state, GuardSpec::positive(condIfOp, cond)});
        }

        SmallVector<PredEdge> truExits;
        for (Operation &stmt : condIfOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            truExits = calculateStatesRecur(schedule, &stmt, truPreds, builder,
                                            module);
        }

        // False branch: predecessors with negative condition guard (!cond)
        SmallVector<PredEdge> falExits;
        if (!condIfOp.getElseRegion().empty()) {
          SmallVector<PredEdge> falPreds;
          for (auto &p : preds) {
            falPreds.push_back({p.state, GuardSpec::negative(condIfOp, cond)});
          }
          for (Operation &stmt : condIfOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
              falExits = calculateStatesRecur(schedule, &stmt, falPreds,
                                              builder, module);
          }
        } else {
          // No else branch: false condition skips the if entirely
          // Pass through predecessors with negative guard
          for (auto &p : preds) {
            falExits.push_back({p.state, GuardSpec::negative(condIfOp, cond)});
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
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
            bodyExits = calculateStatesRecur(schedule, &stmt, bodyPreds,
                                             builder, module);
        }

        // Add back-edge: body exit -> header (to re-check condition)
        // Preserve the exit guard (done signal) from the body
        for (auto &exitPred : bodyExits) {
          schedule.addTransition(exitPred.state, headerState, exitPred.guard);
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
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
          if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
            if (!isa<ProcControlEndOp, ProcYieldOp>(stmt))
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
        // Parallel: fork-join pattern with completion tracking
        //
        // Two modes based on branch complexity:
        //
        // 1. Simple par (all branches are single enables):
        //    - All enables share the SAME state (true hardware parallelism)
        //    - Single transition: predecessor -> execState
        //    - All enables fire simultaneously when FSM is in execState
        //    - Exit: combined done guard (all done signals must be true)
        //
        // 2. Complex par (branches have nested control):
        //    - Each branch gets non-overlapping state ranges
        //    - Requires per-branch FSM (current implementation has issues)

        uint64_t forkState = stateIds[par];

        // Check if this is a simple par (all children are enables)
        bool isSimplePar = true;
        SmallVector<ProcEnableOp> enableOps;
        for (Operation &stmt : par.getBody().front()) {
          if (isa<ProcControlEndOp, ProcYieldOp>(stmt))
            continue;
          if (auto enable = dyn_cast<ProcEnableOp>(stmt)) {
            enableOps.push_back(enable);
          } else {
            isSimplePar = false;
            break;
          }
        }

        if (isSimplePar && !enableOps.empty()) {
          // Simple par: all enables share the same state
          // They were already assigned the same state in computeUniqueIdsForOp

          uint64_t execState = stateIds[enableOps[0]];

          // Add transition from predecessor to fork state
          for (auto &pred : preds) {
            schedule.addTransition(pred.state, forkState, pred.guard);
          }

          // Add transition from fork state to exec state (unconditional)
          schedule.addTransition(forkState, execState);

          // Compute max latency of branches (used to allocate internal states).
          int64_t maxLatency = 1;

          for (auto enable : enableOps) {
            StringRef stepName = enable.getStepName();
            int64_t latency = 1;

            auto it = stepMap.find(stepName);
            if (it != stepMap.end()) {
              if (auto staticStep = dyn_cast<ProcStaticStepOp>(it->second)) {
                latency = staticStep.getLatency();
              }
            }

            maxLatency = std::max(maxLatency, latency);

            // Record enable for this state
            schedule.addEnable(execState, nullptr, nullptr, nullptr);
          }

          // Add internal transitions for multi-cycle steps (unconditional)
          for (int64_t i = 0; i < maxLatency - 1; ++i) {
            schedule.addTransition(execState + i, execState + i + 1);
          }

          // Exit state is after all cycles complete
          uint64_t exitState = execState + maxLatency - 1;

          // Exit after max latency cycles. Dynamic steps advance on fire, so
          // no step-local done guard is required.
          GuardSpec exitGuard = GuardSpec::unconditional();

          // Return exit from the last state
          return SmallVector<PredEdge>{{exitState, exitGuard}};
        }

        // Complex par: per-branch FSM approach
        // Each branch has its own FSM register that runs independently.
        // Main FSM only tracks fork/join states.
        //
        // When main FSM enters fork state:
        //   - All branch FSMs are initialized to state 1 (start)
        //   - Main FSM waits at fork state
        // When all branch FSMs return to state 0 (done):
        //   - Main FSM transitions to join state

        // Find the ComplexParInfo for this par block
        ComplexParInfo *complexParPtr = nullptr;
        for (auto &cpInfo : complexParBlocks) {
          if (cpInfo.parOp == par) {
            complexParPtr = &cpInfo;
            break;
          }
        }

        if (!complexParPtr) {
          // Fallback: shouldn't happen if computeUniqueIdsForOp ran correctly
          LLVM_DEBUG(llvm::dbgs() << "  Warning: ComplexParInfo not found for par\n");
          return preds;
        }

        ComplexParInfo &complexPar = *complexParPtr;
        uint64_t joinState = complexPar.joinState;

        // Add transitions from predecessors to fork state
        for (auto &pred : preds) {
          schedule.addTransition(pred.state, forkState, pred.guard);
        }

        // Collect branch FSM done value names for the join guard
        SmallVector<std::string> branchDoneNames;
        for (const auto &branchFsm : complexPar.branchFsms) {
          branchDoneNames.push_back(branchFsm.getDoneValueName());
        }

        // Main FSM transition: fork -> join (when all branches done)
        // The guard is: AND of all branch FSM done signals (branch_fsm == 0)
        GuardSpec joinGuard = GuardSpec::parJoin(branchDoneNames);
        schedule.addTransition(forkState, joinState, joinGuard);

        // Create ParBlockInfo for realization
        ParBlockInfo parInfo;
        parInfo.forkState = forkState;
        parInfo.joinState = joinState;
        parInfo.needsPerBranchFsm = true;

        // Add branch info for each branch
        for (const auto &branchFsm : complexPar.branchFsms) {
          ParBranchInfo branchInfo;
          branchInfo.name = branchFsm.name;
          branchInfo.exitState = 0; // Branch FSM state 0 = done
          branchInfo.exitGuard = GuardSpec::unconditional();
          branchInfo.firstState = 1; // Branch FSM starts at state 1
          branchInfo.lastState = branchFsm.numStates - 1;
          branchInfo.needsSeparateFsm = true;
          parInfo.branches.push_back(branchInfo);
        }

        // Store the parallel block info for realization
        schedule.parBlocks.push_back(parInfo);

        LLVM_DEBUG({
          llvm::dbgs() << "  Complex par " << complexPar.parId
                       << ": fork=" << forkState << " -> join=" << joinState
                       << " (guarded by " << branchDoneNames.size() << " branch done signals)\n";
          for (const auto &branchFsm : complexPar.branchFsms) {
            llvm::dbgs() << "    Branch " << branchFsm.name
                         << ": " << branchFsm.numStates << " states\n";
          }
        });

        // Return exit from join state (unconditional since join is already guarded)
        return SmallVector<PredEdge>{{joinState, GuardSpec::unconditional()}};
      })
      .Default([&](Operation *) { return preds; });
}

//===----------------------------------------------------------------------===//
// Schedule Realization
//===----------------------------------------------------------------------===//

void TDCCPass::realizeSchedule(Schedule &schedule, Operation *procOp,
                               cmt2::ModuleOp module, OpBuilder &builder) {
  (void)module; // Unused for now but may be needed for future extensions

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
  procOp->setAttr("tdcc.num_states", builder.getI64IntegerAttr(numStates));
  procOp->setAttr("tdcc.fsm_width", builder.getI64IntegerAttr(fsmWidth));
  procOp->setAttr("tdcc.done_state", builder.getI64IntegerAttr(schedule.maxState + 1));

  // Store state sequence order for precedence generation
  // Maps state ID to sequence index (higher seq_idx = later in control flow = higher precedence)
  SmallVector<Attribute> stateOrderAttrs;
  for (auto &[state, seqIdx] : stateSequenceIndex) {
    auto entry = builder.getDictionaryAttr({
      builder.getNamedAttr("state", builder.getI64IntegerAttr(state)),
      builder.getNamedAttr("seq_idx", builder.getI64IntegerAttr(seqIdx))
    });
    stateOrderAttrs.push_back(entry);
  }
  if (!stateOrderAttrs.empty()) {
    procOp->setAttr("tdcc.state_order", builder.getArrayAttr(stateOrderAttrs));
    LLVM_DEBUG(llvm::dbgs() << "  State sequence order: " << stateOrderAttrs.size()
                            << " entries\n");
  }

  // Store state assignments for each enable with timing information
  SmallVector<Attribute> stateAssigns;
  // First add non-iteration state assignments
  for (auto &[enableOp, state] : stateIds) {
    if (auto enable = dyn_cast<ProcEnableOp>(enableOp)) {
      // Read timing attributes we set earlier
      int64_t startState = state;
      int64_t endState = state + 1;
      int64_t latency = 1;
      bool isStatic = false;

      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.end_state"))
        endState = attr.getInt();
      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.latency"))
        latency = attr.getInt();
      if (auto attr = enable->getAttrOfType<BoolAttr>("tdcc.is_static"))
        isStatic = attr.getValue();

      auto entry = builder.getDictionaryAttr({
        builder.getNamedAttr("step", enable.getStepNameAttr()),
        builder.getNamedAttr("state", builder.getI64IntegerAttr(state)),
        builder.getNamedAttr("end_state", builder.getI64IntegerAttr(endState)),
        builder.getNamedAttr("latency", builder.getI64IntegerAttr(latency)),
        builder.getNamedAttr("is_static", builder.getBoolAttr(isStatic))
      });
      stateAssigns.push_back(entry);
    }
  }
  // Then add iteration-aware state assignments from static_repeat
  for (auto &[key, state] : iterStateIds) {
    auto [enableOp, iteration] = key;
    if (auto enable = dyn_cast<ProcEnableOp>(enableOp)) {
      // Read timing attributes we set earlier
      int64_t startState = state;
      int64_t endState = state + 1;
      int64_t latency = 1;
      bool isStatic = false;

      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.end_state"))
        endState = attr.getInt();
      if (auto attr = enable->getAttrOfType<IntegerAttr>("tdcc.latency"))
        latency = attr.getInt();
      if (auto attr = enable->getAttrOfType<BoolAttr>("tdcc.is_static"))
        isStatic = attr.getValue();

      auto entry = builder.getDictionaryAttr({
        builder.getNamedAttr("step", enable.getStepNameAttr()),
        builder.getNamedAttr("state", builder.getI64IntegerAttr(state)),
        builder.getNamedAttr("end_state", builder.getI64IntegerAttr(endState)),
        builder.getNamedAttr("latency", builder.getI64IntegerAttr(latency)),
        builder.getNamedAttr("is_static", builder.getBoolAttr(isStatic)),
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
    } else if (isa<ProcCondIfOp>(condOp)) {
      attrs.push_back(builder.getNamedAttr("type", builder.getStringAttr("cond_if")));
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
      // Store parallel join guard (AND of all branch completions)
      if (guard.hasParJoinGuard()) {
        SmallVector<Attribute> branchAttrs;
        for (const auto &branchName : guard.parJoinBranches) {
          branchAttrs.push_back(builder.getStringAttr(branchName));
        }
        attrs.push_back(builder.getNamedAttr("par_join_branches",
                                             builder.getArrayAttr(branchAttrs)));
      }
    }

    transAttrs.push_back(builder.getDictionaryAttr(attrs));
  }
  procOp->setAttr("tdcc.transitions", builder.getArrayAttr(transAttrs));

  // Store parallel block info for realization
  if (!schedule.parBlocks.empty()) {
    SmallVector<Attribute> parBlockAttrs;
    for (const auto &parBlock : schedule.parBlocks) {
      SmallVector<NamedAttribute> blockAttrs;
      blockAttrs.push_back(builder.getNamedAttr("fork_state",
                                                builder.getI64IntegerAttr(parBlock.forkState)));
      blockAttrs.push_back(builder.getNamedAttr("join_state",
                                                builder.getI64IntegerAttr(parBlock.joinState)));
      blockAttrs.push_back(builder.getNamedAttr("needs_per_branch_fsm",
                                                builder.getBoolAttr(parBlock.needsPerBranchFsm)));

      SmallVector<Attribute> branchAttrs;
      for (const auto &branch : parBlock.branches) {
        SmallVector<NamedAttribute> brAttrs;
        brAttrs.push_back(builder.getNamedAttr("name", builder.getStringAttr(branch.name)));
        brAttrs.push_back(builder.getNamedAttr("exit_state",
                                               builder.getI64IntegerAttr(branch.exitState)));
        brAttrs.push_back(builder.getNamedAttr("first_state",
                                               builder.getI64IntegerAttr(branch.firstState)));
        brAttrs.push_back(builder.getNamedAttr("last_state",
                                               builder.getI64IntegerAttr(branch.lastState)));
        brAttrs.push_back(builder.getNamedAttr("needs_separate_fsm",
                                               builder.getBoolAttr(branch.needsSeparateFsm)));
        branchAttrs.push_back(builder.getDictionaryAttr(brAttrs));
      }
      blockAttrs.push_back(builder.getNamedAttr("branches", builder.getArrayAttr(branchAttrs)));

      parBlockAttrs.push_back(builder.getDictionaryAttr(blockAttrs));
    }
    procOp->setAttr("tdcc.par_blocks", builder.getArrayAttr(parBlockAttrs));
  }

  // Store detailed branch FSM info for complex par blocks
  if (!complexParBlocks.empty()) {
    SmallVector<Attribute> complexParAttrs;
    for (const auto &complexPar : complexParBlocks) {
      SmallVector<NamedAttribute> cpAttrs;
      cpAttrs.push_back(builder.getNamedAttr("par_id",
                                             builder.getI64IntegerAttr(complexPar.parId)));
      cpAttrs.push_back(builder.getNamedAttr("fork_state",
                                             builder.getI64IntegerAttr(complexPar.forkState)));
      cpAttrs.push_back(builder.getNamedAttr("join_state",
                                             builder.getI64IntegerAttr(complexPar.joinState)));

      // Serialize each branch FSM
      SmallVector<Attribute> branchFsmAttrs;
      for (const auto &branchFsm : complexPar.branchFsms) {
        SmallVector<NamedAttribute> bfAttrs;
        bfAttrs.push_back(builder.getNamedAttr("name",
                                               builder.getStringAttr(branchFsm.name)));
        bfAttrs.push_back(builder.getNamedAttr("branch_idx",
                                               builder.getI64IntegerAttr(branchFsm.branchIdx)));
        bfAttrs.push_back(builder.getNamedAttr("num_states",
                                               builder.getI64IntegerAttr(branchFsm.numStates)));

        // Serialize transitions
        // Build map from while ops to their index for guard serialization
        DenseMap<Operation *, int64_t> whileOpToIdx;
        int64_t whileIdx = 0;
        std::function<void(Region *)> indexWhileOps = [&](Region *region) {
          if (!region || region->empty())
            return;
          for (auto &op : region->front()) {
            if (isa<ProcWhileOp>(op)) {
              whileOpToIdx[&op] = whileIdx++;
            }
            for (auto &nestedRegion : op.getRegions()) {
              indexWhileOps(&nestedRegion);
            }
          }
        };
        // Get control region based on proc op type
        Region *controlRegion = nullptr;
        if (auto rule = dyn_cast<ProcRuleOp>(procOp))
          controlRegion = &rule.getControl();
        else if (auto method = dyn_cast<ProcMethodOp>(procOp))
          controlRegion = &method.getControl();
        if (controlRegion)
          indexWhileOps(controlRegion);

        SmallVector<Attribute> transAttrs;
        for (const auto &trans : branchFsm.transitions) {
          SmallVector<NamedAttribute> tAttrs;
          tAttrs.push_back(builder.getNamedAttr("from",
                                                builder.getI64IntegerAttr(trans.fromState)));
          tAttrs.push_back(builder.getNamedAttr("to",
                                                builder.getI64IntegerAttr(trans.toState)));

          // Serialize guard information for conditional transitions (while loops)
          if (trans.guard.sourceOp && isa<ProcWhileOp>(trans.guard.sourceOp)) {
            // Find the while op index
            auto whileIt = whileOpToIdx.find(trans.guard.sourceOp);
            if (whileIt != whileOpToIdx.end()) {
              tAttrs.push_back(builder.getNamedAttr("guard_while_idx",
                  builder.getI64IntegerAttr(whileIt->second)));
              tAttrs.push_back(builder.getNamedAttr("guard_inverted",
                  builder.getBoolAttr(trans.guard.inverted)));
            }
          }

          transAttrs.push_back(builder.getDictionaryAttr(tAttrs));
        }
        bfAttrs.push_back(builder.getNamedAttr("transitions",
                                               builder.getArrayAttr(transAttrs)));

        // Serialize enables
        SmallVector<Attribute> enableAttrs;
        for (const auto &enable : branchFsm.enables) {
          SmallVector<NamedAttribute> eAttrs;
          eAttrs.push_back(builder.getNamedAttr("state",
                                                builder.getI64IntegerAttr(enable.state)));
          eAttrs.push_back(builder.getNamedAttr("step",
                                                builder.getStringAttr(enable.stepName)));
          eAttrs.push_back(builder.getNamedAttr("is_static",
                                                builder.getBoolAttr(enable.isStatic)));
          eAttrs.push_back(builder.getNamedAttr("latency",
                                                builder.getI64IntegerAttr(enable.latency)));
          enableAttrs.push_back(builder.getDictionaryAttr(eAttrs));
        }
        bfAttrs.push_back(builder.getNamedAttr("enables",
                                               builder.getArrayAttr(enableAttrs)));

        branchFsmAttrs.push_back(builder.getDictionaryAttr(bfAttrs));
      }
      cpAttrs.push_back(builder.getNamedAttr("branch_fsms",
                                             builder.getArrayAttr(branchFsmAttrs)));

      complexParAttrs.push_back(builder.getDictionaryAttr(cpAttrs));
    }
    procOp->setAttr("tdcc.complex_pars", builder.getArrayAttr(complexParAttrs));
  }

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
  stateSequenceIndex.clear();
  nextSequenceIndex = 0;
  currentIteration = -1;
  complexParBlocks.clear();
  nextParId = 0;

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
    if (!isa<ProcControlEndOp, ProcYieldOp>(op))
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

  // Process DataflowTaskOp bodies with proc control (C1 - Dataflow-Proc Compatibility)
  SmallVector<DataflowTaskOp> dataflowTasks;
  module.walk([&](DataflowTaskOp task) {
    dataflowTasks.push_back(task);
  });

  for (auto task : dataflowTasks) {
    processDataflowTask(task, module);
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
