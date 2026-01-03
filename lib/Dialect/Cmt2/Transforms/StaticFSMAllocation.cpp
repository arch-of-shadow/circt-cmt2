//===- StaticFSMAllocation.cpp - FSM state allocation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the StaticFSMAllocation pass for the Cmt2 dialect.
// It maps cycle-precise timing intervals to FSM states, preparing for
// hardware code generation.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cmath>

#define DEBUG_TYPE "cmt2-static-fsm-allocation"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_STATICFSMALLOCATION
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// FSM Allocation Data Structures
//===----------------------------------------------------------------------===//

/// Information about a call's FSM state requirements.
struct CallStateInfo {
  CallOp call;
  int64_t startState;
  int64_t endState;
};

/// FSM allocation result for a static step.
struct StepFSMInfo {
  int64_t numStates;      // Total number of states (0 to numStates-1)
  int64_t bitwidth;       // Bits needed for FSM register
  bool isOneHot;          // Whether to use one-hot encoding
  SmallVector<CallStateInfo> callStates; // State info for each call
  int64_t sharedFSMId = -1; // ID of shared FSM register (-1 = not shared)
};

//===----------------------------------------------------------------------===//
// FSM Sharing via Graph Coloring
//===----------------------------------------------------------------------===//

/// Interference graph for FSM sharing.
/// Nodes are static steps, edges connect steps that may execute concurrently.
class FSMInterferenceGraph {
public:
  /// Add a step to the graph.
  void addStep(ProcStaticStepOp step, int64_t numStates) {
    size_t idx = steps.size();
    stepToIndex[step] = idx;
    steps.push_back(step);
    stateCount.push_back(numStates);
    neighbors.push_back({});
  }

  /// Add interference edge between two steps.
  void addInterference(ProcStaticStepOp step1, ProcStaticStepOp step2) {
    auto it1 = stepToIndex.find(step1);
    auto it2 = stepToIndex.find(step2);
    if (it1 == stepToIndex.end() || it2 == stepToIndex.end())
      return;

    size_t idx1 = it1->second;
    size_t idx2 = it2->second;
    neighbors[idx1].insert(idx2);
    neighbors[idx2].insert(idx1);
  }

  /// Perform greedy graph coloring with largest-first ordering.
  /// Returns a map from step to color (shared FSM ID).
  DenseMap<ProcStaticStepOp, int64_t> colorGraph() {
    DenseMap<ProcStaticStepOp, int64_t> colors;

    if (steps.empty())
      return colors;

    // Sort steps by number of states (largest first) for better packing
    SmallVector<size_t> order;
    for (size_t i = 0; i < steps.size(); ++i)
      order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return stateCount[a] > stateCount[b];
    });

    // Track which colors are used by each step's neighbors
    SmallVector<int64_t> stepColors(steps.size(), -1);
    int64_t maxColor = -1;

    for (size_t idx : order) {
      // Find colors used by neighbors
      DenseSet<int64_t> usedColors;
      for (size_t neighbor : neighbors[idx]) {
        if (stepColors[neighbor] >= 0)
          usedColors.insert(stepColors[neighbor]);
      }

      // Find the smallest available color
      int64_t color = 0;
      while (usedColors.count(color))
        ++color;

      stepColors[idx] = color;
      maxColor = std::max(maxColor, color);

      LLVM_DEBUG(llvm::dbgs() << "    Step @" << steps[idx].getSymName()
                              << " -> color " << color << "\n");
    }

    // Build result map
    for (size_t i = 0; i < steps.size(); ++i)
      colors[steps[i]] = stepColors[i];

    LLVM_DEBUG(llvm::dbgs() << "  Graph coloring: " << steps.size()
                            << " steps -> " << (maxColor + 1) << " FSMs\n");

    return colors;
  }

private:
  SmallVector<ProcStaticStepOp> steps;
  SmallVector<int64_t> stateCount;
  SmallVector<DenseSet<size_t>> neighbors;
  DenseMap<ProcStaticStepOp, size_t> stepToIndex;
};

//===----------------------------------------------------------------------===//
// StaticFSMAllocation Pass Implementation
//===----------------------------------------------------------------------===//

