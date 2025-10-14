//===- InstanceGraph.h - Instance graph for Cmt2 --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the InstanceGraph analysis for the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_TRANSFORMS_INSTANCEGRAPH_H
#define CIRCT_DIALECT_CMT2_TRANSFORMS_INSTANCEGRAPH_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Support/InstanceGraph.h"
#include "circt/Support/LLVM.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator.h"

namespace circt {
namespace cmt2 {
using InstanceRecord = igraph::InstanceRecord;
using InstanceGraphNode = igraph::InstanceGraphNode;
using InstancePathCache = igraph::InstancePathCache;

/// This graph tracks modules and where they are instantiated. This is intended
/// to be used as a cached analysis on Cmt2 circuits.  This class can be used
/// to walk the modules efficiently in a bottom-up or top-down order.
///
/// To use this class, retrieve a cached copy from the analysis manager:
///   auto &instanceGraph = getAnalysis<InstanceGraph>(getOperation());
class InstanceGraph : public igraph::InstanceGraph {
public:
  /// Create a new module graph of a circuit.  This must be called on a Cmt2
  /// CircuitOp.
  explicit InstanceGraph(Operation *operation);

  /// Get the node corresponding to the top-level module of a circuit.
  /// For Cmt2, we don't have an explicit top-level module concept, so we
  /// return nullptr. Users should use getInferredTopLevelNodes() instead.
  igraph::InstanceGraphNode *getTopLevelNode() override { return topLevelNode; }

private:
  InstanceGraphNode *topLevelNode = nullptr;
};

} // namespace cmt2
} // namespace circt

template <>
struct llvm::GraphTraits<circt::cmt2::InstanceGraph *>
    : public llvm::GraphTraits<circt::igraph::InstanceGraph *> {};

template <>
struct llvm::DOTGraphTraits<circt::cmt2::InstanceGraph *>
    : public llvm::DOTGraphTraits<circt::igraph::InstanceGraph *> {
  using llvm::DOTGraphTraits<circt::igraph::InstanceGraph *>::DOTGraphTraits;
};

#endif // CIRCT_DIALECT_CMT2_TRANSFORMS_INSTANCEGRAPH_H
