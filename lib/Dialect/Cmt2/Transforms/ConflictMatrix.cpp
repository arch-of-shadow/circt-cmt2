//===- ConflictMatrix.cpp - Conflict Matrix Analysis for Cmt2 --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/ConflictMatrix.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/InstanceGraph.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-conflict-matrix"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// ModuleConflictMatrix
//===----------------------------------------------------------------------===//

FunctionPair ModuleConflictMatrix::normalizePair(StringAttr fx,
                                                   StringAttr fy) const {
  // Always store in sorted order to ensure consistent lookup
  if (fx.getValue() < fy.getValue())
    return {fx, fy};
  return {fy, fx};
}

void ModuleConflictMatrix::setRelationship(StringAttr fx, StringAttr fy,
                                            Relationship rel) {
  auto pair = normalizePair(fx, fy);

  // If the pair is the same function, only allow ConflictFree or Conflict
  if (fx == fy && rel == Relationship::SequentialBefore) {
    // fx < fx doesn't make sense, treat as ConflictFree
    rel = Relationship::ConflictFree;
  }

  // Check for existing relationship
  auto it = relationships.find(pair);
  if (it != relationships.end()) {
    // Merge relationships according to rule 3:
    // If fx < fy AND fy < fx, then fx <> fy
    if (it->second == Relationship::SequentialBefore &&
        rel == Relationship::SequentialBefore) {
      // Check if we're setting the reverse relationship
      bool isReverse = (pair.first == fy && pair.second == fx);
      if (isReverse) {
        it->second = Relationship::Conflict;
        return;
      }
    }

    // Conflict takes precedence
    if (rel == Relationship::Conflict) {
      it->second = Relationship::Conflict;
    }
  } else {
    relationships[pair] = rel;
  }
}

Relationship ModuleConflictMatrix::getRelationship(StringAttr fx,
                                                     StringAttr fy) const {
  auto pair = normalizePair(fx, fy);
  auto it = relationships.find(pair);
  if (it != relationships.end())
    return it->second;
  return Relationship::ConflictFree; // Default: ConflictFree
}

bool ModuleConflictMatrix::hasRelationship(StringAttr fx, StringAttr fy) const {
  auto pair = normalizePair(fx, fy);
  return relationships.count(pair) > 0;
}

void ModuleConflictMatrix::print(llvm::raw_ostream &os,
                                  StringAttr moduleName) const {
  os << "Conflict Matrix for @" << moduleName.getValue() << ":\n";

  // Group by relationship type
  SmallVector<FunctionPair> conflicts, conflictFrees, sequentials;
  for (const auto &[pair, rel] : relationships) {
    switch (rel) {
    case Relationship::Conflict:
      conflicts.push_back(pair);
      break;
    case Relationship::ConflictFree:
      conflictFrees.push_back(pair);
      break;
    case Relationship::SequentialBefore:
      sequentials.push_back(pair);
      break;
    }
  }

  if (!conflicts.empty()) {
    os << "  Conflicts (<>):\n";
    for (const auto &pair : conflicts) {
      os << "    @" << pair.first.getValue() << " <> @"
         << pair.second.getValue() << "\n";
    }
  }

  if (!sequentials.empty()) {
    os << "  Sequential Before (<):\n";
    for (const auto &pair : sequentials) {
      os << "    @" << pair.first.getValue() << " < @"
         << pair.second.getValue() << "\n";
    }
  }

  if (!conflictFrees.empty()) {
    os << "  Conflict Free (/):\n";
    for (const auto &pair : conflictFrees) {
      os << "    @" << pair.first.getValue() << " / @"
         << pair.second.getValue() << "\n";
    }
  }

  if (conflicts.empty() && sequentials.empty() && conflictFrees.empty()) {
    os << "  (no relationships recorded)\n";
  }
}

//===----------------------------------------------------------------------===//
// ConflictMatrixAnalysis
//===----------------------------------------------------------------------===//

