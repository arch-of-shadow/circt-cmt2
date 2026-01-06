# CMT2 Timing Attribute Syntax Reference

This document describes the cycle-precise timing attributes available in the CMT2 dialect for static scheduling of hardware operations.

## Overview

CMT2's timing system enables cycle-precise control of method calls within static steps. Timing information is specified at two levels:

1. **Method Signatures** - Define timing contracts that callers must respect
2. **Call Sites** - Specify when arguments are driven and results captured

## Timing Attributes

### TimingIntervalAttr

Half-open cycle intervals `[start, end)` specifying when signals are active.

**Syntax:**
```mlir
#cmt2.timing<[start, end]>
```

**Examples:**
```mlir
#cmt2.timing<[0, 1]>   // Active during cycle 0
#cmt2.timing<[3, 5]>   // Active during cycles 3 and 4
#cmt2.timing<[0, 4]>   // Active during cycles 0, 1, 2, 3
```

**Semantics:**
- `start`: First cycle the signal is active (inclusive)
- `end`: First cycle the signal is NOT active (exclusive)
- Duration = `end - start` cycles
- Constraints: `start >= 0`, `end > start`

### IntervalAttr

Initiation interval for pipelined methods.

**Syntax:**
```mlir
#cmt2.interval<n>
```

**Examples:**
```mlir
#cmt2.interval<3>   // Can accept new inputs every 3 cycles
#cmt2.interval<1>   // Fully pipelined (new input every cycle)
```

**Semantics:**
- Specifies minimum cycles between successive calls to the same method
- Used for resource-sharing in pipelined units
- Constraint: `n >= 1`

### LatencyAttr

Total latency of a method in cycles.

**Syntax:**
```mlir
#cmt2.latency<n>
```

**Examples:**
```mlir
#cmt2.latency<4>   // Result available 4 cycles after call
#cmt2.latency<1>   // Single-cycle operation
```

### PortTimingAttr

Port timing specification with kind and optional latency.

**Syntax:**
```mlir
#cmt2.port<kind>
#cmt2.port<kind, latency>
```

**Port Kinds:**
- `data`: Regular data port
- `go`: Enable/trigger signal
- `done`: Completion signal
- `stable`: Signal that remains stable throughout operation

## Method Signature Timing

### External Module Methods (`BindMethodOp`)

External methods declare timing contracts with `static<n>` syntax:

```mlir
cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
    cmt2.bind.bare %clk, @clk : !firrtl.clock
    cmt2.bind.method @multiply static<4> :
        (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) ->
        (!firrtl.uint<1>, !firrtl.uint<32>) [
            enable = "go", ready = "done",
            arguments = ["left", "right"], results = ["out"]
        ] {interval = #cmt2.interval<3>}
} {
    conflict = [[@multiply, @multiply]],
    conflictFree = []
}
```

**Attributes:**
- `static<n>`: Total method latency in cycles
- `interval = #cmt2.interval<k>`: Initiation interval for pipelining

### CMT2 Module Methods (`ProcMethodOp`)

Internal methods can also declare static latency:

```mlir
cmt2.proc.method @compute static<5> (...) -> (...) {
    ...
} body { ... }
```

## Call-Site Timing

### Basic Call Timing

Calls within static steps specify when arguments are driven and results captured:

```mlir
cmt2.proc.static_step @compute<6> {
    %c10 = firrtl.constant 10 : !firrtl.uint<32>
    %c20 = firrtl.constant 20 : !firrtl.uint<32>

    %result = cmt2.call @mult_unit @multiply(%c10, %c20) {
        arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
        result_timing = [#cmt2.timing<[4, 5]>]
    } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
}
```

**Attributes:**
- `arg_timing`: Array of timing intervals for each argument
- `result_timing`: Array of timing intervals for each result

### Timing Constraints

Call-site timing must satisfy:

1. **Non-negative start**: `timing.start >= 0`
2. **Within step bounds**: `timing.end <= step.latency`
3. **Result after latency**: `result_timing.start >= method.static_latency`
4. **Respects initiation interval**: Successive calls spaced by at least II cycles

### Pipelined Calls

Multiple calls to the same pipelined unit with staggered timing:

```mlir
cmt2.proc.static_step @pipeline_step<10> {
    %c1 = firrtl.constant 1 : !firrtl.uint<32>
    %c2 = firrtl.constant 2 : !firrtl.uint<32>
    %c3 = firrtl.constant 3 : !firrtl.uint<32>
    %c4 = firrtl.constant 4 : !firrtl.uint<32>

    // First call at cycle 0, result at cycle 4
    %r1 = cmt2.call @mult_unit @multiply(%c1, %c2) {
        arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
        result_timing = [#cmt2.timing<[4, 5]>]
    } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

    // Second call at cycle 3 (respects II=3), result at cycle 7
    %r2 = cmt2.call @mult_unit @multiply(%c3, %c4) {
        arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>],
        result_timing = [#cmt2.timing<[7, 8]>]
    } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
}
```

