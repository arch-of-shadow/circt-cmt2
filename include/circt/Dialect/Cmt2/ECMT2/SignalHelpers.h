//===- SignalHelpers.h - ECMT2 Signal Helper Functions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper functions and utilities for working with ECMT2 Signal, Bundle, and
// FVector types. These helpers provide a more convenient API for creating
// and manipulating complex bundle and vector structures.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_SIGNALHELPERS_H
#define CIRCT_DIALECT_CMT2_ECMT2_SIGNALHELPERS_H

#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace circt {
namespace cmt2 {
namespace ecmt2 {

//===----------------------------------------------------------------------===//
// BundleBuilder - Fluent API for Bundle Creation
//===----------------------------------------------------------------------===//

/// Builder class for easy Bundle creation with fluent API.
/// Provides convenient methods to add fields without verbose element creation.
///
/// Example usage:
///   auto bundle = BundleBuilder(context)
///                     .addUInt("addr", 32)
///                     .addUInt("data", 64)
///                     .addVector("tags", UIntType::get(context, 8), 4)
///                     .build(builder, loc);
class BundleBuilder {
public:
  /// Create a BundleBuilder with the given MLIR context
  BundleBuilder(mlir::MLIRContext *context) : context_(context) {}

  /// Add a UInt field to the bundle
  /// \param name Field name
  /// \param width Bit width of the UInt
  /// \param isFlip Whether the field is flipped (default: false)
  /// \return Reference to this builder for chaining
  BundleBuilder &addUInt(llvm::StringRef name, unsigned width,
                         bool isFlip = false);

  /// Add a SInt field to the bundle
  /// \param name Field name
  /// \param width Bit width of the SInt
  /// \param isFlip Whether the field is flipped (default: false)
  /// \return Reference to this builder for chaining
  BundleBuilder &addSInt(llvm::StringRef name, unsigned width,
                         bool isFlip = false);

  /// Add a vector field to the bundle
  /// \param name Field name
  /// \param elemType Element type of the vector
  /// \param numElems Number of elements in the vector
  /// \param isFlip Whether the field is flipped (default: false)
  /// \return Reference to this builder for chaining
  BundleBuilder &addVector(llvm::StringRef name, mlir::Type elemType,
                           size_t numElems, bool isFlip = false);

  /// Add a custom typed field to the bundle
  /// \param name Field name
  /// \param type FIRRTL type of the field
  /// \param isFlip Whether the field is flipped (default: false)
  /// \return Reference to this builder for chaining
  BundleBuilder &addField(llvm::StringRef name, mlir::Type type,
                          bool isFlip = false);

  /// Build and return the Bundle signal
  /// \param builder MLIR OpBuilder to use for creating operations
  /// \param loc Location information for the created operations
  /// \return The constructed Bundle signal
  Bundle build(mlir::OpBuilder &builder, mlir::Location loc);

  /// Get the bundle elements without building
  /// Useful for creating bundle types for vectors of bundles
  /// \return Array of bundle elements
  llvm::ArrayRef<firrtl::BundleType::BundleElement> getElements() const {
    return elements_;
  }

private:
  mlir::MLIRContext *context_;
  llvm::SmallVector<firrtl::BundleType::BundleElement> elements_;
};

//===----------------------------------------------------------------------===//
// Smart Type Conversion Helpers
//===----------------------------------------------------------------------===//

/// Convert a Signal to a Bundle, wrapping with proper type
/// \param signal The signal to convert
/// \param builder OpBuilder pointer for the bundle
/// \param loc Location for operations
/// \return Bundle wrapping the signal
inline Bundle AsBundle(Signal signal, mlir::OpBuilder *builder,
                       mlir::Location loc) {
  return Bundle(signal.getValue(), builder, loc);
}

/// Convert a Signal to an FVector, wrapping with proper type
/// \param signal The signal to convert
/// \param builder OpBuilder pointer for the vector
/// \param loc Location for operations
/// \return FVector wrapping the signal
inline FVector AsVector(Signal signal, mlir::OpBuilder *builder,
                        mlir::Location loc) {
  return FVector(signal.getValue(), builder, loc);
}

/// Convert a Signal to a UInt, wrapping with proper type
/// \param signal The signal to convert
/// \param builder OpBuilder pointer for the UInt
/// \param loc Location for operations
/// \return UInt wrapping the signal
inline UInt AsUInt(Signal signal, mlir::OpBuilder *builder,
                   mlir::Location loc) {
  return UInt(signal.getValue(), builder, loc);
}

/// Convert a Signal to a SInt, wrapping with proper type
/// \param signal The signal to convert
/// \param builder OpBuilder pointer for the SInt
/// \param loc Location for operations
/// \return SInt wrapping the signal
inline SInt AsSInt(Signal signal, mlir::OpBuilder *builder,
                   mlir::Location loc) {
  return SInt(signal.getValue(), builder, loc);
}

//===----------------------------------------------------------------------===//
// Control Flow Helpers
//===----------------------------------------------------------------------===//

/// Helper class for building if-else control flow operations
/// Provides a fluent API for creating cmt2.if operations with then/else branches
///
/// Example usage:
///   auto result = IfBuilder(condition, builder, loc)
///                     .Then([](OpBuilder& b) {
///                       // then branch operations
///                       return thenValue;
///                     })
///                     .Else([](OpBuilder& b) {
///                       // else branch operations
///                       return elseValue;
///                     })
///                     .build();
class IfBuilder {
public:
  /// Create an IfBuilder with the given condition
  /// \param condition A 1-bit signal representing the condition
  /// \param builder OpBuilder to use for creating operations
  /// \param loc Location for the created operations
  IfBuilder(const Signal &condition, mlir::OpBuilder &builder,
            mlir::Location loc)
      : condition_(condition.getValue()), builder_(builder), loc_(loc) {}

