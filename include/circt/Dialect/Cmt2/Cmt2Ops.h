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
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
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

//===----------------------------------------------------------------------===//
// Interface-related Helper Functions
//===----------------------------------------------------------------------===//

/// Get all InterfaceDefOp operations in a module
llvm::SmallVector<InterfaceDefOp, 4> getInterfaceDefs(ModuleOp module);

/// Get all InterfaceDeclOp operations in a module
llvm::SmallVector<InterfaceDeclOp, 4> getInterfaceDecls(ModuleOp module);

/// Look up an InterfaceDefOp by symbol name in a module
InterfaceDefOp lookupInterfaceDef(ModuleOp module, mlir::StringRef name);

/// Look up an InterfaceDeclOp by symbol name in a module
InterfaceDeclOp lookupInterfaceDecl(ModuleOp module, mlir::StringRef name);

/// Look up an InterfaceOp by symbol name in a circuit
InterfaceOp lookupInterface(CircuitOp circuit, mlir::StringRef name);

/// Get the InterfaceOp that a decl refers to
InterfaceOp getInterfaceForDecl(InterfaceDeclOp decl);

/// Get the InterfaceOp that a def refers to
InterfaceOp getInterfaceForDef(InterfaceDefOp def);

/// Resolve an interface call to the actual instance and method/value
/// Returns a pair of (instance symbol, method/value symbol) or (nullptr, nullptr) if not found
std::pair<mlir::SymbolRefAttr, mlir::SymbolRefAttr>
resolveInterfaceCall(ModuleOp module, mlir::SymbolRefAttr interfaceDefName,
                     mlir::SymbolRefAttr interfaceMethodName);

/// Check if a CallOp is calling through an interface (i.e., callee is an InterfaceDefOp)
bool isInterfaceCall(CallOp callOp);

/// Get interface bindings for an instance
/// Returns a map from interface decl name to interface def name
llvm::DenseMap<mlir::StringAttr, mlir::StringAttr>
getInterfaceBindings(InstanceOp instance);

/// Get all function-like operations (RuleOp, MethodOp, ValueOp) in a module
llvm::SmallVector<Cmt2FunctionLike, 4> getFunctions(ModuleOp module);

/// Get all instances in a module
llvm::SmallVector<InstanceOp, 4> getInstances(ModuleOp module);

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_Cmt2_Cmt2OPS_H