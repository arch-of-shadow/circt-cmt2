//===- StaticInference.cpp - Infer latencies for static control -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the StaticInference pass for the Cmt2 dialect.
// It infers latencies for control structures and marks promotable control.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/TimingAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"
#include <optional>

#define DEBUG_TYPE "cmt2-static-inference"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_STATICINFERENCE
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// Result of latency inference for a control structure.
struct InferenceResult {
  std::optional<uint64_t> latency; // nullopt means unknown/not promotable
  bool promotable;

  static InferenceResult unknown() { return {std::nullopt, false}; }
  static InferenceResult known(uint64_t lat) { return {{lat}, true}; }
};

//===----------------------------------------------------------------------===//
// StaticInference Pass Implementation
//===----------------------------------------------------------------------===//

struct StaticInferencePass
    : public circt::cmt2::impl::StaticInferenceBase<StaticInferencePass> {

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Infer latency for a control operation recursively.
  InferenceResult inferLatency(Operation *op);

  /// Get latency for a step operation.
  std::optional<uint64_t> getStepLatency(StringRef stepName,
                                          cmt2::ModuleOp module);

  /// Get latency for a method invocation using TimingAnalysis.
  std::optional<uint64_t> getInvokeLatency(ProcInvokeOp invoke);

  /// Get latency from a static step's calls using timing attributes.
  std::optional<uint64_t> getStepLatencyFromCalls(ProcStaticStepOp step);

  /// Map from step name to latency (for current module).
  DenseMap<StringRef, std::optional<uint64_t>> stepLatencies;

  /// Timing analysis instance (created per runOnOperation).
  TimingAnalysis *timingAnalysis = nullptr;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Step Latency Lookup
//===----------------------------------------------------------------------===//

std::optional<uint64_t>
StaticInferencePass::getStepLatency(StringRef stepName, cmt2::ModuleOp module) {
  // Check cache
  auto it = stepLatencies.find(stepName);
  if (it != stepLatencies.end())
    return it->second;

  // Look up step in module
  for (auto &op : module.getBodyRegion().front()) {
    if (auto step = dyn_cast<ProcStepOp>(op)) {
      if (step.getSymName() == stepName) {
        // Dynamic step - check for explicit latency attribute
        if (auto latAttr = step->getAttrOfType<IntegerAttr>("latency")) {
          uint64_t lat = latAttr.getInt();
          stepLatencies[stepName] = lat;
          return lat;
        }
        // Dynamic step without latency - not promotable
        stepLatencies[stepName] = std::nullopt;
        return std::nullopt;
      }
    } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(op)) {
      if (staticStep.getSymName() == stepName) {
        // Static step has known latency
        uint64_t lat = staticStep.getLatency();
        stepLatencies[stepName] = lat;
        return lat;
      }
    }
  }

  // Step not found
  stepLatencies[stepName] = std::nullopt;
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// TimingAnalysis Integration
//===----------------------------------------------------------------------===//

std::optional<uint64_t>
StaticInferencePass::getInvokeLatency(ProcInvokeOp invoke) {
  if (!timingAnalysis)
    return std::nullopt;

  // Get the target module and method
  auto parentModule = invoke->getParentOfType<cmt2::ModuleOp>();
  if (!parentModule)
    return std::nullopt;

  // FlatSymbolRefAttr can be used directly as SymbolRefAttr
  auto instanceRef = invoke.getInstanceAttr();
  auto methodRef = invoke.getMethodAttr();

  // Look up timing for the invoked method
  auto methodTiming = timingAnalysis->lookupMethodTiming(
      parentModule, instanceRef, methodRef);

  if (methodTiming && methodTiming->isStatic())
    return static_cast<uint64_t>(*methodTiming->latency);

  return std::nullopt;
}

std::optional<uint64_t>
StaticInferencePass::getStepLatencyFromCalls(ProcStaticStepOp step) {
  if (!timingAnalysis)
    return std::nullopt;

  auto parentModule = step->getParentOfType<cmt2::ModuleOp>();
  if (!parentModule)
    return std::nullopt;

  uint64_t maxLatency = 0;
  bool hasTimingInfo = false;

  step.getBody().walk([&](CallOp call) {
    // First check explicit result timing on the call
    if (auto resultTiming = call.getResultTiming()) {
      for (auto attr : *resultTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          maxLatency = std::max(maxLatency, static_cast<uint64_t>(timing.getEnd()));
          hasTimingInfo = true;
        }
      }
    }

    // Also check method timing if no explicit result timing
    if (!call.getResultTiming()) {
      auto methodTiming = timingAnalysis->lookupMethodTiming(
          parentModule, call.getCallee(), call.getMethodOrValueAttr());
      if (methodTiming && methodTiming->isStatic()) {
        maxLatency = std::max(maxLatency, static_cast<uint64_t>(*methodTiming->latency));
        hasTimingInfo = true;
      }
    }
  });

  if (hasTimingInfo)
    return maxLatency;

  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Latency Inference
//===----------------------------------------------------------------------===//

InferenceResult StaticInferencePass::inferLatency(Operation *op) {
  return llvm::TypeSwitch<Operation *, InferenceResult>(op)
      .Case<ProcEnableOp>([&](ProcEnableOp enable) {
        // Get latency from step
        auto module = enable->getParentOfType<cmt2::ModuleOp>();
        auto lat = getStepLatency(enable.getStepName(), module);
        if (lat)
          return InferenceResult::known(*lat);
        return InferenceResult::unknown();
      })
      .Case<ProcSeqOp>([&](ProcSeqOp seq) {
        // Sequential: sum of all children
        uint64_t totalLatency = 0;
        for (Operation &stmt : seq.getBody().front()) {
          if (isa<ProcControlEndOp>(stmt))
            continue;
          auto result = inferLatency(&stmt);
          if (!result.promotable)
            return InferenceResult::unknown();
          totalLatency += *result.latency;
        }
        return InferenceResult::known(totalLatency);
      })
      .Case<ProcParOp>([&](ProcParOp par) {
        // Parallel: max of all children
        uint64_t maxLatency = 0;
        for (Operation &stmt : par.getBody().front()) {
          if (isa<ProcControlEndOp>(stmt))
            continue;
          auto result = inferLatency(&stmt);
          if (!result.promotable)
            return InferenceResult::unknown();
          maxLatency = std::max(maxLatency, *result.latency);
        }
        return InferenceResult::known(maxLatency);
      })
      .Case<ProcIfOp>([&](ProcIfOp ifOp) {
        // If: max of both branches (must have else for promotion)
        if (ifOp.getElseRegion().empty())
          return InferenceResult::unknown();

        uint64_t thenLatency = 0;
        for (Operation &stmt : ifOp.getThenRegion().front()) {
          if (isa<ProcControlEndOp>(stmt))
            continue;
          auto result = inferLatency(&stmt);
          if (!result.promotable)
            return InferenceResult::unknown();
          thenLatency += *result.latency;
        }

        uint64_t elseLatency = 0;
        for (Operation &stmt : ifOp.getElseRegion().front()) {
          if (isa<ProcControlEndOp>(stmt))
            continue;
          auto result = inferLatency(&stmt);
          if (!result.promotable)
            return InferenceResult::unknown();
          elseLatency += *result.latency;
        }

        return InferenceResult::known(std::max(thenLatency, elseLatency));
      })
      .Case<ProcWhileOp>([&](ProcWhileOp whileOp) {
        // While: only promotable if it has a @bound attribute
        auto boundAttr = whileOp->getAttrOfType<IntegerAttr>("bound");
        if (!boundAttr)
          return InferenceResult::unknown();

        uint64_t bound = boundAttr.getInt();
        uint64_t bodyLatency = 0;
        for (Operation &stmt : whileOp.getBody().front()) {
          if (isa<ProcControlEndOp>(stmt))
            continue;
          auto result = inferLatency(&stmt);
          if (!result.promotable)
            return InferenceResult::unknown();
          bodyLatency += *result.latency;
        }

        return InferenceResult::known(bound * bodyLatency);
      })
      .Case<ProcStaticRepeatOp>([&](ProcStaticRepeatOp repeatOp) {
        // Static repeat: count * body_latency
        uint64_t count = repeatOp.getCount();

        // Use explicit body_latency if provided
        if (auto bodyLat = repeatOp.getBodyLatency())
          return InferenceResult::known(count * *bodyLat);

        // Otherwise compute from body
        uint64_t bodyLatency = 0;
        for (Operation &stmt : repeatOp.getBody().front()) {
          if (isa<ProcControlEndOp>(stmt))
            continue;
          auto result = inferLatency(&stmt);
          if (!result.promotable)
            return InferenceResult::unknown();
          bodyLatency += *result.latency;
        }

        return InferenceResult::known(count * bodyLatency);
      })
      .Case<ProcStaticIfOp>([&](ProcStaticIfOp staticIf) {
        // Static if: use explicit latencies if provided
        if (staticIf.getThenLatency() && staticIf.getElseLatency()) {
          uint64_t thenLat = *staticIf.getThenLatency();
          uint64_t elseLat = *staticIf.getElseLatency();
          return InferenceResult::known(std::max(thenLat, elseLat));
        }

        // Otherwise compute from branches
        uint64_t thenLatency = 0;
        for (Operation &stmt : staticIf.getThenRegion().front()) {
          if (isa<ProcControlEndOp>(stmt))
            continue;
          auto result = inferLatency(&stmt);
          if (!result.promotable)
            return InferenceResult::unknown();
          thenLatency += *result.latency;
        }

        uint64_t elseLatency = 0;
        if (!staticIf.getElseRegion().empty()) {
          for (Operation &stmt : staticIf.getElseRegion().front()) {
            if (isa<ProcControlEndOp>(stmt))
              continue;
            auto result = inferLatency(&stmt);
            if (!result.promotable)
              return InferenceResult::unknown();
            elseLatency += *result.latency;
          }
        }

        return InferenceResult::known(std::max(thenLatency, elseLatency));
      })
      .Case<ProcInvokeOp>([&](ProcInvokeOp invoke) {
        // Invoke: look up method latency from target module using TimingAnalysis
        auto latency = getInvokeLatency(invoke);
        if (latency)
          return InferenceResult::known(*latency);
        return InferenceResult::unknown();
      })
      .Default([](Operation *) {
        // Unknown operation - not promotable
        return InferenceResult::unknown();
      });
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void StaticInferencePass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Clear step latency cache for this module
  stepLatencies.clear();

  // Pre-populate step latencies
  for (auto &op : module.getBodyRegion().front()) {
    if (auto step = dyn_cast<ProcStepOp>(op)) {
      getStepLatency(step.getSymName(), module);
    } else if (auto staticStep = dyn_cast<ProcStaticStepOp>(op)) {
      getStepLatency(staticStep.getSymName(), module);
    }
  }

  // Process all procedural rules and methods
  module.walk([&](Operation *op) {
    Region *controlRegion = nullptr;
    StringRef opName;

    if (auto rule = dyn_cast<ProcRuleOp>(op)) {
      controlRegion = &rule.getControl();
      opName = rule.getSymName();
    } else if (auto method = dyn_cast<ProcMethodOp>(op)) {
      controlRegion = &method.getControl();
      opName = method.getSymName();
    }

    if (!controlRegion || controlRegion->empty())
      return;

    // Infer latency for the control region
    uint64_t totalLatency = 0;
    bool allPromotable = true;

    for (Operation &stmt : controlRegion->front()) {
      if (isa<ProcControlEndOp>(stmt))
        continue;

      auto result = inferLatency(&stmt);
      if (result.promotable) {
        // Mark operation with inferred latency
        OpBuilder builder(op->getContext());
        stmt.setAttr("inferred_latency",
                     builder.getI64IntegerAttr(*result.latency));
        stmt.setAttr("promotable", builder.getUnitAttr());
        totalLatency += *result.latency;
      } else {
        allPromotable = false;
      }
    }

    // Mark the proc rule/method with total latency if fully promotable
    if (allPromotable && totalLatency > 0) {
      OpBuilder builder(op->getContext());
      op->setAttr("total_latency", builder.getI64IntegerAttr(totalLatency));
      op->setAttr("promotable", builder.getUnitAttr());
      LLVM_DEBUG(llvm::dbgs()
                 << "  @" << opName << " is promotable with latency "
                 << totalLatency << "\n");
    } else {
      LLVM_DEBUG(llvm::dbgs() << "  @" << opName << " is not promotable\n");
    }
  });
}

void StaticInferencePass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Create TimingAnalysis for cross-module timing lookup
  TimingAnalysis analysis(circuit);
  timingAnalysis = &analysis;

  // Process each module
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }

  // Clean up
  timingAnalysis = nullptr;
}
