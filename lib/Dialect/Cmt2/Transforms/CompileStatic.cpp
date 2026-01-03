//===- CompileStatic.cpp - Compile static control to FSM -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CompileStatic pass for the Cmt2 dialect.
// It compiles static control structures to optimized FSM hardware by using
// the FSM allocation info from StaticFSMAllocation pass.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-compile-static"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_COMPILESTATIC
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// FSM Information Structures
//===----------------------------------------------------------------------===//

/// FSM configuration read from step attributes.
struct StepFSMConfig {
  int64_t numStates;
  int64_t bitwidth;
  bool isOneHot;
  ArrayAttr stateAssignments;
};

//===----------------------------------------------------------------------===//
// CompileStatic Pass Implementation
//===----------------------------------------------------------------------===//

/// Early-reset analysis result.
struct EarlyResetInfo {
  bool canEarlyReset = false;      // Whether early reset is possible
  int64_t earlyResetState = -1;    // State at which early reset is safe
  int64_t savedCycles = 0;         // Number of cycles saved by early reset
};

struct CompileStaticPass
    : public circt::cmt2::impl::CompileStaticBase<CompileStaticPass> {
  using CompileStaticBase::CompileStaticBase;

  void runOnOperation() override;

private:
  /// Process a single module.
  void processModule(cmt2::ModuleOp module);

  /// Process a static step with FSM allocation info.
  void processStaticStep(ProcStaticStepOp step, cmt2::ModuleOp module);

  /// Read FSM config from step attributes.
  std::optional<StepFSMConfig> readFSMConfig(ProcStaticStepOp step);

  /// Generate timing guard expression for a call.
  void annotateCallWithStateGuard(CallOp call, int64_t startState,
                                   int64_t endState, bool isOneHot);

  /// Generate FSM register info for a step.
  void annotateFSMRegisterInfo(ProcStaticStepOp step,
                                const StepFSMConfig &config,
                                const EarlyResetInfo &earlyReset);

  /// Analyze a step for early-reset opportunities.
  EarlyResetInfo analyzeEarlyReset(ProcStaticStepOp step,
                                    const StepFSMConfig &config);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// FSM Configuration Reading
//===----------------------------------------------------------------------===//

std::optional<StepFSMConfig>
CompileStaticPass::readFSMConfig(ProcStaticStepOp step) {
  // Check for FSM allocation attributes
  auto numStatesAttr = step->getAttrOfType<IntegerAttr>("fsm_states");
  if (!numStatesAttr)
    return std::nullopt;

  StepFSMConfig config;
  config.numStates = numStatesAttr.getInt();

  if (auto bitwidthAttr = step->getAttrOfType<IntegerAttr>("fsm_bitwidth"))
    config.bitwidth = bitwidthAttr.getInt();
  else
    config.bitwidth = config.numStates; // Default to one-hot

  if (auto encodingAttr = step->getAttrOfType<StringAttr>("fsm_encoding"))
    config.isOneHot = encodingAttr.getValue() == "one_hot";
  else
    config.isOneHot = true;

  config.stateAssignments =
      step->getAttrOfType<ArrayAttr>("state_assignments");

  return config;
}

//===----------------------------------------------------------------------===//
// Call Annotation with State Guards
//===----------------------------------------------------------------------===//

void CompileStaticPass::annotateCallWithStateGuard(CallOp call,
                                                    int64_t startState,
                                                    int64_t endState,
                                                    bool isOneHot) {
  OpBuilder builder(call->getContext());

  // Add state guard attributes to the call
  // These will be used by subsequent passes to generate actual guard logic
  call->setAttr("fsm_start_state", builder.getI64IntegerAttr(startState));
  call->setAttr("fsm_end_state", builder.getI64IntegerAttr(endState));
  call->setAttr("fsm_is_one_hot", builder.getBoolAttr(isOneHot));

  // Generate guard expression description
  // For binary encoding: fsm >= start && fsm < end
  // For one-hot encoding: |fsm[end-1:start]
  std::string guardExpr;
  if (isOneHot) {
    if (endState - startState == 1) {
      guardExpr = "fsm[" + std::to_string(startState) + "]";
    } else {
      guardExpr = "|fsm[" + std::to_string(endState - 1) + ":" +
                  std::to_string(startState) + "]";
    }
  } else {
    if (endState - startState == 1) {
      guardExpr = "fsm == " + std::to_string(startState);
    } else {
      guardExpr = "fsm >= " + std::to_string(startState) + " && fsm < " +
                  std::to_string(endState);
    }
  }
  call->setAttr("fsm_guard_expr", builder.getStringAttr(guardExpr));

  LLVM_DEBUG(llvm::dbgs() << "    Call guard: " << guardExpr << "\n");
}

//===----------------------------------------------------------------------===//
// Early-Reset Analysis
//===----------------------------------------------------------------------===//

EarlyResetInfo
CompileStaticPass::analyzeEarlyReset(ProcStaticStepOp step,
                                      const StepFSMConfig &config) {
  EarlyResetInfo result;

  // Find the latest end state among all calls
  // This is the earliest state where all operations have completed
  int64_t maxEndState = 0;

  step.getBody().walk([&](CallOp call) {
    if (auto resultTiming = call.getResultTiming()) {
      for (auto attr : *resultTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          maxEndState = std::max(maxEndState, timing.getEnd());
        }
      }
    } else if (auto argTiming = call.getArgTiming()) {
      // If no result timing, use arg timing + 1 as end
      for (auto attr : *argTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          maxEndState = std::max(maxEndState, timing.getEnd());
        }
      }
    }
  });

  // Check if we can reset earlier than the declared latency
  // Early reset is possible if all operations complete before the last state
  if (maxEndState > 0 && maxEndState < config.numStates) {
    result.canEarlyReset = true;
    result.earlyResetState = maxEndState;
    result.savedCycles = config.numStates - maxEndState;

    LLVM_DEBUG(llvm::dbgs() << "    Early reset analysis: can reset at state "
                            << maxEndState << " (saving " << result.savedCycles
                            << " cycles)\n");
  }

  return result;
}

