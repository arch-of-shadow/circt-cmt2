//===- PrivateFuncAnalysis.h - Private Function Analysis --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the private function analysis for the Cmt2 dialect.
// A private function is only called by functions in the same module via the
// @this instance.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_TRANSFORMS_PRIVATEFUNCANALYSIS_H
#define CIRCT_DIALECT_CMT2_TRANSFORMS_PRIVATEFUNCANALYSIS_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "mlir/Pass/AnalysisManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace circt {
namespace cmt2 {

/// Analysis to identify private functions in each module.
/// A private function is only called by functions in the same module via @this.
class PrivateFuncAnalysis {
public:
  explicit PrivateFuncAnalysis(Operation *operation);

  /// Check if a function (method/value) is private in its module
  bool isPrivateFunc(mlir::Operation *func) const;

  /// Get all private functions in a specific module
  llvm::DenseSet<mlir::Operation *> getPrivateFuncs(Cmt2ModuleLike module) const;

  /// Print the analysis results for debugging
  void print(llvm::raw_ostream &os) const;

private:
  /// Build the private function sets for all modules
  void buildPrivateFuncSets(Operation *operation, CallInfoView &callInfo);

  /// Map from module to set of private functions in that module
  llvm::DenseMap<Cmt2ModuleLike, llvm::DenseSet<mlir::Operation *>>
      privateFuncs;
};

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_TRANSFORMS_PRIVATEFUNCANALYSIS_H
