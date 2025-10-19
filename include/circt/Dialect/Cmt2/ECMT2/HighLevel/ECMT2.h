//===- ECMT2.h - Convenience Header for ECMT2 High-Level API ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header includes all commonly used ECMT2 high-level API headers.
// Include this single header to access the full high-level API.
//
// Usage:
//   #include "circt/Dialect/Cmt2/ECMT2/HighLevel/ECMT2.h"
//   using namespace circt::cmt2::ecmt2::highlevel;
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_ECMT2_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_ECMT2_H

// Core high-level API
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Input.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/InterfaceWrapper.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Registry.h"

// Helper functions
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Helpers.h"

// Required dialects
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"

// MLIR infrastructure
#include "mlir/IR/MLIRContext.h"

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_ECMT2_H
