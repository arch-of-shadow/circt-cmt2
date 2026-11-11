//===- Cmt2Attributes.cpp - Cmt2 Dialect Attributes -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Cmt2 dialect attributes for cycle-precise timing.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// Attribute Verification
//===----------------------------------------------------------------------===//

LogicalResult TimingIntervalAttr::verify(
    ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError,
    int64_t start, int64_t end) {
  if (start < 0)
    return emitError() << "timing interval start must be non-negative, got "
                       << start;
  if (end <= start)
    return emitError() << "timing interval end (" << end
                       << ") must be greater than start (" << start << ")";
  return success();
}

LogicalResult
LatencyAttr::verify(::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError,
                    int64_t cycles) {
  if (cycles < 0)
    return emitError() << "latency must be non-negative, got " << cycles;
  return success();
}

LogicalResult IntervalAttr::verify(
    ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError,
    int64_t cycles) {
  if (cycles <= 0)
    return emitError() << "initiation interval must be positive, got "
                       << cycles;
  return success();
}

//===----------------------------------------------------------------------===//
// Enum Definitions
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Enums.cpp.inc"

//===----------------------------------------------------------------------===//
// ODS Boilerplate
//===----------------------------------------------------------------------===//

#define GET_ATTRDEF_CLASSES
#include "circt/Dialect/Cmt2/Cmt2Attributes.cpp.inc"

void Cmt2Dialect::registerAttributes() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "circt/Dialect/Cmt2/Cmt2Attributes.cpp.inc"
      >();
}
