//===- Signal.cpp - ECMT2 Signal Implementation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/Seq/SeqOps.h"

using namespace circt;
using namespace cmt2::ecmt2;

//===----------------------------------------------------------------------===//
// Signal
//===----------------------------------------------------------------------===//

Signal Signal::operator+(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::AddPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator-(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::SubPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator*(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::MulPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator/(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::DivPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator%(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::RemPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator&(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::AndPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator|(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::OrPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator^(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::XorPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator~() const {
  llvm::SmallVector<mlir::Value> operands = {value_};
  auto result = builder_->create<firrtl::NotPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator==(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::EQPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator!=(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::NEQPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator<(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::LTPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator<=(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::LEQPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator>(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::GTPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::operator>=(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::GEQPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::bits(unsigned high, unsigned low) const {
  auto result =
      builder_->create<firrtl::BitsPrimOp>(loc_, value_, high, low);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::bit(unsigned index) const { return bits(index, index); }

Signal Signal::cat(const Signal &other) const {
  llvm::SmallVector<mlir::Value> operands = {value_, other.value_};
  auto result = builder_->create<firrtl::CatPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::mux(const Signal &trueVal, const Signal &falseVal) const {
  llvm::SmallVector<mlir::Value> operands = {value_, trueVal.value_, falseVal.value_};
  auto result = builder_->create<firrtl::MuxPrimOp>(loc_, operands);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::pad(unsigned width) const {
  auto result = builder_->create<firrtl::PadPrimOp>(loc_, value_, width);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::shl(unsigned amount) const {
  auto result = builder_->create<firrtl::ShlPrimOp>(loc_, value_, amount);
  return Signal(result.getResult(), builder_, loc_);
}

Signal Signal::shr(unsigned amount) const {
  auto result = builder_->create<firrtl::ShrPrimOp>(loc_, value_, amount);
  return Signal(result.getResult(), builder_, loc_);
}

unsigned Signal::getWidth() const {
  if (auto intType = mlir::dyn_cast<firrtl::FIRRTLBaseType>(value_.getType())) {
    if (auto width = intType.getBitWidthOrSentinel(); width >= 0)
      return width;
  }
  return 0;
}

//===----------------------------------------------------------------------===//
// UInt
//===----------------------------------------------------------------------===//

UInt::UInt(unsigned width, mlir::OpBuilder &builder, mlir::Location loc)
    : Signal(createWire(width, builder, loc), &builder, loc) {}

mlir::Value UInt::createWire(unsigned width, mlir::OpBuilder &builder,
                            mlir::Location loc) {
  auto type = firrtl::UIntType::get(builder.getContext(), width);
  // Don't create firrtl.wire inside cmt2.module - just create a constant 0
  // The actual wire will be created during Cmt2-to-FIRRTL lowering
  auto constOp = builder.create<firrtl::ConstantOp>(
      loc, type, llvm::APInt(width, 0));
  return constOp.getResult();
}

UInt UInt::constant(uint64_t value, unsigned width, mlir::OpBuilder &builder,
                   mlir::Location loc) {
  auto type = firrtl::UIntType::get(builder.getContext(), width);
  auto constOp = builder.create<firrtl::ConstantOp>(
      loc, type, llvm::APInt(width, value));
  return UInt(constOp.getResult(), &builder, loc);
}

//===----------------------------------------------------------------------===//
// SInt
//===----------------------------------------------------------------------===//

SInt::SInt(unsigned width, mlir::OpBuilder &builder, mlir::Location loc)
    : Signal(createWire(width, builder, loc), &builder, loc) {}

mlir::Value SInt::createWire(unsigned width, mlir::OpBuilder &builder,
                            mlir::Location loc) {
  auto type = firrtl::SIntType::get(builder.getContext(), width);
  // Don't create firrtl.wire inside cmt2.module - just create a constant 0
  // The actual wire will be created during Cmt2-to-FIRRTL lowering
  auto constOp = builder.create<firrtl::ConstantOp>(
      loc, type, llvm::APInt(width, 0, /*isSigned=*/true));
  return constOp.getResult();
}

SInt SInt::constant(int64_t value, unsigned width, mlir::OpBuilder &builder,
                   mlir::Location loc) {
  auto type = firrtl::SIntType::get(builder.getContext(), width);
  auto constOp = builder.create<firrtl::ConstantOp>(
      loc, type, llvm::APInt(width, value, /*isSigned=*/true));
  return SInt(constOp.getResult(), &builder, loc);
}

//===----------------------------------------------------------------------===//
// Clock
//===----------------------------------------------------------------------===//

// Clock now only wraps existing values - no auto-creation

//===----------------------------------------------------------------------===//
// Reset
//===----------------------------------------------------------------------===//

// Reset now only wraps existing values - no auto-creation
