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
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/Builders.h"
#include <functional>
#include <string>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

class Cmt2Module;

/// Type traits for converting C++ types to MLIR types
/// Specialize this for custom types
template <typename T>
struct TypeTraits {
  static mlir::Type get(mlir::MLIRContext *ctx) {
    // Default: 32-bit unsigned integer
    return firrtl::UIntType::get(ctx, 32);
  }
};

// Specialization for void (no return type)
template <>
struct TypeTraits<void> {
  static mlir::Type get(mlir::MLIRContext *ctx) {
    return mlir::Type(); // Empty type for void
  }
};

// Templated type markers for FIRRTL types (used in Method/Value template parameters)
// These are lightweight compile-time type markers - not the actual Signal runtime types
//
// Usage:
//   Method<UInt<32>, UInt<16>> myMethod;  // Takes UInt<32>, returns UInt<16>
//   Value<SInt<64>> myValue;              // Returns SInt<64>
//
template <unsigned Width>
struct UInt {};

template <unsigned Width>
struct SInt {};

// Forward declarations for Bundle and Vector type descriptors
struct BundleTypeDescriptor;
struct VectorTypeDescriptor;

/// Runtime bundle type descriptor
/// Used to specify bundle types for Method/Value arguments and return types
class BundleTypeDescriptor {
public:
  BundleTypeDescriptor() = default;

  BundleTypeDescriptor &addUInt(llvm::StringRef name, unsigned width, bool isFlip = false) {
    fields_.push_back({name.str(), "uint", width, 0, isFlip});
    return *this;
  }

  BundleTypeDescriptor &addSInt(llvm::StringRef name, unsigned width, bool isFlip = false) {
    fields_.push_back({name.str(), "sint", width, 0, isFlip});
    return *this;
  }

  BundleTypeDescriptor &addVector(llvm::StringRef name, llvm::StringRef elemKind,
                                   unsigned elemWidth, size_t numElems, bool isFlip = false) {
    fields_.push_back({name.str(), "vector", elemWidth, numElems, isFlip, elemKind.str()});
    return *this;
  }

  mlir::Type toMLIRType(mlir::MLIRContext *ctx) const;

private:
  struct FieldSpec {
    std::string name;
    std::string kind; // "uint", "sint", "vector", "bundle"
    unsigned width;
    size_t numElems; // For vectors
    bool isFlip;
    std::string elemKind; // For nested types
  };
  llvm::SmallVector<FieldSpec, 4> fields_;
};

/// Runtime vector type descriptor
class VectorTypeDescriptor {
public:
  VectorTypeDescriptor(llvm::StringRef elemKind, unsigned elemWidth, size_t numElems)
      : elemKind_(elemKind.str()), elemWidth_(elemWidth), numElems_(numElems) {}

  mlir::Type toMLIRType(mlir::MLIRContext *ctx) const;

private:
  std::string elemKind_; // "uint", "sint"
  unsigned elemWidth_;
  size_t numElems_;
};

// Helper functions to create type descriptors
inline BundleTypeDescriptor MakeBundleType() {
  return BundleTypeDescriptor();
}

inline VectorTypeDescriptor MakeVectorType(llvm::StringRef elemKind, unsigned elemWidth, size_t numElems) {
  return VectorTypeDescriptor(elemKind, elemWidth, numElems);
}

// Specializations for templated FIRRTL types
template <unsigned Width>
struct TypeTraits<UInt<Width>> {
  static mlir::Type get(mlir::MLIRContext *ctx) {
    return firrtl::UIntType::get(ctx, Width);
  }
};

template <unsigned Width>
struct TypeTraits<SInt<Width>> {
  static mlir::Type get(mlir::MLIRContext *ctx) {
    return firrtl::SIntType::get(ctx, Width);
  }
};

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

//===----------------------------------------------------------------------===//
// Custom Type Support for Bundle/Vector Methods and Values
//===----------------------------------------------------------------------===//

/// CustomValue: Value with runtime-specified types (for bundles/vectors)
/// Used when return types cannot be specified at compile time
class CustomValue {
public:
  CustomValue() = default;

  /// Specify return type using type descriptor
  CustomValue &returnType(const BundleTypeDescriptor &desc) {
    returnTypeDesc_ = desc;
    hasCustomReturnType_ = true;
    returnTypeKind_ = TypeKind::Bundle;
    return *this;
  }

  CustomValue &returnType(const VectorTypeDescriptor &desc) {
    vectorReturnTypeDesc_ = desc;
    hasCustomReturnType_ = true;
    returnTypeKind_ = TypeKind::Vector;
    return *this;
  }