//===----------------------------------------------------------------------===//
// FSM Register Info Annotation
//===----------------------------------------------------------------------===//

void CompileStaticPass::annotateFSMRegisterInfo(ProcStaticStepOp step,
                                                 const StepFSMConfig &config,
                                                 const EarlyResetInfo &earlyReset) {
  OpBuilder builder(step->getContext());

  // Generate FSM register name
  std::string fsmRegName = ("__fsm_" + step.getSymName()).str();
  step->setAttr("fsm_reg_name", builder.getStringAttr(fsmRegName));

  // Generate done signal expression
  std::string doneExpr;
  if (config.isOneHot) {
    // For one-hot, done when MSB is set (last state)
    doneExpr = "fsm[" + std::to_string(config.numStates - 1) + "]";
  } else {
    // For binary, done when fsm == numStates - 1
    doneExpr = "fsm == " + std::to_string(config.numStates - 1);
  }
  step->setAttr("fsm_done_expr", builder.getStringAttr(doneExpr));

  // Generate next-state expression
  std::string nextExpr;
  if (config.isOneHot) {
    // For one-hot, shift left by 1
    nextExpr = "{fsm[" + std::to_string(config.numStates - 2) + ":0], 1'b0}";
  } else {
    // For binary, increment by 1
    nextExpr = "fsm + 1";
  }
  step->setAttr("fsm_next_expr", builder.getStringAttr(nextExpr));

  // Generate initial value
  std::string initExpr;
  if (config.isOneHot) {
    initExpr = std::to_string(config.numStates) + "'b1";
  } else {
    initExpr = std::to_string(config.bitwidth) + "'d0";
  }
  step->setAttr("fsm_init_expr", builder.getStringAttr(initExpr));

  // Add early-reset info if applicable
  if (earlyReset.canEarlyReset) {
    step->setAttr("fsm_early_reset", builder.getUnitAttr());
    step->setAttr("fsm_early_reset_state",
                  builder.getI64IntegerAttr(earlyReset.earlyResetState));
    step->setAttr("fsm_saved_cycles",
                  builder.getI64IntegerAttr(earlyReset.savedCycles));

    // Generate early-reset done expression (can signal done earlier)
    std::string earlyDoneExpr;
    if (config.isOneHot) {
      earlyDoneExpr = "fsm[" + std::to_string(earlyReset.earlyResetState - 1) + "]";
    } else {
      earlyDoneExpr = "fsm == " + std::to_string(earlyReset.earlyResetState - 1);
    }
    step->setAttr("fsm_early_done_expr", builder.getStringAttr(earlyDoneExpr));
  }

  // Mark as compiled
  step->setAttr("static_compiled", builder.getUnitAttr());

  LLVM_DEBUG({
    llvm::dbgs() << "  FSM register: " << fsmRegName << "\n";
    llvm::dbgs() << "    done: " << doneExpr << "\n";
    llvm::dbgs() << "    next: " << nextExpr << "\n";
    llvm::dbgs() << "    init: " << initExpr << "\n";
    if (earlyReset.canEarlyReset) {
      llvm::dbgs() << "    early_reset_state: " << earlyReset.earlyResetState
                   << " (saves " << earlyReset.savedCycles << " cycles)\n";
    }
  });
}

