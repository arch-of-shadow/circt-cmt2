//===- InterfaceAPI.h - Declarative Interface API ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a declarative API for defining circuit-level interfaces
// using templates and compile-time type checking.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INTERFACEAPI_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INTERFACEAPI_H

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include <functional>
#include <string>
#include <type_traits>

namespace circt {
namespace cmt2 {
namespace ecmt2 {


namespace highlevel {

//===----------------------------------------------------------------------===//
// InterfaceMethod and InterfaceValue Templates
//===----------------------------------------------------------------------===//

/// InterfaceMethod: Type-safe interface method declaration
template <typename RetType, typename... Args>
class InterfaceMethod {
public:
  InterfaceMethod() = default;

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

  /// Build the method signature and add to parent interface
  void buildSignature(ecmt2::Interface *parent, mlir::MLIRContext *ctx);

private:
  std::string name_;
};

/// InterfaceValue: Type-safe interface value declaration
template <typename RetType, typename... Args>
class InterfaceValue {
public:
  InterfaceValue() = default;

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

  /// Build the value signature and add to parent interface
  void buildSignature(ecmt2::Interface *parent, mlir::MLIRContext *ctx);

private:
  std::string name_;
};

//===----------------------------------------------------------------------===//
// Cmt2Interface Base Class
//===----------------------------------------------------------------------===//

/// Base class for declarative circuit-level interfaces
class Cmt2Interface {
public:
  Cmt2Interface(llvm::StringRef name) : name_(name.str()) {}
  virtual ~Cmt2Interface() = default;

  /// Called to build the interface - override in derived classes
  virtual void build() = 0;

  /// Initialize with low-level circuit and create low-level interface
  void init(ecmt2::Circuit *circuit);

  llvm::StringRef getName() const { return name_; }
  ecmt2::Interface *lowLevelInterface() { return lowLevelInterface_; }

protected:
  std::string name_;
  ecmt2::Interface *lowLevelInterface_ = nullptr;
  ecmt2::Circuit *circuit_ = nullptr;
  mlir::MLIRContext *context_ = nullptr;
};

//===----------------------------------------------------------------------===//
// Template Implementations
//===----------------------------------------------------------------------===//

template <typename RetType, typename... Args>
void InterfaceMethod<RetType, Args...>::buildSignature(
    ecmt2::Interface *parent, mlir::MLIRContext *ctx) {

  // Build argument types
  llvm::SmallVector<std::pair<std::string, mlir::Type>, 4> argTypes;
  unsigned argIdx = 0;
  ([&]() {
    std::string argName = "arg" + std::to_string(argIdx++);
    argTypes.push_back({argName, TypeTraits<Args>::get(ctx)});
  }(), ...);

  // Build result types
  llvm::SmallVector<mlir::Type, 1> resultTypes;
  if constexpr (!std::is_same_v<RetType, void>) {
    resultTypes.push_back(TypeTraits<RetType>::get(ctx));
  }

  parent->addMethod(name_, argTypes, resultTypes);
}

template <typename RetType, typename... Args>
void InterfaceValue<RetType, Args...>::buildSignature(
    ecmt2::Interface *parent, mlir::MLIRContext *ctx) {

  // Build argument types
  llvm::SmallVector<std::pair<std::string, mlir::Type>, 4> argTypes;
  unsigned argIdx = 0;
  ([&]() {
    std::string argName = "arg" + std::to_string(argIdx++);
    argTypes.push_back({argName, TypeTraits<Args>::get(ctx)});
  }(), ...);

  // Build result type attributes
  llvm::SmallVector<mlir::TypeAttr, 1> resultTypeAttrs;
  if constexpr (!std::is_same_v<RetType, void>) {
    resultTypeAttrs.push_back(mlir::TypeAttr::get(TypeTraits<RetType>::get(ctx)));
  }

  parent->addValue(name_, argTypes, resultTypeAttrs);
}

//===----------------------------------------------------------------------===//
// Initialization Macros
//===----------------------------------------------------------------------===//

/// Initialize an interface method member
#define INIT_INTERFACE_METHOD(member) \
  do { \
    member.setName(#member); \
    member.buildSignature(lowLevelInterface_, context_); \
  } while (0)

/// Initialize an interface value member
#define INIT_INTERFACE_VALUE(member) \
  do { \
    member.setName(#member); \
    member.buildSignature(lowLevelInterface_, context_); \
  } while (0)

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INTERFACEAPI_H
