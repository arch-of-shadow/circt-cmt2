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
// Cmt2ModuleLike getReferenceModule(InstanceOp instance);
// llvm::SmallVector<InstanceOp, 4> getInstances(Cmt2ModuleLike module);
// llvm::SmallVector<Cmt2FunctionLike, 4> getFunctions(Cmt2ModuleLike module);
// llvm::SmallVector<MethodOp, 4> getMethods(ModuleOp module);
// llvm::SmallVector<BindMethodOp, 4> getMethods(ExtModuleOp module);
// llvm::SmallVector<ValueOp, 4> getValues(ModuleOp module);
// llvm::SmallVector<BindValueOp, 4> getValues(ExtModuleOp module);
// llvm::SmallVector<RuleOp, 4> getRules(ModuleOp module);
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_Cmt2_Cmt2OPS_H