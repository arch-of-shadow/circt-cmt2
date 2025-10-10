//===-- Cmt2Passes.td - Cmt2 pass definition file ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions for passes that work on the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_Cmt2_Cmt2PASSES_TD
#define CIRCT_DIALECT_Cmt2_Cmt2PASSES_TD

include "mlir/Pass/PassBase.td"

// def InferRules : Pass<"cmt2-infer-rules", "cmt2::CircuitOp"> {
//   let summary = "generate all Cmt2::RuleRefOp for a circuit.";
//   let description = [{
//     This pass DFS all Rules via InstanceGraph, find all available rules
//     to construct cmt2::RuleRefOp.
//   }];
//   let constructor = "circt::cmt2::createInferRules()";
// }

// def GenerateConflictMatrix : Pass<"cmt2-generate-conflict-matrix", "cmt2::CircuitOp"> {
//   let summary = "generate the conflict matrix for scheduling.";
//   let description = [{
//     This pass use CallGraph to generate the native call stack store conflict
//     matrix to CircuitOp, scheduling pragma will be gather as well.
//   }];
//   let constructor = "circt::cmt2::createGenerateConflictMatrix()";
// }

#endif // CIRCT_DIALECT_Cmt2_Cmt2PASSES_TD