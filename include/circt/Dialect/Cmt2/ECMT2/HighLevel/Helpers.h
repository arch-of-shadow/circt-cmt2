//===- Helpers.h - High-Level Helper Functions ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Convenience helper functions for cleaner syntax
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_HELPERS_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_HELPERS_H

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "mlir/IR/Value.h"

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

/// Get current builder from implicit context
inline mlir::OpBuilder &B() {
  auto *ctx = Cmt2Module::getCurrentContext();
  assert(ctx && "No build context available");
  return ctx->builder();
}

/// Get current location from implicit context
inline mlir::Location L() {
  auto *ctx = Cmt2Module::getCurrentContext();
  assert(ctx && "No build context available");
  return ctx->loc();
}

/// Create a return operation with no values
inline void Return() {
  B().create<circt::cmt2::ReturnOp>(L(), mlir::ValueRange{});
}

/// Create a return operation with a single value
inline void Return(mlir::Value val) {
  B().create<circt::cmt2::ReturnOp>(L(), mlir::ValueRange{val});
}

/// Create a return operation with multiple values
inline void Return(llvm::ArrayRef<mlir::Value> vals) {
  B().create<circt::cmt2::ReturnOp>(L(), vals);
}

// Note: Call() helper is not provided here because CallOp requires
// instance symbols and proper type inference. Use the low-level
// Instance::callMethod() or CallBuilder API for method calls.

/// Create a constant UInt value
inline mlir::Value UIntConst(uint64_t value, unsigned width) {
  auto type = circt::firrtl::UIntType::get(B().getContext(), width);
  auto intType = mlir::IntegerType::get(B().getContext(), width,
                                         mlir::IntegerType::Unsigned);
  auto attr = mlir::IntegerAttr::get(intType, llvm::APInt(width, value));
  return B().create<circt::firrtl::ConstantOp>(L(), type, attr);
}

/// Create a constant SInt value
inline mlir::Value SIntConst(int64_t value, unsigned width) {
  auto type = circt::firrtl::SIntType::get(B().getContext(), width);
  auto intType = mlir::IntegerType::get(B().getContext(), width,
                                         mlir::IntegerType::Signed);
  auto attr = mlir::IntegerAttr::get(intType, llvm::APInt(width, value));
  return B().create<circt::firrtl::ConstantOp>(L(), type, attr);
}

/// Create an add operation
inline mlir::Value Add(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::AddPrimOp>(L(), lhs, rhs);
}

/// Create a subtract operation
inline mlir::Value Sub(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::SubPrimOp>(L(), lhs, rhs);
}

/// Create a multiply operation
inline mlir::Value Mul(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::MulPrimOp>(L(), lhs, rhs);
}

/// Create a divide operation
inline mlir::Value Div(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::DivPrimOp>(L(), lhs, rhs);
}

/// Create a greater-than comparison
inline mlir::Value Gt(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::GTPrimOp>(L(), lhs, rhs);
}

/// Create a greater-or-equal comparison
inline mlir::Value Geq(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::GEQPrimOp>(L(), lhs, rhs);
}

/// Create a less-than comparison
inline mlir::Value Lt(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::LTPrimOp>(L(), lhs, rhs);
}

/// Create a less-or-equal comparison
inline mlir::Value Leq(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::LEQPrimOp>(L(), lhs, rhs);
}

/// Create an equality comparison
inline mlir::Value Eq(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::EQPrimOp>(L(), lhs, rhs);
}

/// Create an inequality comparison
inline mlir::Value Neq(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::NEQPrimOp>(L(), lhs, rhs);
}

/// Create a bitwise AND operation
inline mlir::Value And(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::AndPrimOp>(L(), lhs, rhs);
}

/// Create a bitwise OR operation
inline mlir::Value Or(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::OrPrimOp>(L(), lhs, rhs);
}

/// Create a bitwise XOR operation
inline mlir::Value Xor(mlir::Value lhs, mlir::Value rhs) {
  return B().create<circt::firrtl::XorPrimOp>(L(), lhs, rhs);
}

