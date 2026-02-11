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

  /// Validate that proc method's declared static_latency matches control flow.
  LogicalResult validateProcMethodLatency(ProcMethodOp procMethod,
                                          cmt2::ModuleOp module);

  /// Validate dataflow task timing against TDCC-computed control flow (C3).
  LogicalResult validateDataflowTaskTiming(DataflowTaskOp task,
                                           cmt2::ModuleOp module);

  /// Validate all dataflow tasks in a ProcDataflowOp.
  LogicalResult validateProcDataflow(ProcDataflowOp dataflow,
                                     cmt2::ModuleOp module);

  /// Compute the static latency of a control region.
  /// Returns std::nullopt if the region contains dynamic constructs.
  std::optional<int64_t> computeControlLatency(Region &region,
                                               cmt2::ModuleOp module);

  /// Compute the static latency of a single operation.
  std::optional<int64_t> computeOpLatency(Operation *op, cmt2::ModuleOp module);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Strict Mode Validation
//===----------------------------------------------------------------------===//

LogicalResult TimingValidationPass::validateStrictTiming(ProcStaticStepOp step) {
  LogicalResult result = success();

  step.getBody().walk([&](CallOp call) {
    // In strict mode, all calls must have explicit timing
    if (!call.getCallTiming() || !call.getArgTiming() ||
        (!call.getOutputs().empty() && !call.getResultTiming())) {
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
      int64_t start = 0;
      if (auto callTiming = call.getCallTiming())
        start = callTiming->getStart();
      startTimes.emplace_back(start, call);
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
// Proc Method Latency Computation
//===----------------------------------------------------------------------===//

std::optional<int64_t>
TimingValidationPass::computeOpLatency(Operation *op, cmt2::ModuleOp module) {
  // ProcEnableOp: Look up the step's latency
  if (auto enable = dyn_cast<ProcEnableOp>(op)) {
    auto stepName = enable.getStepName();
    // Look up the step in the module
    Operation *stepOp = module.lookupSymbol(stepName);
    if (!stepOp)
      return std::nullopt; // Step not found, can't compute

    if (auto staticStep = dyn_cast<ProcStaticStepOp>(stepOp)) {
      return staticStep.getLatency();
    }
    // Dynamic step (ProcStepOp) - latency is unknown
    return std::nullopt;
  }

  // ProcSeqOp: Sum of children latencies
  if (auto seq = dyn_cast<ProcSeqOp>(op)) {
    return computeControlLatency(seq.getBody(), module);
  }

  // ProcParOp: Max of children latencies
  if (auto par = dyn_cast<ProcParOp>(op)) {
    int64_t maxLatency = 0;
    for (Operation &child : par.getBody().front()) {
      auto childLat = computeOpLatency(&child, module);
      if (!childLat)
        return std::nullopt;
      maxLatency = std::max(maxLatency, *childLat);
    }
    return maxLatency;
  }

  // ProcStaticRepeatOp: count * body_latency
  if (auto repeat = dyn_cast<ProcStaticRepeatOp>(op)) {
    int64_t count = repeat.getCount();
    // Use explicit body_latency if provided
    if (auto bodyLat = repeat.getBodyLatency()) {
      return count * *bodyLat;
    }
    // Otherwise compute from body
    auto bodyLat = computeControlLatency(repeat.getBody(), module);
    if (!bodyLat)
      return std::nullopt;
    return count * *bodyLat;
  }

  // ProcStaticIfOp: max of branch latencies
  if (auto staticIf = dyn_cast<ProcStaticIfOp>(op)) {
    int64_t thenLat = 0;
    int64_t elseLat = 0;

    // Get then latency
    if (auto explicitThen = staticIf.getThenLatency()) {
      thenLat = *explicitThen;
    } else {
      auto computed = computeControlLatency(staticIf.getThenRegion(), module);
      if (!computed)
        return std::nullopt;
      thenLat = *computed;
    }

    // Get else latency
    if (staticIf.getElseRegion().empty()) {
      elseLat = 0;
    } else if (auto explicitElse = staticIf.getElseLatency()) {
      elseLat = *explicitElse;
    } else {
      auto computed = computeControlLatency(staticIf.getElseRegion(), module);
      if (!computed)
        return std::nullopt;
      elseLat = *computed;
    }

    return std::max(thenLat, elseLat);
  }

  // Dynamic control constructs - can't compute static latency
  if (isa<ProcIfOp, ProcWhileOp>(op))
    return std::nullopt;

  // Other ops (control_end, yield, etc.) - zero latency
  return 0;
}

std::optional<int64_t>
TimingValidationPass::computeControlLatency(Region &region,
                                            cmt2::ModuleOp module) {
  if (region.empty())
    return 0;

  int64_t totalLatency = 0;
  for (Operation &op : region.front()) {
    auto opLat = computeOpLatency(&op, module);
    if (!opLat)
      return std::nullopt;
    totalLatency += *opLat;
  }
  return totalLatency;
}

//===----------------------------------------------------------------------===//
// Proc Method Latency Validation
//===----------------------------------------------------------------------===//

LogicalResult
TimingValidationPass::validateProcMethodLatency(ProcMethodOp procMethod,
                                                cmt2::ModuleOp module) {
  auto declaredLatency = procMethod.getStaticLatency();
  if (!declaredLatency)
    return success(); // No static_latency declared, nothing to validate

  // Compute actual latency from control region
  auto computedLatency = computeControlLatency(procMethod.getControl(), module);

  if (!computedLatency) {
    // Control region contains dynamic constructs
    return procMethod.emitOpError("has static_latency=")
           << *declaredLatency << " but control region contains dynamic "
           << "constructs (dynamic steps, if, or while); use only static_step, "
           << "static_repeat, static_if, seq, and par for static methods";
  }

  if (static_cast<uint64_t>(*computedLatency) != *declaredLatency) {
    return procMethod.emitOpError("declared static_latency=")
           << *declaredLatency << " but control flow computes to "
           << *computedLatency << " cycles";
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Dataflow Task Timing Validation (C3)
//===----------------------------------------------------------------------===//

LogicalResult
TimingValidationPass::validateDataflowTaskTiming(DataflowTaskOp task,
                                                 cmt2::ModuleOp module) {
  // Check if task has TDCC attributes (has proc control)
  auto hasProcControl = task->hasAttr("tdcc.has_proc_control");
  if (!hasProcControl)
    return success(); // No proc control, nothing to cross-validate

  // Get TDCC-computed latency from done_state
  auto doneStateAttr = task->getAttrOfType<IntegerAttr>("tdcc.done_state");
  if (!doneStateAttr) {
    return task.emitOpError("has proc control but missing tdcc.done_state; "
                            "run TDCC pass first");
  }

  int64_t tdccLatency = doneStateAttr.getInt();

  // Get declared timing if present
  auto declaredTiming = task.getTiming();
  if (!declaredTiming)
    return success(); // No declared timing, nothing to cross-validate

  // The timing interval gives (start, end) cycles
  int64_t declaredLatency = declaredTiming->getEnd() - declaredTiming->getStart();

  // Validate that declared latency matches TDCC-computed latency
  if (declaredLatency != tdccLatency) {
    return task.emitOpError("declared timing interval [")
           << declaredTiming->getStart() << ", " << declaredTiming->getEnd()
           << "] implies " << declaredLatency << " cycles, but TDCC computed "
           << tdccLatency << " cycles from control flow";
  }

  LLVM_DEBUG(llvm::dbgs() << "  Task @" << task.getSymName()
                          << " timing validated: " << tdccLatency << " cycles\n");

  return success();
}

LogicalResult
TimingValidationPass::validateProcDataflow(ProcDataflowOp dataflow,
                                           cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "  Validating dataflow @" << dataflow.getSymName()
                          << "\n");

  LogicalResult result = success();

  // Validate each task in the dataflow
  for (auto &op : dataflow.getBody().front()) {
    if (auto task = dyn_cast<DataflowTaskOp>(op)) {
      if (failed(validateDataflowTaskTiming(task, module)))
        result = failure();
    }
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

    // Validate static_latency matches control flow (TV5)
    if (failed(validateProcMethodLatency(procMethod, module)))
      result = failure();
  });

  // Validate dataflow task timing (C3: Unified timing validation)
  module.walk([&](ProcDataflowOp dataflow) {
    if (failed(validateProcDataflow(dataflow, module)))
      result = failure();
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
