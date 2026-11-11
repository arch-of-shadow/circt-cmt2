//===- ModuleInterpreter.h - External Module Interpreter Interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the interface for interpreting external modules in CMT2.
// External modules (Reg, FIFO, Memory, custom modules) need interpretation
// logic to provide behavioral simulation.
//
// The ModuleInterpreter interface provides:
// - Instance initialization with parameters
// - Method invocation (guard checking and body execution)
// - Cycle lifecycle (tick, commit, reset)
// - State inspection for debugging
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_INTERPRETER_MODULEINTERPRETER_H
#define CIRCT_DIALECT_CMT2_INTERPRETER_MODULEINTERPRETER_H

#include "mlir/IR/Attributes.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace interp {

/// Value storage for the interpreter (matches existing InterpValue)
using InterpValue = llvm::APInt;

//===----------------------------------------------------------------------===//
// ModuleInterpreter Interface
//===----------------------------------------------------------------------===//

/// Abstract base class for external module interpretation.
///
/// Each external module type (Reg, FIFO, Memory, custom) can have an
/// associated interpreter that provides behavioral simulation. The interpreter
/// manages per-instance state and implements method semantics.
///
/// Lifecycle:
/// 1. initializeInstance() - Called once per instance to set up state
/// 2. resetInstance() - Called on circuit reset
/// 3. For each cycle:
///    a. tick() - Called at cycle start (advance pipelines, timers)
///    b. checkMethodGuard() / callMethodBody() - Method invocations
///    c. commitCycle() - Apply pending state changes atomically
///
class ModuleInterpreter {
public:
  virtual ~ModuleInterpreter() = default;

  //===--------------------------------------------------------------------===//
  // Module Type Identification
  //===--------------------------------------------------------------------===//

  /// Get the module type this interpreter handles (e.g., "Reg", "FIFO")
  virtual llvm::StringRef getModuleType() const = 0;

  /// Check if this interpreter can handle a module with the given name
  virtual bool canHandle(llvm::StringRef moduleName) const {
    return moduleName.contains_insensitive(getModuleType());
  }

  //===--------------------------------------------------------------------===//
  // Instance Lifecycle
  //===--------------------------------------------------------------------===//

  /// Initialize state for an instance of this module type.
  ///
  /// @param instanceName Unique name for this instance
  /// @param params Module parameters (width, depth, etc.)
  virtual void initializeInstance(llvm::StringRef instanceName,
                                  llvm::ArrayRef<mlir::NamedAttribute> params) = 0;

  /// Reset instance to initial state.
  /// Called on circuit reset or explicit reset command.
  virtual void resetInstance(llvm::StringRef instanceName) = 0;

  /// Called at the start of each cycle.
  /// Use this to advance pipelines, increment timers, etc.
  virtual void tick(llvm::StringRef instanceName) {}

  /// Commit pending state changes at end of cycle.
  /// All state changes from method calls should be queued and
  /// applied atomically here.
  virtual void commitCycle(llvm::StringRef instanceName) = 0;

  //===--------------------------------------------------------------------===//
  // Method Invocation
  //===--------------------------------------------------------------------===//

  /// Check if a method's guard is satisfied (method is ready).
  ///
  /// @param instanceName The instance to query
  /// @param methodName The method to check
  /// @param args Arguments to the method (may affect guard)
  /// @return true if method can be called, false otherwise
  virtual bool checkMethodGuard(llvm::StringRef instanceName,
                                llvm::StringRef methodName,
                                llvm::ArrayRef<InterpValue> args) {
    // Default: methods are always ready
    return true;
  }

  /// Execute a method's body and return results.
  ///
  /// @param instanceName The instance to call on
  /// @param methodName The method to execute
  /// @param args Arguments to the method
  /// @return Results of the method call, or nullopt if method not found
  virtual std::optional<std::vector<InterpValue>>
  callMethodBody(llvm::StringRef instanceName, llvm::StringRef methodName,
                 llvm::ArrayRef<InterpValue> args) = 0;

  /// Convenience: Check guard and call body if ready.
  std::optional<std::vector<InterpValue>>
  callMethod(llvm::StringRef instanceName, llvm::StringRef methodName,
             llvm::ArrayRef<InterpValue> args) {
    if (!checkMethodGuard(instanceName, methodName, args))
      return std::nullopt;
    return callMethodBody(instanceName, methodName, args);
  }

