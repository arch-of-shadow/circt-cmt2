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

//===----------------------------------------------------------------------===//
// Procedural Operations
//===----------------------------------------------------------------------===//

/// ProcStep: single-fire procedural step
class ProcStep {
public:
  ProcStep(llvm::StringRef name, Module *parent);

  /// Body builder - where step actions are defined
  template <typename Func> ProcStep &body(Func &&fn) {
    fn(*bodyBuilder_);
    return *this;
  }

  llvm::StringRef getName() const { return name_; }

private:
  std::string name_;
  ProcStepOp op_;
  std::unique_ptr<mlir::OpBuilder> bodyBuilder_;
};

/// ProcStaticStep: fixed-latency step
class ProcStaticStep {
public:
  ProcStaticStep(llvm::StringRef name, uint64_t latency, Module *parent);

  /// Body builder
  template <typename Func> ProcStaticStep &body(Func &&fn) {
    fn(*bodyBuilder_);
    return *this;
  }

  llvm::StringRef getName() const { return name_; }

private:
  std::string name_;
  ProcStaticStepOp op_;
  std::unique_ptr<mlir::OpBuilder> bodyBuilder_;
};

/// Control region builder for procedural operations
class ControlBuilder {
public:
  ControlBuilder(mlir::OpBuilder &builder, mlir::Location loc);

  /// Sequential composition
  template <typename Func> ControlBuilder &seq(Func &&fn) {
    auto seqOp = builder_.create<ProcSeqOp>(loc_);
    mlir::OpBuilder seqBuilder(seqOp.getBodyRegion());
    auto *block = new mlir::Block();
    seqOp.getBodyRegion().push_back(block);
    seqBuilder.setInsertionPointToStart(block);
    ControlBuilder nested(seqBuilder, loc_);
    fn(nested);
    return *this;
  }

  /// Parallel composition
  template <typename Func> ControlBuilder &par(Func &&fn) {
    auto parOp = builder_.create<ProcParOp>(loc_);
    mlir::OpBuilder parBuilder(parOp.getBodyRegion());
    auto *block = new mlir::Block();
    parOp.getBodyRegion().push_back(block);
    parBuilder.setInsertionPointToStart(block);
    ControlBuilder nested(parBuilder, loc_);
    fn(nested);
    return *this;
  }

  /// Conditional execution
  template <typename ThenFunc, typename ElseFunc>
  ControlBuilder &ifThenElse(mlir::Value cond, ThenFunc &&thenFn,
                             ElseFunc &&elseFn) {
    auto ifOp = builder_.create<ProcIfOp>(loc_, cond);

    // Then region
    auto *thenBlock = new mlir::Block();
    ifOp.getThenRegion().push_back(thenBlock);
    mlir::OpBuilder thenBuilder(thenBlock, thenBlock->begin());
    ControlBuilder thenNested(thenBuilder, loc_);
    thenFn(thenNested);

    // Else region
    auto *elseBlock = new mlir::Block();
    ifOp.getElseRegion().push_back(elseBlock);
    mlir::OpBuilder elseBuilder(elseBlock, elseBlock->begin());
    ControlBuilder elseNested(elseBuilder, loc_);
    elseFn(elseNested);

    return *this;
  }

  /// Conditional execution (no else)
  template <typename ThenFunc>
  ControlBuilder &ifThen(mlir::Value cond, ThenFunc &&thenFn) {
    return ifThenElse(cond, std::forward<ThenFunc>(thenFn),
                      [](ControlBuilder &) {});
  }

  /// While loop with condition region
  /// @param condFn Function that builds the condition and returns the condition value
  /// @param bodyFn Function that builds the loop body
  template <typename CondFunc, typename BodyFunc>
  ControlBuilder &whileLoop(CondFunc &&condFn, BodyFunc &&bodyFn) {
    auto whileOp = builder_.create<ProcWhileOp>(loc_);

    // Build condition region
    auto *condBlock = new mlir::Block();
    whileOp.getCondRegion().push_back(condBlock);
    mlir::OpBuilder condBuilder(condBlock, condBlock->begin());
    mlir::Value cond = condFn(condBuilder);
    condBuilder.create<ProcWhileCondYieldOp>(loc_, cond);

    // Build body region
    auto *bodyBlock = new mlir::Block();
    whileOp.getBody().push_back(bodyBlock);
    mlir::OpBuilder bodyBuilder(bodyBlock, bodyBlock->begin());
    ControlBuilder nested(bodyBuilder, loc_);
    bodyFn(nested);

    // Add terminator for body region
    bodyBuilder.setInsertionPointToEnd(bodyBlock);
    bodyBuilder.create<ProcYieldOp>(loc_);

    return *this;
  }

  /// While loop (legacy API - condition must be computed before call)
  /// @deprecated Use the two-function version instead
  template <typename Func>
  ControlBuilder &whileLoop(mlir::Value cond, Func &&fn) {
    // Create a condition function that just returns the pre-computed value
    return whileLoop(
        [cond](mlir::OpBuilder &) { return cond; },
        std::forward<Func>(fn));
  }

  /// Enable a group
  ControlBuilder &enable(llvm::StringRef stepName);

  /// Invoke a method on an instance
  mlir::Value invoke(llvm::StringRef instance, llvm::StringRef method,
                     llvm::ArrayRef<mlir::Value> args,
                     llvm::ArrayRef<mlir::Type> results);

  /// End control region
  void end();

  /// Access builder for inline expressions
  mlir::OpBuilder &getBuilder() { return builder_; }

private:
  mlir::OpBuilder &builder_;
  mlir::Location loc_;
};

/// ProcRule: procedural rule with guard and control regions
class ProcRule {
public:
  ProcRule(llvm::StringRef name, Module *parent);

  /// Guard region builder
  template <typename Func> ProcRule &guard(Func &&fn) {
    fn(*guardBuilder_);
    return *this;
  }

  /// Control region builder
  template <typename Func> ProcRule &control(Func &&fn) {
    ControlBuilder cb(*controlBuilder_, loc_);
    fn(cb);
    cb.end();
    return *this;
  }

  llvm::StringRef getName() const { return name_; }

private:
  std::string name_;
  ProcRuleOp op_;
  mlir::Location loc_;
  std::unique_ptr<mlir::OpBuilder> guardBuilder_;
  std::unique_ptr<mlir::OpBuilder> controlBuilder_;
};

/// ProcMethod: procedural method with guard, control regions, and arguments
class ProcMethod {
public:
  ProcMethod(llvm::StringRef name,
             llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
             llvm::ArrayRef<mlir::Type> results, Module *parent);

  /// Guard region builder (has access to arguments)
  template <typename Func> ProcMethod &guard(Func &&fn) {
    fn(*guardBuilder_, arguments_);
    return *this;
  }

  /// Control region builder (has access to arguments)
  template <typename Func> ProcMethod &control(Func &&fn) {
    ControlBuilder cb(*controlBuilder_, loc_);
    fn(cb, arguments_);
    cb.end();
    return *this;
  }

  /// Access arguments
  llvm::ArrayRef<mlir::BlockArgument> getArguments() const { return arguments_; }

  llvm::StringRef getName() const { return name_; }

private:
  std::string name_;
  ProcMethodOp op_;
  mlir::Location loc_;
  std::unique_ptr<mlir::OpBuilder> guardBuilder_;
  std::unique_ptr<mlir::OpBuilder> controlBuilder_;
  llvm::SmallVector<mlir::BlockArgument, 4> arguments_;
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_FUNCTIONLIKE_H
