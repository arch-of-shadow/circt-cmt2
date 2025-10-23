//===- Scheduler.cpp - Scheduler Analysis Implementation --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/Scheduler.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include <algorithm>
#include <map>

#define DEBUG_TYPE "cmt2-scheduler"

using namespace circt;
using namespace cmt2;

//===----------------------------------------------------------------------===//
// ScheduleGroup
//===----------------------------------------------------------------------===//

void ScheduleGroup::print(llvm::raw_ostream &os) const {
  os << "[";
  for (size_t i = 0; i < functions.size(); ++i) {
    if (i > 0)
      os << ", ";
    os << "@" << functions[i].getValue();
  }
  os << "]";
}

//===----------------------------------------------------------------------===//
// ModuleScheduleResult
//===----------------------------------------------------------------------===//

void ModuleScheduleResult::print(llvm::raw_ostream &os,
                                  StringAttr moduleName) const {
  os << "Schedule for module @" << moduleName.getValue() << ":\n";
  os << "[\n";
  for (size_t i = 0; i < groups.size(); ++i) {
    os << "  ";
    groups[i].print(os);
    if (i + 1 < groups.size())
      os << ",";
    os << "\n";
    
    auto preventingFirings = groups[i].getPreventingFirings();
    // Print preventing firing summary if there are any violations
    if (!preventingFirings.empty()) {
      os << "\tPreventing Firing Analysis:\n";
      os << "\t  Violations (functions scheduled in wrong order):\n";
      for (const auto &pf : preventingFirings) {
        os << "\t    @" << pf.earlier.getValue() << " scheduled before @"
          << pf.later.getValue();
        if (pf.relationship == Relationship::SequentialBefore) {
          os << " (violates: " << pf.later.getValue() << " < "
            << pf.earlier.getValue() << ")\n";
        } else if (pf.relationship == Relationship::Conflict) {
          os << " (violates: " << pf.later.getValue() << " <> "
            << pf.earlier.getValue() << ")\n";
        }
      }
      os << "\t  Total violations: " << preventingFirings.size() << "\n";
    } else {
      os << "\tPreventing Firing Analysis: No violations found.\n";
    }

  }
  os << "]\n";

}

//===----------------------------------------------------------------------===//
// SchedulerAnalysis
//===----------------------------------------------------------------------===//

SchedulerAnalysis::SchedulerAnalysis(CircuitOp circuit)
    : circuit(circuit), conflictAnalysis(circuit) {
  runAnalysis();
}

const ModuleScheduleResult *
SchedulerAnalysis::getModuleSchedule(StringAttr moduleName) const {
  auto it = schedules.find(moduleName);
  if (it == schedules.end())
    return nullptr;
  return &it->second;
}

bool SchedulerAnalysis::hasModuleSchedule(StringAttr moduleName) const {
  return schedules.count(moduleName) > 0;
}

void SchedulerAnalysis::print(llvm::raw_ostream &os) const {
  os << "=== Scheduler Analysis ===\n\n";
  for (const auto &[moduleName, schedule] : schedules) {
    schedule.print(os, moduleName);
    os << "\n";
  }
}

void SchedulerAnalysis::runAnalysis() {
  // Process each module in the circuit
  for (auto &op : circuit.getBodyRegion().front()) {
    if (auto module = dyn_cast<ModuleOp>(op)) {
      auto moduleName = module.getSymNameAttr();
      schedules[moduleName] = computeModuleSchedule(module);
    }
  }
}

ModuleScheduleResult
SchedulerAnalysis::computeModuleSchedule(ModuleOp module) {
  ModuleScheduleResult result;

  // Step 1: Collect all functions in the module to be scheduled
  SmallVector<StringAttr> allFunctions;
  SmallVector<StringAttr> privateFunctions;

  for (auto &op : module.getBodyRegion().front()) {
    if (auto func = dyn_cast<Cmt2FunctionLike>(op)) {
      auto funcName = func.functionNameAttr();
      allFunctions.push_back(funcName);

      // Check if this is a private function
      if (isPrivateFunction(module, funcName)) {
        privateFunctions.push_back(funcName);
      }
    }
  }

  // Warn about private functions that should be inlined first
  if (!privateFunctions.empty()) {
    llvm::errs() << "Warning: Module @" << module.getSymName()
                 << " contains private functions that should be inlined before scheduling:\n";
    for (auto privFunc : privateFunctions) {
      llvm::errs() << "  @" << privFunc.getValue() << "\n";
    }
    llvm::errs() << "Run -cmt2-inline-private-funcs before scheduling.\n";
  }

  // Filter out private functions from scheduling
  SmallVector<StringAttr> functions;
  for (auto func : allFunctions) {
    if (llvm::find(privateFunctions, func) == privateFunctions.end()) {
      functions.push_back(func);
    }
  }

  if (functions.empty())
    return result;

  // Step 2: Get conflict matrix for this module
  auto moduleName = module.getSymNameAttr();
  const ModuleConflictMatrix *matrix =
      conflictAnalysis.getModuleMatrix(moduleName);

  // Step 3: Parse precedence constraints
  auto precedence = parsePrecedence(module);

  // Step 4: Group functions using union-find
  // Functions with Conflict or SequentialBefore must be in same group
  std::vector<size_t> groupIds = groupFunctions(functions, matrix);

  // Step 5: Organize functions by group
  std::map<size_t, SmallVector<StringAttr>> groupMap;
  for (size_t i = 0; i < functions.size(); ++i) {
    groupMap[groupIds[i]].push_back(functions[i]);
  }

  
  LLVM_DEBUG(matrix->print(llvm::dbgs(), "ToSchedule"));
  
  // Step 6: Solve scheduling for each group and analyze preventing firing
  for (const auto &[groupId, groupFuncs] : groupMap) {
    auto scheduled = solveGroupSchedule(groupFuncs, matrix, precedence);

    
    ScheduleGroup group;

    // Analyze preventing firing relationships
    analyzePreventingFiring(scheduled, matrix, group);

    for (auto func : scheduled) {
      group.addFunction(func);
    }
    result.addGroup(std::move(group));
  }

  return result;
}