  //===--------------------------------------------------------------------===//
  // State Inspection (for debugging)
  //===--------------------------------------------------------------------===//

  /// Get current state of an instance as JSON for inspection.
  virtual llvm::json::Value getInstanceState(llvm::StringRef instanceName) const {
    return llvm::json::Object{{"error", "not implemented"}};
  }

  /// Get list of all instance names managed by this interpreter.
  virtual std::vector<std::string> getInstanceNames() const = 0;

  /// Check if an instance exists.
  virtual bool hasInstance(llvm::StringRef instanceName) const = 0;
};

//===----------------------------------------------------------------------===//
// Built-in Module Interpreters
//===----------------------------------------------------------------------===//

/// Interpreter for Register modules.
///
/// Supports:
/// - read() -> value: Returns current register value (always ready)
/// - write(value): Queues value for update at cycle end
///
/// Parameters:
/// - width: Bit width of register (default: 32)
/// - init: Initial/reset value (default: 0)
///
class RegInterpreter : public ModuleInterpreter {
public:
  llvm::StringRef getModuleType() const override { return "Reg"; }

  bool canHandle(llvm::StringRef moduleName) const override {
    return moduleName.contains_insensitive("reg") ||
           moduleName.contains_insensitive("register");
  }

  void initializeInstance(llvm::StringRef instanceName,
                          llvm::ArrayRef<mlir::NamedAttribute> params) override;
  void resetInstance(llvm::StringRef instanceName) override;
  void commitCycle(llvm::StringRef instanceName) override;

  bool checkMethodGuard(llvm::StringRef instanceName,
                        llvm::StringRef methodName,
                        llvm::ArrayRef<InterpValue> args) override;
  std::optional<std::vector<InterpValue>>
  callMethodBody(llvm::StringRef instanceName, llvm::StringRef methodName,
                 llvm::ArrayRef<InterpValue> args) override;

  llvm::json::Value getInstanceState(llvm::StringRef instanceName) const override;
  std::vector<std::string> getInstanceNames() const override;
  bool hasInstance(llvm::StringRef instanceName) const override;

  // Direct access for backward compatibility
  std::optional<InterpValue> readRegister(llvm::StringRef instanceName) const;
  void writeRegister(llvm::StringRef instanceName, const InterpValue &value);

private:
  struct RegState {
    unsigned width = 32;
    InterpValue value;
    InterpValue resetValue;
    std::optional<InterpValue> pendingWrite;
  };

  llvm::StringMap<RegState> instances_;
};

/// Interpreter for Wire modules.
///
/// Wire is a combinational element with immediate read/write:
/// - read() -> value: Returns current value (always ready)
/// - write(value): Immediately updates value (visible to subsequent reads)
///
/// Unlike Reg, writes are immediately visible within the same cycle.
///
/// Parameters:
/// - width: Bit width of wire (default: 32)
///
class WireInterpreter : public ModuleInterpreter {
public:
  llvm::StringRef getModuleType() const override { return "Wire"; }

  bool canHandle(llvm::StringRef moduleName) const override {
    return moduleName.contains_insensitive("wire");
  }

  void initializeInstance(llvm::StringRef instanceName,
                          llvm::ArrayRef<mlir::NamedAttribute> params) override;
  void resetInstance(llvm::StringRef instanceName) override;
  void commitCycle(llvm::StringRef instanceName) override;

  bool checkMethodGuard(llvm::StringRef instanceName,
                        llvm::StringRef methodName,
                        llvm::ArrayRef<InterpValue> args) override;
  std::optional<std::vector<InterpValue>>
  callMethodBody(llvm::StringRef instanceName, llvm::StringRef methodName,
                 llvm::ArrayRef<InterpValue> args) override;

  llvm::json::Value getInstanceState(llvm::StringRef instanceName) const override;
  std::vector<std::string> getInstanceNames() const override;
  bool hasInstance(llvm::StringRef instanceName) const override;

private:
  struct WireState {
    unsigned width = 32;
    InterpValue value;
  };

  llvm::StringMap<WireState> instances_;
};

// MemoryInterpreter removed - use MLIR-based behavioral models in ModuleLibrary
// See docs/Cmt2/features/Interpreter.md for overview and extension pointers.

} // namespace interp
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_INTERPRETER_MODULEINTERPRETER_H