//===----------------------------------------------------------------------===//
// Static Step Processing
//===----------------------------------------------------------------------===//

void CompileStaticPass::processStaticStep(ProcStaticStepOp step,
                                           cmt2::ModuleOp module) {
  auto config = readFSMConfig(step);
  if (!config) {
    LLVM_DEBUG(llvm::dbgs() << "  Skipping @" << step.getSymName()
                            << " (no FSM allocation)\n");
    return;
  }

  LLVM_DEBUG(llvm::dbgs() << "  Processing @" << step.getSymName()
                          << " (" << config->numStates << " states, "
                          << (config->isOneHot ? "one-hot" : "binary") << ")\n");

  // Analyze early-reset opportunities
  auto earlyReset = analyzeEarlyReset(step, *config);

  // Annotate FSM register info
  annotateFSMRegisterInfo(step, *config, earlyReset);

  // Walk calls and annotate with state guards
  int64_t callIdx = 0;
  step.getBody().walk([&](CallOp call) {
    // Get timing info from the call
    int64_t startState = 0;
    int64_t endState = 1;

    // Try to get start state from arg_timing
    if (auto argTiming = call.getArgTiming()) {
      int64_t minStart = INT64_MAX;
      for (auto attr : *argTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          minStart = std::min(minStart, timing.getStart());
        }
      }
      if (minStart != INT64_MAX)
        startState = minStart;
    }

    // Try to get end state from result_timing
    if (auto resultTiming = call.getResultTiming()) {
      int64_t maxEnd = 0;
      for (auto attr : *resultTiming) {
        if (auto timing = dyn_cast<TimingIntervalAttr>(attr)) {
          maxEnd = std::max(maxEnd, timing.getEnd());
        }
      }
      endState = maxEnd;
    } else {
      endState = startState + 1;
    }

    annotateCallWithStateGuard(call, startState, endState, config->isOneHot);
    ++callIdx;
  });
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void CompileStaticPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Walk all static steps
  module.walk([&](ProcStaticStepOp step) {
    processStaticStep(step, module);
  });
}

//===----------------------------------------------------------------------===//
// Pass Entry Point
//===----------------------------------------------------------------------===//

void CompileStaticPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs() << "=== CompileStatic Pass ===\n");
  LLVM_DEBUG(llvm::dbgs() << "Options: one-hot-threshold=" << oneHotThreshold
                          << "\n");

  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