ConflictMatrixAnalysis::ConflictMatrixAnalysis(CircuitOp circuit)
    : circuit(circuit) {
  runAnalysis();
}

void ConflictMatrixAnalysis::runAnalysis() {
  LLVM_DEBUG(llvm::dbgs() << "=== ConflictMatrix Analysis ===\n");

  // First, parse conflict matrices from external modules
  circuit.walk([&](ExtModuleFirrtlOp extModule) {
    parseExternalModuleMatrix(extModule);
  });

  // Build InstanceGraph and CallInfo for efficient lookup
  InstanceGraph instanceGraph(circuit);
  CallInfoView callInfo(circuit);

  // Build module map for efficient lookup
  DenseMap<StringAttr, ModuleOp> moduleMap;
  circuit.walk([&](ModuleOp module) {
    moduleMap[module.getSymNameAttr()] = module;
  });

  // Build instance map for each module for O(1) lookup
  DenseMap<StringAttr, DenseMap<StringAttr, InstanceOp>> moduleInstanceMap;
  circuit.walk([&](ModuleOp module) {
    DenseMap<StringAttr, InstanceOp> instanceMap;
    for (auto &op : module.getBody().front().getOperations()) {
      if (auto instance = dyn_cast<InstanceOp>(op)) {
        instanceMap[instance.getInstanceNameAttr()] = instance;
      }
    }
    moduleInstanceMap[module.getSymNameAttr()] = instanceMap;
  });

  // Use InstanceGraph to get modules in topological order (bottom-up)
  // Process leaf modules first, then their parents
  SmallVector<ModuleOp> modulesInOrder;
  DenseSet<StringAttr> visited;

  // Simple topological sort using instance graph
  for (auto *node : instanceGraph) {
    if (auto module = dyn_cast_or_null<ModuleOp>(node->getModule().getOperation())) {
      if (!visited.count(module.getSymNameAttr())) {
        visited.insert(module.getSymNameAttr());
        modulesInOrder.push_back(module);
      }
    }
  }

  // Process modules in topological order
  for (auto module : modulesInOrder) {
    inferModuleMatrix(module, callInfo, moduleInstanceMap[module.getSymNameAttr()]);
  }

  LLVM_DEBUG(llvm::dbgs() << "=== ConflictMatrix Analysis Complete ===\n");
}

void ConflictMatrixAnalysis::parseExternalModuleMatrix(
    ExtModuleFirrtlOp extModule) {
  StringAttr moduleName = extModule.getSymNameAttr();
  LLVM_DEBUG(llvm::dbgs() << "Parsing external module: @"
                          << moduleName.getValue() << "\n");

  ModuleConflictMatrix matrix;
  
  extModule.getBodyRegion().walk([&](Cmt2FunctionLike func) {
    matrix.setRelationship(
      func.functionNameAttr(), 
      func.functionNameAttr(), 
      func.getFunctionKind() == FunctionKind::Value && func.getNumArguments() == 0 ?
        Relationship::ConflictFree:
        Relationship::Conflict
    );
  });

  // Parse "conflict" attribute: [[@f1, @f2], ...]
  if (auto conflictAttr = extModule->getAttrOfType<ArrayAttr>("conflict")) {
    for (auto pairAttr : conflictAttr) {
      if (auto array = dyn_cast<ArrayAttr>(pairAttr)) {
        if (array.size() == 2) {
          auto f1 = cast<SymbolRefAttr>(array[0]).getLeafReference();
          auto f2 = cast<SymbolRefAttr>(array[1]).getLeafReference();
          matrix.setRelationship(f1, f2, Relationship::Conflict);
          LLVM_DEBUG(llvm::dbgs() << "  @" << f1.getValue() << " <> @"
                                  << f2.getValue() << "\n");
        }
      }
    }
  }

  // Parse "conflictFree" attribute: [[@f1, @f2], ...]
  if (auto cfAttr = extModule->getAttrOfType<ArrayAttr>("conflictFree")) {
    for (auto pairAttr : cfAttr) {
      if (auto array = dyn_cast<ArrayAttr>(pairAttr)) {
        if (array.size() == 2) {
          auto f1 = cast<SymbolRefAttr>(array[0]).getLeafReference();
          auto f2 = cast<SymbolRefAttr>(array[1]).getLeafReference();
          matrix.setRelationship(f1, f2, Relationship::ConflictFree);
          LLVM_DEBUG(llvm::dbgs() << "  @" << f1.getValue() << " / @"
                                  << f2.getValue() << "\n");
        }
      }
    }
  }

  // Parse "sequenceBefore" attribute: [[@f1, @f2], ...] means f1 < f2
  if (auto sbAttr = extModule->getAttrOfType<ArrayAttr>("sequenceBefore")) {
    for (auto pairAttr : sbAttr) {
      if (auto array = dyn_cast<ArrayAttr>(pairAttr)) {
        if (array.size() == 2) {
          auto f1 = cast<SymbolRefAttr>(array[0]).getLeafReference();
          auto f2 = cast<SymbolRefAttr>(array[1]).getLeafReference();
          matrix.setRelationship(f1, f2, Relationship::SequentialBefore);
          LLVM_DEBUG(llvm::dbgs() << "  @" << f1.getValue() << " < @"
                                  << f2.getValue() << "\n");
        }
      }
    }
  }

  matrices[moduleName] = matrix;
}

