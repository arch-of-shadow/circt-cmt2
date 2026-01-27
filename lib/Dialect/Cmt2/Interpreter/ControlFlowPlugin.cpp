//===- ControlFlowPlugin.cpp - Control Flow Plugin Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the control flow plugins for the CMT2 interpreter.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Interpreter/ControlFlowPlugin.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-control-flow"

using namespace circt;
using namespace cmt2;
using namespace interp;

//===----------------------------------------------------------------------===//
// DynamicControlPlugin Implementation
//===----------------------------------------------------------------------===//

void DynamicControlPlugin::initialize(cmt2::ModuleOp module,
                                       StateManager &state) {
  state_ = &state;
  steps_.clear();
  stepsDone_.clear();

  // Collect proc.step definitions
  module.walk([&](ProcStepOp step) {
    steps_[step.getSymName()] = step;
    LLVM_DEBUG(llvm::dbgs() << "DynamicControlPlugin: found step '"
                            << step.getSymName() << "'\n");
  });

  LLVM_DEBUG(llvm::dbgs() << "DynamicControlPlugin: initialized with "
                          << steps_.size() << " steps\n");
}

bool DynamicControlPlugin::handles(mlir::Operation *op) const {
  return mlir::isa<ProcStepOp, ProcEnableOp, ProcStepDoneOp>(op);
}

bool DynamicControlPlugin::execute(mlir::Operation *op, OpContext &ctx) {
  if (auto stepOp = mlir::dyn_cast<ProcStepOp>(op))
    return executeStep(stepOp, ctx);

  if (auto enableOp = mlir::dyn_cast<ProcEnableOp>(op)) {
    // Find and execute the enabled step
    llvm::StringRef stepName = enableOp.getStepName();
    auto it = steps_.find(stepName);
    if (it != steps_.end())
      return executeStep(it->second, ctx);
    return true; // Step not found, consider done
  }

  if (auto doneOp = mlir::dyn_cast<ProcStepDoneOp>(op)) {
    // Check the done condition
    InterpValue doneVal = ctx.getValue(doneOp.getDone());
    return doneVal != 0;
  }

  return true;
}

void DynamicControlPlugin::tick() {
  // Clear step done status at cycle start
  clearStepsDone();
}

void DynamicControlPlugin::commit() {
  // Nothing to commit for dynamic control
}

void DynamicControlPlugin::reset() {
  stepsDone_.clear();
}

bool DynamicControlPlugin::isStepDone(llvm::StringRef stepName) const {
  return stepsDone_.count(stepName) > 0;
}

void DynamicControlPlugin::markStepDone(llvm::StringRef stepName) {
  stepsDone_.insert(stepName);
}

void DynamicControlPlugin::clearStepsDone() { stepsDone_.clear(); }

bool DynamicControlPlugin::executeRegionOps(mlir::Region &region, OpContext &ctx) {
  if (region.empty())
    return true;

  mlir::Block &block = region.front();

  for (mlir::Operation &op : block) {
    // Skip step done ops - they are handled separately
    if (mlir::isa<ProcStepDoneOp>(op))
      continue;

    // Skip yield ops
    if (mlir::isa<YieldOp>(op))
      continue;

    // Dispatch to the registry if available
    if (registry_ && registry_->hasHandler(&op)) {
      auto result = registry_->execute(&op, ctx);
      // If operation has results, they're stored in ctx.valueMap by the handler
      (void)result;
    } else {
      LLVM_DEBUG(llvm::dbgs() << "DynamicControlPlugin: no handler for op '"
                              << op.getName().getStringRef() << "'\n");
    }
  }

  return true;
}

bool DynamicControlPlugin::executeStep(ProcStepOp step, OpContext &ctx) {
  mlir::Region &body = step.getBody();
  if (body.empty())
    return true;

  mlir::Block &block = body.front();
  bool stepDone = false;

  // Execute the step body operations first
  executeRegionOps(body, ctx);

  // Then check for done condition
  for (mlir::Operation &op : block) {
    if (auto doneOp = mlir::dyn_cast<ProcStepDoneOp>(op)) {
      // Evaluate the done condition
      InterpValue doneVal = ctx.getValue(doneOp.getDone());
      stepDone = doneVal != 0;
      break;
    }
  }

  if (stepDone) {
    markStepDone(step.getSymName());
    LLVM_DEBUG(llvm::dbgs() << "DynamicControlPlugin: step '"
                            << step.getSymName() << "' done\n");
  }

  return stepDone;
}

//===----------------------------------------------------------------------===//
// StaticControlPlugin Implementation
//===----------------------------------------------------------------------===//

