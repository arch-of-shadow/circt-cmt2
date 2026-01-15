//===- DeadlockAnalysis.cpp - CMT2 Deadlock Analysis --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the deadlock analysis for CMT2 dataflow pipelines.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Analysis/DeadlockAnalysis.h"
#include "circt/Dialect/Cmt2/Cmt2Types.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace circt::cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// TokenCycle
//===----------------------------------------------------------------------===//

std::string TokenCycle::describe() const {
  std::string result;
  llvm::raw_string_ostream os(result);

  os << "Cycle with " << operations.size() << " operations, ";
  os << numLITokens << " LI tokens, ";
  os << "total FIFO depth " << totalFIFODepth;

  if (isPotentialDeadlock()) {
    os << " [POTENTIAL DEADLOCK]";
  }

  return result;
}

//===----------------------------------------------------------------------===//
// DeadlockAnalysis
//===----------------------------------------------------------------------===//

DeadlockAnalysis::DeadlockAnalysis(CircuitOp circuit)
    : circuit_(circuit), tokenAnalysis_(circuit), fifoAnalysis_(circuit) {}

LogicalResult DeadlockAnalysis::checkForDeadlock(ModuleOp module) {
  const TokenGraph &graph = tokenAnalysis_.getTokenGraph(module);

  // Find all cycles with LI tokens
  auto cycles = findLICycles(graph);
  detectedCycles_ = cycles;

  // Check each cycle for potential deadlock
  for (const TokenCycle &cycle : cycles) {
    if (failed(checkCycleForDeadlock(cycle, module.getOperation())))
      return failure();
  }

  return success();
}

LogicalResult DeadlockAnalysis::checkForDeadlock(ProcDataflowOp dataflow) {
  const TokenGraph &graph = tokenAnalysis_.getTokenGraph(dataflow);

  // Find all cycles with LI tokens
  auto cycles = findLICycles(graph);
  detectedCycles_ = cycles;

  // Check each cycle for potential deadlock
  for (const TokenCycle &cycle : cycles) {
    if (failed(checkCycleForDeadlock(cycle, dataflow.getOperation())))
      return failure();
  }

  return success();
}

SmallVector<TokenCycle, 4> DeadlockAnalysis::findCycles(const TokenGraph &graph) {
  SmallVector<TokenCycle, 4> cycles;
  llvm::DenseSet<Operation *> visited;
  llvm::DenseSet<Operation *> inStack;
  SmallVector<Operation *, 16> path;

  // Collect all operations in the graph
  llvm::DenseSet<Operation *> allOps;
  for (const TokenInfo &info : graph.getTokens()) {
    if (info.producer)
      allOps.insert(info.producer);
    for (Operation *consumer : info.consumers)
      allOps.insert(consumer);
  }

  // Run DFS from each unvisited node
  for (Operation *op : allOps) {
    if (!visited.count(op)) {
      findCyclesDFS(op, path, visited, inStack, graph, cycles);
    }
  }

  return cycles;
}

SmallVector<TokenCycle, 4>
DeadlockAnalysis::findLICycles(const TokenGraph &graph) {
  auto allCycles = findCycles(graph);

  // Filter to cycles that contain at least one LI token
  SmallVector<TokenCycle, 4> liCycles;
  for (const TokenCycle &cycle : allCycles) {
    if (cycle.numLITokens > 0) {
      liCycles.push_back(cycle);
    }
  }

  return liCycles;
}

void DeadlockAnalysis::findCyclesDFS(
    Operation *current, SmallVector<Operation *, 16> &path,
    llvm::DenseSet<Operation *> &visited,
    llvm::DenseSet<Operation *> &inStack, const TokenGraph &graph,
    SmallVector<TokenCycle, 4> &cycles) {

  visited.insert(current);
  inStack.insert(current);
  path.push_back(current);

  // Get all tokens produced by current operation
  auto producedTokens = graph.getProducedTokens(current);

  for (Value token : producedTokens) {
    const TokenInfo *info = graph.getTokenInfo(token);
    if (!info)
      continue;

    for (Operation *successor : info->consumers) {
      if (!visited.count(successor)) {
        // Not visited - recurse
        findCyclesDFS(successor, path, visited, inStack, graph, cycles);
      } else if (inStack.count(successor)) {
        // Back edge - found a cycle
        // Extract the cycle from the path
        SmallVector<Operation *, 16> cyclePath;
        bool inCycle = false;
        for (Operation *op : path) {
          if (op == successor)
            inCycle = true;
          if (inCycle)
            cyclePath.push_back(op);
        }

        if (!cyclePath.empty()) {
          TokenCycle cycle = buildCycle(cyclePath, graph);
          cycles.push_back(cycle);
        }
      }
    }
  }

  path.pop_back();
  inStack.erase(current);
}

TokenCycle DeadlockAnalysis::buildCycle(ArrayRef<Operation *> cyclePath,
                                         const TokenGraph &graph) {
  TokenCycle cycle;
  cycle.operations.assign(cyclePath.begin(), cyclePath.end());
  cycle.numLITokens = 0;
  cycle.totalFIFODepth = 0;

  // Find tokens along the cycle edges
  for (size_t i = 0; i < cyclePath.size(); ++i) {
    Operation *from = cyclePath[i];
    Operation *to = cyclePath[(i + 1) % cyclePath.size()];

    // Find tokens from 'from' that are consumed by 'to'
    auto producedTokens = graph.getProducedTokens(from);
    for (Value token : producedTokens) {
      const TokenInfo *info = graph.getTokenInfo(token);
      if (!info)
        continue;

      // Check if 'to' is a consumer
      for (Operation *consumer : info->consumers) {
        if (consumer == to) {
          cycle.tokens.push_back(token);

          // Check if LI and accumulate FIFO depth
          if (info->isLatencyInsensitive) {
            cycle.numLITokens++;
            // Get FIFO depth
            FIFODepthInfo depthInfo = fifoAnalysis_.inferDepth(token);
            cycle.totalFIFODepth += depthInfo.minDepth;
          }
          break;
        }
      }
    }
  }

  return cycle;
}

LogicalResult DeadlockAnalysis::checkCycleForDeadlock(const TokenCycle &cycle,
                                                       Operation *loc) {
  if (!cycle.isPotentialDeadlock())
    return success();

  // Emit error with details
  return emitError(loc->getLoc())
         << "Potential deadlock detected: cycle of " << cycle.operations.size()
         << " operations with " << cycle.numLITokens
         << " LI tokens, total FIFO depth " << cycle.totalFIFODepth
         << " (need at least " << cycle.numLITokens << " to prevent deadlock)";
}

void DeadlockAnalysis::invalidate() {
  detectedCycles_.clear();
  tokenAnalysis_.invalidate();
  fifoAnalysis_.invalidate();
}
