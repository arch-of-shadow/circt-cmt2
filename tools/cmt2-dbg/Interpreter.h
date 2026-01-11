//===- Interpreter.h - CMT2 GAA Interpreter ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CMT2 GAA interpreter for debugging and simulation.
//
// The interpreter implements One-Rule-At-A-Time (ORAAT) semantics:
// 1. Evaluate all rule guards
// 2. Resolve conflicts using scheduler annotations
// 3. Execute selected rules atomically
// 4. Update state
//
//===----------------------------------------------------------------------===//

#ifndef CMT2_DBG_INTERPRETER_H
#define CMT2_DBG_INTERPRETER_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Interpreter/ControlFlowPlugin.h"
#include "circt/Dialect/Cmt2/Interpreter/ModuleInterpreterRegistry.h"
#include "circt/Dialect/Cmt2/Interpreter/OpHandlerRegistry.h"
#include "circt/Dialect/Cmt2/Interpreter/Scheduler.h"
#include "circt/Dialect/Cmt2/Interpreter/StateManager.h"
#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace circt {
namespace cmt2 {

/// Value storage for the interpreter
using InterpValue = llvm::APInt;

/// Represents the state of a register/memory element
struct RegisterState {
  std::string name;
  InterpValue value;
  unsigned width;
  bool hasReset;
  InterpValue resetValue;
};

/// Represents pending method calls
struct PendingCall {
  std::string instanceName;
  std::string methodName;
  std::vector<InterpValue> args;
  bool executed = false;
};

/// Transition info parsed from TDCC attributes
struct TDCCTransition {
  uint64_t fromState;
  uint64_t toState;
  int64_t guardOpId = -1;      // -1 = unconditional
  bool guardInverted = false;  // true for else/exit branches
  std::string doneStep;        // non-empty = guarded by step done signal
  std::vector<std::string> parJoinBranches;  // non-empty = parallel join
};

/// Step enable info parsed from TDCC attributes
struct TDCCStepEnable {
  uint64_t state;
  std::string stepName;
  int64_t iteration = -1;  // -1 = not in static_repeat
};

/// Parallel branch info parsed from TDCC attributes
struct TDCCParBranch {
  std::string name;
  uint64_t firstState;
  uint64_t lastState;
  uint64_t exitState;
  bool needsSeparateFsm;
};

/// Parallel block info
struct TDCCParBlock {
  uint64_t forkState;
  uint64_t joinState;
  bool needsPerBranchFsm;
  std::vector<TDCCParBranch> branches;
};

/// Condition operation info (for while loops)
struct TDCCCondOp {
  int64_t id;
  std::string type;  // "while"
};

/// FSM state for procedural rules - enhanced with TDCC support
/// DEPRECATED: Will be replaced by ProcRuleExecState for direct interpretation
struct ProcFSMState {
  std::string ruleName;
  unsigned currentState = 0;  // 0 = idle
  unsigned numStates = 1;
  unsigned doneState = 0;
  bool isRunning = false;

  // Legacy simple FSM (for backward compatibility)
  std::vector<std::string> stateSteps; // step name for each state

  // TDCC FSM structure
  bool hasTDCC = false;
  std::vector<TDCCTransition> transitions;
  std::vector<TDCCStepEnable> enables;
  std::vector<TDCCParBlock> parBlocks;
  std::vector<TDCCCondOp> condOps;

  // Per-branch FSM state for proc.par (branch_name -> current_state)
  // State encoding: 0=idle, 1=header, 2=body, 3=done
  llvm::StringMap<unsigned> branchFSMStates;

  // Map from state to step name (built from enables)
  llvm::DenseMap<uint64_t, std::string> stateToStep;

