# CMT2 Timing Validation Rules

This document describes the timing validation rules enforced by the CMT2 dialect for cycle-precise static scheduling.

## Overview

Timing validation occurs at two levels:

1. **Verifiers** (compile-time): Basic structural checks in op verifiers (`CallOp::verify`, `BindMethodOp::verify`)
2. **Passes** (analysis-time): Semantic checks in `cmt2-timing-validation` pass

## Validation Passes

Run the full timing validation pipeline:

```bash
circt-opt input.mlir \
    -cmt2-timing-inference \
    -cmt2-timing-validation
```

### Pass Options

```bash
# Strict mode: require explicit timing on all calls
circt-opt input.mlir -cmt2-timing-validation="strict=true"
```

## Verifier-Level Checks

These checks run automatically when parsing or building IR.

### CallOp Verifier

| Check | Condition | Error Message |
|-------|-----------|---------------|
| Array size match | `arg_timing.size() == inputs.size()` | `arg_timing array size (N) must match number of inputs (M)` |
| Array size match | `result_timing.size() == outputs.size()` | `result_timing array size (N) must match number of outputs (M)` |
| Non-negative start | `timing.start >= 0` | `arg_timing[i] start (-N) must be non-negative` |
| Non-negative start | `timing.start >= 0` | `result_timing[i] start (-N) must be non-negative` |
| Within step bounds | `timing.end <= step.latency` | `arg_timing[i] end (N) exceeds step latency (M)` |
| Within step bounds | `timing.end <= step.latency` | `result_timing[i] end (N) exceeds step latency (M)` |

**Example Error:**
```mlir
cmt2.proc.static_step @compute<4> {
    // ERROR: result_timing[0] end (6) exceeds step latency (4)
    %result = cmt2.call @mult @multiply(%a, %b) {
        arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
        result_timing = [#cmt2.timing<[4, 6]>]  // End 6 > step latency 4
    } : ...
}
```

### BindMethodOp Verifier

| Check | Condition | Error Message |
|-------|-----------|---------------|
| Positive latency | `static_latency > 0` | `static_latency must be positive` |
| Valid interval | `interval <= static_latency` | `interval (N) exceeds static_latency (M)` |

**Example Error:**
```mlir
// ERROR: static_latency must be positive
cmt2.bind.method @multiply static<0> : ...

// ERROR: interval (5) exceeds static_latency (4)
cmt2.bind.method @multiply static<4> : ... {interval = #cmt2.interval<5>}
```

### ProcStaticStepOp Verifier

| Check | Condition | Error Message |
|-------|-----------|---------------|
| Positive latency | `latency > 0` | `latency must be positive` |
| Valid interval | `interval <= latency` | `interval exceeds step latency` |

## Pass-Level Checks

These checks require cross-operation analysis and run in the `cmt2-timing-validation` pass.

### Result Timing vs Method Latency

Results cannot be captured before the method completes.

| Check | Condition | Error Message |
|-------|-----------|---------------|
| Result ready | `result_timing.start >= method.static_latency` | `result N timing [S, E) starts before method completes at cycle L` |

**Example Error:**
```mlir
// Method has 4-cycle latency
cmt2.bind.method @multiply static<4> : ...

cmt2.proc.static_step @compute<6> {
    // ERROR: result 0 timing [2, 3) starts before method completes at cycle 4
    %result = cmt2.call @mult @multiply(%a, %b) {
        arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
        result_timing = [#cmt2.timing<[2, 3]>]  // Too early!
    } : ...
}
```

**Fix:** Set result timing to start at or after method latency:
```mlir
result_timing = [#cmt2.timing<[4, 5]>]  // Wait for method to complete
```

### Pipelined Call Spacing (Initiation Interval)

Multiple calls to the same pipelined method must respect the initiation interval.

| Check | Condition | Error Message |
|-------|-----------|---------------|
| II respected | `call_spacing >= method.interval` | `pipelined call spacing (N cycles) is less than initiation interval (M cycles)` |

