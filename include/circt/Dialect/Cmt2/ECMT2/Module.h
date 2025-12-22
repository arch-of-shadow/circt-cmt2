//===- Module.h - ECMT2 Module Classes --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines module classes for the ECMT2 embedded DSL.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_MODULE_H
#define CIRCT_DIALECT_CMT2_ECMT2_MODULE_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace ecmt2 {

// Forward declarations
class Rule;
class Method;
class Value;
class Instance;
class InterfaceDecl;
class InterfaceDef;

/// Base module interface
class ModuleBase {
public:
  virtual ~ModuleBase() = default;

  virtual llvm::StringRef getName() const = 0;
  virtual mlir::Operation *getOperation() const = 0;
};

// ExternalModule class has been removed and unified into Module class.
// Use Module with the external constructor instead:
//   Module(name, firrtlModuleName, builder, loc)

/// Cmt2 Module (supports both regular and external modules)
class Module : public ModuleBase {
public:
  /// Create a regular module
  Module(llvm::StringRef name, mlir::OpBuilder &builder, mlir::Location loc);

  /// Create an external module that binds to a FIRRTL module
  Module(llvm::StringRef name, llvm::StringRef firrtlModuleName,
         mlir::OpBuilder &builder, mlir::Location loc);

  ~Module() override;

  /// Check if this is an external module
  bool isExternal() const { return isExternal_; }

  /// Add a module argument and return its block argument
  mlir::BlockArgument addArgument(llvm::StringRef name, mlir::Type type);

  /// Convenience methods for clock and reset (for regular modules)
  Clock addClockArgument(llvm::StringRef name = "clk");
  Reset addResetArgument(llvm::StringRef name = "rst");

  //===--------------------------------------------------------------------===//
  // External module binding methods (only valid for external modules)
  //===--------------------------------------------------------------------===//

  /// Bind clock argument to external port (for external modules)
  Module &bindClock(llvm::StringRef argName, llvm::StringRef port);

  /// Bind reset argument to external port (for external modules)
  Module &bindReset(llvm::StringRef argName, llvm::StringRef port);

  /// Bind a method to external ports (for external modules)
  Module &bindMethod(llvm::StringRef name, llvm::StringRef enablePort,
                     llvm::StringRef readyPort,
                     llvm::ArrayRef<std::string> argPorts,
                     llvm::ArrayRef<std::string> resPorts);

  /// Bind a value to external ports (for external modules)
  Module &bindValue(llvm::StringRef name, llvm::StringRef readyPort,
                    llvm::ArrayRef<std::string> argPorts,
                    llvm::ArrayRef<std::string> resPorts);

  /// Add conflict relationship (for external modules)
  Module &addConflict(llvm::StringRef a, llvm::StringRef b);

  /// Add conflict-free relationship (for external modules)
  Module &addConflictFree(llvm::StringRef a, llvm::StringRef b);

  /// Add sequence-before relationship (for external modules)
  Module &addSequenceBefore(llvm::StringRef before, llvm::StringRef after);

  //===--------------------------------------------------------------------===//
  // Regular module methods (only valid for regular modules)
  //===--------------------------------------------------------------------===//

  /// Instance management
  Instance *
  addInstance(llvm::StringRef name, ModuleBase *moduleType,
              llvm::ArrayRef<mlir::Value> args,
              llvm::ArrayRef<std::pair<std::string, std::string>>
                  interfaceBindings = {});

  /// Function-like operations
  Rule *addRule(llvm::StringRef name);
  Method *addMethod(llvm::StringRef name,
                   llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
                   llvm::ArrayRef<mlir::Type> results);
  Value *addValue(llvm::StringRef name, llvm::ArrayRef<mlir::Type> results);

  /// Interface support
  InterfaceDecl *defineInterfaceDecl(llvm::StringRef name, llvm::StringRef type);
  InterfaceDef *defineInterfaceDef(llvm::StringRef name,
                                  llvm::StringRef type);

  /// Access internal functions
  template <typename Func> Func *getFunction(llvm::StringRef name);

  /// Set precedence relationships between methods/rules
  void setPrecedence(llvm::ArrayRef<std::pair<std::string, std::string>> pairs);

  /// Overrides
  llvm::StringRef getName() const override { return name_; }
  mlir::Operation *getOperation() const override { return op_; }

  /// MLIR integration
  mlir::OpBuilder &getBuilder() { return builder_; }
  mlir::Location getLoc() const { return loc_; }
  ModuleOp getInnerOp() { return op_; }

private:
  std::string name_;
  ModuleOp op_;
  mlir::OpBuilder &builder_;
  mlir::Location loc_;
  bool isExternal_ = false;

  std::vector<std::unique_ptr<Instance>> instances_;
  std::vector<std::unique_ptr<Rule>> rules_;
  std::vector<std::unique_ptr<Method>> methods_;
  std::vector<std::unique_ptr<Value>> values_;
  std::vector<std::unique_ptr<InterfaceDecl>> interfaces_;
  std::vector<std::unique_ptr<InterfaceDef>> interfaceDefs_;
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_MODULE_H
