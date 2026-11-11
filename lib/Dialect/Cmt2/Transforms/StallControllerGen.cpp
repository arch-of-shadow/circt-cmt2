//===- StallControllerGen.cpp - Generate stall controllers ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass analyzes mixed LS/LI regions and generates stall controller
// annotations for hardware generation.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/TokenAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-stall-controller-gen"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_STALLCONTROLLERGEN
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace circt::cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// LSRegion - Represents a latency-sensitive region
//===----------------------------------------------------------------------===//

/// Represents a contiguous latency-sensitive region with LI boundaries.
struct LSRegion {
  /// Operations in this LS region
  llvm::SetVector<Operation *> operations;

  /// LI tokens at entry (consumed by LS operations)
  llvm::SmallVector<Value, 4> liInputTokens;

  /// LI tokens at exit (produced by LS operations, consumed by LI)
  llvm::SmallVector<Value, 4> liOutputTokens;

  /// LS tokens internal to this region
  llvm::SmallVector<Value, 8> lsTokens;

  /// Whether this region needs a stall controller
  bool needsStallController() const {
    return !liInputTokens.empty() || !liOutputTokens.empty();
  }

  /// Get unique identifier for this region (based on first op)
  StringRef getIdentifier() const {
    if (operations.empty())
      return "";
    if (auto named = dyn_cast<SymbolOpInterface>(operations.front()))
      return named.getName();
    return "";
  }
};

//===----------------------------------------------------------------------===//
// StallControllerGenPass Implementation
//===----------------------------------------------------------------------===//

struct StallControllerGenPass
    : public circt::cmt2::impl::StallControllerGenBase<StallControllerGenPass> {
  using StallControllerGenBase::StallControllerGenBase;

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Find LS regions in a module.
  llvm::SmallVector<LSRegion, 4> findLSRegions(cmt2::ModuleOp module,
                                                 const TokenGraph &graph);

  /// Annotate a region with stall controller info.
  void annotateRegion(const LSRegion &region, cmt2::ModuleOp module);

  /// Check if an operation is part of an LS pipeline (produces/consumes LS tokens).
  bool isLSOperation(Operation *op, const TokenGraph &graph);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// LS Region Analysis
//===----------------------------------------------------------------------===//

bool StallControllerGenPass::isLSOperation(Operation *op,
                                            const TokenGraph &graph) {
  // Check if any produced token is LS
  auto producedTokens = graph.getProducedTokens(op);
  for (Value token : producedTokens) {
    if (isTokenModeLS(token.getType()))
      return true;
  }

  // Check if any consumed token is LS
  auto consumedTokens = graph.getConsumedTokens(op);
  for (Value token : consumedTokens) {
    if (isTokenModeLS(token.getType()))
      return true;
  }

  return false;
}

llvm::SmallVector<LSRegion, 4>
StallControllerGenPass::findLSRegions(cmt2::ModuleOp module,
                                       const TokenGraph &graph) {
  llvm::SmallVector<LSRegion, 4> regions;

  // For now, treat the entire module as one potential LS region
  // A more sophisticated analysis would identify connected LS subgraphs
  LSRegion region;

  for (const TokenInfo &info : graph.getTokens()) {
    if (!info.producer)
      continue;

    if (info.isLatencyInsensitive) {
      // LI token - check if it's at a boundary
      bool hasLSProducer = isLSOperation(info.producer, graph);
      bool hasLSConsumer = false;
      for (Operation *consumer : info.consumers) {
        if (isLSOperation(consumer, graph)) {
          hasLSConsumer = true;
          break;
        }
      }

      if (hasLSProducer && !hasLSConsumer) {
        // LS produces, LI consumes - this is an LS output boundary
        region.liOutputTokens.push_back(info.token);
      } else if (!hasLSProducer && hasLSConsumer) {
        // LI produces, LS consumes - this is an LS input boundary
        region.liInputTokens.push_back(info.token);
      }
    } else {
      // LS token - track it
      region.lsTokens.push_back(info.token);
      if (info.producer)
        region.operations.insert(info.producer);
      for (Operation *consumer : info.consumers)
        region.operations.insert(consumer);
    }
  }

  if (region.needsStallController()) {
    regions.push_back(std::move(region));
    LLVM_DEBUG(llvm::dbgs() << "  Found LS region with "
                            << region.liInputTokens.size() << " LI inputs, "
                            << region.liOutputTokens.size() << " LI outputs, "
                            << region.lsTokens.size() << " LS tokens\n");
  }

  return regions;
}

void StallControllerGenPass::annotateRegion(const LSRegion &region,
                                             cmt2::ModuleOp module) {
  OpBuilder builder(module.getContext());

  // Count LI boundaries
  unsigned numLIInputs = region.liInputTokens.size();
  unsigned numLIOutputs = region.liOutputTokens.size();
  unsigned numLIBoundaries = numLIInputs + numLIOutputs;

  // Create annotation on the module
  module->setAttr("stall.controller", builder.getUnitAttr());
  module->setAttr("stall.num_li_inputs",
                  builder.getI64IntegerAttr(numLIInputs));
  module->setAttr("stall.num_li_outputs",
                  builder.getI64IntegerAttr(numLIOutputs));
  module->setAttr("stall.num_boundaries",
                  builder.getI64IntegerAttr(numLIBoundaries));

  // Track which operations need stall gating
  SmallVector<Attribute> gatedOps;
  for (Operation *op : region.operations) {
    if (auto named = dyn_cast<SymbolOpInterface>(op)) {
      gatedOps.push_back(FlatSymbolRefAttr::get(module.getContext(),
                                                 named.getName()));
    }
  }
  if (!gatedOps.empty())
    module->setAttr("stall.gated_ops", builder.getArrayAttr(gatedOps));

  LLVM_DEBUG(llvm::dbgs() << "  Annotated module @" << module.getSymName()
                          << " with stall controller: "
                          << numLIInputs << " inputs, "
                          << numLIOutputs << " outputs\n");
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void StallControllerGenPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << " for stall controller generation\n");

  // Skip if no tokens have been lowered
  if (!module->hasAttr("tokens.lowered"))
    return;

  // Build token analysis
  auto circuit = module->getParentOfType<cmt2::CircuitOp>();
  TokenAnalysis analysis(circuit);
  const TokenGraph &graph = analysis.getTokenGraph(module);

  // Find LS regions with LI boundaries
  auto regions = findLSRegions(module, graph);

  // Annotate each region
  for (const LSRegion &region : regions) {
    annotateRegion(region, module);
  }
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void StallControllerGenPass::runOnOperation() {
  cmt2::CircuitOp circuit = getOperation();

  // Process each module
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