  // Map from fromState to indices in transitions vector
  llvm::DenseMap<uint64_t, llvm::SmallVector<size_t, 4>> stateTransitions;
};

//===----------------------------------------------------------------------===//
// Direct Proc Interpretation (New Architecture)
//===----------------------------------------------------------------------===//
// These structures support direct interpretation of proc operations without
// requiring TDCC lowering. The interpreter maintains hierarchical execution
// state that mirrors the control structure.

/// Execution status for a control construct
enum class ProcExecStatus { Idle, Running, Done };

/// Forward declaration for recursive state
struct ProcExecState;

/// Execution state for proc.seq (sequential composition)
struct SeqExecState {
  size_t currentChildIndex = 0;  // Which child is currently executing
};

/// Execution state for proc.par (parallel composition)
struct ParExecState {
  std::vector<bool> childDone;   // Which children have completed
  bool allStarted = false;       // Have all children been started
};

/// Execution state for proc.while (dynamic loop)
struct WhileExecState {
  bool evaluatingCond = true;    // true = evaluating condition, false = body active
};

/// Execution state for proc.static_repeat (static loop)
struct StaticRepeatExecState {
  size_t currentIteration = 0;   // Current iteration number (0-based)
  size_t totalIterations = 0;    // Total number of iterations
};

/// Execution state for proc.enable (step activation)
struct EnableExecState {
  std::string stepName;          // Name of the step being executed
  bool isStatic = false;         // true = proc.static_step, false = proc.step
  size_t cycleCount = 0;         // Cycles since activation (for static steps)
  size_t staticLatency = 0;      // Latency for static steps
  bool activated = false;        // Has the step been activated this cycle
};

/// Execution state for a control construct (polymorphic via variant)
struct ProcExecState {
  ProcExecStatus status = ProcExecStatus::Idle;

  /// The MLIR operation this state corresponds to
  mlir::Operation *op = nullptr;

  /// State specific to the control construct type
  std::variant<
    std::monostate,        // Default/empty
    SeqExecState,          // proc.seq
    ParExecState,          // proc.par
    WhileExecState,        // proc.while
    StaticRepeatExecState, // proc.static_repeat
    EnableExecState        // proc.enable
  > state;

  /// Child states (for nested control structures)
  /// - proc.seq: single active child
  /// - proc.par: one per parallel branch
  /// - proc.while: single body child
  /// - proc.static_repeat: single body child
  std::vector<std::unique_ptr<ProcExecState>> children;

  /// Create state for a specific control construct
  static std::unique_ptr<ProcExecState> createFor(mlir::Operation *op);

  /// Check if this state is for a specific operation type
  template <typename OpT>
  bool isFor() const { return mlir::isa<OpT>(op); }

  /// Get typed state (returns nullptr if wrong type)
  template <typename StateT>
  StateT *getState() { return std::get_if<StateT>(&state); }

  template <typename StateT>
  const StateT *getState() const { return std::get_if<StateT>(&state); }
};

/// Root execution state for a proc.rule (direct interpretation)
struct ProcRuleExecState {
  std::string ruleName;
  bool isRunning = false;

  /// State for the control region (nullptr when idle)
  std::unique_ptr<ProcExecState> controlState;

  /// Reference to the proc.rule operation
  ProcRuleOp ruleOp;
};

/// Breakpoint types
enum class BreakpointType {
  RuleFire,     // Break when a specific rule fires
  RuleGuard,    // Break when evaluating a specific rule's guard
  RegisterWrite, // Break when a register is written
  Cycle         // Break at a specific cycle
};

/// Breakpoint definition
struct Breakpoint {
  BreakpointType type;
  std::string target;  // Rule name, register name, etc.
  uint64_t cycleTarget = 0; // For cycle breakpoints
  bool enabled = true;
  unsigned id;
};

/// Rule execution result
struct RuleResult {
  std::string ruleName;
  bool guardEnabled;
  bool fired;
  std::vector<std::string> methodsCalled;
};

/// Single cycle trace entry
struct CycleTrace {
  uint64_t cycle;
  std::vector<RuleResult> ruleResults;
  llvm::StringMap<InterpValue> stateChanges;
};

/// The CMT2 GAA Interpreter
///
/// Implements cycle-accurate simulation with GAA semantics:
/// - One-Rule-At-A-Time (ORAAT) execution
/// - Conflict resolution via scheduler annotations
/// - Breakpoint support
/// - State inspection
///
class Cmt2Interpreter {
public:
  explicit Cmt2Interpreter(mlir::ModuleOp module, llvm::raw_ostream &os);
  ~Cmt2Interpreter();

  //===--------------------------------------------------------------------===//
  // Initialization and Reset
  //===--------------------------------------------------------------------===//

  /// Initialize the interpreter for a specific circuit
  mlir::LogicalResult initialize(llvm::StringRef circuitName);

  /// Reset the circuit to initial state
  void reset();

  /// Get the current cycle count
  uint64_t getCycle() const { return cycle_; }

  //===--------------------------------------------------------------------===//
  // Execution Control
  //===--------------------------------------------------------------------===//

  /// Execute a single cycle (ORAAT semantics)
  /// Returns the rules that fired this cycle
  std::vector<RuleResult> step();

  /// Run until a breakpoint is hit or maxCycles is reached
  /// Returns the breakpoint that was hit, or std::nullopt if maxCycles reached
  std::optional<Breakpoint> run(uint64_t maxCycles = 1000000);

