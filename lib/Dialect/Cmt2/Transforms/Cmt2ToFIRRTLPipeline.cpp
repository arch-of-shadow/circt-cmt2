//===- Cmt2ToFIRRTLPipeline.cpp - Cmt2 to FIRRTL pipeline setup  -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the pipeline setup for converting Cmt2 to FIRRTL.
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/Cmt2ToFIRRTL.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"

using namespace circt;
using namespace cmt2;

void circt::cmt2::populateCmt2ToFIRRTLPipeline(mlir::OpPassManager &pm) {
  // Step 1: Inline all private functions (functions called via @this)
  pm.addPass(createInlinePrivateFuncs());

  // Step 2: Verify that all private functions have been inlined
  pm.addPass(createVerifyPrivateFuncsInlined());

  // Step 3: Verify that call sequences respect conflict matrix constraints
  pm.addPass(createVerifyCallSequence());

  // Step 4: Convert Cmt2 to FIRRTL
  pm.addPass(circt::createLowerCmt2ToFIRRTLPass());
}
