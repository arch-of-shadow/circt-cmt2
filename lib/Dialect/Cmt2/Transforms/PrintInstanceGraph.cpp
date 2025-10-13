//===- PrintInstanceGraph.cpp - Print the instance graph --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// Print the Cmt2 module hierarchy.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2InstanceGraph.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_PRINTINSTANCEGRAPH
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;

namespace {
struct PrintInstanceGraphPass
    : public circt::cmt2::impl::PrintInstanceGraphBase<PrintInstanceGraphPass> {
  PrintInstanceGraphPass() : os(llvm::errs()) {}
  void runOnOperation() override {
    InstanceGraph &instanceGraph = getAnalysis<InstanceGraph>();
    llvm::WriteGraph(os, &instanceGraph, /*ShortNames=*/false);
    markAllAnalysesPreserved();
  }
  raw_ostream &os;
};
} // end anonymous namespace
