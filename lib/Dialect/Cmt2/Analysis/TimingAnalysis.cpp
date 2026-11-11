//===- TimingAnalysis.cpp - CMT2 Timing Analysis --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the timing analysis infrastructure for CMT2.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/TimingAnalysis.h"
#include "mlir/IR/SymbolTable.h"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// TimingAnalysis Implementation
//===----------------------------------------------------------------------===//

TimingAnalysis::TimingAnalysis(CircuitOp circuit) : circuit_(circuit) {}

TimingInfo TimingAnalysis::getMethodTiming(BindMethodOp method) {
  auto it = methodTimingCache_.find(method.getOperation());
  if (it != methodTimingCache_.end())
    return it->second;

  auto timing = extractBindMethodTiming(method);
  methodTimingCache_[method.getOperation()] = timing;
  return timing;
}

TimingInfo TimingAnalysis::getStepTiming(ProcStaticStepOp step) {
  auto it = stepTimingCache_.find(step.getOperation());
  if (it != stepTimingCache_.end())
    return it->second;

  auto timing = extractProcStaticStepTiming(step);
  stepTimingCache_[step.getOperation()] = timing;
  return timing;
}

TimingInfo TimingAnalysis::getMethodTiming(ProcMethodOp method) {
  auto it = methodTimingCache_.find(method.getOperation());
  if (it != methodTimingCache_.end())
    return it->second;

  auto timing = extractProcMethodTiming(method);
  methodTimingCache_[method.getOperation()] = timing;
  return timing;
}

std::optional<TimingInfo>
TimingAnalysis::lookupMethodTiming(ModuleOp module, SymbolRefAttr callee,
                                   SymbolRefAttr methodRef) {
  // Handle @this reference (calling own methods)
  if (callee.getRootReference() == "this") {
    // Look for method in current module
    auto *op = mlir::SymbolTable::lookupNearestSymbolFrom(
        module, methodRef.getRootReference());
    if (auto bindMethod = dyn_cast_or_null<BindMethodOp>(op))
      return getMethodTiming(bindMethod);
    if (auto procMethod = dyn_cast_or_null<ProcMethodOp>(op))
      return getMethodTiming(procMethod);
    return std::nullopt;
  }

  // Look up the instance
  auto *instanceOp = mlir::SymbolTable::lookupNearestSymbolFrom(
      module, callee.getRootReference());
  auto instance = dyn_cast_or_null<InstanceOp>(instanceOp);
  if (!instance)
    return std::nullopt;

  // Get the target module name
  auto targetModuleRef = instance.getModuleNameAttr();

  // Look up the method in the target module
  // For external modules, look up BindMethodOp
  // For regular modules, look up MethodOp or ProcMethodOp

  // Find the target module in the circuit
  auto *targetOp = mlir::SymbolTable::lookupNearestSymbolFrom(
      circuit_, targetModuleRef.getAttr());

  if (auto extModule = dyn_cast_or_null<ExtModuleFirrtlOp>(targetOp)) {
    // Look for BindMethodOp in external module
    for (auto &op : extModule.getBody().getOps()) {
      if (auto bindMethod = dyn_cast<BindMethodOp>(&op)) {
        if (bindMethod.getSymName() == methodRef.getRootReference())
          return getMethodTiming(bindMethod);
      }
    }
  } else if (auto cmt2Module = dyn_cast_or_null<ModuleOp>(targetOp)) {
    // Look for MethodOp or ProcMethodOp in CMT2 module
    for (auto &op : cmt2Module.getBody().getOps()) {
      if (auto procMethod = dyn_cast<ProcMethodOp>(&op)) {
        if (procMethod.getSymName() == methodRef.getRootReference())
          return getMethodTiming(procMethod);
      }
      // Note: Regular MethodOp is always dynamic
    }
  }

  return std::nullopt;
}

std::optional<int64_t> TimingAnalysis::inferStepLatency(ProcStaticStepOp step) {
  int64_t maxCycle = 0;

  // Walk all calls in the step's body
  step.getBody().walk([&](CallOp call) {
    int64_t callMax = getMaxCycleFromCall(call);
    maxCycle = std::max(maxCycle, callMax);
  });

  // If no timing guards were found, return the step's declared latency
  if (maxCycle == 0)
    return step.getLatency();

  // Return the maximum cycle + 1 (since timing is half-open intervals)
  return maxCycle;
}

std::optional<int64_t> TimingAnalysis::canPromoteToStatic(ProcMethodOp method) {
  // A method can be promoted to static if all called methods are static
  // and we can compute a total latency

  // If the method already has static_latency, use that
  if (method.isStatic())
    return method.getLatencyOrZero();

  // Get the parent module for step lookup
  auto module = method->getParentOfType<ModuleOp>();
  if (!module)
    return std::nullopt;

  // Compute latency from control region
  return computeControlLatency(method.getControl(), module);
}

//===----------------------------------------------------------------------===//
// Control Flow Latency Computation for Inference
//===----------------------------------------------------------------------===//

