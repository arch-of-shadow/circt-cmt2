//===- Cmt2Dialect.cpp - Implement the FIRRTL operations -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implement the Cmt2 Dialect.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace circt;
using namespace cmt2;
// StringRef InstanceOp::instanceName() { return getName(); }

void Cmt2Dialect::initialize() {
  // Register attributes.
  registerAttributes();

  // Register types.
  registerTypes();

  // Register operations.
  addOperations<
#define GET_OP_LIST
#include "circt/Dialect/Cmt2/Cmt2.cpp.inc"
      >();
}

#include "circt/Dialect/Cmt2/Cmt2Dialect.cpp.inc"