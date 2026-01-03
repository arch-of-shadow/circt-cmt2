//===- TimingInference.cpp - Infer cycle-precise timing --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TimingInference pass for the Cmt2 dialect.
// It infers cycle-precise timing for methods and steps based on method
// contracts and propagates timing information through the module hierarchy.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/TimingAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-timing-inference"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_TIMINGINFERENCE
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// TimingInference Pass Implementation
//===----------------------------------------------------------------------===//

struct TimingInferencePass
    : public circt::cmt2::impl::TimingInferenceBase<TimingInferencePass> {
  using TimingInferenceBase::TimingInferenceBase;

  void runOnOperation() override;

private:
  /// Process a single module for timing inference.
  void processModule(cmt2::ModuleOp module, TimingAnalysis &analysis);

  /// Infer timing for a ProcMethodOp and potentially promote to static.
  void inferMethodTiming(ProcMethodOp method, TimingAnalysis &analysis);

  /// Infer timing for a ProcStaticStepOp.
  void inferStepTiming(ProcStaticStepOp step, TimingAnalysis &analysis);

  /// Infer timing annotations for a CallOp.
  void inferTimingForCall(CallOp call, cmt2::ModuleOp module, TimingAnalysis &analysis);

  /// Compute required latency for a step based on its calls.
  std::optional<int64_t> computeRequiredLatency(ProcStaticStepOp step,
                                                 TimingAnalysis &analysis);

  /// Check if a method's body has deterministic timing.
  std::optional<int64_t> computeMethodLatency(ProcMethodOp method,
                                               TimingAnalysis &analysis);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Call Timing Inference
//===----------------------------------------------------------------------===//

void TimingInferencePass::inferTimingForCall(CallOp call, cmt2::ModuleOp module,
                                              TimingAnalysis &analysis) {
  // Skip calls that already have timing annotations
  if (call.getArgTiming() || call.getResultTiming())
    return;

  // Look up the method timing
  auto methodTiming = analysis.lookupMethodTiming(
      module, call.getCallee(), call.getMethodOrValueAttr());
  if (!methodTiming || !methodTiming->isStatic())
    return;

  // Get parent step to determine step latency
  auto parentStep = call->getParentOfType<ProcStaticStepOp>();
  if (!parentStep)
    return;

  OpBuilder builder(call);
  int64_t methodLatency = *methodTiming->latency;

  // Infer default arg timing: args provided at cycle 0
  SmallVector<Attribute> argTimingAttrs;
  for (size_t i = 0; i < call.getInputs().size(); ++i) {
    // Default: arg is stable at cycle 0
    argTimingAttrs.push_back(
        TimingIntervalAttr::get(builder.getContext(), 0, 1));
  }

  // Infer default result timing: results available at method latency
  SmallVector<Attribute> resultTimingAttrs;
  for (size_t i = 0; i < call.getOutputs().size(); ++i) {
    // Default: result available at method latency, stable for 1 cycle
    resultTimingAttrs.push_back(
        TimingIntervalAttr::get(builder.getContext(), methodLatency,
                                methodLatency + 1));
  }

  // Set the inferred timing attributes
  if (!argTimingAttrs.empty())
    call.setArgTimingAttr(builder.getArrayAttr(argTimingAttrs));
  if (!resultTimingAttrs.empty())
    call.setResultTimingAttr(builder.getArrayAttr(resultTimingAttrs));

  LLVM_DEBUG(llvm::dbgs() << "  Inferred timing for call to "
                          << call.getCallee() << "." << call.getMethodOrValueAttr()
                          << ": args@[0,1), results@[" << methodLatency
                          << "," << methodLatency + 1 << ")\n");
}

//===----------------------------------------------------------------------===//
// Step Timing Inference
//===----------------------------------------------------------------------===//

std::optional<int64_t>
TimingInferencePass::computeRequiredLatency(ProcStaticStepOp step,
                                            TimingAnalysis &analysis) {
  int64_t maxCycle = 0;

  step.getBody().walk([&](CallOp call) {
    // Get the max cycle from explicit timing
    int64_t callMax = getMaxCycleFromCall(call);
    maxCycle = std::max(maxCycle, callMax);

    // Also consider method latency if we need to read results
    auto parentModule = step->getParentOfType<cmt2::ModuleOp>();
    if (parentModule) {
      auto methodTiming = analysis.lookupMethodTiming(
          parentModule, call.getCallee(), call.getMethodOrValueAttr());
      if (methodTiming && methodTiming->isStatic() &&
          !call.getOutputs().empty()) {
        // Need at least method latency to get results
        maxCycle = std::max(maxCycle, *methodTiming->latency);
      }
    }
  });

  // Latency must be at least 1
  return maxCycle > 0 ? std::optional<int64_t>(maxCycle) : std::nullopt;
}

void TimingInferencePass::inferStepTiming(ProcStaticStepOp step,
                                          TimingAnalysis &analysis) {
  auto requiredLatency = computeRequiredLatency(step, analysis);

  // Check if step latency is sufficient
  int64_t stepLatency = step.getLatency();
  if (requiredLatency && *requiredLatency > stepLatency) {
    step.emitWarning() << "step latency (" << stepLatency
                       << ") may be insufficient for contained calls (need "
                       << *requiredLatency << ")";
  }

  LLVM_DEBUG(llvm::dbgs() << "  Step @" << step.getSymName()
                          << ": declared latency=" << stepLatency
                          << ", required=" << (requiredLatency ? std::to_string(*requiredLatency) : "unknown")
                          << "\n");
}

//===----------------------------------------------------------------------===//
// Method Timing Inference
//===----------------------------------------------------------------------===//

std::optional<int64_t>
TimingInferencePass::computeMethodLatency(ProcMethodOp method,
                                          TimingAnalysis &analysis) {
  // If method already has static latency, use it
  if (auto staticLat = method.getStaticLatency())
    return *staticLat;

  // Try to infer from control region
  if (method.getControl().empty())
    return std::nullopt;

  // For now, simple heuristic: sum of all step latencies in seq control
  int64_t totalLatency = 0;
  bool allStatic = true;

  method.getControl().walk([&](ProcEnableOp enable) {
    // Find the referenced step
    auto parentModule = method->getParentOfType<cmt2::ModuleOp>();
    if (!parentModule)
      return;

    for (auto &op : parentModule.getBodyRegion().front()) {
      if (auto staticStep = dyn_cast<ProcStaticStepOp>(op)) {
        if (staticStep.getSymName() == enable.getStepName()) {
          totalLatency += staticStep.getLatency();
          return;
        }
      } else if (auto dynStep = dyn_cast<ProcStepOp>(op)) {
        if (dynStep.getSymName() == enable.getStepName()) {
          allStatic = false;
          return;
        }
      }
    }
  });

  if (allStatic && totalLatency > 0)
    return totalLatency;

  return std::nullopt;
}

void TimingInferencePass::inferMethodTiming(ProcMethodOp method,
                                            TimingAnalysis &analysis) {
  // Skip if already has static latency
  if (method.getStaticLatency())
    return;

  // Try to compute latency from control
  auto inferredLatency = computeMethodLatency(method, analysis);

  if (inferredLatency && this->promoteToStatic) {
    OpBuilder builder(method);
    method.setStaticLatencyAttr(builder.getI64IntegerAttr(*inferredLatency));
    LLVM_DEBUG(llvm::dbgs() << "  Promoted method @" << method.getSymName()
                            << " to static with latency=" << *inferredLatency
                            << "\n");
  }
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void TimingInferencePass::processModule(cmt2::ModuleOp module,
                                        TimingAnalysis &analysis) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Phase 1: Infer timing for all calls in static steps
  if (this->inferCallTiming) {
    module.walk([&](ProcStaticStepOp step) {
      step.getBody().walk([&](CallOp call) {
        inferTimingForCall(call, module, analysis);
      });
    });
  }

  // Phase 2: Infer timing for static steps
  module.walk([&](ProcStaticStepOp step) {
    inferStepTiming(step, analysis);
  });

  // Phase 3: Try to promote methods to static
  if (this->promoteToStatic) {
    module.walk([&](ProcMethodOp method) {
      inferMethodTiming(method, analysis);
    });
  }
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void TimingInferencePass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Create timing analysis for the circuit
  TimingAnalysis analysis(circuit);

  // Process modules in post-order (bottom-up through instance hierarchy)
  // This ensures child module timing is known before parent
  // For simplicity, just process all modules in order for now
  // TODO: Use InstanceGraph for proper post-order traversal

  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module, analysis);
    }
  }
}
