//===- TimingCompatibility.h - CMT2 Timing Compatibility ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the timing compatibility checking infrastructure for CMT2.
// It validates that call-site timing respects method contracts.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ANALYSIS_TIMINGCOMPATIBILITY_H
#define CIRCT_DIALECT_CMT2_ANALYSIS_TIMINGCOMPATIBILITY_H

#include "circt/Dialect/Cmt2/Analysis/TimingAnalysis.h"
#include "mlir/IR/Diagnostics.h"

namespace circt {
namespace cmt2 {

//===----------------------------------------------------------------------===//
// TimingCompatibility - Compatibility checking class
//===----------------------------------------------------------------------===//

/// Provides timing compatibility validation for CMT2 operations.
/// This class checks that call-site timing respects method contracts
/// and reports meaningful error messages.
class TimingCompatibility {
public:
  explicit TimingCompatibility(TimingAnalysis &analysis);

  /// Check that a call's timing is compatible with the method's timing.
  /// Returns success if compatible, failure with diagnostics if not.
  mlir::LogicalResult checkCallCompatibility(CallOp call);

  /// Check that a call's result timing is >= method output latency.
  /// This ensures we don't try to read results before they're available.
  mlir::LogicalResult checkResultTiming(CallOp call, const TimingInfo &methodTiming);

  /// Check that a call's arg timing is valid (stable during required period).
  mlir::LogicalResult checkArgTiming(CallOp call, const TimingInfo &methodTiming);

  /// Check that a call respects the method's initiation interval.
  /// This is used for pipelined methods to ensure proper spacing.
  mlir::LogicalResult checkIntervalCompatibility(CallOp call1, CallOp call2,
                                                  const TimingInfo &methodTiming);

  /// Check that all timing in a call is within the parent step's bounds.
  mlir::LogicalResult checkTimingBounds(CallOp call, int64_t stepLatency);

  /// Validate all calls within a static step for timing correctness.
  mlir::LogicalResult validateStaticStep(ProcStaticStepOp step);

  /// Validate all timing in a module.
  mlir::LogicalResult validateModule(ModuleOp module);

private:
  TimingAnalysis &analysis_;

  /// Get the parent static step for a call, if any.
  ProcStaticStepOp getParentStaticStep(CallOp call);

  /// Check if two timing intervals overlap.
  bool intervalsOverlap(TimingIntervalAttr a, TimingIntervalAttr b);
};

//===----------------------------------------------------------------------===//
// Diagnostic Helpers
//===----------------------------------------------------------------------===//

/// Emit a diagnostic for timing violation with source locations.
mlir::InFlightDiagnostic emitTimingError(mlir::Operation *op,
                                          llvm::StringRef message);

/// Format a timing interval for diagnostic output.
std::string formatTimingInterval(TimingIntervalAttr timing);

/// Format a timing info for diagnostic output.
std::string formatTimingInfo(const TimingInfo &info);

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ANALYSIS_TIMINGCOMPATIBILITY_H
