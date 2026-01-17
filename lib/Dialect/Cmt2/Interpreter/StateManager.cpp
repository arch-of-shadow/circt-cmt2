//===- StateManager.cpp - Interpreter State Management -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the StateManager class for centralized interpreter state
// management.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Interpreter/StateManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "cmt2-state-manager"

using namespace circt;
using namespace cmt2;
using namespace interp;

//===----------------------------------------------------------------------===//
// Register Access
//===----------------------------------------------------------------------===//

void StateManager::addRegister(llvm::StringRef name, unsigned width,
                               std::optional<InterpValue> resetValue) {
  RegisterState state;
  state.name = name.str();
  state.width = width;
  state.value = llvm::APInt(width, 0);
  state.hasReset = resetValue.has_value();
  state.resetValue = resetValue.value_or(llvm::APInt(width, 0));
  registers_[name] = std::move(state);

  LLVM_DEBUG(llvm::dbgs() << "StateManager: added register '" << name
                          << "' width=" << width << "\n");
}

std::optional<InterpValue> StateManager::readRegister(llvm::StringRef name) {
  auto it = registers_.find(name);
  if (it == registers_.end())
    return std::nullopt;

  notifyRegisterRead(name, it->second.value);
  return it->second.value;
}

void StateManager::writeRegister(llvm::StringRef name,
                                 const InterpValue &value) {
  if (!registers_.count(name))
    return;

  // Queue the write for later commit
  pendingWrites_[name] = value;
  LLVM_DEBUG(llvm::dbgs() << "StateManager: queued write to '" << name << "'\n");
}

void StateManager::commitWrites() {
  for (const auto &write : pendingWrites_) {
    auto it = registers_.find(write.first());
    if (it != registers_.end()) {
      InterpValue oldVal = it->second.value;
      InterpValue newVal = write.second;

      // Truncate or extend to match register width
      if (newVal.getBitWidth() > it->second.width)
        newVal = newVal.trunc(it->second.width);
      else if (newVal.getBitWidth() < it->second.width)
        newVal = newVal.zext(it->second.width);

      it->second.value = newVal;
      notifyRegisterWrite(write.first(), oldVal, newVal);

      LLVM_DEBUG(llvm::dbgs() << "StateManager: committed write to '"
                              << write.first() << "'\n");
    }
  }
  pendingWrites_.clear();
}

void StateManager::discardWrites() {
  pendingWrites_.clear();
  LLVM_DEBUG(llvm::dbgs() << "StateManager: discarded pending writes\n");
}

std::vector<std::string> StateManager::getRegisterNames() const {
  std::vector<std::string> names;
  for (const auto &entry : registers_)
    names.push_back(entry.first().str());
  return names;
}

bool StateManager::hasRegister(llvm::StringRef name) const {
  return registers_.count(name) > 0;
}

std::optional<unsigned> StateManager::getRegisterWidth(llvm::StringRef name) const {
  auto it = registers_.find(name);
  if (it == registers_.end())
    return std::nullopt;
  return it->second.width;
}

//===----------------------------------------------------------------------===//
// FSM State
//===----------------------------------------------------------------------===//

void StateManager::addFSM(llvm::StringRef name, unsigned numStates,
                          const std::vector<std::string> &stateSteps) {
  FSMState state;
  state.ruleName = name.str();
  state.numStates = numStates;
  state.currentState = 0;
  state.isRunning = false;
  state.stateSteps = stateSteps;
  fsmStates_[name] = std::move(state);

  LLVM_DEBUG(llvm::dbgs() << "StateManager: added FSM '" << name
                          << "' numStates=" << numStates << "\n");
}

unsigned StateManager::getFSMState(llvm::StringRef name) const {
  auto it = fsmStates_.find(name);
  if (it == fsmStates_.end())
    return 0;
  return it->second.currentState;
}

