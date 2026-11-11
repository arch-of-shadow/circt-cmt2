//===- StaticPromotion.cpp - Promote dynamic control to static -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the StaticPromotion pass for the Cmt2 dialect.
// It converts dynamic procedural control to static when heuristics are met.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-static-promotion"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_STATICPROMOTION
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// StaticPromotion Pass Implementation
//===----------------------------------------------------------------------===//

struct StaticPromotionPass
    : public circt::cmt2::impl::StaticPromotionBase<StaticPromotionPass> {
  using StaticPromotionBase::StaticPromotionBase;

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Check if an operation should be promoted based on heuristics.
  bool shouldPromote(Operation *op);

  /// Get approximate size of control (for heuristic).
  unsigned approxSize(Operation *op);

  /// Get latency from inferred attribute.
  std::optional<uint64_t> getInferredLatency(Operation *op);

  /// Promote a while loop to static_repeat.
  void promoteWhileToStaticRepeat(ProcWhileOp whileOp, OpBuilder &builder);

  /// Promote an if to static_if.
  void promoteIfToStaticIf(ProcIfOp ifOp, OpBuilder &builder);

  /// Check if all children of a control op are promotable.
  bool allChildrenPromotable(Region &region);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Heuristics
//===----------------------------------------------------------------------===//

std::optional<uint64_t> StaticPromotionPass::getInferredLatency(Operation *op) {
  if (auto latAttr = op->getAttrOfType<IntegerAttr>("inferred_latency"))
    return latAttr.getInt();
  return std::nullopt;
}

unsigned StaticPromotionPass::approxSize(Operation *op) {
  // Approximate "size" in terms of FSM transitions needed
  return llvm::TypeSwitch<Operation *, unsigned>(op)
      .Case<ProcEnableOp>([](ProcEnableOp) { return 1; })
      .Case<ProcSeqOp>([&](ProcSeqOp seq) {
        unsigned size = 0;
        for (Operation &stmt : seq.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            size += approxSize(&stmt);
        }
        return size;
      })
      .Case<ProcParOp>([&](ProcParOp par) {
        unsigned size = 0;
        for (Operation &stmt : par.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            size = std::max(size, approxSize(&stmt));
        }
        return size;
      })
      .Case<ProcIfOp>([&](ProcIfOp ifOp) {
        unsigned thenSize = 0;
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            thenSize += approxSize(&stmt);
        }
        unsigned elseSize = 0;
        if (!ifOp.getElseRegion().empty()) {
          for (Operation &stmt : ifOp.getElseRegion().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              elseSize += approxSize(&stmt);
          }
        }
        return std::max(thenSize, elseSize) + 3; // +3 for if overhead
      })
      .Case<ProcWhileOp>([&](ProcWhileOp whileOp) {
        unsigned bodySize = 0;
        for (Operation &stmt : whileOp.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            bodySize += approxSize(&stmt);
        }
        return bodySize + 3; // +3 for loop overhead
      })
      .Case<ProcStaticRepeatOp>([&](ProcStaticRepeatOp repeatOp) {
        unsigned bodySize = 0;
        for (Operation &stmt : repeatOp.getBody().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            bodySize += approxSize(&stmt);
        }
        return bodySize * repeatOp.getCount();
      })
      .Case<ProcStaticIfOp>([&](ProcStaticIfOp staticIf) {
        unsigned thenSize = 0;
        for (Operation &stmt : staticIf.getThenRegion().front()) {
          if (!isa<ProcControlEndOp>(stmt))
            thenSize += approxSize(&stmt);
        }
        unsigned elseSize = 0;
        if (!staticIf.getElseRegion().empty()) {
          for (Operation &stmt : staticIf.getElseRegion().front()) {
            if (!isa<ProcControlEndOp>(stmt))
              elseSize += approxSize(&stmt);
        }
        }
        return std::max(thenSize, elseSize);
      })
      .Default([](Operation *) { return 1; });
}

bool StaticPromotionPass::shouldPromote(Operation *op) {
  // Must be marked as promotable by StaticInference
  if (!op->hasAttr("promotable"))
    return false;

  auto latency = getInferredLatency(op);
  if (!latency)
    return false;

  // Check cycle limit
  if (*latency > cycleLimit) {
    LLVM_DEBUG(llvm::dbgs() << "  Skipping: latency " << *latency
                            << " exceeds limit " << cycleLimit << "\n");
    return false;
  }

  // Check size threshold
  unsigned size = approxSize(op);
  if (size < threshold) {
    LLVM_DEBUG(llvm::dbgs() << "  Skipping: size " << size
                            << " below threshold " << threshold << "\n");
    return false;
  }

  // Special handling for if: check branch difference
  if (auto ifOp = dyn_cast<ProcIfOp>(op)) {
    uint64_t thenLatency = 0;
    for (Operation &stmt : ifOp.getThenRegion().front()) {
      if (auto lat = getInferredLatency(&stmt))
        thenLatency += *lat;
    }

    uint64_t elseLatency = 0;
    if (!ifOp.getElseRegion().empty()) {
      for (Operation &stmt : ifOp.getElseRegion().front()) {
        if (auto lat = getInferredLatency(&stmt))
          elseLatency += *lat;
      }
    }

    uint64_t diff =
        thenLatency > elseLatency ? thenLatency - elseLatency
                                   : elseLatency - thenLatency;
    if (diff > ifDiffLimit) {
      LLVM_DEBUG(llvm::dbgs() << "  Skipping if: branch diff " << diff
                              << " exceeds limit " << ifDiffLimit << "\n");
      return false;
    }
  }

  return true;
}

