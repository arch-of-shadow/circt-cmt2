//===- Cmt2OpInterfaces.cpp - Implement Cmt2 op interfaces ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the operation interfaces for the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2OpInterfaces.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"

using namespace circt;
using namespace cmt2;

#include "circt/Dialect/Cmt2/Cmt2OpInterfaces.cpp.inc"

bool cmt2::isMethodOp(mlir::Operation *op) {
  return llvm::isa<MethodOp>(op) || llvm::isa<BindMethodOp>(op);
}

bool cmt2::isValueOp(mlir::Operation *op) {
  return llvm::isa<ValueOp>(op) || llvm::isa<BindValueOp>(op);
}

bool cmt2::isRuleOp(mlir::Operation *op) {
  return llvm::isa<RuleOp>(op);
}

CallType cmt2::getCalleeType(mlir::Operation *op) {
  if (llvm::isa<MethodOp>(op) || llvm::isa<BindMethodOp>(op)) {
    return CallType::MethodCall;
  } else if (llvm::isa<ValueOp>(op) || llvm::isa<BindValueOp>(op)) {
    return CallType::ValueCall;
  }
  // This should not happen if the operation is valid
  return CallType::MethodCall;  // Default fallback
}

FunctionKind cmt2::getFunctionKind(mlir::Operation *op) {
  if (llvm::isa<RuleOp>(op)) {
    return FunctionKind::Rule;
  } else if (llvm::isa<MethodOp>(op) || llvm::isa<BindMethodOp>(op)) {
    return FunctionKind::Method;
  } else if (llvm::isa<ValueOp>(op) || llvm::isa<BindValueOp>(op)) {
    return FunctionKind::Value;
  }
  // This should not happen if the operation is valid
  return FunctionKind::Method;  // Default fallback
}