  /// Continue execution after breakpoint
  std::optional<Breakpoint> continueExec(uint64_t maxCycles = 1000000);

  //===--------------------------------------------------------------------===//
  // Rule Evaluation
  //===--------------------------------------------------------------------===//

  /// Evaluate all rule guards, return names of enabled rules
  std::vector<std::string> evaluateGuards();

  /// Check if a specific rule's guard is enabled
  bool isRuleEnabled(llvm::StringRef ruleName);

  /// Manually fire a specific rule (for debugging)
  mlir::LogicalResult fireRule(llvm::StringRef ruleName);

  //===--------------------------------------------------------------------===//
  // Conflict Resolution
  //===--------------------------------------------------------------------===//

  /// Resolve conflicts among enabled rules
  /// Returns the rules that should fire this cycle
  std::vector<std::string> resolveConflicts(
      const std::vector<std::string> &enabledRules);

  //===--------------------------------------------------------------------===//
  // State Inspection
  //===--------------------------------------------------------------------===//

  /// Read a register value
  std::optional<InterpValue> readRegister(llvm::StringRef name);

  /// Write a register value (for debugging)
  mlir::LogicalResult writeRegister(llvm::StringRef name,
                                     const InterpValue &value);

  /// Get all register names
  std::vector<std::string> getRegisterNames() const;

  /// Get all rule names
  std::vector<std::string> getRuleNames() const;

  /// Get all method names
  std::vector<std::string> getMethodNames() const;

  /// Get all value names
  std::vector<std::string> getValueNames() const;

  /// Read a value method's current output
  std::optional<InterpValue> readValue(llvm::StringRef name);

  /// Call a method with arguments
  std::optional<std::vector<InterpValue>> callMethod(
      llvm::StringRef instanceName, llvm::StringRef methodName,
      const std::vector<InterpValue> &args);

  //===--------------------------------------------------------------------===//
  // Breakpoints
  //===--------------------------------------------------------------------===//

  /// Add a breakpoint on rule fire
  unsigned addBreakpointOnRuleFire(llvm::StringRef ruleName);

  /// Add a breakpoint on register write
  unsigned addBreakpointOnRegisterWrite(llvm::StringRef registerName);

  /// Add a breakpoint at a specific cycle
  unsigned addBreakpointAtCycle(uint64_t cycle);

  /// Remove a breakpoint by ID
  bool removeBreakpoint(unsigned id);

  /// Enable/disable a breakpoint
  void setBreakpointEnabled(unsigned id, bool enabled);

  /// List all breakpoints
  const std::vector<Breakpoint> &getBreakpoints() const { return breakpoints_; }

  /// Clear all breakpoints
  void clearBreakpoints();

  //===--------------------------------------------------------------------===//
  // Tracing
  //===--------------------------------------------------------------------===//

  /// Enable/disable tracing
  void setTracing(bool enabled) { tracingEnabled_ = enabled; }

  /// Check if tracing is enabled
  bool isTracingEnabled() const { return tracingEnabled_; }

  /// Get trace for a specific cycle
  std::optional<CycleTrace> getTrace(uint64_t cycle) const;

  /// Get all traces
  const std::vector<CycleTrace> &getTraces() const { return traces_; }

  /// Clear trace history
  void clearTraces() { traces_.clear(); }

  /// Set maximum trace entries (0 = unlimited)
  void setMaxTraceEntries(unsigned max) { maxTraceEntries_ = max; }

  //===--------------------------------------------------------------------===//
  // Plugin-Based Execution (Experimental)
  //===--------------------------------------------------------------------===//

  /// Enable/disable plugin-based execution path.
  /// When enabled, uses ControlFlowPlugins for proc control flow.
  void setPluginExecution(bool enabled) { usePluginExecution_ = enabled; }

  /// Check if plugin-based execution is enabled.
  bool isPluginExecutionEnabled() const { return usePluginExecution_; }

  /// Get the state manager (for external access/debugging).
  interp::StateManager &getStateManager() { return stateManager_; }

  /// Get the static control plugin (for timing validation access).
  interp::StaticControlPlugin *getStaticControlPlugin() {
    return staticControlPlugin_.get();
  }

  /// Set the scheduler to use for conflict resolution.
  /// Takes ownership of the scheduler. If null, uses default ORAATScheduler.
  void setScheduler(std::unique_ptr<interp::Scheduler> scheduler);

  /// Get the current scheduler.
  interp::Scheduler *getScheduler() { return scheduler_.get(); }