/// Create a bitwise NOT operation
inline mlir::Value Not(mlir::Value val) {
  return B().create<circt::firrtl::NotPrimOp>(L(), val);
}

/// Create a mux (multiplexer) operation
inline mlir::Value Mux(mlir::Value sel, mlir::Value high, mlir::Value low) {
  return B().create<circt::firrtl::MuxPrimOp>(L(), sel, high, low);
}

/// Extract a range of bits
inline mlir::Value Bits(mlir::Value val, unsigned high, unsigned low) {
  return B().create<circt::firrtl::BitsPrimOp>(L(), val, high, low);
}

/// Extract a single bit
inline mlir::Value Bit(mlir::Value val, unsigned index) {
  return Bits(val, index, index);
}

//===----------------------------------------------------------------------===//
// Bundle and Vector Helpers
//===----------------------------------------------------------------------===//

/// Create a bundle value with fluent API
/// Example: auto bundle = MakeBundle()
///                           .addUInt("addr", 32)
///                           .addUInt("data", 64)
///                           .build();
class BundleBuilder {
public:
  BundleBuilder() : context_(B().getContext()) {}

  BundleBuilder &addUInt(llvm::StringRef name, unsigned width, bool isFlip = false) {
    auto nameAttr = mlir::StringAttr::get(context_, name);
    auto type = circt::firrtl::UIntType::get(context_, width);
    elements_.push_back(circt::firrtl::BundleType::BundleElement(nameAttr, isFlip, type));
    return *this;
  }

  BundleBuilder &addSInt(llvm::StringRef name, unsigned width, bool isFlip = false) {
    auto nameAttr = mlir::StringAttr::get(context_, name);
    auto type = circt::firrtl::SIntType::get(context_, width);
    elements_.push_back(circt::firrtl::BundleType::BundleElement(nameAttr, isFlip, type));
    return *this;
  }

  BundleBuilder &addVector(llvm::StringRef name, mlir::Type elemType,
                           size_t numElems, bool isFlip = false) {
    auto nameAttr = mlir::StringAttr::get(context_, name);
    auto vecType = circt::firrtl::FVectorType::get(
        mlir::cast<circt::firrtl::FIRRTLBaseType>(elemType), numElems);
    elements_.push_back(circt::firrtl::BundleType::BundleElement(nameAttr, isFlip, vecType));
    return *this;
  }

  BundleBuilder &addField(llvm::StringRef name, mlir::Type type, bool isFlip = false) {
    auto nameAttr = mlir::StringAttr::get(context_, name);
    auto firrtlType = mlir::cast<circt::firrtl::FIRRTLBaseType>(type);
    elements_.push_back(circt::firrtl::BundleType::BundleElement(nameAttr, isFlip, firrtlType));
    return *this;
  }

  mlir::Value build() {
    auto bundleType = circt::firrtl::BundleType::get(context_, elements_);
    return B().create<circt::firrtl::InvalidValueOp>(L(), bundleType);
  }

  llvm::ArrayRef<circt::firrtl::BundleType::BundleElement> getElements() const {
    return elements_;
  }

private:
  mlir::MLIRContext *context_;
  llvm::SmallVector<circt::firrtl::BundleType::BundleElement> elements_;
};

/// Factory function for BundleBuilder
inline BundleBuilder MakeBundle() {
  return BundleBuilder();
}

/// Create a vector value
inline mlir::Value MakeVector(mlir::Type elemType, size_t numElements) {
  auto vecType = circt::firrtl::FVectorType::get(
      mlir::cast<circt::firrtl::FIRRTLBaseType>(elemType), numElements);
  return B().create<circt::firrtl::InvalidValueOp>(L(), vecType);
}

/// Access a bundle field
inline mlir::Value GetField(mlir::Value bundle, llvm::StringRef fieldName) {
  return B().create<circt::firrtl::SubfieldOp>(L(), bundle, fieldName);
}

/// Access a vector element
inline mlir::Value GetElement(mlir::Value vector, unsigned index) {
  return B().create<circt::firrtl::SubindexOp>(L(), vector, index);
}

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_HELPERS_H
