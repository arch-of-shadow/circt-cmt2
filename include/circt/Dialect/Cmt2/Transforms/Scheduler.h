//===- Scheduler.h - Scheduler Analysis for Cmt2 --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Scheduler analysis for the Cmt2 dialect.
// The Scheduler steps functions and orders them for optimal scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_TRANSFORMS_SCHEDULER_H
#define CIRCT_DIALECT_CMT2_TRANSFORMS_SCHEDULER_H

#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Support/LLVM.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <vector>

namespace circt {
namespace cmt2 {

/// Information about a preventing firing relationship
struct PreventingFiring {
  StringAttr earlier;      // Function scheduled earlier (c[i] > c[j])
  StringAttr later;        // Function scheduled later (c[i] < c[j] in schedule)
  Relationship relationship; // Type: SequentialBefore or Conflict
};

/// A schedule step contains functions that have Conflict or SequentialBefore
/// relationships. Functions in the step are ordered to satisfy constraints.
class ScheduleStep {
public:
  ScheduleStep() = default;

  /// Add a function to the step (ordered)
  void addFunction(StringAttr func) { functions.push_back(func); }

  /// Get all functions in this step (in scheduled order)
  const SmallVector<StringAttr> &getFunctions() const { return functions; }

  /// Add a preventing firing violation
  void addPreventingFiring(StringAttr earlier, StringAttr later,
                           Relationship rel) {
    preventingFirings.push_back({earlier, later, rel});
  }
  
  /// Get all preventing firing violations
  const SmallVector<PreventingFiring> &getPreventingFirings() const {
    return preventingFirings;
  }

  /// Get the number of functions
  size_t size() const { return functions.size(); }

  /// Print the group
  void print(llvm::raw_ostream &os) const;

private:
  SmallVector<StringAttr> functions;
  SmallVector<PreventingFiring> preventingFirings;
};



/// Result of scheduling analysis for a module
class ModuleScheduleResult {
public:
  ModuleScheduleResult() = default;

  /// Add a schedule step
  void addStep(ScheduleStep step) { steps.push_back(std::move(step)); }

  /// Get all schedule steps
  const SmallVector<ScheduleStep> &getSteps() const { return steps; }

  /// Print the schedule result
  void print(llvm::raw_ostream &os, StringAttr moduleName) const;

private:
  SmallVector<ScheduleStep> steps;
};

/// SchedulerAnalysis: computes scheduling for all modules
class SchedulerAnalysis {
public:
  SchedulerAnalysis(CircuitOp circuit);

  /// Get the schedule result for a module
  const ModuleScheduleResult *getModuleSchedule(StringAttr moduleName) const;

  /// Check if analysis has been computed for a module
  bool hasModuleSchedule(StringAttr moduleName) const;

  /// Print all schedule results
  void print(llvm::raw_ostream &os) const;

private:
  /// Union-Find data structure for grouping
  class UnionFind {
  public:
    UnionFind(size_t n) : parent(n), rank(n, 0) {
      for (size_t i = 0; i < n; ++i)
        parent[i] = i;
    }

    /// Find the root of x with path compression
    size_t find(size_t x) {
      if (parent[x] != x)
        parent[x] = find(parent[x]);
      return parent[x];
    }

    /// Union two sets
    void unite(size_t x, size_t y) {
      size_t rootX = find(x);
      size_t rootY = find(y);
      if (rootX == rootY)
        return;

      if (rank[rootX] < rank[rootY]) {
        parent[rootX] = rootY;
      } else if (rank[rootX] > rank[rootY]) {
        parent[rootY] = rootX;
      } else {
        parent[rootY] = rootX;
        rank[rootX]++;
      }
    }

    /// Check if two elements are in the same set
    bool connected(size_t x, size_t y) { return find(x) == find(y); }

  private:
    std::vector<size_t> parent;
    std::vector<size_t> rank;
  };

  /// Run the analysis
  void runAnalysis();

  /// Compute schedule for a single module
  ModuleScheduleResult computeModuleSchedule(ModuleOp module);

  /// Parse precedence constraints from module attributes
  /// Returns list of precedence chains: [[@a, @b, @c], ...] means a << b << c
  SmallVector<SmallVector<StringAttr>> parsePrecedence(ModuleOp module);

  /// Step functions using union-find based on conflict relationships
  /// Returns mapping from function index to step id
  std::vector<size_t> groupFunctions(
      const SmallVector<StringAttr> &functions,
      const ModuleConflictMatrix *matrix);

  /// Solve scheduling for a single step
  /// Minimizes violations while respecting precedence constraints
  SmallVector<StringAttr> solveStepSchedule(
      const SmallVector<StringAttr> &stepFunctions,
      const ModuleConflictMatrix *matrix,
      const SmallVector<SmallVector<StringAttr>> &precedence);

  /// Check if there's a precedence constraint: fx << fy
  bool hasPrecedence(StringAttr fx, StringAttr fy,
                     const SmallVector<SmallVector<StringAttr>> &precedence);

  /// Analyze preventing firing relationships in a scheduled step
  /// Returns violations where c[i] > c[j] but f[i] < f[j] or f[i] <> f[j]
  void analyzePreventingFiring(const SmallVector<StringAttr> &scheduledFunctions,
                               const ModuleConflictMatrix *matrix,
                               ScheduleStep &step);

  /// Check if a function is a private function (only called via @this)
  /// Returns true if the function should not be in the schedule
  bool isPrivateFunction(ModuleOp module, StringAttr funcName);

  CircuitOp circuit;
  ConflictMatrixAnalysis conflictAnalysis;
  DenseMap<StringAttr, ModuleScheduleResult> schedules;
};

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_TRANSFORMS_SCHEDULER_H