SmallVector<SmallVector<StringAttr>>
SchedulerAnalysis::parsePrecedence(ModuleOp module) {
  SmallVector<SmallVector<StringAttr>> precedence;

  // Get the precedence attribute from the module
  auto precAttr = module->getAttrOfType<ArrayAttr>("precedence");
  if (!precAttr)
    return precedence;

  // Parse the precedence chains: [[@a, @b, @c], ...] means a << b << c
  for (auto chainAttr : precAttr) {
    if (auto chainArray = dyn_cast<ArrayAttr>(chainAttr)) {
      SmallVector<StringAttr> chain;
      for (auto elemAttr : chainArray) {
        if (auto symRef = dyn_cast<SymbolRefAttr>(elemAttr)) {
          chain.push_back(symRef.getRootReference());
        }
      }
      if (!chain.empty())
        precedence.push_back(std::move(chain));
    }
  }

  return precedence;
}

std::vector<size_t> SchedulerAnalysis::groupFunctions(
    const SmallVector<StringAttr> &functions,
    const ModuleConflictMatrix *matrix) {
  size_t n = functions.size();
  UnionFind uf(n);

  if (!matrix) {
    // No conflict matrix, all functions are in separate groups
    std::vector<size_t> result(n);
    for (size_t i = 0; i < n; ++i)
      result[i] = i;
    return result;
  }

  // Unite functions that have Conflict or SequentialBefore relationships
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      auto rel = matrix->getRelationship(functions[i], functions[j]);
      if (rel == Relationship::Conflict ||
          rel == Relationship::SequentialBefore) {
        uf.unite(i, j);
      }
    }
  }

  // Map roots to sequential group IDs
  std::map<size_t, size_t> rootToGroup;
  std::vector<size_t> result(n);
  size_t nextGroupId = 0;

  for (size_t i = 0; i < n; ++i) {
    size_t root = uf.find(i);
    if (rootToGroup.find(root) == rootToGroup.end()) {
      rootToGroup[root] = nextGroupId++;
    }
    result[i] = rootToGroup[root];
  }

  return result;
}

