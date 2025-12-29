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

def PrintConflictMatrix : Pass<"cmt2-print-conflict-matrix", "cmt2::CircuitOp"> {
  let summary = "Print conflict matrix for Cmt2 modules.";
  let description = [{
    This pass analyzes and prints the conflict matrix for each module in a Cmt2
    circuit. The conflict matrix defines scheduling relationships between
    rules/methods/values:
    - Conflict (<>): cannot execute in the same cycle
    - Conflict Free (/): can execute in the same cycle, any order
    - Sequential Before (<): can execute in the same cycle, but ordered

    The analysis is performed bottom-up in the instance hierarchy, using conflict
    information from external modules and propagating it through the design.
  }];
}

def InlinePrivateFuncs : Pass<"cmt2-inline-private-funcs", "cmt2::CircuitOp"> {
  let summary = "Inline private functions in Cmt2 modules.";
  let description = [{
    This pass inlines private functions (methods and values) in Cmt2 modules.
    A private function is defined as a function that is only called by other
    functions in the same module via the @this instance.

    The pass:
    1. Identifies all private functions in each module
    2. Inlines the body of each private function at its call sites
    3. Removes the private function definition after inlining
  }];
}

def PrintScheduler : Pass<"cmt2-print-scheduler", "cmt2::CircuitOp"> {
  let summary = "Print scheduler analysis results for Cmt2 modules.";
  let description = [{
    This pass computes and prints the scheduling analysis results for each module
    in a Cmt2 circuit. The scheduler:
    1. Collects all functions (rules/methods/values) to be scheduled
    2. Uses conflict matrix analysis to determine relationships
    3. Parses precedence constraints from module attributes
    4. Groups functions that must be in the same scheduling group
    5. Computes optimal ordering within each group

    The output shows groups of functions where each group can be scheduled
    independently (conflict-free with other groups), and functions within each
    group are ordered to satisfy SequentialBefore and precedence constraints.
  }];
}

def VerifyPrivateFuncsInlined : Pass<"cmt2-verify-private-funcs-inlined", "cmt2::CircuitOp"> {
  let summary = "Verify that all private functions have been inlined.";
  let description = [{
    This pass verifies that all private functions have been inlined before
    conversion to FIRRTL. It checks each function in every module to ensure
    there are no @this calls remaining.

    If any @this calls are found, the pass fails with an error indicating
    which function contains the call. This validation should be run after
    the cmt2-inline-private-funcs pass and before the Cmt2 to FIRRTL conversion.
  }];
}

def VerifyCallSequence : Pass<"cmt2-verify-call-sequence", "cmt2::CircuitOp"> {
  let summary = "Verify that call sequences respect conflict matrix constraints.";
  let description = [{
    This pass validates that the order of calls within each function respects
    the SequentialBefore relationships defined in the conflict matrix.

    For each function, it examines all calls and verifies that if method A
    must be called before method B (A < B in the conflict matrix), then A
    actually appears before B in the call sequence.

    If any violations are found, the pass fails with an error indicating
    which methods are called in the wrong order. This validation should be
    run before the Cmt2 to FIRRTL conversion.
  }];
}

//===----------------------------------------------------------------------===//
// Procedural Layer Passes (cmt2.proc.*)
//===----------------------------------------------------------------------===//

def CompileInvoke : Pass<"cmt2-compile-invoke", "cmt2::CircuitOp"> {
  let summary = "Compile proc.invoke operations to groups and enables.";
  let description = [{
    This pass converts `cmt2.proc.invoke` operations in procedural control flow
    to `cmt2.proc.enable` operations with generated groups.

    For each `cmt2.proc.invoke`:
    1. Creates a new `cmt2.proc.group` containing the equivalent `cmt2.call`
    2. Replaces the `cmt2.proc.invoke` with a `cmt2.proc.enable` referencing
       the generated group

    This pass must run BEFORE the TDCC pass, as TDCC only handles
    `cmt2.proc.enable` operations, not `cmt2.proc.invoke`.
  }];
}

def TDCC : Pass<"cmt2-tdcc", "cmt2::CircuitOp"> {
  let summary = "Top-Down Compile Control for procedural rules/methods.";
  let description = [{
    This pass implements the Top-Down Compile Control (TDCC) algorithm to
    compile procedural control flow (`cmt2.proc.seq`, `cmt2.proc.par`,
    `cmt2.proc.if`, `cmt2.proc.while`) into an FSM-based implementation.

    The algorithm:
    1. Assigns unique state IDs to each `cmt2.proc.enable` operation
    2. Builds a schedule with enables (state → assignments) and transitions
    3. Applies optimizations (state merging, early transitions)
    4. Generates metadata attributes for FSM generation

    After this pass, procedural rules/methods have metadata attributes
    describing the FSM states and transitions.
  }];
}

def ProcStmtToAction : Pass<"cmt2-proc-stmt-to-action", "cmt2::CircuitOp"> {
  let summary = "Convert procedural control statements to FSM-based actions.";
  let description = [{
    This pass converts procedural control statements to FSM-based action rules.
    It uses the TDCC metadata to generate:

    1. An FSM register external module (if not exists)
    2. An FSM register instance for each proc.rule/method
    3. Regular cmt2.rule operations for each state transition
    4. cmt2.value operations for idle/running status

    The proc.group operations are preserved and their content is inlined
    into the generated rules.

    This pass must run AFTER TDCC and BEFORE ProcToGAA.
  }];
}

def ProcToGAA : Pass<"cmt2-proc-to-gaa", "cmt2::CircuitOp"> {
  let summary = "Convert procedural rules/methods to regular GAA rules.";
  let description = [{
    This pass converts procedural rules/methods (`cmt2.proc.rule`,
    `cmt2.proc.method`) to regular GAA constructs.

    For each procedural rule:
    - Generates an FSM state register
    - Creates regular GAA rules for each state transition
    - Generates @name__idle() and @name__running() value methods

    For each procedural method:
    - Generates argument registers to hold inputs across cycles
    - Generates result register
    - Creates entry method that latches args and starts FSM
    - Creates regular GAA rules for state transitions
    - Generates result value method

    This pass must run AFTER TDCC.
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