bool StaticPromotionPass::allChildrenPromotable(Region &region) {
  for (Operation &op : region.front()) {
    if (isa<ProcControlEndOp>(op))
      continue;
    if (!op.hasAttr("promotable"))
      return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Promotion Transformations
//===----------------------------------------------------------------------===//

void StaticPromotionPass::promoteWhileToStaticRepeat(ProcWhileOp whileOp,
                                                      OpBuilder &builder) {
  // Get bound attribute
  auto boundAttr = whileOp->getAttrOfType<IntegerAttr>("bound");
  if (!boundAttr)
    return;

  uint64_t bound = boundAttr.getInt();

  // Compute body latency
  uint64_t bodyLatency = 0;
  for (Operation &stmt : whileOp.getBody().front()) {
    if (auto lat = getInferredLatency(&stmt))
      bodyLatency += *lat;
  }

  LLVM_DEBUG(llvm::dbgs() << "  Promoting while to static_repeat(" << bound
                          << ") with body_latency=" << bodyLatency << "\n");

  // Create static_repeat
  builder.setInsertionPoint(whileOp);
  auto repeatOp = builder.create<ProcStaticRepeatOp>(
      whileOp.getLoc(), builder.getI64IntegerAttr(bound),
      bodyLatency > 0 ? builder.getI64IntegerAttr(bodyLatency)
                      : IntegerAttr());

  // Move body operations
  IRMapping mapping;
  Block *newBlock = &repeatOp.getBody().front();
  builder.setInsertionPointToStart(newBlock);

  for (Operation &op : whileOp.getBody().front()) {
    if (!isa<ProcControlEndOp>(op)) {
      builder.clone(op, mapping);
    }
  }

  // Copy promotable attributes to new ops
  repeatOp->setAttr("promotable", builder.getUnitAttr());
  repeatOp->setAttr("inferred_latency",
                    builder.getI64IntegerAttr(bound * bodyLatency));

  // Replace old while
  whileOp.erase();
}

void StaticPromotionPass::promoteIfToStaticIf(ProcIfOp ifOp,
                                               OpBuilder &builder) {
  // Compute branch latencies
  uint64_t thenLatency = 0;
  for (Operation &stmt : ifOp.getThenRegion().front()) {
    if (auto lat = getInferredLatency(&stmt))
      thenLatency += *lat;
  }

  uint64_t elseLatency = 0;
  if (!ifOp.getElseRegion().empty()) {
    for (Operation &stmt : ifOp.getElseRegion().front()) {
      if (auto lat = getInferredLatency(&stmt))
        elseLatency += *lat;
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "  Promoting if to static_if with latencies ("
                          << thenLatency << ", " << elseLatency << ")\n");

  // Create static_if
  builder.setInsertionPoint(ifOp);
  auto staticIfOp = builder.create<ProcStaticIfOp>(
      ifOp.getLoc(), ifOp.getCond(),
      builder.getI64IntegerAttr(thenLatency),
      builder.getI64IntegerAttr(elseLatency));

  // Move then branch operations
  IRMapping mapping;
  Block *thenBlock = &staticIfOp.getThenRegion().front();
  builder.setInsertionPointToStart(thenBlock);

  for (Operation &op : ifOp.getThenRegion().front()) {
    if (!isa<ProcControlEndOp>(op)) {
      builder.clone(op, mapping);
    }
  }

  // Move else branch operations (if present)
  if (!ifOp.getElseRegion().empty()) {
    Block *elseBlock = new Block();
    staticIfOp.getElseRegion().push_back(elseBlock);
    builder.setInsertionPointToStart(elseBlock);

    for (Operation &op : ifOp.getElseRegion().front()) {
      if (!isa<ProcControlEndOp>(op)) {
        builder.clone(op, mapping);
      }
    }
  }

  // Copy attributes
  staticIfOp->setAttr("promotable", builder.getUnitAttr());
  staticIfOp->setAttr("inferred_latency",
                      builder.getI64IntegerAttr(std::max(thenLatency, elseLatency)));

  // Replace old if
  ifOp.erase();
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void StaticPromotionPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  OpBuilder builder(module.getContext());

  // Collect operations to promote (can't modify while iterating)
  SmallVector<ProcWhileOp> whileOps;
  SmallVector<ProcIfOp> ifOps;

  module.walk([&](Operation *op) {
    if (auto whileOp = dyn_cast<ProcWhileOp>(op)) {
      if (shouldPromote(whileOp))
        whileOps.push_back(whileOp);
    } else if (auto ifOp = dyn_cast<ProcIfOp>(op)) {
      if (shouldPromote(ifOp))
        ifOps.push_back(ifOp);
    }
  });

  // Promote while loops
  for (auto whileOp : whileOps) {
    promoteWhileToStaticRepeat(whileOp, builder);
  }

  // Promote if statements
  for (auto ifOp : ifOps) {
    promoteIfToStaticIf(ifOp, builder);
  }

  // Mark proc rules/methods as promoted if all control is static
  module.walk([&](Operation *op) {
    Region *controlRegion = nullptr;
    if (auto rule = dyn_cast<ProcRuleOp>(op)) {
      controlRegion = &rule.getControl();
    } else if (auto method = dyn_cast<ProcMethodOp>(op)) {
      controlRegion = &method.getControl();
    }

    if (!controlRegion || controlRegion->empty())
      return;

    // Check if all control is now static
    bool allStatic = true;
    controlRegion->walk([&](Operation *child) {
      if (isa<ProcWhileOp, ProcIfOp>(child)) {
        allStatic = false;
      }
    });

    if (allStatic && op->hasAttr("promotable")) {
      op->setAttr("promoted", builder.getUnitAttr());
      LLVM_DEBUG(llvm::dbgs() << "  Marked as promoted\n");
    }
  });
}

void StaticPromotionPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Process each module
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