  /// Get scheduler name.
  llvm::StringRef getSchedulerName() const {
    return scheduler_ ? scheduler_->getName() : "ORAAT";
  }

  //===--------------------------------------------------------------------===//
  // Direct Proc Interpretation (Experimental)
  //===--------------------------------------------------------------------===//

  /// Enable/disable direct proc interpretation.
  /// When enabled, proc.rule execution uses hierarchical state tracking
  /// instead of TDCC-based FSM simulation.
  void setDirectProcInterpretation(bool enabled) {
    useDirectProcInterpretation_ = enabled;
  }

  /// Check if direct proc interpretation is enabled.
  bool isDirectProcInterpretationEnabled() const {
    return useDirectProcInterpretation_;
  }

  //===--------------------------------------------------------------------===//
  // Output
  //===--------------------------------------------------------------------===//

  /// Print current state
  void printState();

  /// Print rule status (guards enabled/disabled)
  void printRuleStatus();

  /// Print a trace entry
  void printTrace(const CycleTrace &trace);

private:
  //===--------------------------------------------------------------------===//
  // Internal Execution Helpers
  //===--------------------------------------------------------------------===//

  /// Find the top-level module
  cmt2::ModuleOp findTopModule();

  /// Initialize register state from module
  void initializeRegisters(cmt2::ModuleOp module);

  /// Evaluate a guard region
  bool evaluateGuard(mlir::Region &guardRegion);

  /// Evaluate method guards from calls in a body region
  /// Returns true if all method guards pass, false if any fails
  bool evaluateMethodGuards(mlir::Region &bodyRegion);

  /// Execute a rule body
  void executeBody(mlir::Region &bodyRegion);

  /// Execute a single operation
  std::optional<InterpValue> executeOp(mlir::Operation *op);

  /// Initialize procedural constructs (proc.rule, proc.step, etc.)
  void initializeProcConstructs(cmt2::ModuleOp module);

  /// Execute a proc.step body
  bool executeProcStep(llvm::StringRef stepName);

  /// Execute proc control flow region
  bool executeProcControl(mlir::Region &controlRegion, ProcFSMState &fsm);

  /// Check if a proc.rule guard is enabled (including FSM idle check)
  bool isProcRuleEnabled(ProcRuleOp procRule);

  /// Execute proc.rule state transition
  void executeProcRuleStep(ProcRuleOp procRule, ProcFSMState &fsm);

  /// Execute proc.rule with TDCC FSM semantics
  void executeProcRuleTDCC(ProcRuleOp procRule, ProcFSMState &fsm);

  /// Parse TDCC attributes from proc.rule
  void parseTDCCAttributes(ProcRuleOp procRule, ProcFSMState &fsm);

  /// Evaluate while condition for TDCC
  bool evaluateTDCCWhileCondition(ProcRuleOp procRule, int64_t condOpId);

  /// Check if all parallel branches are done (for join state)
  bool areAllBranchesDone(const ProcFSMState &fsm, const TDCCParBlock &parBlock);

  /// Get next state based on current state and conditions
  uint64_t getNextTDCCState(ProcRuleOp procRule, ProcFSMState &fsm);

  //===--------------------------------------------------------------------===//
  // Direct Proc Interpretation (New Architecture)
  //===--------------------------------------------------------------------===//

  /// Execute proc.rule using direct interpretation (not TDCC-based)
  void executeProcRuleDirect(ProcRuleOp procRule, ProcRuleExecState &execState);

  /// Advance execution state for a control construct by one cycle
  void advanceState(ProcExecState &state);

  /// Advance proc.seq - sequential composition
  void advanceSeq(ProcSeqOp seqOp, ProcExecState &state);

  /// Advance proc.par - parallel composition (fork-join)
  void advancePar(ProcParOp parOp, ProcExecState &state);

  /// Advance proc.while - dynamic loop with condition
  void advanceWhile(ProcWhileOp whileOp, ProcExecState &state);

  /// Advance proc.static_repeat - static loop with fixed iterations
  void advanceStaticRepeat(ProcStaticRepeatOp repeatOp, ProcExecState &state);

  /// Advance proc.enable - step activation
  void advanceEnable(ProcEnableOp enableOp, ProcExecState &state);

  /// Advance proc.if - conditional control (if supported)
  void advanceIf(ProcIfOp ifOp, ProcExecState &state);

  /// Check if a step is done (for dynamic steps)
  bool isStepDone(llvm::StringRef stepName);

  /// Activate a step (set go signal, execute body)
  void activateStep(llvm::StringRef stepName);

