//===- Module.h - High-Level Module API ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// High-level class-based API for Cmt2 modules
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_MODULE_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_MODULE_H

#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Registry.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include <string>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

class Circuit;

/// Build context that stores current builder and location
/// This enables implicit context for helper functions
class BuildContext {
public:
  BuildContext(mlir::OpBuilder *builder, mlir::Location loc)
      : builder_(builder), loc_(loc) {}

  mlir::OpBuilder &builder() { return *builder_; }
  mlir::Location loc() const { return loc_; }

private:
  mlir::OpBuilder *builder_;
  mlir::Location loc_;
};

/// Base class for all high-level Cmt2 modules
/// Wraps the low-level ecmt2::Module class
class Cmt2Module {
public:
  Cmt2Module(llvm::StringRef name) : name_(name.str()) {}
  virtual ~Cmt2Module() = default;

  /// Override this to define module structure
  virtual void build() = 0;

  /// Access to underlying low-level module
  ecmt2::Module *lowLevelModule() { return lowLevelModule_; }
  const ecmt2::Module *lowLevelModule() const { return lowLevelModule_; }

  /// Module name
  llvm::StringRef name() const { return name_; }

  /// Get current build context (for helper functions)
  static BuildContext *getCurrentContext() { return currentContext_; }

protected:
  /// Called by Circuit to set up low-level module
  void setLowLevelModule(ecmt2::Module *module) {
    lowLevelModule_ = module;
  }

  /// Convenience accessors
  mlir::OpBuilder &builder() { return lowLevelModule_->getBuilder(); }
  mlir::Location loc() const { return lowLevelModule_->getLoc(); }

  /// Access to member registry for macro-based registration
  MemberRegistry &getRegistry() { return registry_; }

  /// Set current build context (used internally by function builders)
  static void setCurrentContext(BuildContext *ctx) { currentContext_ = ctx; }

  /// Add a precedence constraint (before, after)
  /// Usage in constructor: addPrecedence("methodA", "methodB"); // methodA < methodB
  void addPrecedence(llvm::StringRef before, llvm::StringRef after) {
    precedenceConstraints_.push_back({before.str(), after.str()});
  }

  /// Get accumulated precedence constraints
  const std::vector<std::pair<std::string, std::string>> &getPrecedenceConstraints() const {
    return precedenceConstraints_;
  }

private:
  std::string name_;
  ecmt2::Module *lowLevelModule_ = nullptr;
  MemberRegistry registry_;

  /// Precedence constraints accumulated via addPrecedence()
  std::vector<std::pair<std::string, std::string>> precedenceConstraints_;

  /// Thread-local current context for implicit builder access
  static thread_local BuildContext *currentContext_;

  friend class Circuit;
  template <typename RetType> friend class Value;
  template <typename RetType, typename... Args> friend class Method;
  friend class Rule;
  friend class CustomValue;
  friend class CustomMethod;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_MODULE_H