void StaticControlPlugin::initialize(cmt2::ModuleOp module,
                                      StateManager &state) {
  state_ = &state;
  staticSteps_.clear();
  staticIfs_.clear();
  staticRepeats_.clear();
  timingViolations_.clear();

  // Register static steps
  registerStaticSteps(module);

  LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: initialized with "
                          << staticSteps_.size() << " static steps\n");
}

void StaticControlPlugin::registerStaticSteps(cmt2::ModuleOp module) {
  // Collect proc.static_step definitions
  module.walk([&](ProcStaticStepOp step) {
    llvm::StringRef stepName = step.getSymName();
    unsigned latency = step.getLatency();

    staticSteps_[stepName] = step;

    // Register with state manager for cycle tracking
    std::optional<unsigned> interval;
    int64_t ii = step.getInitiationInterval();
    if (ii > 0 && static_cast<unsigned>(ii) != latency)
      interval = static_cast<unsigned>(ii);
    state_->addStaticStep(stepName, latency, interval);

    LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: found static step '"
                            << stepName << "' latency=" << latency << "\n");
  });

  // Collect proc.static_if definitions
  module.walk([&](ProcStaticIfOp ifOp) {
    // Key by parent step name if available
    if (auto parentStep = ifOp->getParentOfType<ProcStaticStepOp>()) {
      staticIfs_[parentStep.getSymName()] = ifOp;
    }
  });

  // Collect proc.static_repeat definitions
  module.walk([&](ProcStaticRepeatOp repeatOp) {
    // Key by parent step name if available
    if (auto parentStep = repeatOp->getParentOfType<ProcStaticStepOp>()) {
      staticRepeats_[parentStep.getSymName()] = repeatOp;
    }
  });
}

bool StaticControlPlugin::handles(mlir::Operation *op) const {
  return mlir::isa<ProcStaticStepOp, ProcStaticIfOp, ProcStaticRepeatOp>(op);
}

bool StaticControlPlugin::execute(mlir::Operation *op, OpContext &ctx) {
  if (auto stepOp = mlir::dyn_cast<ProcStaticStepOp>(op))
    return executeStaticStep(stepOp, ctx);

  if (auto ifOp = mlir::dyn_cast<ProcStaticIfOp>(op))
    return executeStaticIf(ifOp, ctx);

  if (auto repeatOp = mlir::dyn_cast<ProcStaticRepeatOp>(op))
    return executeStaticRepeat(repeatOp, ctx);

  return true;
}

void StaticControlPlugin::tick() {
  // Tick all active static steps
  for (const auto &entry : staticSteps_) {
    llvm::StringRef stepName = entry.first();
    if (state_->isStaticStepActive(stepName)) {
      state_->tickStaticStep(stepName);
    }
  }
}

void StaticControlPlugin::commit() {
  // Nothing to commit for static control
}

void StaticControlPlugin::reset() {
  timingViolations_.clear();
  repeatIterations_.clear();
  for (const auto &entry : staticSteps_)
    state_->resetStaticStep(entry.first());
}

bool StaticControlPlugin::executeRegionOps(mlir::Region &region, OpContext &ctx) {
  if (region.empty())
    return true;

  mlir::Block &block = region.front();

  for (mlir::Operation &op : block) {
    // Handle nested static constructs recursively
    if (auto nestedIf = mlir::dyn_cast<ProcStaticIfOp>(op)) {
      executeStaticIf(nestedIf, ctx);
      continue;
    }
    if (auto nestedRepeat = mlir::dyn_cast<ProcStaticRepeatOp>(op)) {
      executeStaticRepeat(nestedRepeat, ctx);
      continue;
    }

    // Skip yield ops
    if (mlir::isa<YieldOp>(op))
      continue;

    // Dispatch to the registry if available
    if (registry_ && registry_->hasHandler(&op)) {
      auto result = registry_->execute(&op, ctx);
      (void)result;
    } else {
      LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: no handler for op '"
                              << op.getName().getStringRef() << "'\n");
    }
  }

  return true;
}

bool StaticControlPlugin::executeStaticStep(ProcStaticStepOp step,
                                            OpContext &ctx) {
  llvm::StringRef stepName = step.getSymName();

  // Start the step if not already active
  if (!state_->isStaticStepActive(stepName)) {
    state_->startStaticStep(stepName);
    LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: started step '"
                            << stepName << "' at cycle " << state_->getCycle()
                            << "\n");
  }

  // Execute the step body if this is the first cycle
  unsigned currentCycle = state_->getStaticStepCycle(stepName);
  if (currentCycle == 0) {
    mlir::Region &body = step.getBody();
    if (!body.empty()) {
      // Execute body operations using registry dispatch
      executeRegionOps(body, ctx);

      // Validate timing of CallOps in the body
      if (validateTiming_) {
        for (mlir::Operation &op : body.front()) {
          if (auto call = mlir::dyn_cast<CallOp>(op)) {
            validateCallTiming(call, currentCycle);
          }
        }
      }
    }
  }

  // Check if step is done
  bool done = state_->isStaticStepDone(stepName);
  if (done) {
    LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: step '" << stepName
                            << "' completed after " << step.getLatency()
                            << " cycles\n");
  }

  return done;
}

