//===- Cmt2.td - Cmt2 dialect definition ---------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the top level file for the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//
#ifndef CIRCT_DIALECT_Cmt2_TD
#define CIRCT_DIALECT_Cmt2_TD

include "mlir/IR/OpBase.td"
include "mlir/IR/OpAsmInterface.td"
// FunctionOpInterface
include "mlir/Interfaces/FunctionInterfaces.td"
// OpInterface
include "mlir/Interfaces/InferTypeOpInterface.td"
// Symbol, SymbolTable
include "mlir/IR/SymbolInterfaces.td"
// ReturnLike
include "mlir/Interfaces/ControlFlowInterfaces.td"
// HWIntegerType
include "circt/Dialect/HW/HWTypes.td"
// HWModuleLike
// include "circt/Dialect/HW/HWOpInterfaces.td"

include "Cmt2OpInterfaces.td"
include "Cmt2Dialect.td"
// include "Cmt2Attributes.td"
// include "Cmt2Types.td"
include "Cmt2Ops.td"

#endif // CIRCT_DIALECT_Cmt2_TD