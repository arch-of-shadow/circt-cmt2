//===- TimingValidation.cpp - Validate cycle-precise timing ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TimingValidation pass for the Cmt2 dialect.
// It validates that all cycle-precise timing constraints are satisfied
// throughout the design.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/TimingAnalysis.h"
#include "circt/Dialect/Cmt2/Analysis/TimingCompatibility.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-timing-validation"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_TIMINGVALIDATION
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// TimingValidation Pass Implementation
//===----------------------------------------------------------------------===//

struct TimingValidationPass
    : public circt::cmt2::impl::TimingValidationBase<TimingValidationPass> {
  using TimingValidationBase::TimingValidationBase;

  void runOnOperation() override;

private:
  /// Validate a single module's timing.
  LogicalResult validateModule(cmt2::ModuleOp module, TimingAnalysis &analysis,
                               TimingCompatibility &compatibility);

  /// Validate a static step's timing.
  LogicalResult validateStaticStep(ProcStaticStepOp step, cmt2::ModuleOp module,
                                   TimingAnalysis &analysis,
                                   TimingCompatibility &compatibility);

  /// Validate that all calls in a static step have explicit timing (strict mode).
  LogicalResult validateStrictTiming(ProcStaticStepOp step);

  /// Validate pipelined call spacing within a step.
  LogicalResult validatePipelinedCalls(ProcStaticStepOp step, cmt2::ModuleOp module,
                                       TimingAnalysis &analysis);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Strict Mode Validation
//===----------------------------------------------------------------------===//

LogicalResult TimingValidationPass::validateStrictTiming(ProcStaticStepOp step) {
  LogicalResult result = success();

  step.getBody().walk([&](CallOp call) {
    // In strict mode, all calls must have explicit timing
    if (!call.getArgTiming() && !call.getResultTiming()) {
      call.emitOpError("call in static step must have explicit timing "
                       "annotations (strict mode enabled)");
      result = failure();
    }
  });

  return result;
}

//===----------------------------------------------------------------------===//
// Pipelined Call Validation
//===----------------------------------------------------------------------===//

LogicalResult TimingValidationPass::validatePipelinedCalls(
    ProcStaticStepOp step, cmt2::ModuleOp module, TimingAnalysis &analysis) {
  // Group calls by target (callee + method)
  using CallKey = std::pair<StringRef, StringRef>;
  llvm::DenseMap<CallKey, SmallVector<CallOp>> callGroups;

  step.getBody().walk([&](CallOp call) {
    auto key = std::make_pair(
        call.getCallee().getRootReference(),
        call.getMethodOrValueAttr().getRootReference());
    callGroups[key].push_back(call);
  });

  LogicalResult result = success();

  // Check each group for proper spacing
  for (auto &[key, calls] : callGroups) {
    if (calls.size() < 2)
      continue;

    // Look up method timing
    auto methodTiming = analysis.lookupMethodTiming(
        module, calls[0].getCallee(), calls[0].getMethodOrValueAttr());
    if (!methodTiming || !methodTiming->isPipelined())
      continue;

    int64_t ii = methodTiming->getInitiationInterval();

    // Extract start times from calls
    SmallVector<std::pair<int64_t, CallOp>> startTimes;
    for (auto call : calls) {
      auto argTiming = call.getArgTiming();
      if (!argTiming || argTiming->empty())
        continue;

      auto timing = dyn_cast<TimingIntervalAttr>((*argTiming)[0]);
      if (!timing)
        continue;

      startTimes.emplace_back(timing.getStart(), call);
    }

    // Sort by start time
    llvm::sort(startTimes, [](auto &a, auto &b) { return a.first < b.first; });

    // Check spacing between consecutive calls
    for (size_t i = 1; i < startTimes.size(); ++i) {
      int64_t gap = startTimes[i].first - startTimes[i - 1].first;
      if (gap < ii) {
        startTimes[i].second.emitOpError("pipelined call spacing (")
            << gap << " cycles) is less than initiation interval (" << ii
            << " cycles)";
        startTimes[i - 1].second.emitRemark("previous call to same method");
        result = failure();
      }
    }
  }

  return result;
}

//===----------------------------------------------------------------------===//
// Static Step Validation
//===----------------------------------------------------------------------===//

LogicalResult TimingValidationPass::validateStaticStep(
    ProcStaticStepOp step, cmt2::ModuleOp module, TimingAnalysis &analysis,
    TimingCompatibility &compatibility) {
  LLVM_DEBUG(llvm::dbgs() << "  Validating step @" << step.getSymName() << "\n");

  LogicalResult result = success();
  int64_t stepLatency = step.getLatency();

  // Check each call in the step
  step.getBody().walk([&](CallOp call) {
    // Validate timing bounds
    if (failed(checkCallArgTimingBounds(call, stepLatency)))
      result = failure();

    // Validate result timing against method latency
    auto methodTiming = analysis.lookupMethodTiming(
        module, call.getCallee(), call.getMethodOrValueAttr());
    if (methodTiming && methodTiming->isStatic()) {
      if (failed(checkCallResultTiming(call, *methodTiming->latency)))
        result = failure();
    }
  });

  // Validate pipelined call spacing
  if (failed(validatePipelinedCalls(step, module, analysis)))
    result = failure();

  // Strict mode: all calls must have explicit timing
  if (this->strict) {
    if (failed(validateStrictTiming(step)))
      result = failure();
  }

  return result;
}

//===----------------------------------------------------------------------===//
// Module Validation
//===----------------------------------------------------------------------===//

LogicalResult TimingValidationPass::validateModule(
    cmt2::ModuleOp module, TimingAnalysis &analysis,
    TimingCompatibility &compatibility) {
  LLVM_DEBUG(llvm::dbgs() << "Validating module @" << module.getSymName()
                          << "\n");

  LogicalResult result = success();

  // Validate all static steps
  module.walk([&](ProcStaticStepOp step) {
    if (failed(validateStaticStep(step, module, analysis, compatibility)))
      result = failure();
  });

  // Validate bind method timing constraints
  module.walk([&](BindMethodOp bindMethod) {
    // If method has static latency, validate it's consistent with external
    if (auto staticLat = bindMethod.getStaticLatency()) {
      if (*staticLat <= 0) {
        bindMethod.emitOpError("static_latency must be positive");
        result = failure();
      }
    }

    // If method has interval, validate it's <= latency
    if (auto interval = bindMethod.getInterval()) {
      if (auto staticLat = bindMethod.getStaticLatency()) {
        if (interval->getCycles() > *staticLat) {
          bindMethod.emitOpError("interval (")
              << interval->getCycles() << ") must be <= static_latency ("
              << *staticLat << ")";
          result = failure();
        }
      }
    }
  });

  // Validate proc method timing constraints
  module.walk([&](ProcMethodOp procMethod) {
    // If method has static latency, validate it's consistent
    if (auto staticLat = procMethod.getStaticLatency()) {
      if (*staticLat <= 0) {
        procMethod.emitOpError("static_latency must be positive");
        result = failure();
      }
    }

    // If method has interval, validate it's <= latency
    if (auto interval = procMethod.getInterval()) {
      if (auto staticLat = procMethod.getStaticLatency()) {
        if (interval->getCycles() > *staticLat) {
          procMethod.emitOpError("interval (")
              << interval->getCycles() << ") must be <= static_latency ("
              << *staticLat << ")";
          result = failure();
        }
      }
    }
  });

  return result;
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void TimingValidationPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Create analysis infrastructure
  TimingAnalysis analysis(circuit);
  TimingCompatibility compatibility(analysis);

  bool hasErrors = false;

  // Validate each module
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      if (failed(validateModule(module, analysis, compatibility)))
        hasErrors = true;
    }
  }

  if (hasErrors)
    signalPassFailure();
}
