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

def Test: Pass<"cmt2-test", "cmt2::CircuitOp"> {
  let summary = "a test pass for cmt2 dialect.";
  let description = [{
    This is a test pass for cmt2 dialect.
  }];
  let constructor = "circt::cmt2::createTestPass()";
}

def PrintInstanceGraph : Pass<"cmt2-print-instance-graph", "cmt2::CircuitOp"> {
  let summary = "Print a DOT graph of the Cmt2 module hierarchy.";
  let description = [{
    This pass prints the instance graph of a Cmt2 circuit in DOT format,
    showing the module hierarchy and instantiation relationships.
  }];
}

def PrintCallInfo : Pass<"cmt2-print-call-info", "cmt2::CircuitOp"> {
  let summary = "Print call information for Cmt2 modules.";
  let description = [{
    This pass prints the call information for each module in a Cmt2 circuit,
    showing which methods and values are called by each rule, method, and value.
  }];
}

def ModuleInliner : Pass<"cmt2-inline-modules", "cmt2::CircuitOp"> {
  let summary = "Inline Cmt2 modules into their instantiation sites.";
  let description = [{
    This pass inlines Cmt2 modules into their instantiation sites. Modules are
    inlined from bottom-up in the instance hierarchy. The following modules are
    NOT inlined:
    - Top-level module (the one referenced by CircuitOp)
    - External modules (ExtModuleHwOp)
    - Modules with "synthesis" attribute set to true

    For each inlined instance:
    1. Subinstances are added to the parent module
    2. Calls to the instance's methods/values are replaced with the cloned body
    3. The inlined module is removed if no longer referenced
  }];
}

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