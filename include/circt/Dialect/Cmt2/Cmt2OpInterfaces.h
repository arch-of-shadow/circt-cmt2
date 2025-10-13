//===- Cmt2OpInterfaces.h - Declare Cmt2 op interfaces ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the operation interfaces for the Cmt2 IR and supporting
// types.
//
//===----------------------------------------------------------------------===//
#ifndef CIRCT_DIALECT_Cmt2_Cmt2OPINTERFAES_H
#define CIRCT_DIALECT_Cmt2_Cmt2OPINTERFAES_H

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"

// Forward declarations
namespace circt {
namespace cmt2 {
class MethodOp;
class ValueOp;
class RuleOp;
enum class CallType;
enum class FunctionKind;
} // namespace cmt2
} // namespace circt

#include "circt/Dialect/Cmt2/Cmt2OpInterfaces.h.inc"

namespace circt {
namespace cmt2 {

/// Utility functions for working with Cmt2 operations

/// Check if an operation is a MethodOp
bool isMethodOp(mlir::Operation *op);

/// Check if an operation is a ValueOp
bool isValueOp(mlir::Operation *op);

/// Check if an operation is a RuleOp
bool isRuleOp(mlir::Operation *op);

/// Determine the CallType of an operation
/// Returns CallType::MethodCall if it's a MethodOp, CallType::ValueCall if it's a ValueOp
/// Requires that the operation is either a MethodOp or ValueOp
CallType getCalleeType(mlir::Operation *op);

/// Get the FunctionKind of an operation
/// Returns FunctionKind::Rule if it's a RuleOp,
///         FunctionKind::Method if it's a MethodOp or BindMethodOp,
///         FunctionKind::Value if it's a ValueOp or BindValueOp
/// Requires that the operation implements the Cmt2FunctionLike interface
FunctionKind getFunctionKind(mlir::Operation *op);

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_Cmt2_Cmt2OPINTERFAES_H