void StateManager::setFSMState(llvm::StringRef name, unsigned state) {
  auto it = fsmStates_.find(name);
  if (it == fsmStates_.end())
    return;

  unsigned oldState = it->second.currentState;
  it->second.currentState = state;
  it->second.isRunning = (state != 0);

  if (oldState != state)
    notifyFSMTransition(name, oldState, state);

  LLVM_DEBUG(llvm::dbgs() << "StateManager: FSM '" << name << "' state "
                          << oldState << " -> " << state << "\n");
}

bool StateManager::isFSMIdle(llvm::StringRef name) const {
  auto it = fsmStates_.find(name);
  if (it == fsmStates_.end())
    return true;
  return it->second.currentState == 0;
}

bool StateManager::isFSMRunning(llvm::StringRef name) const {
  auto it = fsmStates_.find(name);
  if (it == fsmStates_.end())
    return false;
  return it->second.isRunning;
}

std::optional<std::string>
StateManager::getFSMCurrentStep(llvm::StringRef name) const {
  auto it = fsmStates_.find(name);
  if (it == fsmStates_.end())
    return std::nullopt;

  unsigned state = it->second.currentState;
  if (state == 0 || state > it->second.stateSteps.size())
    return std::nullopt;

  return it->second.stateSteps[state - 1];
}

std::vector<std::string> StateManager::getFSMNames() const {
  std::vector<std::string> names;
  for (const auto &entry : fsmStates_)
    names.push_back(entry.first().str());
  return names;
}

//===----------------------------------------------------------------------===//
// Static Step Cycle Tracking
//===----------------------------------------------------------------------===//

void StateManager::addStaticStep(llvm::StringRef stepName, unsigned latency,
                                 std::optional<unsigned> interval) {
  StaticStepState state;
  state.latency = latency;
  state.interval = interval;
  state.currentCycle = 0;
  state.active = false;
  state.lastInitiation = 0;
  staticSteps_[stepName] = std::move(state);

  LLVM_DEBUG(llvm::dbgs() << "StateManager: added static step '" << stepName
                          << "' latency=" << latency << "\n");
}

void StateManager::startStaticStep(llvm::StringRef stepName) {
  auto it = staticSteps_.find(stepName);
  if (it == staticSteps_.end())
    return;

  it->second.currentCycle = 0;
  it->second.active = true;
  it->second.lastInitiation = cycle_;

  LLVM_DEBUG(llvm::dbgs() << "StateManager: started static step '" << stepName
                          << "' at cycle " << cycle_ << "\n");

  // Notify observers
  for (auto &observer : observers_)
    observer->onStaticStepStart(stepName, it->second.latency);
}

bool StateManager::tickStaticStep(llvm::StringRef stepName) {
  auto it = staticSteps_.find(stepName);
  if (it == staticSteps_.end())
    return false;

  if (!it->second.active)
    return false;

  it->second.currentCycle++;

  // Notify observers
  for (auto &observer : observers_)
    observer->onStaticStepTick(stepName, it->second.currentCycle,
                               it->second.latency);

  bool done = it->second.currentCycle >= it->second.latency;
  if (done) {
    it->second.active = false;
    LLVM_DEBUG(llvm::dbgs() << "StateManager: static step '" << stepName
                            << "' completed at cycle " << cycle_ << "\n");

    // Notify observers
    for (auto &observer : observers_)
      observer->onStaticStepDone(stepName);
  }

  return done;
}

unsigned StateManager::getStaticStepCycle(llvm::StringRef stepName) const {
  auto it = staticSteps_.find(stepName);
  if (it == staticSteps_.end())
    return 0;
  return it->second.currentCycle;
}

bool StateManager::isStaticStepDone(llvm::StringRef stepName) const {
  auto it = staticSteps_.find(stepName);
  if (it == staticSteps_.end())
    return true;
  return it->second.currentCycle >= it->second.latency;
}

