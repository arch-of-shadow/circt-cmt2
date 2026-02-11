//===- FIFODepthAnalysis.h - CMT2 FIFO Depth Analysis -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FIFO depth analysis for CMT2 latency-insensitive
// tokens. It infers the minimum FIFO depth required to prevent deadlock
// and ensure correct operation.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ANALYSIS_FIFODEPTHANALYSIS_H
#define CIRCT_DIALECT_CMT2_ANALYSIS_FIFODEPTHANALYSIS_H

#include "circt/Dialect/Cmt2/Analysis/TokenAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"

namespace circt {
namespace cmt2 {

//===----------------------------------------------------------------------===//
// FIFODepthInfo - Information about a single FIFO
//===----------------------------------------------------------------------===//

/// Holds information about FIFO depth for a token.
struct FIFODepthInfo {
  /// The token value
  mlir::Value token;

  /// Inferred minimum depth
  unsigned minDepth;

  /// Reason for the inferred depth
  enum class DepthReason {
    Default,           // Default depth of 1
    RateMismatch,      // Producer faster than consumer
    LatencyVariation,  // Variable latency in path
    ForkJoin,          // Fork-join pattern with different consumer latencies
    UserSpecified      // User-provided depth
  };

  DepthReason reason;

  /// Producer firing rate (cycles per firing)
  unsigned producerRate;

  /// Minimum consumer firing rate (slowest consumer)
  unsigned minConsumerRate;

  /// Maximum latency variation in the path
  unsigned maxLatencyVariation;

  /// Create default depth info
  static FIFODepthInfo defaultDepth(mlir::Value token) {
    FIFODepthInfo info;
    info.token = token;
    info.minDepth = 1;
    info.reason = DepthReason::Default;
    info.producerRate = 1;
    info.minConsumerRate = 1;
    info.maxLatencyVariation = 0;
    return info;
  }
};

//===----------------------------------------------------------------------===//
// FIFODepthAnalysis - Main analysis class
//===----------------------------------------------------------------------===//

/// Analyzes latency-insensitive tokens to infer minimum FIFO depth.
/// The analysis considers:
/// - Producer/consumer firing rates
/// - Latency variation in paths
/// - Fork-join patterns with different consumer latencies
class FIFODepthAnalysis {
public:
  explicit FIFODepthAnalysis(CircuitOp circuit);

  /// Infer minimum FIFO depth for a single LI token
  FIFODepthInfo inferDepth(mlir::Value token);

  /// Infer FIFO depths for all LI tokens in a module
  llvm::DenseMap<mlir::Value, FIFODepthInfo> inferAllDepths(ModuleOp module);

  /// Infer FIFO depths for all LI tokens in a proc.dataflow
  llvm::DenseMap<mlir::Value, FIFODepthInfo>
  inferAllDepths(ProcDataflowOp dataflow);

  /// Get the inferred depth for a token (0 if not LI or not analyzed)
  unsigned getDepth(mlir::Value token) const;

  /// Check if a token has been analyzed
  bool hasDepth(mlir::Value token) const;

  /// Clear cached analysis results
  void invalidate();

private:
  CircuitOp circuit_;

  /// Token analysis for dependency information
  TokenAnalysis tokenAnalysis_;

  /// Cache of inferred depths
  llvm::DenseMap<mlir::Value, FIFODepthInfo> depthCache_;

  /// Analyze producer firing rate
  unsigned analyzeProducerRate(mlir::Operation *producer);

  /// Analyze consumer firing rate
  unsigned analyzeConsumerRate(mlir::Operation *consumer);

  /// Analyze latency variation in the path from producer to consumers
  unsigned analyzeLatencyVariation(mlir::Value token,
                                   const TokenGraph &graph);

  /// Compute buffer depth for rate mismatch
  unsigned computeRateMismatchDepth(unsigned prodRate, unsigned consRate);
};

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ANALYSIS_FIFODEPTHANALYSIS_H
