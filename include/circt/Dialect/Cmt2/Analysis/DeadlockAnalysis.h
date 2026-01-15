//===- DeadlockAnalysis.h - CMT2 Deadlock Analysis --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the deadlock analysis for CMT2 dataflow pipelines.
// It detects potential deadlocks in cyclic token dependencies.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ANALYSIS_DEADLOCKANALYSIS_H
#define CIRCT_DIALECT_CMT2_ANALYSIS_DEADLOCKANALYSIS_H

#include "circt/Dialect/Cmt2/Analysis/FIFODepthAnalysis.h"
#include "circt/Dialect/Cmt2/Analysis/TokenAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace circt {
namespace cmt2 {

//===----------------------------------------------------------------------===//
// TokenCycle - Represents a cycle in the token dependency graph
//===----------------------------------------------------------------------===//

/// Represents a cycle of tokens in the dependency graph.
struct TokenCycle {
  /// Tokens in the cycle, in order
  llvm::SmallVector<mlir::Value, 8> tokens;

  /// Operations in the cycle, in order
  llvm::SmallVector<mlir::Operation *, 8> operations;

  /// Number of LI tokens in the cycle
  unsigned numLITokens;

  /// Total FIFO depth in the cycle (sum of all LI token depths)
  unsigned totalFIFODepth;

  /// Whether this cycle is potentially deadlocking
  bool isPotentialDeadlock() const {
    // A cycle can deadlock if:
    // 1. It has LI tokens (FIFOs can fill)
    // 2. Total FIFO depth is less than number of LI tokens in cycle
    return numLITokens > 0 && totalFIFODepth < numLITokens;
  }

  /// Get a human-readable description of the cycle
  std::string describe() const;
};

//===----------------------------------------------------------------------===//
// DeadlockAnalysis - Main analysis class
//===----------------------------------------------------------------------===//

/// Analyzes token dependency graphs for potential deadlocks.
/// Deadlock can occur when cyclic dependencies with LI tokens have
/// insufficient total FIFO depth to prevent all FIFOs from filling.
class DeadlockAnalysis {
public:
  explicit DeadlockAnalysis(CircuitOp circuit);

  /// Check for deadlocks in a module
  /// Returns success if no deadlock detected, failure with diagnostic otherwise
  mlir::LogicalResult checkForDeadlock(ModuleOp module);

  /// Check for deadlocks in a proc.dataflow
  mlir::LogicalResult checkForDeadlock(ProcDataflowOp dataflow);

  /// Find all cycles in a token graph
  llvm::SmallVector<TokenCycle, 4> findCycles(const TokenGraph &graph);

  /// Find cycles containing only LI tokens
  llvm::SmallVector<TokenCycle, 4> findLICycles(const TokenGraph &graph);

  /// Get detected cycles from last analysis
  llvm::ArrayRef<TokenCycle> getDetectedCycles() const { return detectedCycles_; }

  /// Clear cached analysis results
  void invalidate();

private:
  CircuitOp circuit_;

  /// Token analysis for dependency information
  TokenAnalysis tokenAnalysis_;

  /// FIFO depth analysis for depth information
  FIFODepthAnalysis fifoAnalysis_;

  /// Cycles detected in last analysis
  llvm::SmallVector<TokenCycle, 4> detectedCycles_;

  /// Helper to find cycles using DFS
  void findCyclesDFS(mlir::Operation *current,
                     llvm::SmallVector<mlir::Operation *, 16> &path,
                     llvm::DenseSet<mlir::Operation *> &visited,
                     llvm::DenseSet<mlir::Operation *> &inStack,
                     const TokenGraph &graph,
                     llvm::SmallVector<TokenCycle, 4> &cycles);

  /// Helper to build a TokenCycle from a path
  TokenCycle buildCycle(llvm::ArrayRef<mlir::Operation *> cyclePath,
                        const TokenGraph &graph);

  /// Check a single cycle for deadlock
  mlir::LogicalResult checkCycleForDeadlock(const TokenCycle &cycle,
                                             mlir::Operation *loc);
};

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ANALYSIS_DEADLOCKANALYSIS_H
