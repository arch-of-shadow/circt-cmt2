//===- Instance.h - High-Level Instance Template ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// High-level declarative instance template for module instantiation
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

// Avoid name conflict with low-level ecmt2::Instance
// Users should use highlevel::Instance or include proper namespace
using ::circt::cmt2::ecmt2::ModuleBase;

/// High-level declarative instance template
/// Wraps low-level ecmt2::Instance and supports automatic registration
///
/// Usage:
///   class MyModule : public Cmt2Module {
///     Instance<Reg32> x;
///
///     MyModule(ExternalModule *regMod) : Cmt2Module("my") {
///       x.setModuleType(regMod);
///       x.setArgs(clk, rst);
///       CMT2_REGISTER(x);
///     }
///   };
///
template <typename ModuleType>
class Instance {
public:
  /// Default constructor
  Instance() = default;

  /// Constructor with arguments (for inline initialization)
  template <typename... Args>
  explicit Instance(Args &&...args) {
    setArgsImpl(std::forward<Args>(args)...);
  }

  /// Set the module type (external module or module pointer)
  void setModuleType(ModuleBase *moduleType) { moduleType_ = moduleType; }

  /// Set arguments (clock, reset, etc.)
  template <typename... Args>
  void setArgs(Args &&...args) {
    setArgsImpl(std::forward<Args>(args)...);
  }

  /// Get the name of this instance
  llvm::StringRef getName() const { return name_; }

  /// Initialize with parent module (called by registration system)
  /// This creates the actual MLIR instance operation
  void init(Cmt2Module *parent, llvm::StringRef name);

  /// Call a method on this instance
  template <typename... Args>
  llvm::SmallVector<mlir::Value, 4> callMethod(llvm::StringRef method,
                                                 mlir::OpBuilder &builder,
                                                 Args &&...args) {
    if (!lowLevelInstance_) {
      llvm::report_fatal_error("Instance not initialized - did you call CMT2_REGISTER?");
    }
    llvm::SmallVector<mlir::Value, 4> argValues;
    (argValues.push_back(extractValue(std::forward<Args>(args))), ...);
    return lowLevelInstance_->callMethod(method, argValues, builder);
  }

  /// Call a value on this instance
  llvm::SmallVector<mlir::Value, 4> callValue(llvm::StringRef value,
                                               mlir::OpBuilder &builder) {
    if (!lowLevelInstance_) {
      llvm::report_fatal_error("Instance not initialized - did you call CMT2_REGISTER?");
    }
    return lowLevelInstance_->callValue(value, builder);
  }

  /// Access the low-level instance
  ecmt2::Instance *lowLevelInstance() { return lowLevelInstance_; }

  /// Set interface bindings for this instance
  /// Each binding is a pair of (interfaceDefName, interfaceDeclName)
  void setInterfaceBindings(std::vector<std::pair<std::string, std::string>> bindings) {
    interfaceBindings_ = std::move(bindings);
  }

  /// Add a single interface binding
  void addInterfaceBinding(llvm::StringRef interfaceDefName, llvm::StringRef interfaceDeclName) {
    interfaceBindings_.push_back({interfaceDefName.str(), interfaceDeclName.str()});
  }

private:
  /// Helper to extract mlir::Value from various types
  template <typename T>
  static mlir::Value extractValue(T &&val) {
    using BaseT = std::decay_t<T>;
    if constexpr (std::is_same_v<BaseT, mlir::Value>) {
      return val;
    } else if constexpr (std::is_same_v<BaseT, mlir::BlockArgument>) {
      // BlockArgument is a subclass of Value
      return val;
    } else if constexpr (std::is_base_of_v<mlir::Op<BaseT>, BaseT>) {
      // MLIR operation - get its result
      return val.getResult();
    } else if constexpr (std::is_convertible_v<BaseT, mlir::Value>) {
      // Any type convertible to mlir::Value
      return static_cast<mlir::Value>(val);
    } else {
      // Call getValue() for Signal types, value() for Input types
      // We'll use a helper to try both
      return extractValueImpl(std::forward<T>(val));
    }
  }

  template <typename T>
  static auto extractValueImpl(const T &val) -> decltype(val.getValue()) {
    return val.getValue();
  }

  template <typename T>
  static auto extractValueImpl(const T &val) -> decltype(val.value()) {
    return val.value();
  }

  /// Helper to set arguments from parameter pack
  template <typename... Args>
  void setArgsImpl(Args &&...args) {
    args_.clear();
    (args_.push_back(extractValue(std::forward<Args>(args))), ...);
  }

  std::string name_;
  ModuleBase *moduleType_ = nullptr;
  llvm::SmallVector<mlir::Value, 4> args_;
  std::vector<std::pair<std::string, std::string>> interfaceBindings_;
  ecmt2::Instance *lowLevelInstance_ = nullptr;

  friend class Cmt2Module;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

// Include Module.h here so template implementations can see the full definition
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

// Template method implementations

template <typename ModuleType>
void Instance<ModuleType>::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();

  if (!moduleType_) {
    llvm::report_fatal_error(llvm::Twine("Instance '") + name_ +
                             "' has no module type set - did you call setModuleType()?");
  }

  auto *lowLevelModule = parent->lowLevelModule();
  lowLevelInstance_ = lowLevelModule->addInstance(name_, moduleType_, args_, interfaceBindings_);
}

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

/// Registration macro for declarative instances
/// Usage: CMT2_REGISTER_INSTANCE(instanceMember, modulePointer, arg1, arg2, ...)
#define CMT2_REGISTER_INSTANCE(member, modType, ...)                          \
  do {                                                                          \
    member.setModuleType(modType);                                             \
    getRegistry().registerMember(#member, [this](auto *mod) {                 \
      this->member.setArgs(__VA_ARGS__);                                       \
      this->member.init(mod, #member);                                         \
    });                                                                         \
  } while (0)

/// Fluent helper to add interface binding and return self for chaining
/// Used before CMT2_REGISTER_INSTANCE
/// Usage:
///   c.addInterfaceBinding("readX", "reader");
///   CMT2_REGISTER_INSTANCE(c, childMod, clk.get().getValue(), rst.get().getValue());
/// This allows: member.addInterfaceBinding(...).addInterfaceBinding(...)
/// followed by CMT2_REGISTER_INSTANCE

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INSTANCE_H
