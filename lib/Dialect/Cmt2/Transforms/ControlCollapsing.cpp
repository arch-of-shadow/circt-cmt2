//===- ControlCollapsing.cpp - Collapse procedural control -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ControlCollapsing pass for the Cmt2 dialect.
// It simplifies procedural control by collapsing redundant structures and
// adjusting timing guards when flattening nested static control.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-control-collapsing"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_CONTROLCOLLAPSING
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Timing Guard Adjustment Helpers
//===----------------------------------------------------------------------===//

/// Adjust a single timing interval by adding an offset.
/// [start, end) -> [start + offset, end + offset)
TimingIntervalAttr adjustTimingInterval(TimingIntervalAttr timing,
                                         int64_t offset, MLIRContext *ctx) {
  return TimingIntervalAttr::get(ctx, timing.getStart() + offset,
                                  timing.getEnd() + offset);
}

/// Adjust all timing attributes in an array by adding an offset.
ArrayAttr adjustTimingArray(ArrayAttr timingArray, int64_t offset,
                            MLIRContext *ctx) {
  if (!timingArray)
    return nullptr;

  SmallVector<Attribute> adjusted;
  for (auto attr : timingArray) {
    if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
      adjusted.push_back(adjustTimingInterval(timing, offset, ctx));
    } else {
      adjusted.push_back(attr);
    }
  }
  return ArrayAttr::get(ctx, adjusted);
}

/// Adjust timing attributes on a call op by adding an offset.
void adjustCallTiming(CallOp call, int64_t offset) {
  auto ctx = call.getContext();

  if (auto argTiming = call.getArgTiming()) {
    call.setArgTimingAttr(adjustTimingArray(*argTiming, offset, ctx));
  }

  if (auto resultTiming = call.getResultTiming()) {
    call.setResultTimingAttr(adjustTimingArray(*resultTiming, offset, ctx));
  }
}

//===----------------------------------------------------------------------===//
// ControlCollapsing Pass Implementation
//===----------------------------------------------------------------------===//

