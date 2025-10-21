//===- ConflictMatrix.h - Conflict Matrix Analysis for Cmt2 ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ConflictMatrix analysis for the Cmt2 dialect.
// The ConflictMatrix infers scheduling relationships among rules/methods/values
// of a module based on their calls to instance methods.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_TRANSFORMS_CONFLICTMATRIX_H
#define CIRCT_DIALECT_CMT2_TRANSFORMS_CONFLICTMATRIX_H

#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Support/LLVM.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include <set>

namespace circt {
namespace cmt2 {

/// Relationship types between two functions (rules/methods/values)
enum class Relationship {
  ConflictFree,    // CF: fx / fy - can execute in same cycle, any order
  Conflict,        // C:  fx <> fy - cannot execute in same cycle
  SequentialBefore // SB: fx < fy - can execute in same cycle, fx before fy
};

/// Key type for function pairs in the conflict matrix
using FunctionPair = std::pair<StringAttr, StringAttr>;

/// ConflictMatrix for a single module
/// Maps pairs of function names to their relationship
class ModuleConflictMatrix {
public:
  ModuleConflictMatrix() = default;

  /// Set the relationship between two functions
  void setRelationship(StringAttr fx, StringAttr fy, Relationship rel);

  /// Get the relationship between two functions
  /// Returns ConflictFree if no relationship is recorded
  Relationship getRelationship(StringAttr fx, StringAttr fy) const;

  /// Check if a relationship exists
  bool hasRelationship(StringAttr fx, StringAttr fy) const;

  /// Get all function pairs with their relationships
  const DenseMap<FunctionPair, Relationship> &getRelationships() const {
    return relationships;
  }

  /// Print the conflict matrix
  void print(llvm::raw_ostream &os, StringAttr moduleName) const;
  /// Print the conflict matrix
  void print(llvm::raw_ostream &os, llvm::StringRef moduleName) const;

private:
  /// Normalize function pair (always store in sorted order)
  FunctionPair normalizePair(StringAttr fx, StringAttr fy) const;

  /// Map from function pairs to relationships
  DenseMap<FunctionPair, Relationship> relationships;
};

/// ConflictMatrixAnalysis: analyzes conflict relationships for all modules
class ConflictMatrixAnalysis {
public:
  ConflictMatrixAnalysis(CircuitOp circuit);

  /// Get the conflict matrix for a module
  const ModuleConflictMatrix *getModuleMatrix(StringAttr moduleName) const;

  /// Check if analysis has been computed for a module
  bool hasModuleMatrix(StringAttr moduleName) const;

  /// Print all conflict matrices
  void print(llvm::raw_ostream &os) const;

private:
  /// Run the analysis
  void runAnalysis();

  /// Parse conflict matrix from external module attributes
  void parseExternalModuleMatrix(ExtModuleFirrtlOp extModule);

  /// Infer conflict matrix for a regular module
  void inferModuleMatrix(ModuleOp module, const CallInfoView &callInfo,
                         const DenseMap<StringAttr, InstanceOp> &instanceMap);

  /// Apply inference rules to determine relationship between fx and fy
  Relationship inferRelationship(StringAttr fxName, StringAttr fyName,
                                 bool isAction, bool hasArguments,
                                 const ModuleCallInfo &callInfo,
                                 const DenseMap<StringAttr, InstanceOp> &instanceMap);

  CircuitOp circuit;
  DenseMap<StringAttr, ModuleConflictMatrix> matrices;
};

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_TRANSFORMS_CONFLICTMATRIX_H
