//===- PrintConflictMatrix.cpp - Print Cmt2 Conflict Matrix ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a pass to print the conflict matrix for Cmt2 modules.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2ConflictMatrix.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/raw_ostream.h"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_PRINTCONFLICTMATRIX
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

struct PrintConflictMatrixPass
    : public circt::cmt2::impl::PrintConflictMatrixBase<
          PrintConflictMatrixPass> {
  void runOnOperation() override {
    CircuitOp circuit = getOperation();

    // Run the conflict matrix analysis
    ConflictMatrixAnalysis analysis(circuit);

    // Print the results
    llvm::outs() << "\n";
    analysis.print(llvm::outs());
    llvm::outs() << "\n";
  }
};

} // end anonymous namespace
