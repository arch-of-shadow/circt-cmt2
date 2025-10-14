//===- InstanceGraph.cpp - Instance graph for Cmt2 -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/InstanceGraph.h"
#include "mlir/IR/BuiltinOps.h"

using namespace circt;
using namespace cmt2;

InstanceGraph::InstanceGraph(Operation *operation)
    : igraph::InstanceGraph(cast<CircuitOp>(operation)) {
  // For Cmt2, we don't have an explicit top-level module.
  // The topLevelNode remains nullptr, and users should use
  // getInferredTopLevelNodes() to get modules with no uses.
}