std::optional<int64_t> TimingAnalysis::computeControlLatency(Region &region,
                                                             ModuleOp module) {
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

std::optional<int64_t> TimingAnalysis::computeOpLatency(Operation *op,
                                                        ModuleOp module) {
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

void TimingAnalysis::invalidate() {
  methodTimingCache_.clear();
  stepTimingCache_.clear();
}

TimingInfo TimingAnalysis::extractBindMethodTiming(BindMethodOp method) {
  if (!method.isStatic())
    return TimingInfo::dynamic();

  int64_t latency = method.getLatencyOrZero();
  if (method.isPipelined()) {
    int64_t ii = method.getInitiationInterval();
    return TimingInfo::pipelined(latency, ii);
  }
  return TimingInfo::staticLatency(latency);
}

TimingInfo TimingAnalysis::extractProcStaticStepTiming(ProcStaticStepOp step) {
  int64_t latency = step.getLatency();
  if (step.isPipelined()) {
    int64_t ii = step.getInitiationInterval();
    return TimingInfo::pipelined(latency, ii);
  }
  return TimingInfo::staticLatency(latency);
}

TimingInfo TimingAnalysis::extractProcMethodTiming(ProcMethodOp method) {
  if (!method.isStatic())
    return TimingInfo::dynamic();

  int64_t latency = method.getLatencyOrZero();
  if (method.isPipelined()) {
    int64_t ii = method.getInitiationInterval();
    return TimingInfo::pipelined(latency, ii);
  }
  return TimingInfo::staticLatency(latency);
}

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

int64_t cmt2::getMaxCycleFromCall(CallOp call) {
  int64_t maxCycle = 0;

  // Check call issue timing.
  if (auto callTiming = call.getCallTiming()) {
    maxCycle = std::max(maxCycle, callTiming->getEnd());
  }

  // Check arg timing
  if (auto argTiming = call.getArgTiming()) {
    for (auto attr : *argTiming) {
      if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
        maxCycle = std::max(maxCycle, timing.getEnd());
      }
    }
  }

  // Check result timing
  if (auto resultTiming = call.getResultTiming()) {
    for (auto attr : *resultTiming) {
      if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
        maxCycle = std::max(maxCycle, timing.getEnd());
      }
    }
  }

  return maxCycle;
}

LogicalResult cmt2::checkCallResultTiming(CallOp call, int64_t methodLatency) {
  int64_t callStart = 0;
  if (auto callTiming = call.getCallTiming())
    callStart = callTiming->getStart();

  if (auto resultTiming = call.getResultTiming()) {
    for (size_t i = 0; i < resultTiming->size(); ++i) {
      if (auto timing = dyn_cast<TimingIntervalAttr>((*resultTiming)[i])) {
        int64_t requiredStart = callStart + methodLatency;
        if (timing.getStart() < requiredStart) {
          return call.emitOpError("result ")
                 << i << " timing [" << timing.getStart() << ", "
                 << timing.getEnd() << ") starts before method latency ("
                 << methodLatency << ") from call start cycle (" << callStart
                 << "), requires >= " << requiredStart;
        }
        // Restriction (initial): require capture exactly at the declared
        // availability cycle.
        if (timing.getStart() != requiredStart) {
          return call.emitOpError("result ")
                 << i << " timing [" << timing.getStart() << ", "
                 << timing.getEnd() << ") does not match required capture cycle ("
                 << requiredStart << ") for method latency (" << methodLatency
                 << ") from call start cycle (" << callStart << ")";
        }
      }
    }
  }
  return success();
}

LogicalResult cmt2::checkCallArgTimingBounds(CallOp call, int64_t stepLatency) {
  if (auto callTiming = call.getCallTiming()) {
    if (callTiming->getEnd() > stepLatency) {
      return call.emitOpError("call_timing [")
             << callTiming->getStart() << ", " << callTiming->getEnd()
             << ") extends beyond step latency (" << stepLatency << ")";
    }
  }
  if (auto argTiming = call.getArgTiming()) {
    for (size_t i = 0; i < argTiming->size(); ++i) {
      if (auto timing = dyn_cast<TimingIntervalAttr>((*argTiming)[i])) {
        if (timing.getEnd() > stepLatency) {
          return call.emitOpError("arg ")
                 << i << " timing [" << timing.getStart() << ", "
                 << timing.getEnd() << ") extends beyond step latency ("
                 << stepLatency << ")";
        }
      }
    }
  }
  if (auto resultTiming = call.getResultTiming()) {
    for (size_t i = 0; i < resultTiming->size(); ++i) {
      if (auto timing = dyn_cast<TimingIntervalAttr>((*resultTiming)[i])) {
        if (timing.getEnd() > stepLatency) {
          return call.emitOpError("result ")
                 << i << " timing [" << timing.getStart() << ", "
                 << timing.getEnd() << ") extends beyond step latency ("
                 << stepLatency << ")";
        }
      }
    }
  }
  return success();
}
