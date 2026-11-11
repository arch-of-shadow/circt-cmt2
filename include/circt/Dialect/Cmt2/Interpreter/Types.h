//===- Types.h - Interpreter Type Definitions -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines common types used by the CMT2 interpreter infrastructure.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_INTERPRETER_TYPES_H
#define CIRCT_DIALECT_CMT2_INTERPRETER_TYPES_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace interp {

/// Value storage for the interpreter.
/// Uses APInt to support arbitrary bit-width values.
using InterpValue = llvm::APInt;

/// Represents the state of a register element.
struct RegisterState {
  std::string name;
  InterpValue value;
  unsigned width;
  bool hasReset;
  InterpValue resetValue;
};

/// Represents FSM state for procedural rules.
struct FSMState {
  std::string ruleName;
  unsigned currentState = 0;  // 0 = idle
  unsigned numStates = 1;
  bool isRunning = false;
  std::vector<std::string> stateSteps; // step name for each state
};

/// State for static step tracking (cycle-accurate latency).
struct StaticStepState {
  unsigned latency;                    // Total cycles for step
  std::optional<unsigned> interval;    // Initiation interval (for pipelining)
  unsigned currentCycle = 0;           // Current cycle within step
  bool active = false;                 // Is step currently running?
  uint64_t lastInitiation = 0;         // Cycle of last initiation (for II tracking)
};

/// Represents pending method calls.
struct PendingCall {
  std::string instanceName;
  std::string methodName;
  std::vector<InterpValue> args;
  bool executed = false;
};

/// Breakpoint types for debugging.
enum class BreakpointType {
  RuleFire,        // Break when a specific rule fires
  RuleGuard,       // Break when evaluating a specific rule's guard
  RegisterWrite,   // Break when a register is written
  Cycle            // Break at a specific cycle
};

/// Breakpoint definition.
struct Breakpoint {
  BreakpointType type;
  std::string target;        // Rule name, register name, etc.
  uint64_t cycleTarget = 0;  // For cycle breakpoints
  bool enabled = true;
  unsigned id;
};

/// Rule execution result.
struct RuleResult {
  std::string ruleName;
  bool guardEnabled;
  bool fired;
  std::vector<std::string> methodsCalled;
};

/// Single cycle trace entry.
struct CycleTrace {
  uint64_t cycle;
  std::vector<RuleResult> ruleResults;
  llvm::StringMap<InterpValue> stateChanges;
};

} // namespace interp
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_INTERPRETER_TYPES_H
