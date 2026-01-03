//===- Cmt2Attributes.h - Cmt2 Dialect Attributes -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cmt2 dialect custom attributes for cycle-precise
// timing control.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_Cmt2_Cmt2ATTRIBUTES_H
#define CIRCT_DIALECT_Cmt2_Cmt2ATTRIBUTES_H

#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"

// Include generated enum definitions
#include "circt/Dialect/Cmt2/Cmt2Enums.h.inc"

#define GET_ATTRDEF_CLASSES
#include "circt/Dialect/Cmt2/Cmt2Attributes.h.inc"

#endif // CIRCT_DIALECT_Cmt2_Cmt2ATTRIBUTES_H
