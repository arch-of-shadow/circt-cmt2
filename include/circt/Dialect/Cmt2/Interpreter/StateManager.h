//===- StateManager.h - Interpreter State Management -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the StateManager class for centralized interpreter state
// management with observer support.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_INTERPRETER_STATEMANAGER_H
#define CIRCT_DIALECT_CMT2_INTERPRETER_STATEMANAGER_H

#include "circt/Dialect/Cmt2/Interpreter/Types.h"
#include "llvm/ADT/StringMap.h"
#include <memory>
#include <vector>

namespace circt {
namespace cmt2 {
namespace interp {

//===----------------------------------------------------------------------===//
// StateObserver - Observer interface for state changes
//===----------------------------------------------------------------------===//

/// Observer interface for state change notifications.
/// Observers can be registered with StateManager to receive callbacks
/// when state changes occur (useful for tracing, debugging, visualization).
class StateObserver {
public:
  virtual ~StateObserver() = default;

  /// Called when a register is read.
  virtual void onRegisterRead(llvm::StringRef name, const InterpValue &value) {}

  /// Called when a register write is committed.
  virtual void onRegisterWrite(llvm::StringRef name, const InterpValue &oldVal,
                               const InterpValue &newVal) {}

  /// Called when an FSM transitions to a new state.
  virtual void onFSMTransition(llvm::StringRef fsmName, unsigned oldState,
                               unsigned newState) {}

  /// Called at the start of each cycle.
  virtual void onCycleStart(uint64_t cycle) {}

  /// Called at the end of each cycle.
  virtual void onCycleEnd(uint64_t cycle) {}

  /// Called when a static step starts.
  virtual void onStaticStepStart(llvm::StringRef stepName, unsigned latency) {}

  /// Called when a static step advances a cycle.
  virtual void onStaticStepTick(llvm::StringRef stepName, unsigned currentCycle,
                                unsigned latency) {}

  /// Called when a static step completes.
  virtual void onStaticStepDone(llvm::StringRef stepName) {}
};

//===----------------------------------------------------------------------===//
// StateManager - Centralized state management
//===----------------------------------------------------------------------===//

/// Manages interpreter state: registers, FSMs, static steps, pending writes.
///
/// StateManager provides a centralized abstraction for all interpreter state,
/// with support for:
/// - Register read/write with pending write semantics
/// - FSM state for procedural rules
/// - Static step cycle tracking for timing-accurate simulation
/// - Observer pattern for state change notifications
///
class StateManager {
public:
  StateManager() = default;
  ~StateManager() = default;

  // Non-copyable, movable
  StateManager(const StateManager &) = delete;
  StateManager &operator=(const StateManager &) = delete;
  StateManager(StateManager &&) = default;
  StateManager &operator=(StateManager &&) = default;

  //===--------------------------------------------------------------------===//
  // Register Access
  //===--------------------------------------------------------------------===//

  /// Add a register with the given name and width.
  void addRegister(llvm::StringRef name, unsigned width,
                   std::optional<InterpValue> resetValue = std::nullopt);

  /// Read a register value.
  std::optional<InterpValue> readRegister(llvm::StringRef name);

  /// Queue a write to a register (applied at commitWrites).
  void writeRegister(llvm::StringRef name, const InterpValue &value);

  /// Apply all pending writes atomically.
  void commitWrites();

  /// Discard all pending writes.
  void discardWrites();

  /// Get all register names.
  std::vector<std::string> getRegisterNames() const;

  /// Check if a register exists.
  bool hasRegister(llvm::StringRef name) const;

  /// Get register width.
  std::optional<unsigned> getRegisterWidth(llvm::StringRef name) const;

  //===--------------------------------------------------------------------===//
  // FSM State (for procedural control)
  //===--------------------------------------------------------------------===//

  /// Add an FSM for a procedural rule.
  void addFSM(llvm::StringRef name, unsigned numStates,
              const std::vector<std::string> &stateSteps = {});

  /// Get current FSM state (0 = idle).
  unsigned getFSMState(llvm::StringRef name) const;