struct ControlCollapsingPass
    : public circt::cmt2::impl::ControlCollapsingBase<ControlCollapsingPass> {

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Flatten nested seq operations.
  bool flattenNestedSeq(ProcSeqOp outerSeq);

  /// Flatten nested par operations.
  bool flattenNestedPar(ProcParOp outerPar);

  /// Flatten static_if inside seq (inline branches with timing adjustment).
  bool flattenStaticIfInSeq(ProcSeqOp outerSeq);

  /// Unroll static_repeat inside seq (inline iterations with timing adjustment).
  bool unrollStaticRepeatInSeq(ProcSeqOp outerSeq);

  /// Remove single-child seq/par wrappers.
  bool removeSingleChildWrapper(Operation *op);

  /// Remove empty seq/par operations.
  bool removeEmptyControl(Operation *op);

  /// Compute the latency of a control operation for offset calculation.
  std::optional<int64_t> getControlLatency(Operation *op);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Latency Computation
//===----------------------------------------------------------------------===//

std::optional<int64_t>
ControlCollapsingPass::getControlLatency(Operation *op) {
  // Check for inferred_latency attribute (from StaticInference)
  if (auto latAttr = op->getAttrOfType<IntegerAttr>("inferred_latency"))
    return latAttr.getInt();

  // For static_step, use declared latency
  if (auto step = dyn_cast<ProcStaticStepOp>(op))
    return step.getLatency();

  // For static_if, use max of branch latencies
  if (auto staticIf = dyn_cast<ProcStaticIfOp>(op)) {
    int64_t thenLat = staticIf.getThenLatency().value_or(0);
    int64_t elseLat = staticIf.getElseLatency().value_or(0);
    return std::max(thenLat, elseLat);
  }

  // For static_repeat, use count * body_latency
  if (auto staticRepeat = dyn_cast<ProcStaticRepeatOp>(op)) {
    int64_t count = staticRepeat.getCount();
    int64_t bodyLat = staticRepeat.getBodyLatency().value_or(0);
    return count * bodyLat;
  }

  // For enable, try to look up step latency
  if (auto enable = dyn_cast<ProcEnableOp>(op)) {
    auto module = enable->getParentOfType<cmt2::ModuleOp>();
    if (!module)
      return std::nullopt;

    for (auto &bodyOp : module.getBodyRegion().front()) {
      if (auto staticStep = dyn_cast<ProcStaticStepOp>(bodyOp)) {
        if (staticStep.getSymName() == enable.getStepName())
          return staticStep.getLatency();
      }
    }
  }

  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Nested Seq Flattening
//===----------------------------------------------------------------------===//

bool ControlCollapsingPass::flattenNestedSeq(ProcSeqOp outerSeq) {
  // Safety check: ensure body exists and has a block
  if (outerSeq.getBody().empty())
    return false;

  bool hasNestedSeq = false;

  // First, check if there are any nested seq operations
  for (Operation &op : outerSeq.getBody().front()) {
    if (isa<ProcSeqOp>(&op)) {
      hasNestedSeq = true;
      break;
    }
  }

  // If no nested seq, nothing to do
  if (!hasNestedSeq)
    return false;

  OpBuilder builder(outerSeq);

  // Collect all operations and their timing offsets
  SmallVector<std::pair<Operation *, int64_t>> opsWithOffsets;
  int64_t currentOffset = 0;

  for (Operation &op : outerSeq.getBody().front()) {
    if (isa<ProcControlEndOp>(op))
      continue;

    if (auto innerSeq = dyn_cast<ProcSeqOp>(&op)) {
      // Safety check for inner seq
      if (innerSeq.getBody().empty())
        continue;

      // Flatten inner seq: add its children with adjusted offset
      for (Operation &innerOp : innerSeq.getBody().front()) {
        if (isa<ProcControlEndOp>(innerOp))
          continue;

        opsWithOffsets.push_back({&innerOp, currentOffset});

        // Advance offset for this operation
        if (auto lat = getControlLatency(&innerOp))
          currentOffset += *lat;
      }
    } else {
      opsWithOffsets.push_back({&op, 0}); // No offset adjustment needed

      // Advance offset for next operations
      if (auto lat = getControlLatency(&op))
        currentOffset += *lat;
    }
  }

  // Clone operations with adjusted timing
  builder.setInsertionPointAfter(outerSeq);
  auto newSeq = builder.create<ProcSeqOp>(outerSeq.getLoc());

  // Safety check: ensure newSeq body is not empty
  if (newSeq.getBody().empty()) {
    newSeq.erase();
    return false;
  }

  builder.setInsertionPointToStart(&newSeq.getBody().front());

  IRMapping mapping;
  for (auto &pair : opsWithOffsets) {
    Operation *op = pair.first;
    int64_t offset = pair.second;
    auto cloned = builder.clone(*op, mapping);

    // Adjust timing if this came from an inner seq
    if (offset > 0) {
      cloned->walk([offset](CallOp call) { adjustCallTiming(call, offset); });
    }
  }

  // Build terminator
  builder.create<ProcControlEndOp>(outerSeq.getLoc());

  // Replace old seq with new one
  outerSeq.erase();

  return true;
}

//===----------------------------------------------------------------------===//
// Nested Par Flattening
//===----------------------------------------------------------------------===//

bool ControlCollapsingPass::flattenNestedPar(ProcParOp outerPar) {
  // Safety check: ensure body exists and has a block
  if (outerPar.getBody().empty())
    return false;

  bool hasNestedPar = false;

  // Check if there are any nested par operations
  for (Operation &op : outerPar.getBody().front()) {
    if (isa<ProcParOp>(&op)) {
      hasNestedPar = true;
      break;
    }
  }

  if (!hasNestedPar)
    return false;

  OpBuilder builder(outerPar);
  builder.setInsertionPointAfter(outerPar);

  auto newPar = builder.create<ProcParOp>(outerPar.getLoc());

  // Safety check: ensure newPar body is not empty
  if (newPar.getBody().empty()) {
    newPar.erase();
    return false;
  }

  builder.setInsertionPointToStart(&newPar.getBody().front());

  IRMapping mapping;
  for (Operation &op : outerPar.getBody().front()) {
    if (isa<ProcControlEndOp>(op))
      continue;

    if (auto innerPar = dyn_cast<ProcParOp>(&op)) {
      // Safety check for inner par
      if (innerPar.getBody().empty())
        continue;

      // Flatten: add inner par's children directly (no offset change for par)
      for (Operation &innerOp : innerPar.getBody().front()) {
        if (!isa<ProcControlEndOp>(innerOp))
          builder.clone(innerOp, mapping);
      }
    } else {
      builder.clone(op, mapping);
    }
  }

  builder.create<ProcControlEndOp>(outerPar.getLoc());

  outerPar.erase();

  return true;
}

//===----------------------------------------------------------------------===//
// Static If Flattening
//===----------------------------------------------------------------------===//

bool ControlCollapsingPass::flattenStaticIfInSeq(ProcSeqOp outerSeq) {
  // Safety check: ensure body exists and has a block
  if (outerSeq.getBody().empty())
    return false;

  // Find static_if operations in the seq
  SmallVector<ProcStaticIfOp> staticIfs;
  for (Operation &op : outerSeq.getBody().front()) {
    if (auto staticIf = dyn_cast<ProcStaticIfOp>(&op))
      staticIfs.push_back(staticIf);
  }

  if (staticIfs.empty())
    return false;

  LLVM_DEBUG(llvm::dbgs() << "Flattening " << staticIfs.size()
                          << " static_if operations in seq\n");

  OpBuilder builder(outerSeq);
  builder.setInsertionPointAfter(outerSeq);
  auto newSeq = builder.create<ProcSeqOp>(outerSeq.getLoc());

  if (newSeq.getBody().empty()) {
    newSeq.erase();
    return false;
  }

  builder.setInsertionPointToStart(&newSeq.getBody().front());

  // Collect all operations with their timing offsets
  int64_t currentOffset = 0;
  IRMapping mapping;

  for (Operation &op : outerSeq.getBody().front()) {
    if (isa<ProcControlEndOp>(op))
      continue;

    if (auto staticIf = dyn_cast<ProcStaticIfOp>(&op)) {
      // For static_if, we inline it as a new static_if with adjusted timing
      // The condition still controls which branch executes at runtime
      int64_t thenLat = staticIf.getThenLatency().value_or(0);
      int64_t elseLat = staticIf.getElseLatency().value_or(0);
      int64_t maxLat = std::max(thenLat, elseLat);

      // Clone the static_if and adjust timing in both branches
      auto clonedIf = builder.clone(op, mapping);
      if (auto clonedStaticIf = dyn_cast<ProcStaticIfOp>(clonedIf)) {
        // Adjust timing in then region
        if (!clonedStaticIf.getThenRegion().empty()) {
          int64_t offset = currentOffset;
          clonedStaticIf.getThenRegion().walk([offset](CallOp call) {
            adjustCallTiming(call, offset);
          });
        }

        // Adjust timing in else region
        if (!clonedStaticIf.getElseRegion().empty()) {
          int64_t offset = currentOffset;
          clonedStaticIf.getElseRegion().walk([offset](CallOp call) {
            adjustCallTiming(call, offset);
          });
        }
      }

      currentOffset += maxLat;
    } else {
      // Regular operation - clone and adjust timing if needed
      auto cloned = builder.clone(op, mapping);
      if (currentOffset > 0) {
        cloned->walk([currentOffset](CallOp call) {
          adjustCallTiming(call, currentOffset);
        });
      }

      // Advance offset based on operation latency
      if (auto lat = getControlLatency(&op))
        currentOffset += *lat;
    }
  }

  builder.create<ProcControlEndOp>(outerSeq.getLoc());
  outerSeq.erase();

  return true;
}

//===----------------------------------------------------------------------===//
// Static Repeat Unrolling
//===----------------------------------------------------------------------===//

bool ControlCollapsingPass::unrollStaticRepeatInSeq(ProcSeqOp outerSeq) {
  // Safety check: ensure body exists and has a block
  if (outerSeq.getBody().empty())
    return false;

  // Find static_repeat operations in the seq
  SmallVector<ProcStaticRepeatOp> staticRepeats;
  for (Operation &op : outerSeq.getBody().front()) {
    if (auto staticRepeat = dyn_cast<ProcStaticRepeatOp>(&op))
      staticRepeats.push_back(staticRepeat);
  }

  if (staticRepeats.empty())
    return false;

  LLVM_DEBUG(llvm::dbgs() << "Unrolling " << staticRepeats.size()
                          << " static_repeat operations in seq\n");

  OpBuilder builder(outerSeq);
  builder.setInsertionPointAfter(outerSeq);
  auto newSeq = builder.create<ProcSeqOp>(outerSeq.getLoc());

  if (newSeq.getBody().empty()) {
    newSeq.erase();
    return false;
  }

  builder.setInsertionPointToStart(&newSeq.getBody().front());

  // Collect all operations with their timing offsets
  int64_t currentOffset = 0;
  IRMapping mapping;

  for (Operation &op : outerSeq.getBody().front()) {
    if (isa<ProcControlEndOp>(op))
      continue;

    if (auto staticRepeat = dyn_cast<ProcStaticRepeatOp>(&op)) {
      int64_t count = staticRepeat.getCount();
      int64_t bodyLat = staticRepeat.getBodyLatency().value_or(0);

      // Safety check for body region
      if (staticRepeat.getBody().empty())
        continue;

      // Unroll: clone body 'count' times with progressive offset
      for (int64_t iter = 0; iter < count; ++iter) {
        int64_t iterOffset = currentOffset + iter * bodyLat;

        // Clone each operation in the body with adjusted timing
        for (Operation &bodyOp : staticRepeat.getBody().front()) {
          if (isa<ProcControlEndOp>(bodyOp))
            continue;

          auto cloned = builder.clone(bodyOp, mapping);

          // Adjust timing for this iteration
          if (iterOffset > 0) {
            cloned->walk([iterOffset](CallOp call) {
              adjustCallTiming(call, iterOffset);
            });
          }
        }
      }

      currentOffset += count * bodyLat;
    } else {
      // Regular operation - clone and adjust timing if needed
      auto cloned = builder.clone(op, mapping);
      if (currentOffset > 0) {
        cloned->walk([currentOffset](CallOp call) {
          adjustCallTiming(call, currentOffset);
        });
      }

      // Advance offset based on operation latency
      if (auto lat = getControlLatency(&op))
        currentOffset += *lat;
    }
  }

  builder.create<ProcControlEndOp>(outerSeq.getLoc());
  outerSeq.erase();

  return true;
}

//===----------------------------------------------------------------------===//
// Single-Child Wrapper Removal
//===----------------------------------------------------------------------===//

bool ControlCollapsingPass::removeSingleChildWrapper(Operation *op) {
  Region *bodyRegion = nullptr;

  if (auto seq = dyn_cast<ProcSeqOp>(op))
    bodyRegion = &seq.getBody();
  else if (auto par = dyn_cast<ProcParOp>(op))
    bodyRegion = &par.getBody();

  if (!bodyRegion || bodyRegion->empty())
    return false;

  // Count non-terminator children
  int childCount = 0;
  Operation *singleChild = nullptr;
  for (Operation &childOp : bodyRegion->front()) {
    if (!isa<ProcControlEndOp>(childOp)) {
      childCount++;
      singleChild = &childOp;
    }
  }

  // If exactly one child, we can unwrap
  if (childCount != 1)
    return false;

  LLVM_DEBUG(llvm::dbgs() << "Removing single-child wrapper: " << *op << "\n");

  // Clone the single child before the wrapper and erase wrapper
  OpBuilder builder(op);
  IRMapping mapping;
  builder.clone(*singleChild, mapping);
  op->erase();

  return true;
}

//===----------------------------------------------------------------------===//
// Empty Control Removal
//===----------------------------------------------------------------------===//

bool ControlCollapsingPass::removeEmptyControl(Operation *op) {
  Region *bodyRegion = nullptr;

  if (auto seq = dyn_cast<ProcSeqOp>(op))
    bodyRegion = &seq.getBody();
  else if (auto par = dyn_cast<ProcParOp>(op))
    bodyRegion = &par.getBody();

  if (!bodyRegion || bodyRegion->empty())
    return false;

  // Check if region is empty (only terminator)
  bool isEmpty = true;
  for (Operation &childOp : bodyRegion->front()) {
    if (!isa<ProcControlEndOp>(childOp)) {
      isEmpty = false;
      break;
    }
  }

  if (!isEmpty)
    return false;

  LLVM_DEBUG(llvm::dbgs() << "Removing empty control: " << *op << "\n");
  op->erase();
  return true;
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void ControlCollapsingPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  bool changed = true;
  while (changed) {
    changed = false;

    // Collect seq ops first to avoid modifying during walk
    SmallVector<ProcSeqOp> seqOps;
    module.walk([&](ProcSeqOp seq) { seqOps.push_back(seq); });
    for (auto seq : seqOps) {
      if (flattenNestedSeq(seq))
        changed = true;
    }

    // Collect par ops first
    SmallVector<ProcParOp> parOps;
    module.walk([&](ProcParOp par) { parOps.push_back(par); });
    for (auto par : parOps) {
      if (flattenNestedPar(par))
        changed = true;
    }

    // Flatten static_if operations inside seq (with timing adjustment)
    seqOps.clear();
    module.walk([&](ProcSeqOp seq) { seqOps.push_back(seq); });
    for (auto seq : seqOps) {
      if (flattenStaticIfInSeq(seq))
        changed = true;
    }

    // Unroll static_repeat operations inside seq (with timing adjustment)
    seqOps.clear();
    module.walk([&](ProcSeqOp seq) { seqOps.push_back(seq); });
    for (auto seq : seqOps) {
      if (unrollStaticRepeatInSeq(seq))
        changed = true;
    }

    // Collect ops for empty control removal
    SmallVector<Operation *> controlOps;
    module.walk([&](Operation *op) {
      if (isa<ProcSeqOp, ProcParOp>(op))
        controlOps.push_back(op);
    });
    for (auto *op : controlOps) {
      if (removeEmptyControl(op))
        changed = true;
    }

    // Collect ops for single-child wrapper removal
    controlOps.clear();
    module.walk([&](Operation *op) {
      if (isa<ProcSeqOp, ProcParOp>(op))
        controlOps.push_back(op);
    });
    for (auto *op : controlOps) {
      if (removeSingleChildWrapper(op))
        changed = true;
    }
  }
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void ControlCollapsingPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
