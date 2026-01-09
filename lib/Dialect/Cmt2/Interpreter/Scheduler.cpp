//===- Scheduler.cpp - Scheduler Implementations ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the scheduler plugins for the CMT2 interpreter.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Interpreter/Scheduler.h"
#include "llvm/Support/Debug.h"
#include <algorithm>

#define DEBUG_TYPE "cmt2-scheduler"

using namespace circt;
using namespace cmt2;
using namespace interp;

//===----------------------------------------------------------------------===//
// ORAATScheduler Implementation
//===----------------------------------------------------------------------===//

void ORAATScheduler::initialize(cmt2::ModuleOp module) {
  priorities_.clear();

  // Extract priorities from module precedence attribute
  extractPrecedence(module);

  // Extract priorities from individual rule attributes
  extractRulePriorities(module);

  LLVM_DEBUG(llvm::dbgs() << "ORAATScheduler: initialized with "
                          << priorities_.size() << " priority entries\n");
}

std::vector<std::string>
ORAATScheduler::selectRules(const std::vector<std::string> &enabledRules) {
  if (enabledRules.empty())
    return {};

  // Find the rule with highest priority (lowest number)
  std::string bestRule = enabledRules[0];
  unsigned bestPriority = getPriority(bestRule);

  for (size_t i = 1; i < enabledRules.size(); ++i) {
    unsigned priority = getPriority(enabledRules[i]);
    if (priority < bestPriority) {
      bestPriority = priority;
      bestRule = enabledRules[i];
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "ORAATScheduler: selected '" << bestRule
                          << "' with priority " << bestPriority << " from "
                          << enabledRules.size() << " enabled rules\n");

  return {bestRule};
}

void ORAATScheduler::reset() {
  // No runtime state to reset
}

unsigned ORAATScheduler::getPriority(llvm::StringRef ruleName) const {
  auto it = priorities_.find(ruleName);
  if (it != priorities_.end())
    return it->second;
  return defaultPriority_;
}

void ORAATScheduler::setPriority(llvm::StringRef ruleName, unsigned priority) {
  priorities_[ruleName] = priority;
}

void ORAATScheduler::extractPrecedence(cmt2::ModuleOp module) {
  // Check for precedence attribute on module
  // Format: precedence = [[@higher, @lower], ...]
  auto precedenceAttr = module->getAttrOfType<mlir::ArrayAttr>("precedence");
  if (!precedenceAttr)
    return;

  unsigned priorityCounter = 0;
  llvm::StringSet<> seen;

  for (auto pairAttr : precedenceAttr) {
    auto pair = mlir::dyn_cast<mlir::ArrayAttr>(pairAttr);
    if (!pair || pair.size() != 2)
      continue;

    auto higherRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[0]);
    auto lowerRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[1]);
    if (!higherRef || !lowerRef)
      continue;

    llvm::StringRef higher = higherRef.getValue();
    llvm::StringRef lower = lowerRef.getValue();

    // Assign priorities to maintain ordering
    if (!seen.contains(higher)) {
      priorities_[higher] = priorityCounter++;
      seen.insert(higher);
    }
    if (!seen.contains(lower)) {
      priorities_[lower] = priorityCounter++;
      seen.insert(lower);
    }

    LLVM_DEBUG(llvm::dbgs() << "ORAATScheduler: precedence " << higher << " > "
                            << lower << "\n");
  }
}

void ORAATScheduler::extractRulePriorities(cmt2::ModuleOp module) {
  // Walk rules and check for priority attribute
  unsigned textualOrder = 0;
  module.walk([&](RuleOp rule) {
    llvm::StringRef ruleName = rule.getSymName();

    // If not already set by precedence, use textual order
    if (priorities_.find(ruleName) == priorities_.end()) {
      priorities_[ruleName] = 1000 + textualOrder;
    }

    // Check for explicit priority attribute
    if (auto priorityAttr = rule->getAttrOfType<mlir::IntegerAttr>("priority")) {
      priorities_[ruleName] = priorityAttr.getInt();
      LLVM_DEBUG(llvm::dbgs() << "ORAATScheduler: rule '" << ruleName
                              << "' has explicit priority "
                              << priorityAttr.getInt() << "\n");
    }

    textualOrder++;
  });
}

//===----------------------------------------------------------------------===//
// AnnotationScheduler Implementation
//===----------------------------------------------------------------------===//

void AnnotationScheduler::initialize(cmt2::ModuleOp module) {
  conflicts_.clear();
  sequenceBefore_.clear();
  conflictFree_.clear();
  priorities_.clear();

  extractConstraints(module);

  LLVM_DEBUG(llvm::dbgs() << "AnnotationScheduler: initialized\n");
}

