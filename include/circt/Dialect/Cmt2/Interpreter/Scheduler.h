//===- Scheduler.h - Scheduler Interface for CMT2 Interpreter -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Scheduler interface for pluggable conflict resolution
// strategies in the CMT2 interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_INTERPRETER_SCHEDULER_H
#define CIRCT_DIALECT_CMT2_INTERPRETER_SCHEDULER_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace interp {

//===----------------------------------------------------------------------===//
// Scheduler - Abstract interface for rule scheduling
//===----------------------------------------------------------------------===//

/// Abstract interface for rule schedulers.
///
/// Schedulers implement conflict resolution strategies for selecting which
/// rules to fire when multiple rules are enabled. The scheduler is responsible
/// for implementing the desired semantics (ORAAT, parallel firing, etc.).
///
/// Built-in schedulers:
/// - ORAATScheduler: One Rule At A Time (default GAA semantics)
/// - AnnotationScheduler: Uses scheduling attributes (conflict, sequenceBefore)
///
class Scheduler {
public:
  virtual ~Scheduler() = default;

  /// Initialize scheduler with module.
  /// Extracts scheduling constraints from the module (precedence, conflict, etc.)
  virtual void initialize(cmt2::ModuleOp module) = 0;

  /// Select rules to fire from the set of enabled rules.
  /// @param enabledRules Names of all rules whose guards are currently true
  /// @return Names of rules that should fire this cycle
  virtual std::vector<std::string>
  selectRules(const std::vector<std::string> &enabledRules) = 0;

  /// Get scheduler name (for debugging/logging).
  virtual llvm::StringRef getName() const = 0;

  /// Reset scheduler state (if any).
  virtual void reset() {}
};

//===----------------------------------------------------------------------===//
// ORAATScheduler - One Rule At A Time Scheduler
//===----------------------------------------------------------------------===//

/// Default scheduler implementing One-Rule-At-A-Time (ORAAT) semantics.
///
/// In ORAAT mode, only one rule fires per cycle. When multiple rules are
/// enabled, the scheduler selects the highest priority rule based on:
/// 1. Module-level precedence attribute: precedence = [[@a, @b]] means a > b
/// 2. Rule-level priority attribute: priority = N (lower N = higher priority)
/// 3. Textual order (first defined = higher priority)
///
/// Example:
/// ```mlir
/// cmt2.module @M {
///   cmt2.rule @a { ... }
///   cmt2.rule @b { ... }
/// } { precedence = [[@a, @b]] }  // a has higher priority than b
/// ```
///
class ORAATScheduler : public Scheduler {
public:
  void initialize(cmt2::ModuleOp module) override;
  std::vector<std::string>
  selectRules(const std::vector<std::string> &enabledRules) override;
  llvm::StringRef getName() const override { return "ORAAT"; }
  void reset() override;

  //===--------------------------------------------------------------------===//
  // Priority Management
  //===--------------------------------------------------------------------===//

  /// Get priority of a rule (lower number = higher priority).
  unsigned getPriority(llvm::StringRef ruleName) const;

  /// Set priority for a rule.
  void setPriority(llvm::StringRef ruleName, unsigned priority);

private:
  /// Extract priorities from module's precedence attribute.
  void extractPrecedence(cmt2::ModuleOp module);

  /// Extract priorities from individual rule attributes.
  void extractRulePriorities(cmt2::ModuleOp module);

  /// Rule priorities (lower = higher priority).
  llvm::StringMap<unsigned> priorities_;

  /// Default priority for rules without explicit priority.
  unsigned defaultPriority_ = UINT_MAX;
};

//===----------------------------------------------------------------------===//
// AnnotationScheduler - Scheduling Attribute-Based Scheduler
//===----------------------------------------------------------------------===//

/// Scheduler that uses scheduling annotations to allow parallel rule firing.
///
/// This scheduler respects scheduling constraints from the module:
/// - conflict: Rules that cannot fire together (conflict = [[@a, @b]])
/// - conflictFree: Rules that can always fire together
/// - sequenceBefore: Ordering constraints (a must complete before b starts)
///
/// Unlike ORAAT, this scheduler may fire multiple non-conflicting rules
/// in the same cycle if they are enabled and don't conflict.
///
/// Example:
/// ```mlir
/// cmt2.module @M {
///   cmt2.rule @read1 { ... }
///   cmt2.rule @read2 { ... }
///   cmt2.rule @write { ... }
/// } {
///   conflict = [[@write, @write]],
///   conflictFree = [[@read1, @read2]],
///   sequenceBefore = [[@read1, @write], [@read2, @write]]
/// }
/// ```
///
class AnnotationScheduler : public Scheduler {
public:
  void initialize(cmt2::ModuleOp module) override;
  std::vector<std::string>
  selectRules(const std::vector<std::string> &enabledRules) override;
  llvm::StringRef getName() const override { return "Annotation"; }
  void reset() override;

  //===--------------------------------------------------------------------===//
  // Conflict Queries
  //===--------------------------------------------------------------------===//

  /// Check if two rules conflict (cannot fire together).
  bool conflicts(llvm::StringRef ruleA, llvm::StringRef ruleB) const;

  /// Check if ruleA must complete before ruleB.
  bool mustSequenceBefore(llvm::StringRef ruleA, llvm::StringRef ruleB) const;

  /// Check if two rules are explicitly conflict-free.
  bool isConflictFree(llvm::StringRef ruleA, llvm::StringRef ruleB) const;

private:
  /// Extract scheduling constraints from module.
  void extractConstraints(cmt2::ModuleOp module);

  /// Select maximum non-conflicting subset using greedy algorithm.
  std::vector<std::string>
  selectNonConflicting(const std::vector<std::string> &enabledRules);

  /// Conflict matrix: conflicts_[a] contains all rules that conflict with a.
  llvm::StringMap<llvm::StringSet<>> conflicts_;

  /// Sequence constraints: sequenceBefore_[a] contains rules that must run after a.
  llvm::StringMap<llvm::StringSet<>> sequenceBefore_;

  /// Conflict-free pairs: conflictFree_[a] contains rules explicitly marked as
  /// conflict-free with a.
  llvm::StringMap<llvm::StringSet<>> conflictFree_;

  /// Priorities for tie-breaking (from precedence attribute).
  llvm::StringMap<unsigned> priorities_;
};

//===----------------------------------------------------------------------===//
// PriorityScheduler - Priority-Based Parallel Scheduler
//===----------------------------------------------------------------------===//

/// Scheduler that fires all enabled rules in priority order, respecting conflicts.
///
/// This scheduler processes rules in priority order and fires each rule if:
/// 1. Its guard is enabled
/// 2. It doesn't conflict with any already-fired rule this cycle
///
/// This allows maximum parallelism while respecting conflict constraints.
///
class PriorityScheduler : public Scheduler {
public:
  void initialize(cmt2::ModuleOp module) override;
  std::vector<std::string>
  selectRules(const std::vector<std::string> &enabledRules) override;
  llvm::StringRef getName() const override { return "Priority"; }
  void reset() override;

private:
  /// Sorted list of (priority, ruleName) pairs.
  std::vector<std::pair<unsigned, std::string>> sortedRules_;

  /// Conflict matrix from module constraints.
  llvm::StringMap<llvm::StringSet<>> conflicts_;
};

} // namespace interp
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_INTERPRETER_SCHEDULER_H
