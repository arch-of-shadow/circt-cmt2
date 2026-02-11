//===- TokenAnalysis.h - CMT2 Token Analysis --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the token analysis infrastructure for CMT2 dataflow
// pipelines. It provides token dependency graph construction, consumer
// enumeration, and timing coordination validation using MLIR def-use chains.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ANALYSIS_TOKENANALYSIS_H
#define CIRCT_DIALECT_CMT2_ANALYSIS_TOKENANALYSIS_H

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SetVector.h"

namespace circt {
namespace cmt2 {

//===----------------------------------------------------------------------===//
// TokenInfo - Information about a single token
//===----------------------------------------------------------------------===//

/// Holds information about a token value.
struct TokenInfo {
  /// The token value (SSA value of SyncTokenType)
  mlir::Value token;

  /// The operation that produces this token
  mlir::Operation *producer;

  /// All operations that consume this token
  llvm::SmallVector<mlir::Operation *, 4> consumers;

  /// Timing attribute if present
  std::optional<TimingIntervalAttr> timing;

  /// Whether this token is latency-insensitive
  bool isLatencyInsensitive;

  /// Returns true if this token has multiple consumers (fork pattern)
  bool isMultiConsumer() const { return consumers.size() > 1; }

  /// Returns the data type carried by this token, or nullptr if none
  mlir::Type getDataType() const;
};

//===----------------------------------------------------------------------===//
// TokenGraph - Token dependency graph for a dataflow region
//===----------------------------------------------------------------------===//

/// Represents the token dependency graph for a module or dataflow region.
/// Uses MLIR def-use chains to track token flow between operations.
class TokenGraph {
public:
  /// Build the token graph for a module
  static TokenGraph build(ModuleOp module);

  /// Build the token graph for a proc.dataflow operation
  static TokenGraph build(ProcDataflowOp dataflow);

  /// Get all tokens in the graph
  llvm::ArrayRef<TokenInfo> getTokens() const { return tokens_; }

  /// Get TokenInfo for a specific token value
  const TokenInfo *getTokenInfo(mlir::Value token) const;

  /// Get all tokens produced by an operation
  llvm::SmallVector<mlir::Value, 4> getProducedTokens(mlir::Operation *op) const;

  /// Get all tokens consumed by an operation
  llvm::SmallVector<mlir::Value, 4> getConsumedTokens(mlir::Operation *op) const;

  /// Get all tokens that are latency-sensitive
  llvm::SmallVector<mlir::Value, 8> getLatencySensitiveTokens() const;

  /// Get all tokens that are latency-insensitive
  llvm::SmallVector<mlir::Value, 8> getLatencyInsensitiveTokens() const;

  /// Check if there's a path from producer to consumer through tokens
  bool hasPath(mlir::Operation *producer, mlir::Operation *consumer) const;

  /// Get all operations in topological order (respecting token dependencies)
  llvm::SmallVector<mlir::Operation *, 16> getTopologicalOrder() const;

  /// Helper to get timing from token's defining op (public for TokenAnalysis)
  static std::optional<TimingIntervalAttr> getTokenTiming(mlir::Value token);

private:
  /// All tokens in the graph
  llvm::SmallVector<TokenInfo, 16> tokens_;

  /// Map from token value to index in tokens_
  llvm::DenseMap<mlir::Value, size_t> tokenIndex_;

  /// Map from operation to produced tokens
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Value, 2>>
      producerMap_;

  /// Map from operation to consumed tokens
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Value, 2>>
      consumerMap_;

  /// Helper to analyze a token value and add to graph
  void analyzeToken(mlir::Value token);

  /// Helper to determine if token is latency-insensitive
  static bool isTokenLI(mlir::Value token);
};

//===----------------------------------------------------------------------===//
// TokenAnalysis - Main analysis class
//===----------------------------------------------------------------------===//

/// Provides token analysis for CMT2 circuits with dataflow constructs.
/// This class analyzes token flow, validates timing coordination,
/// and provides queries for downstream passes.
class TokenAnalysis {
public:
  explicit TokenAnalysis(CircuitOp circuit);

  /// Get or build the token graph for a module
  const TokenGraph &getTokenGraph(ModuleOp module);

  /// Get or build the token graph for a proc.dataflow
  const TokenGraph &getTokenGraph(ProcDataflowOp dataflow);

  /// Get timing attribute for a token
  std::optional<TimingIntervalAttr> getTokenTiming(mlir::Value token);

  /// Check if a token is latency-sensitive
  bool isLatencySensitive(mlir::Value token);

  /// Get all consumers of a token
  llvm::SmallVector<mlir::Operation *, 4> getConsumers(mlir::Value token);

  /// Get the producer of a token
  mlir::Operation *getProducer(mlir::Value token);

  /// Validate timing coordination for all tokens in a module
  /// Returns success if all tokens have consistent timing
  mlir::LogicalResult checkCoordination(ModuleOp module);

  /// Validate timing coordination for a proc.dataflow
  mlir::LogicalResult checkCoordination(ProcDataflowOp dataflow);

  /// Clear cached analysis results
  void invalidate();

private:
  CircuitOp circuit_;

  /// Cache of token graphs by module
  llvm::DenseMap<mlir::Operation *, TokenGraph> moduleGraphCache_;

  /// Cache of token graphs by proc.dataflow
  llvm::DenseMap<mlir::Operation *, TokenGraph> dataflowGraphCache_;

  /// Helper to validate timing for a token graph
  mlir::LogicalResult checkGraphCoordination(const TokenGraph &graph,
                                              mlir::Operation *loc);
};

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

/// Check if a type is a SyncTokenType
bool isSyncTokenType(mlir::Type type);

/// Get the data type from a SyncTokenType, or nullptr if none
mlir::Type getTokenDataType(mlir::Type tokenType);

/// Get the token mode (LS/LI) from a SyncTokenType
/// Returns true for LS (latency-sensitive), false for LI
bool isTokenModeLS(mlir::Type tokenType);

/// Check if an operation produces tokens
bool producesTokens(mlir::Operation *op);

/// Check if an operation consumes tokens
bool consumesTokens(mlir::Operation *op);

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ANALYSIS_TOKENANALYSIS_H
