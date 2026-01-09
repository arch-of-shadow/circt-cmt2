//===- OpHandlerRegistry.h - Operation Handler Registration ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the OpHandlerRegistry class for extensible operation
// dispatch in the CMT2 interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_INTERPRETER_OPHANDLERREGISTRY_H
#define CIRCT_DIALECT_CMT2_INTERPRETER_OPHANDLERREGISTRY_H

#include "circt/Dialect/Cmt2/Interpreter/Types.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <optional>

namespace circt {
namespace cmt2 {
namespace interp {

// Forward declarations
class StateManager;
class ModuleInterpreterRegistry;

//===----------------------------------------------------------------------===//
// OpContext - Context passed to operation handlers
//===----------------------------------------------------------------------===//

/// Context passed to operation handlers during execution.
/// Provides access to state, value storage, and output streams.
struct OpContext {
  /// State manager for register/FSM access.
  StateManager &state;

  /// Value map for SSA values during execution.
  llvm::DenseMap<mlir::Value, InterpValue> &valueMap;

  /// Module interpreter registry for external module calls.
  ModuleInterpreterRegistry *moduleRegistry = nullptr;

  /// Output stream for messages.
  llvm::raw_ostream &os;

  /// Get value for an SSA value (looks up in valueMap or evaluates constant).
  InterpValue getValue(mlir::Value v);

  /// Set value for an SSA value.
  void setValue(mlir::Value v, const InterpValue &val);
};

//===----------------------------------------------------------------------===//
// OpHandler - Function type for operation handlers
//===----------------------------------------------------------------------===//

/// Handler function type.
/// Returns the result value for the operation, or nullopt if not applicable.
using OpHandler = std::function<std::optional<InterpValue>(
    mlir::Operation *, OpContext &)>;

//===----------------------------------------------------------------------===//
// OpHandlerRegistry - Registry for operation handlers
//===----------------------------------------------------------------------===//

/// Registry for operation handlers with extensible dispatch.
///
/// The registry maps operation types to handler functions, allowing:
/// - Type-safe handler registration using template methods
/// - Dialect-level registration for bulk handler setup
/// - Fallback handling for unregistered operations
///
/// Example usage:
/// ```cpp
/// OpHandlerRegistry registry;
///
/// // Register handler for hw::ConstantOp
/// registry.registerHandler<hw::ConstantOp>([](auto *op, OpContext &ctx) {
///   return op.getValue();
/// });
///
/// // Execute an operation
/// auto result = registry.execute(someOp, ctx);
/// ```
///
class OpHandlerRegistry {
public:
  OpHandlerRegistry();
  ~OpHandlerRegistry() = default;

  //===--------------------------------------------------------------------===//
  // Handler Registration
  //===--------------------------------------------------------------------===//

  /// Register a handler for a specific operation type.
  template <typename OpT>
  void registerHandler(OpHandler handler) {
    handlers_[mlir::TypeID::get<OpT>()] = std::move(handler);
  }

  /// Register a handler by operation name string.
  void registerHandlerByName(llvm::StringRef opName, OpHandler handler);

  /// Register handlers for a dialect.
  /// The registrar function receives this registry and registers handlers.
  void registerDialect(llvm::StringRef dialectName,
                       std::function<void(OpHandlerRegistry &)> registrar);

  //===--------------------------------------------------------------------===//
  // Handler Execution
  //===--------------------------------------------------------------------===//

  /// Execute an operation using registered handlers.
  /// Returns the result value, or nullopt if no handler or handler returns nullopt.
  std::optional<InterpValue> execute(mlir::Operation *op, OpContext &ctx);

  /// Check if a handler exists for an operation type.
  bool hasHandler(mlir::Operation *op) const;

  //===--------------------------------------------------------------------===//
  // Built-in Handler Registration
  //===--------------------------------------------------------------------===//

  /// Register all built-in handlers (HW, Comb, FIRRTL, CMT2).
  void registerBuiltinHandlers();

private:
  /// Handlers keyed by TypeID.
  llvm::DenseMap<mlir::TypeID, OpHandler> handlers_;

  /// Handlers keyed by operation name (fallback for ops not using TypeID).
  llvm::StringMap<OpHandler> namedHandlers_;

  /// Dialect registrars for lazy registration.
  llvm::StringMap<std::function<void(OpHandlerRegistry &)>> dialectRegistrars_;

  /// Track which dialects have been registered.
  llvm::StringSet<> registeredDialects_;
};

//===----------------------------------------------------------------------===//
// Built-in Handler Registration Functions
//===----------------------------------------------------------------------===//

/// Register HW dialect handlers (ConstantOp, etc.).
void registerHWHandlers(OpHandlerRegistry &registry);

/// Register Comb dialect handlers (AddOp, SubOp, AndOp, etc.).
void registerCombHandlers(OpHandlerRegistry &registry);

/// Register FIRRTL dialect handlers (ConstantOp, AddPrimOp, etc.).
void registerFIRRTLHandlers(OpHandlerRegistry &registry);

/// Register CMT2 dialect handlers (CallOp, ReturnOp, etc.).
void registerCMT2Handlers(OpHandlerRegistry &registry);

} // namespace interp
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_INTERPRETER_OPHANDLERREGISTRY_H
