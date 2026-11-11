//===- FIFODepthAnalysis.cpp - CMT2 FIFO Depth Analysis -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the FIFO depth analysis for CMT2 latency-insensitive
// tokens.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/FIFODepthAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"

using namespace circt;
using namespace circt::cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// FIFODepthAnalysis
//===----------------------------------------------------------------------===//

FIFODepthAnalysis::FIFODepthAnalysis(CircuitOp circuit)
    : circuit_(circuit), tokenAnalysis_(circuit) {}

FIFODepthInfo FIFODepthAnalysis::inferDepth(Value token) {
  // Check cache
  auto it = depthCache_.find(token);
  if (it != depthCache_.end())
    return it->second;

  // Only LI tokens need FIFO depth analysis
  if (isTokenModeLS(token.getType())) {
    auto info = FIFODepthInfo::defaultDepth(token);
    info.minDepth = 0; // LS tokens don't need FIFOs
    depthCache_[token] = info;
    return info;
  }

  FIFODepthInfo info = FIFODepthInfo::defaultDepth(token);

  // Get producer
  Operation *producer = token.getDefiningOp();
  if (!producer) {
    depthCache_[token] = info;
    return info;
  }

  // Get consumers
  auto consumers = tokenAnalysis_.getConsumers(token);
  if (consumers.empty()) {
    depthCache_[token] = info;
    return info;
  }

  // 1. Analyze producer rate
  info.producerRate = analyzeProducerRate(producer);

  // 2. Analyze consumer rates and find minimum (slowest)
  info.minConsumerRate = UINT_MAX;
  for (Operation *consumer : consumers) {
    unsigned rate = analyzeConsumerRate(consumer);
    info.minConsumerRate = std::min(info.minConsumerRate, rate);
  }
  if (info.minConsumerRate == UINT_MAX)
    info.minConsumerRate = 1;

  // 3. Check for rate mismatch
  if (info.producerRate < info.minConsumerRate) {
    // Producer is faster than consumers - need buffering
    unsigned rateMismatchDepth =
        computeRateMismatchDepth(info.producerRate, info.minConsumerRate);
    if (rateMismatchDepth > info.minDepth) {
      info.minDepth = rateMismatchDepth;
      info.reason = FIFODepthInfo::DepthReason::RateMismatch;
    }
  }

  // 4. Analyze latency variation
  // Find parent module or dataflow for token graph
  Operation *parentOp = producer->getParentOp();
  while (parentOp && !isa<ModuleOp, ProcDataflowOp>(parentOp)) {
    parentOp = parentOp->getParentOp();
  }

  if (auto module = dyn_cast_or_null<ModuleOp>(parentOp)) {
    const TokenGraph &graph = tokenAnalysis_.getTokenGraph(module);
    info.maxLatencyVariation = analyzeLatencyVariation(token, graph);
  } else if (auto dataflow = dyn_cast_or_null<ProcDataflowOp>(parentOp)) {
    const TokenGraph &graph = tokenAnalysis_.getTokenGraph(dataflow);
    info.maxLatencyVariation = analyzeLatencyVariation(token, graph);
  }

  if (info.maxLatencyVariation > 0) {
    unsigned latencyDepth = info.maxLatencyVariation + 1;
    if (latencyDepth > info.minDepth) {
      info.minDepth = latencyDepth;
      info.reason = FIFODepthInfo::DepthReason::LatencyVariation;
    }
  }

  // 5. Check for fork-join pattern
  if (consumers.size() > 1) {
    // Multiple consumers - check for different latencies
    llvm::SmallVector<int64_t, 4> consumerLatencies;
    for (Operation *consumer : consumers) {
      auto timing = consumer->getAttrOfType<TimingIntervalAttr>("timing");
      if (timing) {
        consumerLatencies.push_back(timing.getEnd() - timing.getStart());
      }
    }

    if (consumerLatencies.size() > 1) {
      int64_t maxLat =
          *std::max_element(consumerLatencies.begin(), consumerLatencies.end());
      int64_t minLat =
          *std::min_element(consumerLatencies.begin(), consumerLatencies.end());
      unsigned forkJoinDepth = static_cast<unsigned>(maxLat - minLat + 1);

      if (forkJoinDepth > info.minDepth) {
        info.minDepth = forkJoinDepth;
        info.reason = FIFODepthInfo::DepthReason::ForkJoin;
      }
    }
  }

  // Ensure minimum depth of 1 for all LI tokens
  if (info.minDepth < 1)
    info.minDepth = 1;

  depthCache_[token] = info;
  return info;
}

