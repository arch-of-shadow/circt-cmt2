//===- Cmt2.cpp - C interface for the Cmt2 dialect -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt-c/Dialect/Cmt2.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"

#include "mlir/CAPI/Registration.h"
#include "mlir/Pass/Pass.h"

using namespace circt::cmt2;

MLIR_DEFINE_CAPI_DIALECT_REGISTRATION(Cmt2, cmt2, circt::cmt2::Cmt2Dialect)

void registerCmt2Passes() { circt::cmt2::registerPasses(); }