std::vector<std::string>
AnnotationScheduler::selectRules(const std::vector<std::string> &enabledRules) {
  if (enabledRules.empty())
    return {};

  return selectNonConflicting(enabledRules);
}

void AnnotationScheduler::reset() {
  // No runtime state to reset
}

bool AnnotationScheduler::conflicts(llvm::StringRef ruleA,
                                     llvm::StringRef ruleB) const {
  // Self-conflict is always true (a rule can only fire once per cycle)
  if (ruleA == ruleB)
    return true;

  auto it = conflicts_.find(ruleA);
  if (it != conflicts_.end() && it->second.contains(ruleB))
    return true;

  // Check reverse direction (conflict is symmetric)
  it = conflicts_.find(ruleB);
  if (it != conflicts_.end() && it->second.contains(ruleA))
    return true;

  return false;
}

bool AnnotationScheduler::mustSequenceBefore(llvm::StringRef ruleA,
                                              llvm::StringRef ruleB) const {
  auto it = sequenceBefore_.find(ruleA);
  if (it != sequenceBefore_.end())
    return it->second.contains(ruleB);
  return false;
}

bool AnnotationScheduler::isConflictFree(llvm::StringRef ruleA,
                                          llvm::StringRef ruleB) const {
  auto it = conflictFree_.find(ruleA);
  if (it != conflictFree_.end() && it->second.contains(ruleB))
    return true;

  // Check reverse direction (conflict-free is symmetric)
  it = conflictFree_.find(ruleB);
  if (it != conflictFree_.end() && it->second.contains(ruleA))
    return true;

  return false;
}

void AnnotationScheduler::extractConstraints(cmt2::ModuleOp module) {
  // Extract conflict constraints
  // Format: conflict = [[@a, @b], ...]
  if (auto conflictAttr = module->getAttrOfType<mlir::ArrayAttr>("conflict")) {
    for (auto pairAttr : conflictAttr) {
      auto pair = mlir::dyn_cast<mlir::ArrayAttr>(pairAttr);
      if (!pair || pair.size() != 2)
        continue;

      auto aRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[0]);
      auto bRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[1]);
      if (!aRef || !bRef)
        continue;

      conflicts_[aRef.getValue()].insert(bRef.getValue());
      conflicts_[bRef.getValue()].insert(aRef.getValue());

      LLVM_DEBUG(llvm::dbgs() << "AnnotationScheduler: conflict " << aRef.getValue()
                              << " <-> " << bRef.getValue() << "\n");
    }
  }

  // Extract conflict-free constraints
  // Format: conflictFree = [[@a, @b], ...]
  if (auto cfAttr = module->getAttrOfType<mlir::ArrayAttr>("conflictFree")) {
    for (auto pairAttr : cfAttr) {
      auto pair = mlir::dyn_cast<mlir::ArrayAttr>(pairAttr);
      if (!pair || pair.size() != 2)
        continue;

      auto aRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[0]);
      auto bRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[1]);
      if (!aRef || !bRef)
        continue;

      conflictFree_[aRef.getValue()].insert(bRef.getValue());
      conflictFree_[bRef.getValue()].insert(aRef.getValue());

      LLVM_DEBUG(llvm::dbgs() << "AnnotationScheduler: conflictFree "
                              << aRef.getValue() << " <-> " << bRef.getValue()
                              << "\n");
    }
  }

  // Extract sequence-before constraints
  // Format: sequenceBefore = [[@first, @second], ...]
  if (auto seqAttr = module->getAttrOfType<mlir::ArrayAttr>("sequenceBefore")) {
    for (auto pairAttr : seqAttr) {
      auto pair = mlir::dyn_cast<mlir::ArrayAttr>(pairAttr);
      if (!pair || pair.size() != 2)
        continue;

      auto firstRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[0]);
      auto secondRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[1]);
      if (!firstRef || !secondRef)
        continue;

      sequenceBefore_[firstRef.getValue()].insert(secondRef.getValue());

      LLVM_DEBUG(llvm::dbgs() << "AnnotationScheduler: sequenceBefore "
                              << firstRef.getValue() << " -> "
                              << secondRef.getValue() << "\n");
    }
  }

  // Extract priorities from precedence attribute
  if (auto precedenceAttr = module->getAttrOfType<mlir::ArrayAttr>("precedence")) {
    unsigned priorityCounter = 0;
    llvm::StringSet<> seen;

    for (auto pairAttr : precedenceAttr) {
      auto pair = mlir::dyn_cast<mlir::ArrayAttr>(pairAttr);
      if (!pair || pair.size() != 2)
        continue;

      auto higherRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[0]);
      auto lowerRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[1]);
      if (!higherRef || !lowerRef)
        continue;

      llvm::StringRef higher = higherRef.getValue();
      llvm::StringRef lower = lowerRef.getValue();

      if (!seen.contains(higher)) {
        priorities_[higher] = priorityCounter++;
        seen.insert(higher);
      }
      if (!seen.contains(lower)) {
        priorities_[lower] = priorityCounter++;
        seen.insert(lower);
      }
    }
  }
}