struct StaticFSMAllocationPass
    : public circt::cmt2::impl::StaticFSMAllocationBase<
          StaticFSMAllocationPass> {

  using StaticFSMAllocationBase::StaticFSMAllocationBase;

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Allocate FSM states for a static step.
  std::optional<StepFSMInfo> allocateStates(ProcStaticStepOp step);

  /// Compute the FSM bitwidth for a given number of states.
  int64_t computeBitwidth(int64_t numStates);

  /// Annotate a step with FSM allocation info.
  void annotateStep(ProcStaticStepOp step, const StepFSMInfo &info);

  /// Build interference graph for FSM sharing.
  FSMInterferenceGraph buildInterferenceGraph(cmt2::ModuleOp module);

  /// Check if two steps may execute concurrently.
  bool mayExecuteConcurrently(ProcStaticStepOp step1, ProcStaticStepOp step2,
                               cmt2::ModuleOp module);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// FSM Bitwidth Computation
//===----------------------------------------------------------------------===//

int64_t StaticFSMAllocationPass::computeBitwidth(int64_t numStates) {
  if (numStates <= 1)
    return 1;

  // For one-hot encoding, use one bit per state
  if (static_cast<unsigned>(numStates) <= oneHotCutoff)
    return numStates;

  // For binary encoding, use ceil(log2(numStates))
  return llvm::Log2_64_Ceil(numStates);
}

//===----------------------------------------------------------------------===//
// FSM State Allocation
//===----------------------------------------------------------------------===//

std::optional<StepFSMInfo>
StaticFSMAllocationPass::allocateStates(ProcStaticStepOp step) {
  StepFSMInfo info;
  info.numStates = step.getLatency();

  if (info.numStates == 0)
    return std::nullopt;

  // Determine encoding
  info.isOneHot = (static_cast<unsigned>(info.numStates) <= oneHotCutoff);
  info.bitwidth = computeBitwidth(info.numStates);

  LLVM_DEBUG(llvm::dbgs() << "  Step @" << step.getSymName() << ": "
                          << info.numStates << " states, "
                          << info.bitwidth << " bits ("
                          << (info.isOneHot ? "one-hot" : "binary") << ")\n");

  // Walk calls and extract their state ranges from timing
  step.getBody().walk([&](CallOp call) {
    // Extract arg timing to determine start state
    int64_t startState = 0;
    if (auto argTiming = call.getArgTiming()) {
      for (auto attr : *argTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          startState = std::min(startState, timing.getStart());
          if (startState == 0)
            startState = timing.getStart();
          else
            startState = std::min(startState, timing.getStart());
        }
      }
      // Re-compute to get the minimum
      startState = INT64_MAX;
      for (auto attr : *argTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          startState = std::min(startState, timing.getStart());
        }
      }
      if (startState == INT64_MAX)
        startState = 0;
    }

    // Extract result timing to determine end state
    int64_t endState = startState + 1;
    if (auto resultTiming = call.getResultTiming()) {
      for (auto attr : *resultTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          endState = std::max(endState, timing.getEnd());
        }
      }
    }

    info.callStates.push_back({call, startState, endState});

    LLVM_DEBUG(llvm::dbgs() << "    Call: states [" << startState << ", "
                            << endState << ")\n");
  });

  return info;
}

//===----------------------------------------------------------------------===//
// FSM Sharing - Interference Graph Building
//===----------------------------------------------------------------------===//

bool StaticFSMAllocationPass::mayExecuteConcurrently(ProcStaticStepOp step1,
                                                       ProcStaticStepOp step2,
                                                       cmt2::ModuleOp module) {
  // Conservative analysis: check if steps are enabled in parallel control
  // Two steps may execute concurrently if:
  // 1. They are enabled within the same par block
  // 2. They are enabled from different rules that may fire simultaneously

  // Collect all enables for each step
  DenseSet<Operation *> step1Enables, step2Enables;

  module.walk([&](ProcEnableOp enable) {
    if (enable.getStepName() == step1.getSymName())
      step1Enables.insert(enable);
    else if (enable.getStepName() == step2.getSymName())
      step2Enables.insert(enable);
  });

  // Check if any pair of enables is within the same par block
  for (auto *enable1 : step1Enables) {
    for (auto *enable2 : step2Enables) {
      // Check if they share a common par ancestor
      Operation *ancestor1 = enable1->getParentOp();
      while (ancestor1) {
        if (isa<ProcParOp>(ancestor1)) {
          // Check if enable2 is also under this par
          Operation *ancestor2 = enable2->getParentOp();
          while (ancestor2) {
            if (ancestor2 == ancestor1)
              return true; // Both under the same par
            ancestor2 = ancestor2->getParentOp();
          }
        }
        ancestor1 = ancestor1->getParentOp();
      }
    }
  }

  return false;
}

