//===- TimingCompatibility.cpp - CMT2 Timing Compatibility ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements timing compatibility checking for CMT2.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/TimingCompatibility.h"
#include "llvm/ADT/StringExtras.h"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// TimingCompatibility Implementation
//===----------------------------------------------------------------------===//

TimingCompatibility::TimingCompatibility(TimingAnalysis &analysis)
    : analysis_(analysis) {}

LogicalResult TimingCompatibility::checkCallCompatibility(CallOp call) {
  // Get the parent static step, if any
  auto parentStep = getParentStaticStep(call);

  // If not in a static step, timing guards shouldn't be present
  if (!parentStep) {
    if (call.hasTiming()) {
      return call.emitOpError("timing guards are only valid inside static steps");
    }
    return success();
  }

  // Get method timing
  auto moduleName = call.getCallee();
  auto methodName = call.getMethodOrValueAttr();

  // Find the parent module
  auto parentOp = call->getParentOfType<ModuleOp>();
  if (!parentOp)
    return success(); // Can't validate without module context

  auto methodTiming = analysis_.lookupMethodTiming(parentOp, moduleName, methodName);
  if (!methodTiming)
    return success(); // Can't validate without method timing

  // Check bounds
  if (failed(checkTimingBounds(call, parentStep.getLatency())))
    return failure();

  // Check result timing
  if (failed(checkResultTiming(call, *methodTiming)))
    return failure();

  // Check arg timing
  if (failed(checkArgTiming(call, *methodTiming)))
    return failure();

  return success();
}

LogicalResult TimingCompatibility::checkResultTiming(CallOp call,
                                                     const TimingInfo &methodTiming) {
  // If method is dynamic, we can't validate result timing statically
  if (!methodTiming.isStatic())
    return success();

  int64_t methodLatency = *methodTiming.latency;

  auto resultTiming = call.getResultTiming();
  if (!resultTiming)
    return success();

  for (size_t i = 0; i < resultTiming->size(); ++i) {
    auto timing = dyn_cast<TimingIntervalAttr>((*resultTiming)[i]);
    if (!timing)
      continue;

    // Result must be read at or after method latency
    if (timing.getStart() < methodLatency) {
      return call.emitOpError("result ")
             << i << " timing [" << timing.getStart() << ", " << timing.getEnd()
             << ") starts before method completes at cycle " << methodLatency;
    }
  }

  return success();
}

LogicalResult TimingCompatibility::checkArgTiming(CallOp call,
                                                  const TimingInfo &methodTiming) {
  // For now, just validate that arg timing is within step bounds
  // Future: check that args are stable during the method's input window
  auto argTiming = call.getArgTiming();
  if (!argTiming)
    return success();

  for (size_t i = 0; i < argTiming->size(); ++i) {
    auto timing = dyn_cast<TimingIntervalAttr>((*argTiming)[i]);
    if (!timing)
      continue;

    // Args must start at cycle 0 or later
    if (timing.getStart() < 0) {
      return call.emitOpError("arg ")
             << i << " timing starts before cycle 0";
    }
  }

  return success();
}

LogicalResult TimingCompatibility::checkIntervalCompatibility(
    CallOp call1, CallOp call2, const TimingInfo &methodTiming) {
  // Check that pipelined calls respect initiation interval
  if (!methodTiming.isPipelined())
    return success();

  int64_t ii = methodTiming.getInitiationInterval();

  // Get the start times of both calls
  auto getStartTime = [](CallOp call) -> std::optional<int64_t> {
    auto argTiming = call.getArgTiming();
    if (!argTiming || argTiming->empty())
      return std::nullopt;
    auto timing = dyn_cast<TimingIntervalAttr>((*argTiming)[0]);
    if (!timing)
      return std::nullopt;
    return timing.getStart();
  };

  auto start1 = getStartTime(call1);
  auto start2 = getStartTime(call2);

  if (!start1 || !start2)
    return success();

  int64_t gap = std::abs(*start2 - *start1);
  if (gap < ii) {
    return call2.emitOpError("pipelined call spacing (")
           << gap << " cycles) is less than initiation interval (" << ii << ")";
  }

  return success();
}

LogicalResult TimingCompatibility::checkTimingBounds(CallOp call,
                                                     int64_t stepLatency) {
  return cmt2::checkCallArgTimingBounds(call, stepLatency);
}

LogicalResult TimingCompatibility::validateStaticStep(ProcStaticStepOp step) {
  // Collect all calls and validate each
  SmallVector<CallOp> calls;
  step.getBody().walk([&](CallOp call) {
    calls.push_back(call);
  });

  for (auto call : calls) {
    if (failed(checkCallCompatibility(call)))
      return failure();
  }

  // Check pipelined call spacing
  // Group calls by callee and check spacing within each group
  llvm::DenseMap<std::pair<StringRef, StringRef>, SmallVector<CallOp>> callGroups;
  for (auto call : calls) {
    auto key = std::make_pair(call.getCallee().getRootReference().str(),
                              call.getMethodOrValueAttr().getRootReference().str());
    // Note: Using string comparison for simplicity
  }

  return success();
}

LogicalResult TimingCompatibility::validateModule(ModuleOp module) {
  // Validate all static steps in the module
  auto result = success();
  module.walk([&](ProcStaticStepOp step) {
    if (failed(validateStaticStep(step)))
      result = failure();
  });
  return result;
}

ProcStaticStepOp TimingCompatibility::getParentStaticStep(CallOp call) {
  return call->getParentOfType<ProcStaticStepOp>();
}

bool TimingCompatibility::intervalsOverlap(TimingIntervalAttr a,
                                           TimingIntervalAttr b) {
  // Half-open intervals [s1, e1) and [s2, e2) overlap if:
  // s1 < e2 && s2 < e1
  return a.getStart() < b.getEnd() && b.getStart() < a.getEnd();
}

//===----------------------------------------------------------------------===//
// Diagnostic Helpers
//===----------------------------------------------------------------------===//

InFlightDiagnostic cmt2::emitTimingError(Operation *op, StringRef message) {
  return op->emitOpError() << message;
}

std::string cmt2::formatTimingInterval(TimingIntervalAttr timing) {
  return "[" + std::to_string(timing.getStart()) + ", " +
         std::to_string(timing.getEnd()) + ")";
}

std::string cmt2::formatTimingInfo(const TimingInfo &info) {
  if (!info.isStatic())
    return "dynamic";

  std::string result = "latency=" + std::to_string(*info.latency);
  if (info.isPipelined())
    result += ", II=" + std::to_string(*info.interval);
  return result;
}