void ConflictMatrixAnalysis::inferModuleMatrix(
    ModuleOp module, const CallInfoView &callInfo,
    const DenseMap<StringAttr, InstanceOp> &instanceMap) {
  StringAttr moduleName = module.getSymNameAttr();
  LLVM_DEBUG(llvm::dbgs() << "Inferring matrix for module: @"
                          << moduleName.getValue() << "\n");

  ModuleConflictMatrix matrix;

  // Get call info for this module
  const ModuleCallInfo *modCallInfo = callInfo.getModuleCallInfo(moduleName.getValue());
  if (!modCallInfo) {
    // No calls in this module, all functions are conflict-free
    matrices[moduleName] = matrix;
    return;
  }

  // Collect all function names, isAction, hasArgs in this module
  SmallVector<std::tuple<StringAttr, bool, bool>> functionNames;
  for (auto &op : module.getBody().front().getOperations()) {
    if (auto funcLike = dyn_cast<Cmt2FunctionLike>(&op)) {
      functionNames.push_back({funcLike.functionNameAttr(), 
        !(funcLike.getFunctionKind() == FunctionKind::Value),
        funcLike.getFunctionType().getNumInputs() > 0
      });
    }
  }

  // Infer relationships between all pairs of functions
  for (size_t i = 0; i < functionNames.size(); ++i) {
    for (size_t j = i; j < functionNames.size(); ++j) {
      auto &[fxName, isAction, hasArgs] = functionNames[i];
      auto &[fyName, isActionY, hasArgsY] = functionNames[j];

      Relationship rel = inferRelationship(fxName, fyName, isAction && isActionY, hasArgs && hasArgsY, *modCallInfo, instanceMap);
      matrix.setRelationship(fxName, fyName, rel);

      LLVM_DEBUG({
        const char *relStr = "";
        switch (rel) {
        case Relationship::Conflict:
          relStr = "<>";
          break;
        case Relationship::ConflictFree:
          relStr = "/";
          break;
        case Relationship::SequentialBefore:
          relStr = "<";
          break;
        }
        llvm::dbgs() << "  @" << fxName.getValue() << " " << relStr << " @"
                     << fyName.getValue() << "\n";
      });
    }
  }

  matrices[moduleName] = matrix;
}

