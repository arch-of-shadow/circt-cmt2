//===- Cmt2OpInterfaces.cpp - Implement the Cmt2 op interfaces --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implement the Cmt2 operation interfaces.
//
//===----------------------------------------------------------------------===//
#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/StringRef.h"

using namespace mlir;
using namespace llvm;
using namespace circt::cmt2;
#include "circt/Dialect/Cmt2/Cmt2OpInterfaces.cpp.inc"