//===- PrintScheduler.cpp - Print Scheduler Analysis ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass prints the scheduler analysis results for Cmt2 modules.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/Scheduler.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/raw_ostream.h"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_PRINTSCHEDULER
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

struct PrintSchedulerPass
    : public circt::cmt2::impl::PrintSchedulerBase<PrintSchedulerPass> {
  void runOnOperation() override {
    CircuitOp circuit = getOperation();

    // Run the scheduler analysis
    SchedulerAnalysis scheduler(circuit);

    // Print the results
    llvm::outs() << "\n";
    scheduler.print(llvm::outs());
    llvm::outs() << "\n";
  }
};

} // end anonymous namespace