Relationship ConflictMatrixAnalysis::inferRelationship(
    StringAttr fxName, StringAttr fyName, bool isAction, bool hasArguments, 
    const ModuleCallInfo &callInfo,
    const DenseMap<StringAttr, InstanceOp> &instanceMap) {
  // If same function, default to ConflictFree
  if (fxName == fyName) {
    if (isAction || hasArguments) 
      return Relationship::Conflict;
    else  
      return Relationship::ConflictFree;
  }

  // Get calls for both functions from CallInfo
  auto fxCallsIt = callInfo.find(SymbolRefAttr::get(fxName));
  auto fyCallsIt = callInfo.find(SymbolRefAttr::get(fyName));

  if (fxCallsIt == callInfo.end() || fyCallsIt == callInfo.end()) {
    // One or both functions have no calls, so they are conflict-free
    return Relationship::ConflictFree;
  }

  const EntityCallInfo &callsFx = fxCallsIt->second;
  const EntityCallInfo &callsFy = fyCallsIt->second;

  // Track inferred relationships
  bool hasConflict = false;
  bool hasSB_fx_fy = false; // fx < fy
  bool hasSB_fy_fx = false; // fy < fx

  // Apply inference rules
  for (const auto &cx : callsFx) {
    for (const auto &cy : callsFy) {
      // Get instance names (leaf references)
      StringAttr cxInstance = cx.calleeInstance.getLeafReference();
      StringAttr cyInstance = cy.calleeInstance.getLeafReference();

      // Check if calls are to the same instance
      if (cxInstance != cyInstance)
        continue;

      // Look up the instance using the pre-built instance map (O(1) lookup)
      auto instIt = instanceMap.find(cxInstance);
      if (instIt == instanceMap.end())
        continue;

      InstanceOp instance = instIt->second;

      // Get the referenced module's conflict matrix
      auto refModule = instance.getReferencedModule();
      if (!refModule)
        continue;

      StringAttr refModuleName = refModule.moduleNameAttr();
      const auto *refMatrix = getModuleMatrix(refModuleName);

      if (!refMatrix)
        continue;

        
      // llvm::dbgs() << "Module " << refModuleName << " 's conflict matrix\n";

      refMatrix->print(llvm::dbgs(), refModuleName);

      // Get method names (leaf references)
      StringAttr cxMethod = cx.calleeEntity.getLeafReference();
      StringAttr cyMethod = cy.calleeEntity.getLeafReference();

      // Get relationship between the two methods in the instance's module
      Relationship instRel = refMatrix->getRelationship(cxMethod, cyMethod);

      // Apply inference rules:
      // Rule 1: If i.m0 <> i.m1, then fx <> fy
      if (instRel == Relationship::Conflict) {
        hasConflict = true;
      }
      // Rule 2: If i.m0 < i.m1, then fx < fy
      else if (instRel == Relationship::SequentialBefore) {
        hasSB_fx_fy = true;
      }

      // Check reverse direction for Rule 3
      Relationship reverseRel = refMatrix->getRelationship(cyMethod, cxMethod);
      if (reverseRel == Relationship::SequentialBefore) {
        hasSB_fy_fx = true;
      }
    }
  }

  // Rule 3: If fx < fy AND fy < fx, then fx <> fy
  if (hasSB_fx_fy && hasSB_fy_fx) {
    hasConflict = true;
  }

  // Return final relationship
  if (hasConflict) {
    return Relationship::Conflict;
  } else if (hasSB_fx_fy) {
    return Relationship::SequentialBefore;
  }

  // Rule 4: Default is ConflictFree
  return Relationship::ConflictFree;
}

const ModuleConflictMatrix *
ConflictMatrixAnalysis::getModuleMatrix(StringAttr moduleName) const {
  auto it = matrices.find(moduleName);
  if (it != matrices.end())
    return &it->second;
  return nullptr;
}

bool ConflictMatrixAnalysis::hasModuleMatrix(StringAttr moduleName) const {
  return matrices.count(moduleName) > 0;
}

void ConflictMatrixAnalysis::print(llvm::raw_ostream &os) const {
  os << "=== Conflict Matrix Analysis Results ===\n\n";
  for (const auto &[moduleName, matrix] : matrices) {
    matrix.print(os, moduleName);
    os << "\n";
  }
}