  /// Get value from SSA value
  InterpValue getValue(mlir::Value value);

  /// Set value for SSA value
  void setValue(mlir::Value value, const InterpValue &v);

  /// Apply pending state updates
  void applyStateUpdates();

  /// Check if any breakpoint is hit
  std::optional<Breakpoint> checkBreakpoints(
      const std::vector<RuleResult> &results);

  /// Add trace entry for current cycle
  void addTraceEntry(const std::vector<RuleResult> &results);

  //===--------------------------------------------------------------------===//
  // Data Members
  //===--------------------------------------------------------------------===//

  /// MLIR module containing the circuit
  mlir::ModuleOp module_;

  /// Output stream for messages
  llvm::raw_ostream &os_;

  /// Current circuit being simulated
  cmt2::CircuitOp circuit_;

  /// Top-level module
  cmt2::ModuleOp topModule_;

  /// Current cycle count
  uint64_t cycle_ = 0;

  /// Module interpreter registry for external modules (Reg, FIFO, Memory, etc.)
  interp::ModuleInterpreterRegistry moduleRegistry_;

  /// Register states (keyed by hierarchical name) - legacy, migrating to moduleRegistry_
  llvm::StringMap<RegisterState> registers_;

  /// Pending register writes (for atomic update) - legacy, migrating to moduleRegistry_
  llvm::StringMap<InterpValue> pendingWrites_;

  /// SSA value storage during execution
  llvm::DenseMap<mlir::Value, InterpValue> valueMap_;

  /// Breakpoints
  std::vector<Breakpoint> breakpoints_;
  unsigned nextBreakpointId_ = 1;

  /// Tracing state
  bool tracingEnabled_ = false;
  std::vector<CycleTrace> traces_;
  unsigned maxTraceEntries_ = 0;

  /// Scheduler info cache
  llvm::DenseMap<mlir::Operation *, unsigned> rulePriorities_;

  /// Procedural FSM states (keyed by proc.rule name)
  /// DEPRECATED: Will be replaced by procExecStates_ for direct interpretation
  llvm::StringMap<ProcFSMState> procFSMStates_;

  /// Procedural execution states for direct interpretation (keyed by proc.rule name)
  /// New architecture: hierarchical state that mirrors control structure
  llvm::StringMap<ProcRuleExecState> procExecStates_;

  /// Flag to enable direct proc interpretation (vs legacy TDCC-based)
  bool useDirectProcInterpretation_ = false;

  /// Proc step definitions (keyed by step name)
  llvm::StringMap<ProcStepOp> procSteps_;

  /// Proc static step definitions (keyed by step name)
  llvm::StringMap<ProcStaticStepOp> procStaticSteps_;

  /// Step done signals for current cycle
  llvm::DenseSet<llvm::StringRef> stepsDoneThisCycle_;

  /// CMT2 module instances (keyed by hierarchical instance name -> referenced module)
  llvm::StringMap<cmt2::ModuleOp> cmt2ModuleInstances_;

  /// Per-instance schedulers for nested CMT2 modules
  /// Each nested module instance gets its own ORAATScheduler initialized with its module
  llvm::StringMap<std::unique_ptr<interp::ORAATScheduler>> instanceSchedulers_;

  /// Current instance path for hierarchical name resolution during nested execution
  std::string currentInstancePath_;

  /// Methods called during current cycle (for conflict detection in nested modules)
  /// Key: instance path (e.g., "fifo"), Value: set of method names called
  llvm::StringMap<llvm::StringSet<>> methodsCalledThisCycle_;

  //===--------------------------------------------------------------------===//
  // Modular Infrastructure (Phase 3 Integration)
  //===--------------------------------------------------------------------===//

  /// Centralized state manager (new modular infrastructure)
  interp::StateManager stateManager_;

  /// Operation handler registry
  interp::OpHandlerRegistry opRegistry_;

  /// Control flow plugins
  std::unique_ptr<interp::DynamicControlPlugin> dynamicControlPlugin_;
  std::unique_ptr<interp::StaticControlPlugin> staticControlPlugin_;

  /// Scheduler plugin (default: ORAATScheduler)
  std::unique_ptr<interp::Scheduler> scheduler_;

  /// Conflict matrix analysis for all modules
  std::unique_ptr<ConflictMatrixAnalysis> conflictMatrixAnalysis_;

  /// Flag to enable new plugin-based execution path
  bool usePluginExecution_ = false;
};

} // namespace cmt2
} // namespace circt

#endif // CMT2_DBG_INTERPRETER_H