DenseMap<Value, FIFODepthInfo> FIFODepthAnalysis::inferAllDepths(ModuleOp module) {
  DenseMap<Value, FIFODepthInfo> result;

  const TokenGraph &graph = tokenAnalysis_.getTokenGraph(module);
  for (Value token : graph.getLatencyInsensitiveTokens()) {
    result[token] = inferDepth(token);
  }

  return result;
}

DenseMap<Value, FIFODepthInfo>
FIFODepthAnalysis::inferAllDepths(ProcDataflowOp dataflow) {
  DenseMap<Value, FIFODepthInfo> result;

  const TokenGraph &graph = tokenAnalysis_.getTokenGraph(dataflow);
  for (Value token : graph.getLatencyInsensitiveTokens()) {
    result[token] = inferDepth(token);
  }

  return result;
}

unsigned FIFODepthAnalysis::getDepth(Value token) const {
  auto it = depthCache_.find(token);
  if (it == depthCache_.end())
    return 0;
  return it->second.minDepth;
}

bool FIFODepthAnalysis::hasDepth(Value token) const {
  return depthCache_.count(token) > 0;
}

void FIFODepthAnalysis::invalidate() {
  depthCache_.clear();
  tokenAnalysis_.invalidate();
}

unsigned FIFODepthAnalysis::analyzeProducerRate(Operation *producer) {
  // Check for timing attribute
  if (auto timing = producer->getAttrOfType<TimingIntervalAttr>("timing")) {
    // Rate = latency (cycles per production)
    return static_cast<unsigned>(timing.getEnd() - timing.getStart());
  }

  // Check if producer is a DataflowTaskOp
  if (auto taskOp = dyn_cast<DataflowTaskOp>(producer)) {
    if (auto timing = taskOp.getTiming()) {
      return static_cast<unsigned>(timing->getEnd() - timing->getStart());
    }
  }

  // Default: assume 1 cycle per production
  return 1;
}

unsigned FIFODepthAnalysis::analyzeConsumerRate(Operation *consumer) {
  // Check for timing attribute
  if (auto timing = consumer->getAttrOfType<TimingIntervalAttr>("timing")) {
    return static_cast<unsigned>(timing.getEnd() - timing.getStart());
  }

  // Check if consumer is a DataflowTaskOp
  if (auto taskOp = dyn_cast<DataflowTaskOp>(consumer)) {
    if (auto timing = taskOp.getTiming()) {
      return static_cast<unsigned>(timing->getEnd() - timing->getStart());
    }
  }

  // Default: assume 1 cycle per consumption
  return 1;
}

unsigned FIFODepthAnalysis::analyzeLatencyVariation(Value token,
                                                     const TokenGraph &graph) {
  const TokenInfo *info = graph.getTokenInfo(token);
  if (!info)
    return 0;

  // For each consumer, compute the path latency variation
  unsigned maxVariation = 0;

  for (Operation *consumer : info->consumers) {
    // Get consumer's timing
    auto consumerTiming =
        consumer->getAttrOfType<TimingIntervalAttr>("timing");
    if (!consumerTiming)
      continue;

    // Check downstream tokens for latency variation
    auto producedTokens = graph.getProducedTokens(consumer);
    for (Value nextToken : producedTokens) {
      const TokenInfo *nextInfo = graph.getTokenInfo(nextToken);
      if (!nextInfo || !nextInfo->isLatencyInsensitive)
        continue;

      // Recursively analyze
      unsigned downstream = analyzeLatencyVariation(nextToken, graph);
      maxVariation = std::max(maxVariation, downstream);
    }

    // Account for this consumer's latency
    unsigned consumerLatency =
        static_cast<unsigned>(consumerTiming.getEnd() - consumerTiming.getStart());
    if (consumerLatency > 1) {
      maxVariation = std::max(maxVariation, consumerLatency - 1);
    }
  }

  return maxVariation;
}

unsigned FIFODepthAnalysis::computeRateMismatchDepth(unsigned prodRate,
                                                      unsigned consRate) {
  // If producer produces every P cycles and consumer consumes every C cycles,
  // and P < C (producer faster), then we need buffer for rate difference.
  // Depth = ceil(C / P) - 1 + 1 = ceil(C / P)
  if (prodRate == 0)
    return 1;
  return (consRate + prodRate - 1) / prodRate;
}