FSMInterferenceGraph
StaticFSMAllocationPass::buildInterferenceGraph(cmt2::ModuleOp module) {
  FSMInterferenceGraph graph;

  LLVM_DEBUG(llvm::dbgs() << "  Building interference graph...\n");

  // Collect all static steps with their state counts
  SmallVector<std::pair<ProcStaticStepOp, int64_t>> steps;
  module.walk([&](ProcStaticStepOp step) {
    int64_t numStates = step.getLatency();
    if (numStates > 0) {
      graph.addStep(step, numStates);
      steps.push_back({step, numStates});
    }
  });

  // Build interference edges
  for (size_t i = 0; i < steps.size(); ++i) {
    for (size_t j = i + 1; j < steps.size(); ++j) {
      if (mayExecuteConcurrently(steps[i].first, steps[j].first, module)) {
        graph.addInterference(steps[i].first, steps[j].first);
        LLVM_DEBUG(llvm::dbgs()
                   << "    Interference: @" << steps[i].first.getSymName()
                   << " <-> @" << steps[j].first.getSymName() << "\n");
      }
    }
  }

  return graph;
}

//===----------------------------------------------------------------------===//
// Step Annotation
//===----------------------------------------------------------------------===//

void StaticFSMAllocationPass::annotateStep(ProcStaticStepOp step,
                                            const StepFSMInfo &info) {
  OpBuilder builder(step->getContext());

  // fsm_states: Total number of states
  step->setAttr("fsm_states", builder.getI64IntegerAttr(info.numStates));

  // fsm_bitwidth: Bits needed for FSM register
  step->setAttr("fsm_bitwidth", builder.getI64IntegerAttr(info.bitwidth));

  // fsm_encoding: Binary or one-hot
  step->setAttr("fsm_encoding",
                builder.getStringAttr(info.isOneHot ? "one_hot" : "binary"));

  // fsm_shared_id: ID of shared FSM register (if sharing is enabled)
  if (info.sharedFSMId >= 0) {
    step->setAttr("fsm_shared_id", builder.getI64IntegerAttr(info.sharedFSMId));
  }

  // state_assignments: Map from state to which calls are active
  // This is stored as an array of arrays: [[state, call_indices...], ...]
  SmallVector<Attribute> stateAssignments;
  for (int64_t state = 0; state < info.numStates; ++state) {
    SmallVector<int64_t> activeCallIndices;
    for (size_t i = 0; i < info.callStates.size(); ++i) {
      const auto &callInfo = info.callStates[i];
      if (state >= callInfo.startState && state < callInfo.endState) {
        activeCallIndices.push_back(i);
      }
    }
    if (!activeCallIndices.empty()) {
      SmallVector<Attribute> stateEntry;
      stateEntry.push_back(builder.getI64IntegerAttr(state));
      for (int64_t idx : activeCallIndices) {
        stateEntry.push_back(builder.getI64IntegerAttr(idx));
      }
      stateAssignments.push_back(builder.getArrayAttr(stateEntry));
    }
  }
  step->setAttr("state_assignments", builder.getArrayAttr(stateAssignments));

  LLVM_DEBUG({
    llvm::dbgs() << "  Annotated @" << step.getSymName() << " with:\n";
    llvm::dbgs() << "    fsm_states=" << info.numStates << "\n";
    llvm::dbgs() << "    fsm_bitwidth=" << info.bitwidth << "\n";
    llvm::dbgs() << "    fsm_encoding=" << (info.isOneHot ? "one_hot" : "binary")
                 << "\n";
    if (info.sharedFSMId >= 0)
      llvm::dbgs() << "    fsm_shared_id=" << info.sharedFSMId << "\n";
  });
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void StaticFSMAllocationPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // First, allocate states for all steps
  DenseMap<ProcStaticStepOp, StepFSMInfo> stepInfos;
  module.walk([&](ProcStaticStepOp step) {
    if (auto info = allocateStates(step)) {
      stepInfos[step] = *info;
    }
  });

  // If sharing is enabled, perform graph coloring
  if (enableSharing && stepInfos.size() > 1) {
    LLVM_DEBUG(llvm::dbgs() << "  FSM sharing enabled, performing graph coloring...\n");

    auto graph = buildInterferenceGraph(module);
    auto colors = graph.colorGraph();

    // Apply colors to step infos
    for (auto &pair : stepInfos) {
      auto it = colors.find(pair.first);
      if (it != colors.end()) {
        pair.second.sharedFSMId = it->second;
      }
    }
  }

  // Annotate all steps with their FSM info
  for (auto &pair : stepInfos) {
    annotateStep(pair.first, pair.second);
  }
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void StaticFSMAllocationPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs() << "=== StaticFSMAllocation Pass ===\n");
  LLVM_DEBUG(llvm::dbgs() << "Options: one-hot-cutoff=" << oneHotCutoff
                          << ", enable-sharing=" << enableSharing << "\n");

  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