bool StateManager::isStaticStepActive(llvm::StringRef stepName) const {
  auto it = staticSteps_.find(stepName);
  if (it == staticSteps_.end())
    return false;
  return it->second.active;
}

bool StateManager::canInitiate(llvm::StringRef stepName) const {
  auto it = staticSteps_.find(stepName);
  if (it == staticSteps_.end())
    return false;

  // If no interval specified, can only initiate when not active
  if (!it->second.interval.has_value())
    return !it->second.active;

  // Check if enough cycles have passed since last initiation
  unsigned interval = *it->second.interval;
  return (cycle_ - it->second.lastInitiation) >= interval;
}

void StateManager::resetStaticStep(llvm::StringRef stepName) {
  auto it = staticSteps_.find(stepName);
  if (it == staticSteps_.end())
    return;

  it->second.currentCycle = 0;
  it->second.active = false;
}

std::vector<std::string> StateManager::getStaticStepNames() const {
  std::vector<std::string> names;
  for (const auto &entry : staticSteps_)
    names.push_back(entry.first().str());
  return names;
}

//===----------------------------------------------------------------------===//
// Observers
//===----------------------------------------------------------------------===//

void StateManager::addObserver(std::shared_ptr<StateObserver> observer) {
  observers_.push_back(std::move(observer));
}

void StateManager::removeObserver(StateObserver *observer) {
  observers_.erase(
      std::remove_if(observers_.begin(), observers_.end(),
                     [observer](const std::shared_ptr<StateObserver> &obs) {
                       return obs.get() == observer;
                     }),
      observers_.end());
}

void StateManager::clearObservers() { observers_.clear(); }

//===----------------------------------------------------------------------===//
// Cycle Management
//===----------------------------------------------------------------------===//

void StateManager::incrementCycle() {
  notifyCycleEnd(cycle_);
  cycle_++;
  notifyCycleStart(cycle_);
}

void StateManager::reset() {
  cycle_ = 0;
  pendingWrites_.clear();

  // Reset all registers to their reset values
  for (auto &entry : registers_) {
    if (entry.second.hasReset)
      entry.second.value = entry.second.resetValue;
    else
      entry.second.value = llvm::APInt(entry.second.width, 0);
  }

  // Reset all FSMs to idle
  for (auto &entry : fsmStates_) {
    entry.second.currentState = 0;
    entry.second.isRunning = false;
  }

  // Reset all static steps
  for (auto &entry : staticSteps_) {
    entry.second.currentCycle = 0;
    entry.second.active = false;
    entry.second.lastInitiation = 0;
  }

  LLVM_DEBUG(llvm::dbgs() << "StateManager: reset to cycle 0\n");
}

void StateManager::clear() {
  cycle_ = 0;
  registers_.clear();
  pendingWrites_.clear();
  fsmStates_.clear();
  staticSteps_.clear();

  LLVM_DEBUG(llvm::dbgs() << "StateManager: cleared all state\n");
}

//===----------------------------------------------------------------------===//
// Observer Notifications
//===----------------------------------------------------------------------===//

void StateManager::notifyRegisterRead(llvm::StringRef name,
                                      const InterpValue &value) {
  for (auto &observer : observers_)
    observer->onRegisterRead(name, value);
}

void StateManager::notifyRegisterWrite(llvm::StringRef name,
                                       const InterpValue &oldVal,
                                       const InterpValue &newVal) {
  for (auto &observer : observers_)
    observer->onRegisterWrite(name, oldVal, newVal);
}

void StateManager::notifyFSMTransition(llvm::StringRef name, unsigned oldState,
                                       unsigned newState) {
  for (auto &observer : observers_)
    observer->onFSMTransition(name, oldState, newState);
}

void StateManager::notifyCycleStart(uint64_t cycle) {
  for (auto &observer : observers_)
    observer->onCycleStart(cycle);
}

void StateManager::notifyCycleEnd(uint64_t cycle) {
  for (auto &observer : observers_)
    observer->onCycleEnd(cycle);
}
