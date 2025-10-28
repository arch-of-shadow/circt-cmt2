//===- FunctionLike.h - ECMT2 Function-Like Operations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines function-like operations (Rule, Method, Value) for the
// ECMT2 embedded DSL.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_FUNCTIONLIKE_H
#define CIRCT_DIALECT_CMT2_ECMT2_FUNCTIONLIKE_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/ArrayRef.h"
#include <functional>
#include <memory>

namespace circt {
namespace cmt2 {
namespace ecmt2 {

// Forward declaration
class Module;

/// Base class for function-like operations with guard/body regions
class FunctionLike {
public:
  virtual ~FunctionLike() = default;

  /// Access to guard and body builders
  mlir::OpBuilder &getGuardBuilder() { return *guardBuilder_; }
  mlir::OpBuilder &getBodyBuilder() { return *bodyBuilder_; }

  /// Block arguments (shared between guard and body)
  llvm::ArrayRef<mlir::BlockArgument> getArguments() const { return arguments_; }

  /// Finalize construction
  virtual void finalize() = 0;

protected:
  FunctionLike(mlir::Operation *op, mlir::OpBuilder &parentBuilder);

  mlir::Operation *op_;
  mlir::OpBuilder &parentBuilder_;
  std::unique_ptr<mlir::OpBuilder> guardBuilder_;
  std::unique_ptr<mlir::OpBuilder> bodyBuilder_;
  llvm::SmallVector<mlir::BlockArgument, 4> arguments_;
};

/// Rule: no inputs, guard returns i1
class Rule : public FunctionLike {
public:
  Rule(llvm::StringRef name, Module *parent);

  /// Guard region builder
  template <typename Func> Rule &guard(Func &&fn) {
    fn(getGuardBuilder());
    return *this;
  }

  /// Body region builder
  template <typename Func> Rule &body(Func &&fn) {
    fn(getBodyBuilder());
    return *this;
  }

  void finalize() override;

private:
  RuleOp op_;
};

/// Method: inputs, guard returns i1, body may have side effects
class Method : public FunctionLike {
public:
  Method(llvm::StringRef name,
         llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
         llvm::ArrayRef<mlir::Type> results, Module *parent);

  /// Guard region builder (has access to arguments)
  template <typename Func> Method &guard(Func &&fn) {
    fn(getGuardBuilder(), getArguments());
    return *this;
  }

  /// Body region builder (has access to arguments)
  template <typename Func> Method &body(Func &&fn) {
    fn(getBodyBuilder(), getArguments());
    return *this;
  }

  /// Set custom signal names
  Method &setEnableName(llvm::StringRef name);
  Method &setReadyName(llvm::StringRef name);

  void finalize() override;

private:
  MethodOp op_;
};

/// Value: no inputs or with inputs, guard returns i1, body returns data
class Value : public FunctionLike {
public:
  Value(llvm::StringRef name, llvm::ArrayRef<mlir::Type> results,
        Module *parent);

  /// Guard region builder
  template <typename Func> Value &guard(Func &&fn) {
    fn(getGuardBuilder());
    return *this;
  }

  /// Body region builder
  template <typename Func> Value &body(Func &&fn) {
    fn(getBodyBuilder());
    return *this;
  }

  /// Set custom ready name
  Value &setReadyName(llvm::StringRef name);

  void finalize() override;

private:
  ValueOp op_;
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_FUNCTIONLIKE_H