## Static Steps

### Basic Static Step

Fixed-latency control regions:

```mlir
cmt2.proc.static_step @step_name<latency> {
    // Operations with timing annotations
}
```

**Semantics:**
- Executes for exactly `latency` cycles
- All calls must have timing within `[0, latency)`
- FSM is generated with `latency` states

### Static Step with Interval

For pipelined static steps:

```mlir
cmt2.proc.static_step @pipelined_step<8> {
    ...
} {interval = #cmt2.interval<4>}
```

## FSM Compilation

After timing passes, static steps are annotated with FSM information:

```mlir
cmt2.proc.static_step @compute<6> {
    %result = cmt2.call @mult_unit @multiply(%c10, %c20) {
        arg_timing = [...],
        result_timing = [...],
        fsm_guard_expr = "fsm[0]",     // Enable on state 0
        fsm_start_state = 0             // Call starts at state 0
    } : ...
} {
    fsm_bitwidth = 6,                   // 6-bit one-hot register
    fsm_encoding = "one_hot",           // Encoding scheme
    fsm_states = 6,                     // Number of states
    fsm_done_expr = "fsm[5]",           // Done when in state 5
    fsm_init_expr = "6'b1",             // Initial state
    fsm_next_expr = "{fsm[4:0], 1'b0}", // Shift left
    static_compiled                      // Marker for compiled step
}
```

**FSM Encoding:**
- **One-hot** (states <= 8): Uses N bits for N states, fast decode
- **Binary** (states > 8): Uses log2(N) bits, smaller but slower decode

## Timing Passes

The timing pass pipeline:

```bash
circt-opt input.mlir \
    -cmt2-timing-inference \     # Infer timing from method contracts
    -cmt2-timing-validation \    # Validate timing constraints
    -cmt2-static-fsm-allocation \ # Allocate FSM states
    -cmt2-compile-static         # Generate FSM control logic
```

### Pass Descriptions

1. **TimingInference**: Infers `arg_timing` and `result_timing` for calls based on method signatures
2. **TimingValidation**: Validates timing constraints (bounds, II, data dependencies)
3. **StaticFSMAllocation**: Maps cycles to FSM states, chooses encoding
4. **CompileStatic**: Generates FSM register info and guard expressions

## Examples

### Combinational Unit (No Latency)

```mlir
cmt2.module.extern.firrtl @adder : @Adder {
    cmt2.bind.value @add : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>[
        arguments = ["a", "b"], results = ["out"]
    ]
}
```

Calls to combinational units use single-cycle timing:

```mlir
%sum = cmt2.call @alu @add(%a, %b) {
    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
    result_timing = [#cmt2.timing<[0, 1]>]
} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
```

### Multi-Cycle Unit

```mlir
cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
    cmt2.bind.bare %clk, @clk : !firrtl.clock
    cmt2.bind.method @multiply static<4> : ...
}
```

Calls must wait for result:

```mlir
%product = cmt2.call @mult_unit @multiply(%a, %b) {
    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
    result_timing = [#cmt2.timing<[4, 5]>]  // Wait 4 cycles
} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
```

### Sequential Data Dependency

Read from memory, then multiply:

```mlir
cmt2.proc.static_step @sequential_step<6> {
    %addr = firrtl.constant 10 : !firrtl.uint<8>
    %const = firrtl.constant 2 : !firrtl.uint<32>

    // Read at cycle 0, result at cycle 1
    %mem_data = cmt2.call @mem_unit @read(%addr) {
        arg_timing = [#cmt2.timing<[0, 1]>],
        result_timing = [#cmt2.timing<[1, 2]>]
    } : (!firrtl.uint<8>) -> !firrtl.uint<32>

    // Multiply starts at cycle 2 (after read completes)
    %mult_r = cmt2.call @mult_unit @multiply(%mem_data, %const) {
        arg_timing = [#cmt2.timing<[2, 3]>, #cmt2.timing<[2, 3]>],
        result_timing = [#cmt2.timing<[5, 6]>]
    } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
}
```

## See Also

- [TimingValidation.md](TimingValidation.md) - Validation rules and error messages
- [Cmt2ProcVsCalyx.md](Cmt2ProcVsCalyx.md) - Comparison with Calyx timing
- [CyclePreciseTimingImplementation.md](CyclePreciseTimingImplementation.md) - Implementation tracker