std::vector<std::string>
AnnotationScheduler::selectNonConflicting(
    const std::vector<std::string> &enabledRules) {
  // Sort enabled rules by priority
  std::vector<std::pair<unsigned, std::string>> sortedRules;
  for (const auto &rule : enabledRules) {
    auto it = priorities_.find(rule);
    unsigned priority = (it != priorities_.end()) ? it->second : UINT_MAX;
    sortedRules.emplace_back(priority, rule);
  }
  std::sort(sortedRules.begin(), sortedRules.end());

  // Greedy selection: pick rules that don't conflict with already selected
  std::vector<std::string> selected;
  for (const auto &[priority, rule] : sortedRules) {
    bool canSelect = true;

    // Check for conflicts with already selected rules
    for (const auto &selectedRule : selected) {
      if (conflicts(rule, selectedRule)) {
        canSelect = false;
        break;
      }
    }

    if (canSelect) {
      selected.push_back(rule);
      LLVM_DEBUG(llvm::dbgs() << "AnnotationScheduler: selected '" << rule
                              << "'\n");
    }
  }

  return selected;
}

//===----------------------------------------------------------------------===//
// PriorityScheduler Implementation
//===----------------------------------------------------------------------===//

void PriorityScheduler::initialize(cmt2::ModuleOp module) {
  sortedRules_.clear();
  conflicts_.clear();

  // Collect all rules with priorities
  unsigned textualOrder = 0;
  module.walk([&](RuleOp rule) {
    llvm::StringRef ruleName = rule.getSymName();
    unsigned priority = 1000 + textualOrder;

    // Check for explicit priority
    if (auto priorityAttr = rule->getAttrOfType<mlir::IntegerAttr>("priority")) {
      priority = priorityAttr.getInt();
    }

    sortedRules_.emplace_back(priority, std::string(ruleName));
    textualOrder++;
  });

  // Sort by priority
  std::sort(sortedRules_.begin(), sortedRules_.end());

  // Extract conflict constraints
  if (auto conflictAttr = module->getAttrOfType<mlir::ArrayAttr>("conflict")) {
    for (auto pairAttr : conflictAttr) {
      auto pair = mlir::dyn_cast<mlir::ArrayAttr>(pairAttr);
      if (!pair || pair.size() != 2)
        continue;

      auto aRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[0]);
      auto bRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(pair[1]);
      if (!aRef || !bRef)
        continue;

      conflicts_[aRef.getValue()].insert(bRef.getValue());
      conflicts_[bRef.getValue()].insert(aRef.getValue());
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "PriorityScheduler: initialized with "
                          << sortedRules_.size() << " rules\n");
}

std::vector<std::string>
PriorityScheduler::selectRules(const std::vector<std::string> &enabledRules) {
  if (enabledRules.empty())
    return {};

  // Convert enabled rules to a set for fast lookup
  llvm::StringSet<> enabledSet;
  for (const auto &rule : enabledRules)
    enabledSet.insert(rule);

  // Select rules in priority order, skipping conflicts
  std::vector<std::string> selected;
  llvm::StringSet<> firedThisCycle;

  for (const auto &[priority, ruleName] : sortedRules_) {
    // Skip if not enabled
    if (!enabledSet.contains(ruleName))
      continue;

    // Check for conflicts with already selected rules
    bool canFire = true;
    auto it = conflicts_.find(ruleName);
    if (it != conflicts_.end()) {
      for (const auto &firedEntry : firedThisCycle) {
        if (it->second.contains(firedEntry.getKey())) {
          canFire = false;
          break;
        }
      }
    }

    if (canFire) {
      selected.push_back(ruleName);
      firedThisCycle.insert(ruleName);
      LLVM_DEBUG(llvm::dbgs() << "PriorityScheduler: selected '" << ruleName
                              << "' with priority " << priority << "\n");
    }
  }

  return selected;
}

void PriorityScheduler::reset() {
  // No runtime state to reset
}
