//===- ControlFlowPlugin.h - Control Flow Plugin Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ControlFlowPlugin interface for modular control flow
// interpretation with static timing support.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_INTERPRETER_CONTROLFLOWPLUGIN_H
#define CIRCT_DIALECT_CMT2_INTERPRETER_CONTROLFLOWPLUGIN_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Interpreter/OpHandlerRegistry.h"
#include "circt/Dialect/Cmt2/Interpreter/StateManager.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace circt {
namespace cmt2 {
namespace interp {

//===----------------------------------------------------------------------===//
// ControlFlowPlugin - Abstract interface for control flow plugins
//===----------------------------------------------------------------------===//

/// Abstract interface for control flow plugins.
///
/// Control flow plugins provide modular handling of different control flow
/// paradigms in CMT2:
/// - DynamicControlPlugin: Dynamic control (proc.step, proc.while, proc.if)
/// - StaticControlPlugin: Static control with timing (proc.static_step, etc.)
///
/// Plugins are initialized with a module and can handle specific operations
/// during execution.
///
class ControlFlowPlugin {
public:
  virtual ~ControlFlowPlugin() = default;

  /// Initialize plugin with module and state manager.
  /// Called once during interpreter initialization.
  virtual void initialize(cmt2::ModuleOp module, StateManager &state) = 0;

  /// Check if plugin handles this operation.
  virtual bool handles(mlir::Operation *op) const = 0;

  /// Execute control flow operation.
  /// @param op The operation to execute
  /// @param ctx Execution context with state and value map
  /// @return true if the operation is done (can proceed), false if waiting
  virtual bool execute(mlir::Operation *op, OpContext &ctx) = 0;

  /// Called at the start of each cycle.
  /// Allows plugins to advance internal state (timers, FSMs, etc.).
  virtual void tick() {}

  /// Called at the end of each cycle.
  /// Allows plugins to commit pending state changes.
  virtual void commit() {}

  /// Reset plugin state.
  virtual void reset() {}

  /// Get plugin name (for debugging/logging).
  virtual llvm::StringRef getName() const = 0;
};

//===----------------------------------------------------------------------===//
// DynamicControlPlugin - Plugin for dynamic control flow
//===----------------------------------------------------------------------===//

/// Plugin for dynamic control (proc.step, proc.while, proc.if).
///
/// Handles:
/// - ProcStepOp: Execute step body, check done condition
/// - ProcEnableOp: Enable a step for execution
/// - ProcStepDoneOp: Signal step completion
///
class DynamicControlPlugin : public ControlFlowPlugin {
public:
  void initialize(cmt2::ModuleOp module, StateManager &state) override;
  bool handles(mlir::Operation *op) const override;
  bool execute(mlir::Operation *op, OpContext &ctx) override;
  llvm::StringRef getName() const override { return "DynamicControl"; }

  void tick() override;
  void commit() override;
  void reset() override;

  //===--------------------------------------------------------------------===//
  // Step Management
  //===--------------------------------------------------------------------===//

  /// Check if a step is done (completed this cycle).
  bool isStepDone(llvm::StringRef stepName) const;

  /// Mark a step as done for this cycle.
  void markStepDone(llvm::StringRef stepName);

  /// Clear step done status (called at cycle start).
  void clearStepsDone();

private:
  /// Execute a proc.step body.
  bool executeStep(ProcStepOp step, OpContext &ctx);

  /// State manager reference.
  StateManager *state_ = nullptr;

  /// Step definitions keyed by name.
  llvm::StringMap<ProcStepOp> steps_;

  /// Steps marked as done this cycle.
  llvm::StringSet<> stepsDone_;
};

//===----------------------------------------------------------------------===//
// StaticControlPlugin - Plugin for static control flow with timing
//===----------------------------------------------------------------------===//

/// Plugin for static control (proc.static_step, proc.static_if, proc.static_repeat).
///
/// This plugin provides cycle-accurate timing for static control constructs:
/// - Tracks cycle counters for each static step
/// - Validates timing attributes at runtime
/// - Supports pipelined initiation (II tracking)
///
/// Key features:
/// - Latency tracking: Each static step takes exactly N cycles
/// - Timing validation: Validates arg_timing and result_timing attributes
/// - Pipeline support: Tracks initiation intervals for pipelined operations
///
class StaticControlPlugin : public ControlFlowPlugin {
public:
  void initialize(cmt2::ModuleOp module, StateManager &state) override;
  bool handles(mlir::Operation *op) const override;
  bool execute(mlir::Operation *op, OpContext &ctx) override;
  llvm::StringRef getName() const override { return "StaticControl"; }

  void tick() override;
  void commit() override;
  void reset() override;

  //===--------------------------------------------------------------------===//
  // Static Step Execution
  //===--------------------------------------------------------------------===//

  /// Execute a static step, tracking cycle-by-cycle progress.
  /// @return true if step is done (all latency cycles completed)
  bool executeStaticStep(ProcStaticStepOp step, OpContext &ctx);

  /// Execute static if, selecting branch based on condition.
  /// @return true if selected branch is done
  bool executeStaticIf(ProcStaticIfOp ifOp, OpContext &ctx);

  /// Execute static repeat, counting iterations.
  /// @return true if all iterations are done
  bool executeStaticRepeat(ProcStaticRepeatOp repeatOp, OpContext &ctx);

  //===--------------------------------------------------------------------===//
  // Timing Validation
  //===--------------------------------------------------------------------===//

  /// Validate call timing attributes at runtime.
  /// @param call The CallOp to validate
  /// @param currentCycle Current cycle within the enclosing static step
  /// @return true if timing is valid, false if violation detected
  bool validateCallTiming(CallOp call, unsigned currentCycle);

  /// Enable/disable runtime timing validation.
  void setTimingValidation(bool enabled) { validateTiming_ = enabled; }

  /// Check if timing validation is enabled.
  bool isTimingValidationEnabled() const { return validateTiming_; }

  //===--------------------------------------------------------------------===//
  // State Access
  //===--------------------------------------------------------------------===//

  /// Get current cycle for a static step.
  unsigned getStepCycle(llvm::StringRef stepName) const;

  /// Check if a static step is active.
  bool isStepActive(llvm::StringRef stepName) const;

  /// Check if a static step is done.
  bool isStepDone(llvm::StringRef stepName) const;

private:
  /// Register static steps with their latencies.
  void registerStaticSteps(cmt2::ModuleOp module);

  /// State manager reference.
  StateManager *state_ = nullptr;

  /// Static step definitions keyed by name.
  llvm::StringMap<ProcStaticStepOp> staticSteps_;

  /// Static if definitions keyed by enclosing step.
  llvm::StringMap<ProcStaticIfOp> staticIfs_;

  /// Static repeat definitions keyed by enclosing step.
  llvm::StringMap<ProcStaticRepeatOp> staticRepeats_;

  /// Whether to validate timing at runtime.
  bool validateTiming_ = true;

  /// Timing violations detected (for reporting).
  std::vector<std::string> timingViolations_;
};

} // namespace interp
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_INTERPRETER_CONTROLFLOWPLUGIN_H
