//===- Signal.h - ECMT2 Signal Types ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines signal types for the ECMT2 embedded DSL.
// Signals wrap MLIR values and provide operator overloading for hardware
// operations.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_SIGNAL_H
#define CIRCT_DIALECT_CMT2_ECMT2_SIGNAL_H

#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/APInt.h"

namespace circt {
namespace cmt2 {
namespace ecmt2 {

/// Base signal class wrapping an MLIR value
class Signal {
public:
  Signal() = default;
  Signal(mlir::Value value, mlir::OpBuilder *builder, mlir::Location loc)
      : value_(value), builder_(builder), loc_(loc) {}
  virtual ~Signal() = default;

  // Arithmetic operations (creates firrtl ops)
  Signal operator+(const Signal &other) const;
  Signal operator-(const Signal &other) const;
  Signal operator*(const Signal &other) const;
  Signal operator/(const Signal &other) const;
  Signal operator%(const Signal &other) const;

  // Bitwise operations
  Signal operator&(const Signal &other) const;
  Signal operator|(const Signal &other) const;
  Signal operator^(const Signal &other) const;
  Signal operator~() const;

  // Comparison operations
  Signal operator==(const Signal &other) const;
  Signal operator!=(const Signal &other) const;
  Signal operator<(const Signal &other) const;
  Signal operator<=(const Signal &other) const;
  Signal operator>(const Signal &other) const;
  Signal operator>=(const Signal &other) const;

  // Bit manipulation
  Signal bits(unsigned high, unsigned low) const;
  Signal bit(unsigned index) const;
  Signal cat(const Signal &other) const;

  // FIRRTL operations
  Signal mux(const Signal &trueVal, const Signal &falseVal) const;
  Signal pad(unsigned width) const;
  Signal shl(unsigned amount) const;
  Signal shr(unsigned amount) const;

  // Getters
  mlir::Value getValue() const { return value_; }
  mlir::Type getType() const { return value_.getType(); }
  mlir::OpBuilder *getBuilder() const { return builder_; }
  mlir::Location getLoc() const { return loc_; }

  unsigned getWidth() const;

protected:
  mlir::Value value_;
  mlir::OpBuilder *builder_ = nullptr;
  mlir::Location loc_;
};

/// Unsigned integer signal
class UInt : public Signal {
public:
  UInt() = default;

  /// Create a wire with firrtl.uint<width> type
  explicit UInt(unsigned width, mlir::OpBuilder &builder, mlir::Location loc);

  /// Wrap an existing value
  explicit UInt(mlir::Value value, mlir::OpBuilder *builder, mlir::Location loc)
      : Signal(value, builder, loc) {}

  /// Create a constant
  static UInt constant(uint64_t value, unsigned width,
                      mlir::OpBuilder &builder, mlir::Location loc);

private:
  static mlir::Value createWire(unsigned width, mlir::OpBuilder &builder,
                               mlir::Location loc);
};

/// Signed integer signal
class SInt : public Signal {
public:
  SInt() = default;

  /// Create a wire with firrtl.sint<width> type
  explicit SInt(unsigned width, mlir::OpBuilder &builder, mlir::Location loc);

  /// Wrap an existing value
  explicit SInt(mlir::Value value, mlir::OpBuilder *builder, mlir::Location loc)
      : Signal(value, builder, loc) {}

  /// Create a constant
  static SInt constant(int64_t value, unsigned width,
                      mlir::OpBuilder &builder, mlir::Location loc);

private:
  static mlir::Value createWire(unsigned width, mlir::OpBuilder &builder,
                               mlir::Location loc);
};

/// Clock signal
class Clock {
public:
  Clock() = default;
  /// Wrap an existing clock value (e.g., module argument)
  explicit Clock(mlir::Value value) : value_(value) {}

  mlir::Value getValue() const { return value_; }

private:
  mlir::Value value_;
};

/// Reset signal
class Reset : public UInt {
public:
  Reset() = default;
  /// Wrap an existing reset value (e.g., module argument)
  explicit Reset(mlir::Value value, mlir::OpBuilder *builder,
                mlir::Location loc)
      : UInt(value, builder, loc) {}
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_SIGNAL_H