SmallVector<StringAttr> SchedulerAnalysis::solveGroupSchedule(
    const SmallVector<StringAttr> &groupFunctions,
    const ModuleConflictMatrix *matrix,
    const SmallVector<SmallVector<StringAttr>> &precedence) {

  size_t n = groupFunctions.size();

  // Map function names to indices
  DenseMap<StringAttr, size_t> funcToIdx;
  for (size_t i = 0; i < n; ++i) {
    funcToIdx[groupFunctions[i]] = i;
  }

  // Build hard constraint graph from precedence constraints ONLY (fx << fy)
  std::vector<std::set<size_t>> hardAdj(n);  // adjacency list for hard constraints
  std::vector<size_t> inDegree(n, 0);

  for (const auto &chain : precedence) {
    for (size_t k = 0; k + 1 < chain.size(); ++k) {
      auto itX = funcToIdx.find(chain[k]);
      auto itY = funcToIdx.find(chain[k + 1]);
      if (itX != funcToIdx.end() && itY != funcToIdx.end()) {
        size_t idxX = itX->second;
        size_t idxY = itY->second;
        if (hardAdj[idxX].insert(idxY).second) {
          inDegree[idxY]++;
        }
      }
    }
  }

  // Build soft constraint map from SequentialBefore relationships (fi < fj)
  // These are preferences to minimize violations, not hard constraints
  std::vector<std::vector<size_t>> softBefore(n);  // softBefore[i] = list of functions that should come after i
  if (matrix) {
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = 0; j < n; ++j) {
        if (i != j) {
          auto rel = matrix->getRelationship(groupFunctions[i], groupFunctions[j]);
          if (rel == Relationship::SequentialBefore) {
            // i < j means we prefer i to be scheduled before j
            softBefore[i].push_back(j);
          }
        }
      }
    }
  }

  // Greedy topological sort that minimizes SequentialBefore violations
  SmallVector<StringAttr> result;
  std::vector<bool> scheduled(n, false);
  std::vector<size_t> currentInDegree = inDegree;  // working copy

  for (size_t iter = 0; iter < n; ++iter) {
    // Find all candidates: nodes not yet scheduled with inDegree == 0
    std::vector<size_t> candidates;
    for (size_t i = 0; i < n; ++i) {
      if (!scheduled[i] && currentInDegree[i] == 0) {
        candidates.push_back(i);
      }
    }

    if (candidates.empty()) {
      // Cycle detected in hard constraints
      llvm::errs() << "Warning: Cycle detected in precedence constraints, "
                      "scheduling may be incomplete\n";
      // Add remaining unscheduled functions
      for (size_t i = 0; i < n; ++i) {
        if (!scheduled[i]) {
          result.push_back(groupFunctions[i]);
        }
      }
      break;
    }

    // Among candidates, pick the one that maximizes satisfied SequentialBefore preferences
    // A preference is "satisfied" if we schedule a function whose softBefore targets
    // are mostly not yet scheduled (i.e., we're scheduling it before its dependencies)
    size_t bestIdx = candidates[0];
    int bestScore = -1;

    for (size_t candidate : candidates) {
      // Score = number of unscheduled functions in softBefore[candidate]
      // Higher score means scheduling this function satisfies more preferences
      int score = 0;
      for (size_t target : softBefore[candidate]) {
        if (!scheduled[target]) {
          score++;
        }
      }
      if (score > bestScore) {
        bestScore = score;
        bestIdx = candidate;
      }
    }

    // Schedule the best candidate
    scheduled[bestIdx] = true;
    result.push_back(groupFunctions[bestIdx]);

    // Update in-degrees by removing edges from bestIdx
    for (size_t next : hardAdj[bestIdx]) {
      currentInDegree[next]--;
    }
  }

  return result;
}

bool SchedulerAnalysis::hasPrecedence(
    StringAttr fx, StringAttr fy,
    const SmallVector<SmallVector<StringAttr>> &precedence) {
  for (const auto &chain : precedence) {
    auto itX = llvm::find(chain, fx);
    auto itY = llvm::find(chain, fy);
    if (itX != chain.end() && itY != chain.end()) {
      // Check if fx comes before fy in the chain
      return std::distance(chain.begin(), itX) < std::distance(chain.begin(), itY);
    }
  }
  return false;
}

void SchedulerAnalysis::analyzePreventingFiring(
    const SmallVector<StringAttr> &scheduledFunctions,
    const ModuleConflictMatrix *matrix,
    ScheduleGroup &group
  ) {

  if (!matrix)
    return;

  size_t n = scheduledFunctions.size();


  // For each pair (i, j) where i is scheduled after j (c[i] > c[j])
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < i; ++j) {
      // i is scheduled later than j, so c[i] > c[j] in position
      StringAttr fi = scheduledFunctions[i];
      StringAttr fj = scheduledFunctions[j];

      auto rel = matrix->getRelationship(fi, fj);
      // llvm::dbgs() << fi << " " << (rel==Relationship::SequentialBefore ? "SB" : 
      //                              rel==Relationship::Conflict ? "C" :
      //                              "CF")
      //   << " " << fj << "\n";

      // Check if fi < fj (SequentialBefore) or fj <> fi (Conflict)
      if (rel == Relationship::SequentialBefore || rel == Relationship::Conflict)  {
        // This is a preventing firing: fj scheduled before fi, but fi < fj or fj <> fi
        // Meaning: fi cannot fire because fj needs to fire first or they conflict
        group.addPreventingFiring(fj, fi, rel);
      } 
    }
  }
}

bool SchedulerAnalysis::isPrivateFunction(ModuleOp module, StringAttr funcName) {
  // A private function is one that is only called via @this instance
  // We need to check if there are any calls to this function from @this

  bool hasThisCall = false;
  bool hasOtherCall = false;

  // Walk all operations in the module
  module.walk([&](CallOp call) {
    // Check if this call is to the function we're looking for
    auto methodOrValue = call.getMethodOrValue().getRootReference();
    if (methodOrValue == funcName) {
      auto callee = call.getCallee().getRootReference().getValue();
      if (callee == "this") {
        hasThisCall = true;
      } else {
        hasOtherCall = true;
      }
    }
  });

  // Private function: has @this calls but no other calls
  return hasThisCall && !hasOtherCall;
}
