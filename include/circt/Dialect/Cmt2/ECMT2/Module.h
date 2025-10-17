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

/// External FIRRTL module wrapper
class ExternalModule : public ModuleBase {
public:
  ExternalModule(llvm::StringRef name, llvm::StringRef firrtlModule,
                 mlir::OpBuilder &builder, mlir::Location loc);

  /// Add a module argument and return its block argument
  mlir::BlockArgument addArgument(llvm::StringRef name, mlir::Type type);

  /// Fluent API for binding (creates bind.bare operations)
  ExternalModule &bindClock(llvm::StringRef argName, llvm::StringRef port);
  ExternalModule &bindReset(llvm::StringRef argName, llvm::StringRef port);

  ExternalModule &bindMethod(llvm::StringRef name, llvm::StringRef enablePort,
                             llvm::StringRef readyPort,
                             llvm::ArrayRef<std::string> inputPorts,
                             llvm::ArrayRef<std::string> outputPorts);

  ExternalModule &bindValue(llvm::StringRef name, llvm::StringRef readyPort,
                            llvm::ArrayRef<std::string> dataPorts);

  /// Conflict matrix setup
  ExternalModule &addConflict(llvm::StringRef a, llvm::StringRef b);
  ExternalModule &addConflictFree(llvm::StringRef a, llvm::StringRef b);
  ExternalModule &addSequenceBefore(llvm::StringRef before,
                                   llvm::StringRef after);

  /// Overrides
  llvm::StringRef getName() const override { return name_; }
  mlir::Operation *getOperation() const override { return op_; }

private:
  std::string name_;
  ExtModuleFirrtlOp op_;
  mlir::OpBuilder &builder_;
  mlir::Location loc_;
};

/// Cmt2 Module
class Module : public ModuleBase {
public:
  Module(llvm::StringRef name, mlir::OpBuilder &builder, mlir::Location loc);
  ~Module() override;

  /// Add a module argument and return its block argument
  mlir::BlockArgument addArgument(llvm::StringRef name, mlir::Type type);

  /// Convenience methods for clock and reset
  Clock addClockArgument(llvm::StringRef name = "clk");
  Reset addResetArgument(llvm::StringRef name = "rst");

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
  InterfaceDecl *defineInterface(llvm::StringRef name, llvm::StringRef type);
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

private:
  std::string name_;
  ModuleOp op_;
  mlir::OpBuilder &builder_;
  mlir::Location loc_;

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
