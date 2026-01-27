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

    The output shows steps of functions where each step can be scheduled
    independently (conflict-free with other groups), and functions within each
    step are ordered to satisfy SequentialBefore and precedence constraints.
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
  let summary = "Compile proc.invoke operations to steps and enables.";
  let description = [{
    This pass converts `cmt2.proc.invoke` operations in procedural control flow
    to `cmt2.proc.enable` operations with generated steps.

    For each `cmt2.proc.invoke`:
    1. Creates a new `cmt2.proc.step` containing the equivalent `cmt2.call`
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

    The proc.step operations are preserved and their content is inlined
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

//===----------------------------------------------------------------------===//
// Static Control Passes
//===----------------------------------------------------------------------===//

def StaticInference : Pass<"cmt2-static-inference", "cmt2::CircuitOp"> {
  let summary = "Infer latencies and mark promotable control structures.";
  let description = [{
    This pass performs static latency inference on procedural control
    structures and marks which dynamic control can be promoted to static.

    The algorithm:
    1. Post-order traversal of modules for invoke latency propagation
    2. Recursively computes latencies for control structures:
       - seq: sum of child latencies
       - par: max of child latencies
       - if/static_if: max of branch latencies
       - while: unknown (not promotable) unless @bound attribute
       - static_repeat: count * body_latency
       - enable: latency from step's @latency attribute or 1

    3. Marks control with "promotable" attribute and computed latency
    4. Steps with static_step are automatically promotable

    This pass should run BEFORE StaticPromotion.
  }];
}

def StaticPromotion : Pass<"cmt2-static-promotion", "cmt2::CircuitOp"> {
  let summary = "Promote dynamic control to static when beneficial.";
  let description = [{
    This pass converts dynamic procedural control to static control when
    it meets promotion heuristics and has been marked promotable by
    StaticInference.

    Promotion heuristics:
    1. Threshold: Minimum control size (approximate FSM transitions needed)
    2. Cycle limit: Maximum latency allowed for promotion
    3. If-diff limit: Maximum latency difference between if-branches

    The pass:
    1. Identifies promotable control sequences
    2. Converts proc.seq to static seq (when all children are static)
    3. Converts proc.par to static par (when all children are static)
    4. Converts proc.if to proc.static_if (when both branches promotable)
    5. Converts proc.while with @bound to proc.static_repeat

    This pass should run AFTER StaticInference and BEFORE TDCC.
  }];

  let options = [
    Option<"threshold", "threshold", "unsigned", /*default=*/"5",
           "Minimum control size for promotion">,
    Option<"cycleLimit", "cycle-limit", "unsigned", /*default=*/"100",
           "Maximum latency for promotion">,
    Option<"ifDiffLimit", "if-diff-limit", "unsigned", /*default=*/"10",
           "Maximum if-branch latency difference">
  ];
}

def CompileStatic : Pass<"cmt2-compile-static", "cmt2::CircuitOp"> {
  let summary = "Compile static control to optimized FSM hardware.";
  let description = [{
    This pass compiles static control structures to hardware with
    optimized FSM implementations.

    The algorithm:
    1. Flattens nested static control into a single static group
    2. Computes timing offsets for each assignment
    3. Builds FSM tree structure tracking parent-child relationships
    4. Uses graph coloring to minimize FSM registers through sharing
    5. Generates counter-based FSM with configurable encoding

    Static control handling:
    - static_seq: concatenated timing with offset accumulation
    - static_par: parallel assignments at cycle 0 (or colored FSMs)
    - static_if: conditional capture at cycle 0, guarded branches
    - static_repeat: body * count with cycle multiplication

    FSM encoding options:
    - Binary: log2(states) bits, more compact for large FSMs
    - One-hot: one bit per state, faster transitions

    This pass should run AFTER StaticPromotion and can replace TDCC
    for fully static control.
  }];

  let options = [
    Option<"oneHotThreshold", "one-hot-threshold", "unsigned", /*default=*/"8",
           "Use one-hot encoding for FSMs with <= this many states">
  ];
}

def ControlCollapsing : Pass<"cmt2-control-collapsing", "cmt2::CircuitOp"> {
  let summary = "Collapse and simplify procedural control structures.";
  let description = [{
    This pass simplifies procedural control by collapsing redundant
    structures and merging compatible operations.

    Transformations:
    1. Flatten nested seq: seq { seq { A; B }; C } -> seq { A; B; C }
    2. Flatten nested par: par { par { A; B }; C } -> par { A; B; C }
    3. Remove empty seq/par
    4. Merge sequential enables: seq { enable @a; enable @b } -> enable @merged
       when @a and @b are conflict-free
    5. Remove single-child seq/par wrappers

    This pass should run AFTER StaticPromotion and BEFORE TDCC.
  }];
}

def SimplifyStaticGuards : Pass<"cmt2-simplify-static-guards", "cmt2::CircuitOp"> {
  let summary = "Simplify timing guards in static control.";
  let description = [{
    This pass simplifies complex timing guards in static groups by
    merging overlapping timing intervals.

    For example:
    - `(condition) & @[2:8] & @[5:10]` -> `(condition) & @[5:8]`
    - `@[0:3] & @[0:5]` -> `@[0:3]`

    This reduces the complexity of guard logic in the generated hardware.
  }];
}

//===----------------------------------------------------------------------===//
// Cycle-Precise Timing Passes
//===----------------------------------------------------------------------===//

def TimingInference : Pass<"cmt2-timing-inference", "cmt2::CircuitOp"> {
  let summary = "Infer cycle-precise timing for methods and steps.";
  let description = [{
    This pass performs cycle-precise timing inference on CMT2 modules.
    It analyzes method contracts and propagates timing information through
    the module hierarchy.

    The algorithm:
    1. Post-order traversal of modules (bottom-up through instance hierarchy)
    2. For each module:
       - Collect timing from bound methods (external module contracts)
       - Infer timing for proc.method ops based on their body
       - Infer timing for proc.static_step ops based on their calls
    3. Propagate timing through calls to determine:
       - Minimum latency required for steps
       - Whether methods can be promoted to static
       - Initiation intervals for pipelined operations

    The pass adds/updates timing attributes:
    - `static_latency` on proc.method ops
    - `interval` on proc.method and proc.static_step ops
    - `arg_timing` and `result_timing` on calls (if inferrable)

    This pass should run BEFORE TimingValidation.
  }];

  let options = [
    Option<"inferCallTiming", "infer-call-timing", "bool", /*default=*/"true",
           "Infer timing annotations for calls when missing">,
    Option<"promoteToStatic", "promote-to-static", "bool", /*default=*/"true",
           "Add static_latency to methods when timing is deterministic">
  ];
}

def TimingValidation : Pass<"cmt2-timing-validation", "cmt2::CircuitOp"> {
  let summary = "Validate cycle-precise timing constraints.";
  let description = [{
    This pass validates that all cycle-precise timing constraints are
    satisfied throughout the design.

    Validation checks:
    1. Call-site timing compatibility:
       - Result timing >= method output latency
       - Arg timing stable during method input window
       - All timing within parent step bounds
    2. Pipelined call spacing:
       - Calls to same pipelined method respect initiation interval
    3. Step timing consistency:
       - Static step latency covers all contained operations
       - No timing overlaps that violate resource constraints

    The pass emits detailed diagnostics for any violations, including:
    - Source location of violating operation
    - Expected vs actual timing values
    - Suggested fixes when applicable

    This pass should run AFTER TimingInference.
  }];

  let options = [
    Option<"strict", "strict", "bool", /*default=*/"false",
           "Require explicit timing on all calls in static steps">
  ];
}

//===----------------------------------------------------------------------===//
// Dataflow/Pipeline Passes
//===----------------------------------------------------------------------===//

def DataflowLowering : Pass<"cmt2-dataflow-lowering", "cmt2::CircuitOp"> {
  let summary = "Lower dataflow constructs to GAA rules with tokens.";
  let description = [{
    This pass converts high-level dataflow constructs (`cmt2.proc.dataflow`
    and `cmt2.dataflow.task`) to regular GAA rules with token-based
    synchronization.

    For each `cmt2.proc.dataflow`:
    1. Each `cmt2.dataflow.task` is converted to a `cmt2.rule` with:
       - Token inputs from `tokens_in()` as rule token arguments
       - Token outputs as rule results
       - Guard includes token validity checks (AND of all token.valid)
       - Body contains the task's computation
    2. Token wiring between rules is preserved via SSA def-use

    For tasks with `dataflow.return`:
    - The result is assigned to the dataflow's output (module wiring)

    Token semantics:
    - Tokens are SSA values representing synchronization points
    - A token can have multiple consumers (fork pattern)
    - Rules fire when all input tokens are valid

    This pass should run BEFORE token lowering passes.
  }];
}

def TokenLowering : Pass<"cmt2-token-lowering", "cmt2::CircuitOp"> {
  let summary = "Lower tokens to hardware (shift registers and FIFOs).";
  let description = [{
    This pass lowers abstract token operations to concrete hardware:

    For latency-sensitive (LS) tokens:
    - Tokens become shift registers for data forwarding
    - Valid signals are direct wires (combinational)
    - Multi-consumer tokens use signal fan-out

    For latency-insensitive (LI) tokens:
    - Tokens become FIFOs with inferred depth
    - Valid/ready handshaking for flow control
    - Multi-consumer tokens use broadcast FIFOs

    Token operations are lowered as:
    - `token.create` → enqueue to shift reg/FIFO
    - `token.valid` → check not-empty (FIFO) or valid wire (shift reg)
    - `token.data` → peek/dequeue from shift reg/FIFO
    - `token.join` → AND of all input valids

    For mixed LS/LI designs, stall controllers are generated at boundaries
    to gate LS registers when blocked by LI interfaces.

    This pass should run AFTER dataflow-lowering and timing-inference.
  }];

  let options = [
    Option<"defaultFIFODepth", "default-fifo-depth", "unsigned", /*default=*/"2",
           "Default FIFO depth when analysis cannot infer">,
    Option<"useBroadcastFIFO", "use-broadcast-fifo", "bool", /*default=*/"true",
           "Use broadcast FIFOs for multi-consumer LI tokens">
  ];
}

def TokenRTLGen : Pass<"cmt2-token-rtl-gen", "cmt2::CircuitOp"> {
  let summary = "Generate RTL for token operations.";
  let description = [{
    This pass converts token annotations to actual RTL hardware by:

    1. Instantiating storage for tokens:
       - LS tokens: Shift registers (chain of Reg instances)
       - LI tokens: FIFOs with inferred depth

    2. Rewriting token operations to storage operations:
       - `token.create` → write to shift register / FIFO enqueue
       - `token.valid` → shift register valid / FIFO notEmpty
       - `token.data` → shift register peek / FIFO dequeue
       - `token.join` → AND of all input valids

    3. Handling multi-consumer (fork) patterns:
       - LS tokens: fan-out valid/data signals
       - LI tokens: broadcast FIFO with per-consumer handshake

    This pass MUST run AFTER TokenLowering (which adds annotations) and
    BEFORE Cmt2ToFIRRTL conversion.
  }];

  let dependentDialects = ["circt::firrtl::FIRRTLDialect"];
}

def StallControllerGen : Pass<"cmt2-stall-controller-gen", "cmt2::CircuitOp"> {
  let summary = "Generate stall controllers for LS/LI boundaries.";
  let description = [{
    This pass analyzes mixed latency-sensitive/latency-insensitive regions
    and generates stall controller annotations for hardware generation.

    For each LS region that interacts with LI interfaces:
    1. Identify all LI boundaries (entry and exit points)
    2. Compute stall condition: stall when ANY LI interface is blocked
    3. Annotate the region with stall controller configuration

    Stall controller interface:
    - Inputs: li_ready[N] (not full for writes), li_valid[N] (not empty for reads)
    - Outputs: stall (gates all LS registers), enable[N] (per-FIFO transaction enables)

    Stall logic:
    - stall = NOT(AND(all_ready) AND AND(all_valid))
    - Each LS register is gated by NOT(stall)

    This pass adds annotations:
    - `stall.controller` on LS regions requiring stall management
    - `stall.li_inputs` list of LI input signals
    - `stall.li_outputs` list of LI output signals
    - `stall.gated_registers` list of registers to gate

    This pass should run AFTER token-lowering.
  }];
}

def StaticFSMAllocation : Pass<"cmt2-static-fsm-allocation", "cmt2::CircuitOp"> {
  let summary = "Allocate FSM states for static control timing.";
  let description = [{
    This pass maps cycle-precise timing intervals to FSM states, preparing
    for hardware code generation.

    The algorithm:
    1. Walk all static steps in each module
    2. For each call with timing annotations:
       - Extract timing intervals (start, end cycles)
       - Compute which FSM states are "live" for the operation
       - Build state → assignment map
    3. Compute FSM bitwidth:
       - Binary encoding: log2(max_state + 1) bits
       - One-hot encoding: one bit per state (for small FSMs)
    4. Optionally perform FSM sharing analysis via graph coloring

    The pass annotates static steps with:
    - `fsm_states`: Total number of FSM states needed
    - `fsm_encoding`: Binary or one-hot
    - `fsm_bitwidth`: Number of bits for FSM register
    - `state_assignments`: Map from state to active assignments

    This pass should run AFTER TimingInference and TimingValidation,
    and BEFORE CompileStatic.
  }];

  let options = [
    Option<"oneHotCutoff", "one-hot-cutoff", "unsigned", /*default=*/"8",
           "Use one-hot encoding for FSMs with <= this many states">,
    Option<"enableSharing", "enable-sharing", "bool", /*default=*/"false",
           "Enable FSM sharing via graph coloring (experimental)">
  ];
}

//===----------------------------------------------------------------------===//
// Debug Passes
//===----------------------------------------------------------------------===//

def AddRuleFiringPort : Pass<"cmt2-add-rule-firing-port", "cmt2::CircuitOp"> {
  let summary = "Add debug output ports for rule firing signals.";
  let description = [{
    This pass adds output ports to modules that indicate when each rule fires.
    These ports are useful for debugging and waveform analysis.

    For each rule in a module, the pass adds:
    - An output port `<rule_name>_firing` of type `!firrtl.uint<1>`

    The firing signal is 1 when the rule's guard is true and it executes
    (i.e., ready ∧ ¬conflict with higher priority rules).

    Note: This pass should run BEFORE Cmt2ToFIRRTL conversion.

    Usage:
    - Run with `--cmt2-add-rule-firing-port`
    - The generated Verilog will have additional output ports
    - Use these in testbenches or waveform viewers to observe rule firing

    Example:
    ```mlir
    // Before:
    cmt2.rule @my_rule { /* guard */ } { /* body */ }

    // After (attribute added):
    cmt2.module @Top ... {
      // ...
      cmt2.rule @my_rule { /* guard */ } { /* body */ } {debug.firing_port = "my_rule_firing"}
    }
    // Module gains output port annotation
    ```
  }];

  let options = [
    Option<"prefix", "prefix", "std::string", /*default=*/"\"dbg_\"",
           "Prefix for generated firing port names">
  ];
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