bool StaticControlPlugin::executeStaticIf(ProcStaticIfOp ifOp, OpContext &ctx) {
  // Evaluate condition
  InterpValue cond = ctx.getValue(ifOp.getCond());
  bool takeThen = cond != 0;

  LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: static_if taking "
                          << (takeThen ? "then" : "else") << " branch\n");

  // Both branches should have same latency in static control
  // Execute the selected branch
  mlir::Region &branch = takeThen ? ifOp.getThenRegion() : ifOp.getElseRegion();

  if (branch.empty())
    return true;

  // Execute the branch body
  executeRegionOps(branch, ctx);

  return true;
}

bool StaticControlPlugin::executeStaticRepeat(ProcStaticRepeatOp repeatOp,
                                              OpContext &ctx) {
  unsigned tripCount = repeatOp.getCount();
  mlir::Operation *opPtr = repeatOp.getOperation();

  // Get or initialize iteration counter for this repeat
  auto it = repeatIterations_.find(opPtr);
  if (it == repeatIterations_.end()) {
    repeatIterations_[opPtr] = 0;
    it = repeatIterations_.find(opPtr);
  }

  unsigned currentIter = it->second;

  LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: static_repeat iteration "
                          << currentIter << "/" << tripCount << "\n");

  // Check if all iterations are done
  if (currentIter >= tripCount) {
    // Reset for next invocation
    repeatIterations_.erase(opPtr);
    LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: static_repeat completed\n");
    return true;
  }

  // Execute the body for this iteration
  mlir::Region &body = repeatOp.getBody();
  if (!body.empty()) {
    executeRegionOps(body, ctx);
  }

  // Increment iteration counter
  repeatIterations_[opPtr] = currentIter + 1;

  // Check if we just finished the last iteration
  if (currentIter + 1 >= tripCount) {
    repeatIterations_.erase(opPtr);
    LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: static_repeat completed\n");
    return true;
  }

  // More iterations remaining
  return false;
}

bool StaticControlPlugin::validateCallTiming(CallOp call,
                                             unsigned currentCycle) {
  if (!validateTiming_)
    return true;

  // Check arg_timing attribute
  if (auto argTiming = call->getAttrOfType<mlir::ArrayAttr>("arg_timing")) {
    for (auto attr : argTiming) {
      if (auto cycleAttr = mlir::dyn_cast<mlir::IntegerAttr>(attr)) {
        unsigned expectedCycle = cycleAttr.getInt();
        if (currentCycle != expectedCycle) {
          std::string violation = "arg_timing violation: expected cycle " +
                                  std::to_string(expectedCycle) + ", got " +
                                  std::to_string(currentCycle);
          timingViolations_.push_back(violation);
          LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: " << violation
                                  << "\n");
          return false;
        }
      }
    }
  }

  // Check result_timing attribute
  if (auto resultTiming = call->getAttrOfType<mlir::ArrayAttr>("result_timing")) {
    for (auto attr : resultTiming) {
      if (auto cycleAttr = mlir::dyn_cast<mlir::IntegerAttr>(attr)) {
        unsigned expectedCycle = cycleAttr.getInt();
        // Results are typically available at the specified cycle
        // For now, just validate that we're at or past that cycle
        if (currentCycle < expectedCycle) {
          std::string violation = "result_timing violation: result expected at cycle " +
                                  std::to_string(expectedCycle) + ", current cycle is " +
                                  std::to_string(currentCycle);
          timingViolations_.push_back(violation);
          LLVM_DEBUG(llvm::dbgs() << "StaticControlPlugin: " << violation
                                  << "\n");
          return false;
        }
      }
    }
  }

  return true;
}

unsigned StaticControlPlugin::getStepCycle(llvm::StringRef stepName) const {
  return state_->getStaticStepCycle(stepName);
}

bool StaticControlPlugin::isStepActive(llvm::StringRef stepName) const {
  return state_->isStaticStepActive(stepName);
}

bool StaticControlPlugin::isStepDone(llvm::StringRef stepName) const {
  return state_->isStaticStepDone(stepName);
}
