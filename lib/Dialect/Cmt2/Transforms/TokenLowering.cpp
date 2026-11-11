//===- TokenLowering.cpp - Lower tokens to hardware -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers abstract token operations by annotating them with hardware
// implementation info (shift register vs FIFO, depths, etc.). The actual
// hardware instantiation is done in cmt2-to-firrtl.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/FIFODepthAnalysis.h"
#include "circt/Dialect/Cmt2/Analysis/TokenAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-token-lowering"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_TOKENLOWERING
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace circt::cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// TokenLoweringPass Implementation
//===----------------------------------------------------------------------===//

struct TokenLoweringPass
    : public circt::cmt2::impl::TokenLoweringBase<TokenLoweringPass> {
  using TokenLoweringBase::TokenLoweringBase;

  void runOnOperation() override;

private:
  /// Process a single module for token lowering.
  void processModule(cmt2::ModuleOp module);

  /// Annotate a token with its hardware implementation info.
  void annotateToken(Value token, cmt2::ModuleOp module);

  /// Annotate token operations with lowering hints.
  void annotateTokenOps(cmt2::ModuleOp module);

  /// Count consumers of a token to detect multi-consumer patterns.
  unsigned countConsumers(Value token);

  /// Map from token value to inferred FIFO depth.
  llvm::DenseMap<Value, unsigned> tokenDepths_;

  /// Token analysis (set during runOnOperation).
  TokenAnalysis *tokenAnalysis_ = nullptr;

  /// FIFO depth analysis (set during runOnOperation).
  FIFODepthAnalysis *fifoAnalysis_ = nullptr;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Token Annotation
//===----------------------------------------------------------------------===//

unsigned TokenLoweringPass::countConsumers(Value token) {
  unsigned count = 0;
  for (OpOperand &use : token.getUses()) {
    (void)use;
    ++count;
  }
  return count;
}

void TokenLoweringPass::annotateToken(Value token, cmt2::ModuleOp module) {
  Operation *producer = token.getDefiningOp();
  if (!producer)
    return;

  auto tokenType = cast<SyncTokenType>(token.getType());
  bool isLI = tokenType.getMode() == TokenMode::LI;
  unsigned numConsumers = countConsumers(token);

  OpBuilder builder(producer->getContext());

  // Annotate the producer with token lowering info
  if (isLI) {
    // Latency-insensitive: will become FIFO
    unsigned depth = defaultFIFODepth;
    auto depthIt = tokenDepths_.find(token);
    if (depthIt != tokenDepths_.end())
      depth = depthIt->second;

    producer->setAttr("token.impl", builder.getStringAttr("fifo"));
    producer->setAttr("token.fifo_depth", builder.getI64IntegerAttr(depth));

    if (numConsumers > 1 && useBroadcastFIFO) {
      producer->setAttr("token.broadcast", builder.getUnitAttr());
      producer->setAttr("token.num_consumers",
                        builder.getI64IntegerAttr(numConsumers));
    }

    LLVM_DEBUG(llvm::dbgs() << "  Token (LI): impl=fifo, depth=" << depth
                            << ", consumers=" << numConsumers << "\n");
  } else {
    // Latency-sensitive: will become shift register or wire
    unsigned depth = 1;

    // Try to infer depth from timing
    if (auto timing = producer->getAttrOfType<TimingIntervalAttr>("timing")) {
      depth = static_cast<unsigned>(timing.getEnd() - timing.getStart());
      if (depth < 1)
        depth = 1;
    }

    producer->setAttr("token.impl", builder.getStringAttr("shiftreg"));
    producer->setAttr("token.shiftreg_depth", builder.getI64IntegerAttr(depth));

    if (numConsumers > 1) {
      producer->setAttr("token.fanout", builder.getUnitAttr());
      producer->setAttr("token.num_consumers",
                        builder.getI64IntegerAttr(numConsumers));
    }

    LLVM_DEBUG(llvm::dbgs() << "  Token (LS): impl=shiftreg, depth=" << depth
                            << ", consumers=" << numConsumers << "\n");
  }
}

void TokenLoweringPass::annotateTokenOps(cmt2::ModuleOp module) {
  // Collect and annotate token operations
  module.walk([&](Operation *op) {
    if (isa<TokenValidOp>(op)) {
      op->setAttr("token.lowered_op", StringAttr::get(op->getContext(), "valid"));
    } else if (isa<TokenDataOp>(op)) {
      op->setAttr("token.lowered_op", StringAttr::get(op->getContext(), "data"));
    } else if (isa<TokenCreateOp>(op)) {
      op->setAttr("token.lowered_op", StringAttr::get(op->getContext(), "create"));
    } else if (isa<TokenJoinOp>(op)) {
      op->setAttr("token.lowered_op", StringAttr::get(op->getContext(), "join"));
    }
  });

  // Mark module as having tokens analyzed for lowering
  module->setAttr("tokens.lowered", UnitAttr::get(module.getContext()));
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void TokenLoweringPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << " for token lowering\n");

  // Skip if already lowered
  if (module->hasAttr("tokens.lowered"))
    return;

  // Analyze tokens in this module
  const TokenGraph &graph = tokenAnalysis_->getTokenGraph(module);

  // Infer FIFO depths for LI tokens
  for (Value token : graph.getLatencyInsensitiveTokens()) {
    FIFODepthInfo depthInfo = fifoAnalysis_->inferDepth(token);
    tokenDepths_[token] = depthInfo.minDepth;
    LLVM_DEBUG(llvm::dbgs() << "  Inferred LI token depth=" << depthInfo.minDepth
                            << "\n");
  }

  // Annotate each token with lowering info
  for (const TokenInfo &info : graph.getTokens()) {
    annotateToken(info.token, module);
  }

  // Annotate token operations
  annotateTokenOps(module);

  LLVM_DEBUG(llvm::dbgs() << "  Annotated " << graph.getTokens().size()
                          << " tokens\n");
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void TokenLoweringPass::runOnOperation() {
  cmt2::CircuitOp circuit = getOperation();

  // Create analyses (local to this invocation)
  TokenAnalysis tokenAnalysis(circuit);
  FIFODepthAnalysis fifoAnalysis(circuit);

  tokenAnalysis_ = &tokenAnalysis;
  fifoAnalysis_ = &fifoAnalysis;

  // Process each module
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }

  // Clear state for next invocation
  tokenDepths_.clear();
  tokenAnalysis_ = nullptr;
  fifoAnalysis_ = nullptr;
}
