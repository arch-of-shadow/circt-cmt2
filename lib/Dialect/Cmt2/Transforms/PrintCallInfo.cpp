//===- PrintCallInfo.cpp - Print Call Information Pass -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a pass that prints call information for Cmt2 modules.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2CallInfo.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/raw_ostream.h"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_PRINTCALLINFO
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;

namespace {
struct PrintCallInfoPass
    : public circt::cmt2::impl::PrintCallInfoBase<PrintCallInfoPass> {
  PrintCallInfoPass() : os(llvm::errs()) {}

  void runOnOperation() override {
    CircuitOp circuit = getOperation();

    // Build the call info analysis
    CallInfoView callInfo(circuit);

    // Print the call info
    callInfo.print(os);

    // Mark all analyses as preserved since this is a print-only pass
    markAllAnalysesPreserved();
  }

  llvm::raw_ostream &os;
};
} // end anonymous namespace

namespace circt {
namespace cmt2 {
std::unique_ptr<mlir::Pass> createPrintCallInfoPass() {
  return std::make_unique<PrintCallInfoPass>();
}
} // namespace cmt2
} // namespace circt
