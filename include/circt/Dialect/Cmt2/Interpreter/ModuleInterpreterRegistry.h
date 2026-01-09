//===- ModuleInterpreterRegistry.h - Module Interpreter Registry -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the registry for module interpreters in CMT2.
// The registry manages interpreter resolution with the following priority:
//
// 1. `interpreter_class` attribute on module (e.g., "Reg", "FIFO")
// 2. Pattern matching on module name (legacy compatibility)
// 3. Fallback JSON configuration file
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_INTERPRETER_MODULEINTERPRETERREGISTRY_H
#define CIRCT_DIALECT_CMT2_INTERPRETER_MODULEINTERPRETERREGISTRY_H

#include "circt/Dialect/Cmt2/Interpreter/ModuleInterpreter.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/StringMap.h"
#include <memory>
#include <vector>

namespace circt {
namespace cmt2 {
namespace interp {

/// Registry for module interpreters with resolution strategies.
///
/// The registry manages a collection of ModuleInterpreter instances and
/// provides resolution logic to find the appropriate interpreter for
/// external module instances.
///
/// Resolution Priority:
/// 1. Check for `interpreter_class` attribute specifying built-in type
/// 2. Pattern matching on module name (Reg, FIFO, Mem, etc.)
/// 3. Check fallback configuration if provided
///
class ModuleInterpreterRegistry {
public:
  ModuleInterpreterRegistry();
  ~ModuleInterpreterRegistry();

  //===--------------------------------------------------------------------===//
  // Interpreter Registration
  //===--------------------------------------------------------------------===//

  /// Register built-in interpreters (Reg, FIFO, Memory).
  /// Called automatically on construction.
  void registerBuiltins();

  /// Register a custom interpreter.
  /// The interpreter's getModuleType() is used as the registration key.
  void registerInterpreter(std::unique_ptr<ModuleInterpreter> interp);

  /// Get a registered interpreter by type name.
  ModuleInterpreter *getInterpreter(llvm::StringRef typeName);

  //===--------------------------------------------------------------------===//
  // Resolution
  //===--------------------------------------------------------------------===//

  /// Resolve interpreter for an external module definition.
  ///
  /// Resolution priority:
  /// 1. `interpreter_class` attribute → look up by class name
  /// 2. Module name pattern matching → canHandle() on each interpreter
  /// 3. Fallback configuration
  ///
  /// @param extModule The external module operation
  /// @return Interpreter that can handle this module, or nullptr
  ModuleInterpreter *resolveInterpreter(ExtModuleFirrtlOp extModule);

  /// Resolve interpreter for a module by name only.
  /// Uses pattern matching on registered interpreters.
  ModuleInterpreter *resolveByName(llvm::StringRef moduleName);

  //===--------------------------------------------------------------------===//
  // Instance Management
  //===--------------------------------------------------------------------===//

  /// Initialize an instance using the appropriate interpreter.
  ///
  /// @param instanceName Unique instance name
  /// @param extModule The external module definition
  /// @param params Instance parameters
  /// @return true if initialization succeeded
  bool initializeInstance(llvm::StringRef instanceName,
                          ExtModuleFirrtlOp extModule,
                          llvm::ArrayRef<mlir::NamedAttribute> params);

  /// Initialize instance with explicit module name (for backward compatibility).
  bool initializeInstance(llvm::StringRef instanceName,
                          llvm::StringRef moduleName,
                          llvm::ArrayRef<mlir::NamedAttribute> params);

  /// Reset all instances.
  void resetAllInstances();

  /// Tick all instances (start of cycle).
  void tickAllInstances();

  /// Commit all instances (end of cycle).
  void commitAllInstances();

  //===--------------------------------------------------------------------===//
  // Method Invocation
  //===--------------------------------------------------------------------===//

  /// Check if a method is ready to be called.
  bool checkMethodGuard(llvm::StringRef instanceName,
                        llvm::StringRef methodName,
                        llvm::ArrayRef<InterpValue> args);

  /// Call a method on an instance.
  std::optional<std::vector<InterpValue>>
  callMethod(llvm::StringRef instanceName, llvm::StringRef methodName,
             llvm::ArrayRef<InterpValue> args);

  //===--------------------------------------------------------------------===//
  // State Inspection
  //===--------------------------------------------------------------------===//

  /// Get state of an instance.
  llvm::json::Value getInstanceState(llvm::StringRef instanceName) const;

  /// Get all instance names.
  std::vector<std::string> getAllInstanceNames() const;

  /// Check if an instance exists.
  bool hasInstance(llvm::StringRef instanceName) const;

  //===--------------------------------------------------------------------===//
  // Configuration
  //===--------------------------------------------------------------------===//

  /// Load module mappings from JSON configuration file.
  /// Format:
  /// {
  ///   "modules": {
  ///     "ModuleName": { "class": "Reg", "width": 32 },
  ///     "OtherModule": { "class": "FIFO", "depth": 4 }
  ///   }
  /// }
  mlir::LogicalResult loadConfig(llvm::StringRef jsonPath);

  //===--------------------------------------------------------------------===//
  // Diagnostics
  //===--------------------------------------------------------------------===//

  /// Get list of external modules without interpreters.
  const std::vector<std::string> &getUnresolvedModules() const {
    return unresolvedModules_;
  }

  /// Clear unresolved modules list.
  void clearUnresolvedModules() { unresolvedModules_.clear(); }

private:
  /// Mapping from interpreter type name to interpreter instance
  llvm::StringMap<std::unique_ptr<ModuleInterpreter>> interpreters_;

  /// Mapping from instance name to interpreter that manages it
  llvm::StringMap<ModuleInterpreter *> instanceToInterpreter_;

  /// Mapping from module name to interpreter class (from JSON config)
  llvm::StringMap<std::string> moduleConfig_;

  /// List of module names that couldn't be resolved
  std::vector<std::string> unresolvedModules_;
};

} // namespace interp
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_INTERPRETER_MODULEINTERPRETERREGISTRY_H
