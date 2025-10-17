//===- Instance.h - High-Level Instance Template ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// High-level instance template for module instantiation
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INSTANCE_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INSTANCE_H

#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "llvm/ADT/SmallVector.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Value.h"
#include <string>
#include <tuple>
#include <utility>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

class Cmt2Module;

/// High-level instance template
/// Wraps low-level ecmt2::Instance
template <typename ModuleType>
class Instance {
public:
  /// Default constructor
  Instance() = default;

  /// Constructor with arguments (clock, reset, etc.)
  template <typename... Args>
  explicit Instance(Args &&...args)
      : args_(std::forward<Args>(args)...) {}

  /// Set the name of this instance
  void setName(llvm::StringRef name) { name_ = name.str(); }

  llvm::StringRef getName() const { return name_; }

  /// Initialize with parent module
  /// Called during Circuit::addModule()
  void init(Cmt2Module *parent, llvm::StringRef name, ModuleBase *moduleType);

  /// Call a method on this instance
  template <typename... Args>
  llvm::SmallVector<mlir::Value, 4> callMethod(llvm::StringRef method,
                                                 mlir::OpBuilder &builder,
                                                 Args &&...args) {
    llvm::SmallVector<mlir::Value, 4> argValues;
    (argValues.push_back(extractValue(std::forward<Args>(args))), ...);
    return lowLevelInstance_->callMethod(method, argValues, builder);
  }

  /// Call a value on this instance
  llvm::SmallVector<mlir::Value, 4> callValue(llvm::StringRef value,
                                               mlir::OpBuilder &builder) {
    return lowLevelInstance_->callValue(value, builder);
  }

  /// Access the low-level instance
  ecmt2::Instance *lowLevelInstance() { return lowLevelInstance_; }

private:
  /// Helper to extract mlir::Value from various types
  template <typename T>
  static mlir::Value extractValue(const T &val) {
    if constexpr (std::is_same_v<T, mlir::Value>) {
      return val;
    } else if constexpr (requires { val.getValue(); }) {
      return val.getValue();
    } else {
      static_assert(std::is_same_v<T, mlir::Value>,
                    "Cannot extract mlir::Value from this type");
    }
  }

  /// Helper to extract arguments from tuple
  template <std::size_t... Is>
  llvm::SmallVector<mlir::Value, 4>
  extractArgsImpl(std::index_sequence<Is...>) {
    llvm::SmallVector<mlir::Value, 4> values;
    (values.push_back(extractValue(std::get<Is>(args_))), ...);
    return values;
  }

  llvm::SmallVector<mlir::Value, 4> extractArgs() {
    return extractArgsImpl(
        std::make_index_sequence<std::tuple_size_v<decltype(args_)>>{});
  }

  std::string name_;
  std::tuple<> args_; // Will be specialized for actual args
  ecmt2::Instance *lowLevelInstance_ = nullptr;

  template <typename T>
  friend class Instance; // Allow specializations to access private members
};

// Specialization for instances with arguments
template <typename ModuleType>
template <typename... Args>
class Instance<ModuleType>::InstanceWithArgs : public Instance<ModuleType> {
public:
  explicit InstanceWithArgs(Args &&...args)
      : args_(std::forward<Args>(args)...) {}

private:
  std::tuple<Args...> args_;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INSTANCE_H
