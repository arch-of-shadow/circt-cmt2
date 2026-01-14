//===- Cmt2Types.cpp - Cmt2 Dialect Types Implementation ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Cmt2 dialect custom types.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// SyncTokenType
//===----------------------------------------------------------------------===//

LogicalResult SyncTokenType::verify(
    function_ref<InFlightDiagnostic()> emitError, Type dataType,
    TokenMode mode) {
  // Data type is optional, so nullptr is valid
  // Mode is always valid (enum)
  return success();
}

//===----------------------------------------------------------------------===//
// TableGen generated type definitions
//===----------------------------------------------------------------------===//

#define GET_TYPEDEF_CLASSES
#include "circt/Dialect/Cmt2/Cmt2Types.cpp.inc"

void Cmt2Dialect::registerTypes() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "circt/Dialect/Cmt2/Cmt2Types.cpp.inc"
      >();
}