**Example Error:**
```mlir
// Method has II=3
cmt2.bind.method @multiply static<4> : ... {interval = #cmt2.interval<3>}

cmt2.proc.static_step @pipeline<10> {
    // First call at cycle 0 - OK
    %r1 = cmt2.call @mult @multiply(%a, %b) {
        arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
        result_timing = [#cmt2.timing<[4, 5]>]
    } : ...

    // ERROR: pipelined call spacing (2 cycles) is less than initiation interval (3 cycles)
    // note: previous call to same method
    %r2 = cmt2.call @mult @multiply(%c, %d) {
        arg_timing = [#cmt2.timing<[2, 3]>, #cmt2.timing<[2, 3]>],  // Cycle 2, gap = 2 < 3
        result_timing = [#cmt2.timing<[6, 7]>]
    } : ...
}
```

**Fix:** Space calls by at least II cycles:
```mlir
// Second call at cycle 3 (gap = 3 = II)
%r2 = cmt2.call @mult @multiply(%c, %d) {
    arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>],
    result_timing = [#cmt2.timing<[7, 8]>]
} : ...
```

### Strict Mode Validation

When `strict=true`, all calls in static steps must have explicit timing annotations.

| Check | Condition | Error Message |
|-------|-----------|---------------|
| Has timing | `call.arg_timing or call.result_timing` | `call in static step must have explicit timing annotations (strict mode enabled)` |

**Example Error (with strict=true):**
```mlir
cmt2.proc.static_step @compute<4> {
    // ERROR: call in static step must have explicit timing annotations
    %result = cmt2.call @mult @multiply(%a, %b) : ...  // No timing attrs!
}
```

### Timing Guards Outside Static Steps

Timing attributes are only valid within static steps.

| Check | Condition | Error Message |
|-------|-----------|---------------|
| In static step | `parentOf(call) is ProcStaticStepOp` | `timing guards are only valid inside static steps` |

**Example Error:**
```mlir
cmt2.module @Example(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    // Not in a static step!
    cmt2.proc.rule @run() -> () {
        %c1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1 : !firrtl.uint<1>
    } body {
        // ERROR: timing guards are only valid inside static steps
        %result = cmt2.call @mult @multiply(%a, %b) {
            arg_timing = [#cmt2.timing<[0, 1]>]
        } : ...
    }
}
```

## Validation Order

The recommended pass order ensures errors are caught early:

1. **TimingInference** - Infer timing from method contracts (fills in missing timing)
2. **TimingValidation** - Validate all timing constraints

Running validation before inference may produce false positives for calls without explicit timing.

## Common Errors and Fixes

### Error: Result timing before method latency

**Cause:** Trying to read result before the operation completes.

**Fix:** Calculate when result is available: `arg_timing.start + method.static_latency`

```mlir
// If method has static<4> and args are at cycle 0:
// Result is ready at cycle 0 + 4 = 4
result_timing = [#cmt2.timing<[4, 5]>]
```

### Error: Timing exceeds step latency

**Cause:** Static step too short for the scheduled operations.

**Fix:** Increase step latency to accommodate all operations:

```mlir
// If result is at cycle [7, 8), step needs latency >= 8
cmt2.proc.static_step @compute<8> { ... }
```

### Error: Pipelined call spacing violation

**Cause:** Multiple calls to same method too close together.

**Fix:** Space calls by at least the initiation interval:

```mlir
// For II=3: calls at cycles 0, 3, 6, 9, ...
```

### Error: Array size mismatch

**Cause:** Number of timing entries doesn't match operand/result count.

**Fix:** Provide one timing entry per operand/result:

```mlir
// 2 arguments need 2 arg_timing entries
%r = cmt2.call @alu @add(%a, %b) {
    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],  // 2 entries
    result_timing = [#cmt2.timing<[0, 1]>]  // 1 result = 1 entry
} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
```

## Diagnostic Helpers

The TimingCompatibility class provides diagnostic utilities:

```cpp
// Emit a timing error with standard formatting
InFlightDiagnostic emitTimingError(Operation *op, StringRef message);

// Format timing interval for error messages
std::string formatTimingInterval(TimingIntervalAttr timing);
// Returns: "[start, end)"
```

## See Also

- [TimingSyntax.md](TimingSyntax.md) - Timing attribute syntax reference
- [CyclePreciseTimingImplementation.md](CyclePreciseTimingImplementation.md) - Implementation details
- [test/Dialect/Cmt2/timing-validation-errors.mlir](../../../test/Dialect/Cmt2/timing-validation-errors.mlir) - Validation error test cases
