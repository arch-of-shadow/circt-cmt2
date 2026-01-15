//===- TokenAnalysis.cpp - CMT2 Token Analysis --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the token analysis infrastructure for CMT2 dataflow
// pipelines.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/TokenAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"

using namespace circt;
using namespace circt::cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

bool cmt2::isSyncTokenType(Type type) {
  return llvm::isa<SyncTokenType>(type);
}

Type cmt2::getTokenDataType(Type tokenType) {
  if (auto syncToken = llvm::dyn_cast<SyncTokenType>(tokenType))
    return syncToken.getDataType();
  return nullptr;
}

bool cmt2::isTokenModeLS(Type tokenType) {
  if (auto syncToken = llvm::dyn_cast<SyncTokenType>(tokenType))
    return syncToken.getMode() == TokenMode::LS;
  return true; // Default to LS
}

bool cmt2::producesTokens(Operation *op) {
  for (Type resultType : op->getResultTypes()) {
    if (isSyncTokenType(resultType))
      return true;
  }
  return false;
}

bool cmt2::consumesTokens(Operation *op) {
  for (Value operand : op->getOperands()) {
    if (isSyncTokenType(operand.getType()))
      return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// TokenInfo
//===----------------------------------------------------------------------===//

Type TokenInfo::getDataType() const {
  return cmt2::getTokenDataType(token.getType());
}

//===----------------------------------------------------------------------===//
// TokenGraph
//===----------------------------------------------------------------------===//

TokenGraph TokenGraph::build(ModuleOp module) {
  TokenGraph graph;

  // Walk all operations in the module looking for token values
  module.walk([&](Operation *op) {
    // Check results for token types
    for (Value result : op->getResults()) {
      if (isSyncTokenType(result.getType())) {
        graph.analyzeToken(result);
      }
    }

    // Also check block arguments (e.g., rule token inputs)
    for (Region &region : op->getRegions()) {
      for (Block &block : region.getBlocks()) {
        for (BlockArgument arg : block.getArguments()) {
          if (isSyncTokenType(arg.getType())) {
            graph.analyzeToken(arg);
          }
        }
      }
    }
  });

  return graph;
}

TokenGraph TokenGraph::build(ProcDataflowOp dataflow) {
  TokenGraph graph;

  // Walk all operations in the dataflow body
  dataflow.walk([&](Operation *op) {
    for (Value result : op->getResults()) {
      if (isSyncTokenType(result.getType())) {
        graph.analyzeToken(result);
      }
    }
  });

  return graph;
}

void TokenGraph::analyzeToken(Value token) {
  // Skip if already analyzed
  if (tokenIndex_.count(token))
    return;

  TokenInfo info;
  info.token = token;
  info.producer = token.getDefiningOp();
  info.isLatencyInsensitive = isTokenLI(token);
  info.timing = getTokenTiming(token);

  // Find all consumers using def-use chains
  for (OpOperand &use : token.getUses()) {
    Operation *consumer = use.getOwner();
    info.consumers.push_back(consumer);

    // Update consumer map
    consumerMap_[consumer].push_back(token);
  }

  // Update producer map if there's a defining op
  if (info.producer) {
    producerMap_[info.producer].push_back(token);
  }

  // Add to tokens list and index
  size_t idx = tokens_.size();
  tokens_.push_back(std::move(info));
  tokenIndex_[token] = idx;
}

bool TokenGraph::isTokenLI(Value token) {
  return !isTokenModeLS(token.getType());
}

std::optional<TimingIntervalAttr> TokenGraph::getTokenTiming(Value token) {
  Operation *defOp = token.getDefiningOp();
  if (!defOp)
    return std::nullopt;

  // Check for timing attribute on the defining op
  if (auto timing = defOp->getAttrOfType<TimingIntervalAttr>("timing"))
    return timing;

  // For DataflowTaskOp, check task's timing
  if (auto taskOp = dyn_cast<DataflowTaskOp>(defOp))
    return taskOp.getTiming();

  // For TokenCreateOp, check its timing attribute
  if (isa<TokenCreateOp>(defOp))
    return defOp->getAttrOfType<TimingIntervalAttr>("timing");

  return std::nullopt;
}

const TokenInfo *TokenGraph::getTokenInfo(Value token) const {
  auto it = tokenIndex_.find(token);
  if (it == tokenIndex_.end())
    return nullptr;
  return &tokens_[it->second];
}

SmallVector<Value, 4> TokenGraph::getProducedTokens(Operation *op) const {
  auto it = producerMap_.find(op);
  if (it == producerMap_.end())
    return {};
  SmallVector<Value, 4> result(it->second.begin(), it->second.end());
  return result;
}

SmallVector<Value, 4> TokenGraph::getConsumedTokens(Operation *op) const {
  auto it = consumerMap_.find(op);
  if (it == consumerMap_.end())
    return {};
  SmallVector<Value, 4> result(it->second.begin(), it->second.end());
  return result;
}

SmallVector<Value, 8> TokenGraph::getLatencySensitiveTokens() const {
  SmallVector<Value, 8> result;
  for (const TokenInfo &info : tokens_) {
    if (!info.isLatencyInsensitive)
      result.push_back(info.token);
  }
  return result;
}

SmallVector<Value, 8> TokenGraph::getLatencyInsensitiveTokens() const {
  SmallVector<Value, 8> result;
  for (const TokenInfo &info : tokens_) {
    if (info.isLatencyInsensitive)
      result.push_back(info.token);
  }
  return result;
}

bool TokenGraph::hasPath(Operation *producer, Operation *consumer) const {
  if (producer == consumer)
    return true;

  // BFS through token dependencies
  llvm::DenseSet<Operation *> visited;
  SmallVector<Operation *, 16> worklist;
  worklist.push_back(producer);

  while (!worklist.empty()) {
    Operation *current = worklist.pop_back_val();
    if (!visited.insert(current).second)
      continue;

    // Get tokens produced by current operation
    auto producedTokens = getProducedTokens(current);
    for (Value token : producedTokens) {
      const TokenInfo *info = getTokenInfo(token);
      if (!info)
        continue;

      for (Operation *nextOp : info->consumers) {
        if (nextOp == consumer)
          return true;
        worklist.push_back(nextOp);
      }
    }
  }

  return false;
}

SmallVector<Operation *, 16> TokenGraph::getTopologicalOrder() const {
  SmallVector<Operation *, 16> result;
  llvm::DenseSet<Operation *> visited;
  llvm::DenseSet<Operation *> allOps;

  // Collect all operations involved in token flow
  for (const TokenInfo &info : tokens_) {
    if (info.producer)
      allOps.insert(info.producer);
    for (Operation *consumer : info.consumers)
      allOps.insert(consumer);
  }

  // Topological sort using DFS post-order
  std::function<void(Operation *)> visit = [&](Operation *op) {
    if (!visited.insert(op).second)
      return;

    // Visit all successors first
    auto producedTokens = getProducedTokens(op);
    for (Value token : producedTokens) {
      const TokenInfo *info = getTokenInfo(token);
      if (!info)
        continue;
      for (Operation *consumer : info->consumers)
        visit(consumer);
    }

    result.push_back(op);
  };

  for (Operation *op : allOps)
    visit(op);

  // Reverse to get topological order
  std::reverse(result.begin(), result.end());
  return result;
}

//===----------------------------------------------------------------------===//
// TokenAnalysis
//===----------------------------------------------------------------------===//

TokenAnalysis::TokenAnalysis(CircuitOp circuit) : circuit_(circuit) {}

const TokenGraph &TokenAnalysis::getTokenGraph(ModuleOp module) {
  auto it = moduleGraphCache_.find(module.getOperation());
  if (it != moduleGraphCache_.end())
    return it->second;

  auto [insertIt, inserted] = moduleGraphCache_.try_emplace(
      module.getOperation(), TokenGraph::build(module));
  return insertIt->second;
}

const TokenGraph &TokenAnalysis::getTokenGraph(ProcDataflowOp dataflow) {
  auto it = dataflowGraphCache_.find(dataflow.getOperation());
  if (it != dataflowGraphCache_.end())
    return it->second;

  auto [insertIt, inserted] = dataflowGraphCache_.try_emplace(
      dataflow.getOperation(), TokenGraph::build(dataflow));
  return insertIt->second;
}

std::optional<TimingIntervalAttr> TokenAnalysis::getTokenTiming(Value token) {
  return TokenGraph::getTokenTiming(token);
}

bool TokenAnalysis::isLatencySensitive(Value token) {
  return isTokenModeLS(token.getType());
}

SmallVector<Operation *, 4> TokenAnalysis::getConsumers(Value token) {
  SmallVector<Operation *, 4> result;
  for (OpOperand &use : token.getUses()) {
    result.push_back(use.getOwner());
  }
  return result;
}

Operation *TokenAnalysis::getProducer(Value token) {
  return token.getDefiningOp();
}

LogicalResult TokenAnalysis::checkCoordination(ModuleOp module) {
  const TokenGraph &graph = getTokenGraph(module);
  return checkGraphCoordination(graph, module.getOperation());
}

LogicalResult TokenAnalysis::checkCoordination(ProcDataflowOp dataflow) {
  const TokenGraph &graph = getTokenGraph(dataflow);
  return checkGraphCoordination(graph, dataflow.getOperation());
}

LogicalResult TokenAnalysis::checkGraphCoordination(const TokenGraph &graph,
                                                     Operation *loc) {
  // Check each LS token for timing coordination
  for (Value token : graph.getLatencySensitiveTokens()) {
    const TokenInfo *info = graph.getTokenInfo(token);
    if (!info)
      continue;

    // If token has multiple consumers, they must all fire at the same time
    // This is implicitly satisfied for LS tokens (all consumers see valid
    // in same cycle)

    // Check timing consistency: if token has timing, consumers should
    // have compatible timing
    if (info->timing.has_value()) {
      TimingIntervalAttr prodTiming = *info->timing;

      // For each consumer, check if it has compatible timing
      for (Operation *consumer : info->consumers) {
        // Get consumer's timing if available
        auto consumerTiming = consumer->getAttrOfType<TimingIntervalAttr>("timing");
        if (consumerTiming) {
          // Consumer should start when token is ready (at producer's end)
          int64_t tokenReady = prodTiming.getEnd();
          int64_t consumerStart = consumerTiming.getStart();

          if (consumerStart < tokenReady) {
            return consumer->emitError()
                   << "consumer starts at cycle " << consumerStart
                   << " but token is not ready until cycle " << tokenReady;
          }
        }
      }
    }
  }

  return success();
}

void TokenAnalysis::invalidate() {
  moduleGraphCache_.clear();
  dataflowGraphCache_.clear();
}
