//===- FunctionLike.h - High-Level Function Templates ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// High-level templates for Values, Methods, and Rules
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_FUNCTIONLIKE_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_FUNCTIONLIKE_H

#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/Builders.h"
#include <functional>
#include <string>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

class Cmt2Module;

/// Value: read-only function returning data
/// Wraps low-level ecmt2::Value
template <typename RetType>
class Value {
public:
  Value() = default;

  /// Define guard condition
  template <typename Func>
  Value &guard(Func &&f) {
    guardFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Define body computation
  template <typename Func>
  Value &body(Func &&f) {
    bodyFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Initialize with parent module
  void init(Cmt2Module *parent, llvm::StringRef name);

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

private:
  std::string name_;
  std::function<void(mlir::OpBuilder &)> guardFn_;
  std::function<void(mlir::OpBuilder &)> bodyFn_;
};

/// Method: function with side effects
/// Wraps low-level ecmt2::Method
template <typename RetType, typename... Args>
class Method {
public:
  Method() = default;

  /// Define guard condition
  template <typename Func>
  Method &guard(Func &&f) {
    guardFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Define body computation
  template <typename Func>
  Method &body(Func &&f) {
    bodyFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Initialize with parent module
  void init(Cmt2Module *parent, llvm::StringRef name);

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

private:
  std::string name_;
  std::function<void(mlir::OpBuilder &, llvm::ArrayRef<mlir::BlockArgument>)>
      guardFn_;
  std::function<void(mlir::OpBuilder &, llvm::ArrayRef<mlir::BlockArgument>)>
      bodyFn_;
};

/// Rule: autonomous behavior
/// Wraps low-level ecmt2::Rule
class Rule {
public:
  Rule() = default;

  /// Define guard condition
  template <typename Func>
  Rule &guard(Func &&f) {
    guardFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Define body computation
  template <typename Func>
  Rule &body(Func &&f) {
    bodyFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Initialize with parent module
  void init(Cmt2Module *parent, llvm::StringRef name);

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

private:
  std::string name_;
  std::function<void(mlir::OpBuilder &)> guardFn_;
  std::function<void(mlir::OpBuilder &)> bodyFn_;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_FUNCTIONLIKE_H