  /// Set the then branch using a lambda/function
  /// \param fn Function that builds the then region, returns a Signal or void
  template <typename Func>
  IfBuilder &Then(Func &&fn) {
    thenFn_ = [fn = std::forward<Func>(fn)](mlir::OpBuilder &b) -> mlir::Value {
      if constexpr (std::is_void_v<std::invoke_result_t<Func, mlir::OpBuilder&>>) {
        fn(b);
        return mlir::Value();
      } else {
        auto result = fn(b);
        if constexpr (std::is_same_v<decltype(result), Signal>) {
          return result.getValue();
        } else {
          return result;
        }
      }
    };
    return *this;
  }

  /// Set the else branch using a lambda/function
  /// \param fn Function that builds the else region, returns a Signal or void
  template <typename Func>
  IfBuilder &Else(Func &&fn) {
    elseFn_ = [fn = std::forward<Func>(fn)](mlir::OpBuilder &b) -> mlir::Value {
      if constexpr (std::is_void_v<std::invoke_result_t<Func, mlir::OpBuilder&>>) {
        fn(b);
        return mlir::Value();
      } else {
        auto result = fn(b);
        if constexpr (std::is_same_v<decltype(result), Signal>) {
          return result.getValue();
        } else {
          return result;
        }
      }
    };
    hasElse_ = true;
    return *this;
  }

  /// Build and return the if operation result as a Signal (if it has a result)
  /// \return Signal wrapping the if operation result, or empty Signal if no results
  Signal build();

private:
  mlir::Value condition_;
  mlir::OpBuilder &builder_;
  mlir::Location loc_;
  std::function<mlir::Value(mlir::OpBuilder&)> thenFn_;
  std::function<mlir::Value(mlir::OpBuilder&)> elseFn_;
  bool hasElse_ = false;
};

/// Simple if-else helper for creating conditional operations with result
/// \param condition 1-bit condition signal
/// \param thenFn Function to build the then branch, returns a Signal
/// \param elseFn Function to build the else branch, returns a Signal
/// \param builder OpBuilder to use
/// \param loc Location for operations
/// \return Signal wrapping the result of the if operation
template <typename ThenFunc, typename ElseFunc>
Signal If(const Signal &condition, ThenFunc &&thenFn, ElseFunc &&elseFn,
          mlir::OpBuilder &builder, mlir::Location loc) {
  return IfBuilder(condition, builder, loc)
      .Then(std::forward<ThenFunc>(thenFn))
      .Else(std::forward<ElseFunc>(elseFn))
      .build();
}

/// Simple if helper without else branch and without result
/// \param condition 1-bit condition signal
/// \param thenFn Function to build the then branch
/// \param builder OpBuilder to use
/// \param loc Location for operations
template <typename ThenFunc>
void If(const Signal &condition, ThenFunc &&thenFn,
        mlir::OpBuilder &builder, mlir::Location loc) {
  IfBuilder(condition, builder, loc)
      .Then(std::forward<ThenFunc>(thenFn))
      .build();
}

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_SIGNALHELPERS_H
