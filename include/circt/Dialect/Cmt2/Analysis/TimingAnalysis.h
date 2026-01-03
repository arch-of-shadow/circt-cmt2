//===- TimingAnalysis.h - CMT2 Timing Analysis ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the timing analysis infrastructure for CMT2.
// It provides timing information lookup, latency inference, and timing
// compatibility checking for cycle-precise static scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ANALYSIS_TIMINGANALYSIS_H
#define CIRCT_DIALECT_CMT2_ANALYSIS_TIMINGANALYSIS_H

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include <optional>

namespace circt {
namespace cmt2 {

//===----------------------------------------------------------------------===//
// TimingInfo - Timing information for a method or step
//===----------------------------------------------------------------------===//

/// Holds timing information for a method or static step.
struct TimingInfo {
  /// Static latency in cycles (nullopt if dynamic)
  std::optional<int64_t> latency;

  /// Initiation interval for pipelined operations (nullopt if not pipelined)
  std::optional<int64_t> interval;

  /// Returns true if this has static timing (known latency)
  bool isStatic() const { return latency.has_value(); }

  /// Returns true if this is pipelined (II < latency)
  bool isPipelined() const {
    return interval.has_value() && latency.has_value() &&
           *interval < *latency;
  }

  /// Get the initiation interval, or latency if not pipelined
  int64_t getInitiationInterval() const {
    if (interval.has_value())
      return *interval;
    if (latency.has_value())
      return *latency;
    return 1; // Default for dynamic
  }

  /// Create a dynamic (unknown) timing info
  static TimingInfo dynamic() { return TimingInfo{}; }

  /// Create a static timing info with given latency
  static TimingInfo staticLatency(int64_t lat) {
    TimingInfo info;
    info.latency = lat;
    return info;
  }

  /// Create a pipelined timing info with latency and II
  static TimingInfo pipelined(int64_t lat, int64_t ii) {
    TimingInfo info;
    info.latency = lat;
    info.interval = ii;
    return info;
  }
};

//===----------------------------------------------------------------------===//
// TimingAnalysis - Main analysis class
//===----------------------------------------------------------------------===//

/// Provides timing analysis for CMT2 circuits.
/// This class caches timing information and provides methods to:
/// - Look up timing for methods, values, and steps
/// - Infer latency from control structure
/// - Check timing compatibility
class TimingAnalysis {
public:
  explicit TimingAnalysis(CircuitOp circuit);

  /// Get timing info for a BindMethodOp
  TimingInfo getMethodTiming(BindMethodOp method);

  /// Get timing info for a ProcStaticStepOp
  TimingInfo getStepTiming(ProcStaticStepOp step);

  /// Get timing info for a ProcMethodOp
  TimingInfo getMethodTiming(ProcMethodOp method);

  /// Look up timing for a method by symbol reference
  /// Returns nullopt if method not found
  std::optional<TimingInfo> lookupMethodTiming(ModuleOp module,
                                               mlir::SymbolRefAttr callee,
                                               mlir::SymbolRefAttr methodRef);

  /// Infer latency for a static step from its body
  /// This computes the minimum latency needed based on all calls within
  std::optional<int64_t> inferStepLatency(ProcStaticStepOp step);

  /// Check if a method can be promoted to static
  /// Returns the inferred latency if promotable, nullopt otherwise
  std::optional<int64_t> canPromoteToStatic(ProcMethodOp method);

  /// Clear cached analysis results
  void invalidate();

private:
  CircuitOp circuit_;

  /// Cache of method timing info by symbol
  llvm::DenseMap<mlir::Operation *, TimingInfo> methodTimingCache_;

  /// Cache of step timing info
  llvm::DenseMap<mlir::Operation *, TimingInfo> stepTimingCache_;

  /// Helper to extract timing from a BindMethodOp
  TimingInfo extractBindMethodTiming(BindMethodOp method);

  /// Helper to extract timing from a ProcStaticStepOp
  TimingInfo extractProcStaticStepTiming(ProcStaticStepOp step);

  /// Helper to extract timing from a ProcMethodOp
  TimingInfo extractProcMethodTiming(ProcMethodOp method);
};

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

/// Get the maximum cycle used in a call's timing guards
/// Returns 0 if no timing guards are present
int64_t getMaxCycleFromCall(CallOp call);

/// Check if a call's result timing is compatible with method latency
/// Returns success if compatible, failure with diagnostic if not
mlir::LogicalResult checkCallResultTiming(CallOp call, int64_t methodLatency);

/// Check if a call's arg timing is within step bounds
/// Returns success if valid, failure with diagnostic if not
mlir::LogicalResult checkCallArgTimingBounds(CallOp call, int64_t stepLatency);

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ANALYSIS_TIMINGANALYSIS_H
