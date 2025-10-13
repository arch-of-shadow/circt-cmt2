//===- Cmt2Ops.h - Cmt2 Dialect Operators -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cmt2 dialect operators.
//
//===----------------------------------------------------------------------===//
#ifndef CIRCT_DIALECT_Cmt2_Cmt2OPS_H
#define CIRCT_DIALECT_Cmt2_Cmt2OPS_H

// #include "llvm/ADT/Any.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/Cmt2/Cmt2OpInterfaces.h"
#include "circt/Dialect/HW/HWAttributes.h"
#include "circt/Dialect/HW/HWOpInterfaces.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Support/InstanceGraphInterface.h"

// provides implementations for FunctionInterface.td
#include "mlir/Interfaces/FunctionInterfaces.h"
// LogicalResult
#include "mlir/IR/Diagnostics.h"
// provides implementations for OpAsmInterface.td
#include "mlir/IR/OpImplementation.h"
// provides implementations for SymbolInterfaces.td
#include "mlir/IR/SymbolTable.h"
// provides implementations for ControlFlowInterfaces.td
#include "mlir/IR/Dialect.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#define GET_OP_CLASSES
#include "circt/Dialect/Cmt2/Cmt2.h.inc"
namespace circt {
namespace cmt2 {

/// Enumeration for the kind of function (Rule, Method, or Value)
enum class FunctionKind {
  Rule,   // RuleOp
  Method, // MethodOp or BindMethodOp
  Value   // ValueOp or BindValueOp
};


} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_Cmt2_Cmt2OPS_H