  /// Set FSM state.
  void setFSMState(llvm::StringRef name, unsigned state);

  /// Check if FSM is in idle state (state 0).
  bool isFSMIdle(llvm::StringRef name) const;

  /// Check if FSM is running (not idle).
  bool isFSMRunning(llvm::StringRef name) const;

  /// Get step name for current FSM state.
  std::optional<std::string> getFSMCurrentStep(llvm::StringRef name) const;

  /// Get all FSM names.
  std::vector<std::string> getFSMNames() const;

  //===--------------------------------------------------------------------===//
  // Static Step Cycle Tracking
  //===--------------------------------------------------------------------===//

  /// Register a static step with its latency.
  /// @param stepName Unique step name
  /// @param latency Total cycles for step completion
  /// @param interval Optional initiation interval (for pipelining)
  void addStaticStep(llvm::StringRef stepName, unsigned latency,
                     std::optional<unsigned> interval = std::nullopt);

  /// Start a static step (sets cycle counter to 0, marks active).
  void startStaticStep(llvm::StringRef stepName);

  /// Advance static step cycle counter.
  /// @return true if step is done (currentCycle >= latency)
  bool tickStaticStep(llvm::StringRef stepName);

  /// Get current cycle within a static step.
  unsigned getStaticStepCycle(llvm::StringRef stepName) const;

  /// Check if static step is done (completed its latency cycles).
  bool isStaticStepDone(llvm::StringRef stepName) const;

  /// Check if static step is currently active.
  bool isStaticStepActive(llvm::StringRef stepName) const;

  /// Check if static step can accept new initiation (for pipelining).
  /// Returns true if enough cycles have passed since last initiation.
  bool canInitiate(llvm::StringRef stepName) const;

  /// Reset a static step to inactive state.
  void resetStaticStep(llvm::StringRef stepName);

  /// Get all static step names.
  std::vector<std::string> getStaticStepNames() const;

  //===--------------------------------------------------------------------===//
  // Observers
  //===--------------------------------------------------------------------===//

  /// Add a state observer.
  void addObserver(std::shared_ptr<StateObserver> observer);

  /// Remove a state observer.
  void removeObserver(StateObserver *observer);

  /// Remove all observers.
  void clearObservers();

  //===--------------------------------------------------------------------===//
  // Cycle Management
  //===--------------------------------------------------------------------===//

  /// Get current cycle count.
  uint64_t getCycle() const { return cycle_; }

  /// Increment cycle count and notify observers.
  void incrementCycle();

  /// Reset all state to initial values.
  void reset();

  /// Clear all state (registers, FSMs, static steps).
  void clear();

private:
  /// Notify observers of register read.
  void notifyRegisterRead(llvm::StringRef name, const InterpValue &value);

  /// Notify observers of register write.
  void notifyRegisterWrite(llvm::StringRef name, const InterpValue &oldVal,
                           const InterpValue &newVal);

  /// Notify observers of FSM transition.
  void notifyFSMTransition(llvm::StringRef name, unsigned oldState,
                           unsigned newState);

  /// Notify observers of cycle start.
  void notifyCycleStart(uint64_t cycle);

  /// Notify observers of cycle end.
  void notifyCycleEnd(uint64_t cycle);

  //===--------------------------------------------------------------------===//
  // Data Members
  //===--------------------------------------------------------------------===//

  /// Current cycle count.
  uint64_t cycle_ = 0;

  /// Register states keyed by name.
  llvm::StringMap<RegisterState> registers_;

  /// Pending register writes (applied at commitWrites).
  llvm::StringMap<InterpValue> pendingWrites_;

  /// FSM states keyed by rule name.
  llvm::StringMap<FSMState> fsmStates_;

  /// Static step states keyed by step name.
  llvm::StringMap<StaticStepState> staticSteps_;

  /// Registered observers.
  std::vector<std::shared_ptr<StateObserver>> observers_;
};

} // namespace interp
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_INTERPRETER_STATEMANAGER_H
