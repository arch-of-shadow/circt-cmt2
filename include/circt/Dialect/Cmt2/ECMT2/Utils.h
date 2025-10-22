//===- Utils.h - ECMT2 Function-Like Operations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines function-like operations (Rule, Method, Value) for the
// ECMT2 embedded DSL.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_UTILS_H
#define CIRCT_DIALECT_CMT2_ECMT2_UTILS_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/ArrayRef.h"
#include <functional>
#include <memory>

namespace circt {
namespace cmt2 {
namespace ecmt2 {

mlir::FunctionType getFunctionTypeFromBinding(
  cmt2::ExtModuleFirrtlOp extMod, 
  llvm::ArrayRef<std::string> argPorts,
  llvm::ArrayRef<std::string> resPorts,
  OpBuilder &builder
);


} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_UTILS_H