  /// Define guard condition
  template <typename Func>
  CustomValue &guard(Func &&f) {
    guardFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Define body computation
  template <typename Func>
  CustomValue &body(Func &&f) {
    bodyFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Initialize with parent module
  void init(Cmt2Module *parent, llvm::StringRef name);

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

private:
  enum class TypeKind { Bundle, Vector };

  std::string name_;
  BundleTypeDescriptor returnTypeDesc_;
  VectorTypeDescriptor vectorReturnTypeDesc_{"uint", 32, 1}; // Default placeholder
  bool hasCustomReturnType_ = false;
  TypeKind returnTypeKind_ = TypeKind::Bundle;
  std::function<void(mlir::OpBuilder &)> guardFn_;
  std::function<void(mlir::OpBuilder &)> bodyFn_;
};

/// CustomMethod: Method with runtime-specified types (for bundles/vectors)
/// Used when argument or return types cannot be specified at compile time
class CustomMethod {
public:
  CustomMethod() = default;

  /// Specify return type using type descriptor
  CustomMethod &returnType(const BundleTypeDescriptor &desc) {
    returnTypeDescs_.clear();
    returnTypeDescs_.push_back(desc);
    returnTypeKind_ = TypeKind::Bundle;
    return *this;
  }

  CustomMethod &returnType(const VectorTypeDescriptor &desc) {
    vectorReturnTypeDescs_.clear();
    vectorReturnTypeDescs_.push_back(desc);
    returnTypeKind_ = TypeKind::Vector;
    return *this;
  }

  /// Specify argument types (can be called multiple times for multiple args)
  CustomMethod &argType(llvm::StringRef name, const BundleTypeDescriptor &desc) {
    argNames_.push_back(name.str());
    argTypeDescs_.push_back(desc);
    argTypeKinds_.push_back(TypeKind::Bundle);
    return *this;
  }

  CustomMethod &argType(llvm::StringRef name, const VectorTypeDescriptor &desc) {
    argNames_.push_back(name.str());
    vectorArgTypeDescs_.push_back(desc);
    argTypeKinds_.push_back(TypeKind::Vector);
    return *this;
  }

  /// Define guard condition
  template <typename Func>
  CustomMethod &guard(Func &&f) {
    guardFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Define body computation
  template <typename Func>
  CustomMethod &body(Func &&f) {
    bodyFn_ = std::forward<Func>(f);
    return *this;
  }

  /// Initialize with parent module
  void init(Cmt2Module *parent, llvm::StringRef name);

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

private:
  enum class TypeKind { Bundle, Vector };

  std::string name_;
  llvm::SmallVector<std::string, 4> argNames_;
  llvm::SmallVector<BundleTypeDescriptor, 4> argTypeDescs_;
  llvm::SmallVector<VectorTypeDescriptor, 4> vectorArgTypeDescs_;
  llvm::SmallVector<TypeKind, 4> argTypeKinds_;
  llvm::SmallVector<BundleTypeDescriptor, 1> returnTypeDescs_;
  llvm::SmallVector<VectorTypeDescriptor, 1> vectorReturnTypeDescs_;
  TypeKind returnTypeKind_ = TypeKind::Bundle;
  std::function<void(mlir::OpBuilder &, llvm::ArrayRef<mlir::BlockArgument>)>
      guardFn_;
  std::function<void(mlir::OpBuilder &, llvm::ArrayRef<mlir::BlockArgument>)>
      bodyFn_;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

// Include Module.h for template implementations
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

// Template method implementations

template <typename RetType>
void Value<RetType>::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();

  // Determine result types
  llvm::SmallVector<mlir::Type, 1> resultTypes;
  if constexpr (!std::is_same_v<RetType, void>) {
    auto *ctx = parent->lowLevelModule()->getBuilder().getContext();
    resultTypes.push_back(TypeTraits<RetType>::get(ctx));
  }

  // Create low-level value
  auto *lowLevelValue = parent->lowLevelModule()->addValue(name, resultTypes);

  // Set guard if defined
  if (guardFn_) {
    lowLevelValue->guard([this, parent](mlir::OpBuilder &builder) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      guardFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  // Set body if defined
  if (bodyFn_) {
    lowLevelValue->body([this, parent](mlir::OpBuilder &builder) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      bodyFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  lowLevelValue->finalize();
}

template <typename RetType, typename... Args>
void Method<RetType, Args...>::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();

  auto *ctx = parent->lowLevelModule()->getBuilder().getContext();

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

  // Create low-level method
  auto *lowLevelMethod = parent->lowLevelModule()->addMethod(name, argTypes, resultTypes);

  // Set guard if defined
  if (guardFn_) {
    lowLevelMethod->guard([this, parent](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      guardFn_(builder, args);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  // Set body if defined
  if (bodyFn_) {
    lowLevelMethod->body([this, parent](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      bodyFn_(builder, args);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  lowLevelMethod->finalize();
}

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_FUNCTIONLIKE_H
