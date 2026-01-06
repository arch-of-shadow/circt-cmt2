# CMT2 Procedural Layer vs Calyx: Feature Comparison and Roadmap

This document compares CMT2-proc with Calyx and defines the expected timing features for CMT2-proc.

## 1. Design Philosophy

| Aspect | CMT2 Proc | Calyx |
|--------|-----------|-------|
| Purpose | GAA with multi-cycle control | HLS intermediate language |
| Abstraction | Rule/method-centric | Group-centric |
| Timing model | **To be enhanced** | Cycle-precise with port annotations |
| Backend | FIRRTL → Verilog | Direct Verilog |

## 2. Calyx Timing System

### 2.1 Port Timing Attributes

Calyx annotates ports with timing information:

```futil
primitive std_mult_pipe[WIDTH](
    @clk clk: 1,
    @reset reset: 1,
    @write_together(1) @interval(3) @go go: 1,   // II = 3 cycles
    @write_together(1) @data left: WIDTH,        // Data port
    @write_together(1) @data right: WIDTH,       // Data port
) -> (
    @stable out: WIDTH,                          // Latched output
    @done done: 1                                // Completion signal
);
```

| Attribute | Meaning |
|-----------|---------|
| `@go(n)` | Go signal, component starts, optional latency n |
| `@done(n)` | Done signal, asserted at cycle n |
| `@interval(n)` | Initiation interval - cycles between invocations |
| `@stable` | Output is latched, not combinationally affected by inputs |
| `@data` | Pure data port (no control timing implications) |
| `@promotable(n)` | Can be promoted to static with latency n |

### 2.2 Static Groups with Cycle Guards

```futil
static<4> group multiply {
  mult.left = %[0:1] ? x;       // Cycle 0
  mult.right = %[0:1] ? y;      // Cycle 0
  mult.go = %[0:1] ? 1'd1;      // Cycle 0
  ans.in = %3 ? mult.out;       // Cycle 3
  ans.write_en = %3 ? 1'd1;     // Cycle 3
}
```

### 2.3 Timing Validation

Calyx validates timing at multiple stages:
1. **Parsing**: Check `@interval` or `static<n>` exists for static invokes
2. **Inference**: Compute group latencies from port timing
3. **Promotion**: Verify inferred matches annotated latencies
4. **Well-formed**: Validate `@interval` consistency across `@go` ports

## 3. Expected CMT2 Timing Features

### 3.1 Method Signature Timing Attributes

CMT2 should support timing annotations on method/value signatures:

```mlir
// External module with timing-annotated method
cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
  // Static method with port timing attributes
  cmt2.bind.method @multiply static<4> : (
    !firrtl.uint<1>  {go},                    // @go port
    !firrtl.uint<32> {data},                  // @data port
    !firrtl.uint<32> {data}                   // @data port
  ) -> (
    !firrtl.uint<1>  {done = 4},              // @done at cycle 4
    !firrtl.uint<32> {stable, latency = 4}    // @stable, available at cycle 4
  ) [enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]]
}

// CMT2 module with timing-annotated method
cmt2.proc.method @pipelined_add static<3> (
  %a: !firrtl.uint<32> {data, latency = 0},   // Input at cycle 0
  %b: !firrtl.uint<32> {data, latency = 0}    // Input at cycle 0
) -> (
  !firrtl.uint<32> {stable, latency = 3}      // Output at cycle 3
) { ... }
```

**Timing Attributes for CMT2:**

| Attribute | Meaning | On |
|-----------|---------|-----|
| `go` | Marks go/enable signal | Input port |
| `done = n` | Done signal, asserted at cycle n | Output port |
| `data` | Pure data port | Input/output |
| `stable` | Output is latched/registered | Output port |
| `latency = n` | Value available/required at cycle n | Input/output |
| `interval = n` | Initiation interval | Method |

### 3.2 Call-Site Timing Guards

Timing guards on `cmt2.call` arguments and results within static steps:

```mlir
cmt2.proc.static_step @pipeline <10> {
  // Call with timing attributes on arguments and results
  // Arguments specify when they are driven
  // Results specify when they are captured
  %r = cmt2.call @mult @multiply(
    %a {timing = [0, 1]},       // Drive 'a' during cycles 0
    %b {timing = [0, 1]}        // Drive 'b' during cycles 0
  ) -> (
    !firrtl.uint<32> {timing = [4, 5]}  // Capture result at cycle 4
  ) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

  // Second call starting at cycle 5
  %s = cmt2.call @mult @multiply(
    %c {timing = [5, 6]},
    %d {timing = [5, 6]}
  ) -> (
    !firrtl.uint<32> {timing = [9, 10]}
  ) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
}
```

**Timing Guard Semantics:**
- `{timing = [i, j]}` - Value is driven/captured during cycles i through j-1
- `{timing = n}` - Shorthand for `{timing = [n, n+1]}`
- Guards are relative to the enclosing static step's activation

### 3.3 Timing Validation Rules

**Signature Compatibility:**
```
For cmt2.call @inst @method(%arg) -> %result:
  - arg.timing must cover method's input latency requirement
  - result.timing must be >= method's output latency
```

**Example validation:**
```mlir
// Method signature: inputs at 0, output at cycle 4
cmt2.bind.method @multiply static<4> : (
  !firrtl.uint<32> {latency = 0},
  !firrtl.uint<32> {latency = 0}
) -> (!firrtl.uint<32> {latency = 4})

// Valid call: inputs at [0,1], result at [4,5]
%r = cmt2.call @mult @multiply(
  %a {timing = [0, 1]},
  %b {timing = [0, 1]}
) -> (!firrtl.uint<32> {timing = [4, 5]})

// Invalid call: result captured too early (cycle 3 < 4)
%r = cmt2.call @mult @multiply(
  %a {timing = [0, 1]},
  %b {timing = [0, 1]}
) -> (!firrtl.uint<32> {timing = [3, 4]})  // ERROR!
```

### 3.4 Static Step with Multiple Calls

```mlir
cmt2.proc.static_step @pipelined_compute <12> {
  // Pipelined: start new multiply every 3 cycles

  // First multiply: cycles 0-3
  %r1 = cmt2.call @mult @multiply(
    %a1 {timing = 0}, %b1 {timing = 0}
  ) -> (!firrtl.uint<32> {timing = 4})

  // Second multiply: cycles 3-6 (pipelined)
  %r2 = cmt2.call @mult @multiply(
    %a2 {timing = 3}, %b2 {timing = 3}
  ) -> (!firrtl.uint<32> {timing = 7})

  // Third multiply: cycles 6-9 (pipelined)
  %r3 = cmt2.call @mult @multiply(
    %a3 {timing = 6}, %b3 {timing = 6}
  ) -> (!firrtl.uint<32> {timing = 10})

  // Accumulate results: cycle 10-11
  %sum = cmt2.call @adder @add3(
    %r1 {timing = 10}, %r2 {timing = 10}, %r3 {timing = 10}
  ) -> (!firrtl.uint<32> {timing = 11})
}
```

## 4. Compilation Pipeline

### 4.1 Calyx Unified Pipeline

Calyx's key insight: **static and dynamic are two input formats that compile to the same hardware**. The pipeline processes static regions first, converting them to dynamic wrappers, then handles everything uniformly:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  Calyx Input: Mixed static<n> and dynamic control in same program           │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
        ┌───────────────────────────────────────────────────────────────────┐
        │                   PHASE 1: Static Compilation                     │
        │  StaticInference → StaticPromotion → StaticInliner               │
        │      → StaticFSMAllocation → CompileStatic                       │
        └───────────────────────────────────────────────────────────────────┘
                                    │
            CompileStatic converts static groups → dynamic wrapper groups
            (internal FSM + done signal, appears dynamic from outside)
                                    │
                                    ▼
        ┌───────────────────────────────────────────────────────────────────┐
        │                   PHASE 2: Unified Dynamic Compilation            │
        │  TopDownCompileControl (TDCC)                                     │
        │  Handles ALL groups uniformly - both original dynamic and        │
        │  wrapped static groups                                            │
        └───────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                               Verilog Output
```

**The Merge Point**: CompileStatic wraps static groups as dynamic groups with:
- Internal FSM register for timing
- Timing-guarded assignments
- Done signal (fires when FSM reaches final state)
- From TDCC's view: just another dynamic group

### 4.2 CMT2 Following Same Architecture

CMT2-proc follows the same two-phase architecture:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  CMT2 Proc Input: Mixed static_seq<n> and dynamic control                   │
│                                                                              │
│  control {                                                                   │
│    seq {                                                                     │
│      enable @init;                        // dynamic                         │
│      while cond {                         // dynamic                         │
│        static_seq<8> { ... }              // static island                   │
│      }                                                                       │
│      enable @finish;                      // dynamic                         │
│    }                                                                         │
│  }                                                                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
        ┌───────────────────────────────────────────────────────────────────┐
        │                   PHASE 1: Static Compilation                     │
        │  TimingInference → TimingValidation → StaticInliner              │
        │      → StaticFSMAllocation → CompileStatic                       │
        └───────────────────────────────────────────────────────────────────┘
                                    │
            CompileStatic converts static_seq<8> → wrapper step
            (internal timing FSM, done signal when FSM reaches end)
                                    │
                                    ▼
        ┌───────────────────────────────────────────────────────────────────┐
        │                   PHASE 2: Unified Dynamic Compilation            │
        │  CompileInvoke → TDCC → ProcStmtToAction → ProcToGAA             │
        │  TDCC sees ALL steps as dynamic (including wrapped statics)      │
        └───────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
        ┌───────────────────────────────────────────────────────────────────┐
        │                   GAA Rules + Methods                             │
        │  • Outer FSM: controls seq/par/while flow                        │
        │  • Inner FSMs: control timing within static wrappers             │
        └───────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                    lower-cmt2-to-firrtl → FIRRTL → Verilog
```

### 4.3 The Merge: Static → Dynamic Wrapper

Both Calyx and CMT2 use the same strategy - CompileStatic **transforms** static control:

```
BEFORE CompileStatic:                 AFTER CompileStatic:
┌─────────────────────────┐           ┌─────────────────────────────────────┐
│ static_seq<8> {         │           │ step @__wrapper {                   │
│   enable @multiply;     │    ──►    │   instance @__fsm = Reg<4>          │
│   enable @accumulate;   │           │   // timing-guarded enables         │
│ }                       │           │   // fsm tick rule                  │
└─────────────────────────┘           │ }                                   │
                                      │ done = (@__fsm.read() == 7)         │
                                      └─────────────────────────────────────┘
```

From TDCC's perspective, this wrapper is just another dynamic step with go/done.

### 4.4 Pass Comparison

| Phase | Calyx Pass | CMT2 Pass | Purpose |
|-------|------------|-----------|---------|
| **Static** | StaticInference | TimingInference | Infer latencies |
| | StaticPromotion | (optional) | Promote dynamic→static |
| | StaticInliner | StaticInliner | Flatten nested static |
| | StaticFSMAllocation | StaticFSMAllocation | Allocate FSM states |
| | **CompileStatic** | **CompileStatic** | **static → dynamic wrapper** |
| **Dynamic** | — | CompileInvoke | invoke → step + enable |
| | TopDownCompileControl | TDCC | Unified FSM scheduling |
| | — | ProcStmtToAction | Generate GAA rules |
| | — | ProcToGAA | Cleanup proc wrapper |

### 4.5 Current Implementation Status

| Component | Calyx | CMT2 | Notes |
|-----------|-------|------|-------|
| Static timing passes | ✅ | ✅ | Both have inference, validation |
| StaticInliner | ✅ | ⚠️ Partial | CMT2 basic flattening |
| **CompileStatic** | ✅ **Creates wrapper** | ❌ **Only adds metadata** | **Gap to fix** |
| TDCC | ✅ | ✅ | Both work for dynamic |
| FSM generation | In CompileStatic | In ProcStmtToAction | Different locations |

**The Gap**: CMT2's CompileStatic currently generates FSM metadata as attributes but doesn't create the dynamic wrapper step. It should follow Calyx's approach and transform `static_seq` into a wrapper step with internal FSM.

### 4.6 Completing CMT2's CompileStatic

To match Calyx's architecture, CompileStatic should:

1. **Create wrapper step** containing the static region's operations
2. **Create internal FSM register** for timing
3. **Add timing-guarded enables** for each sub-operation
4. **Create done signal** (FSM reaches final state)
5. **Replace static_seq with enable of wrapper** in control tree

After this, TDCC handles everything uniformly - it doesn't need to know which steps were originally static.

## 5. Feature Comparison Matrix

| Feature | CMT2 Current | CMT2 Expected | Calyx |
|---------|--------------|---------------|-------|
| Method latency annotation | `static<n>` | `static<n>` | `static<n>` |
| Port timing attributes | ❌ | `{go}`, `{done=n}`, `{stable}`, `{data}`, `{latency=n}` | `@go`, `@done`, `@stable`, `@data`, `@interval` |
| Call-site timing guards | ❌ | `{timing = [i,j]}` | Inside groups: `%[i:j]` |
| Timing validation | ❌ | Compile-time check | Multi-stage validation |
| Initiation interval | ❌ | `{interval = n}` on method | `@interval(n)` on `@go` port |
| Latency inference | ✅ Basic | Enhanced with port timing | Full inference from graph |
| FSM sharing | ❌ | Planned | Graph coloring |
| One-hot encoding | ❌ | Planned | Configurable |

## 6. Example: Complete Static Module

```mlir
cmt2.module @PipelinedProcessor(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
  cmt2.instance @mult = @multiplier (%clk, %rst) : ...
  cmt2.instance @acc = @accumulator (%clk, %rst) : ...

  // Static step for multiply-accumulate with precise timing
  cmt2.proc.static_step @mac <8> {
    // Multiply: inputs at 0, result at 4
    %prod = cmt2.call @mult @multiply(
      %a {timing = 0},
      %b {timing = 0}
    ) -> (!firrtl.uint<32> {timing = 4})

    // Accumulate: input at 4, result at 7
    %sum = cmt2.call @acc @add(
      %prod {timing = 4}
    ) -> (!firrtl.uint<32> {timing = 7})
  }

  // Rule using the static step
  cmt2.proc.rule @process() -> () {
    %ready = cmt2.call @input @valid() : () -> !firrtl.uint<1>
    cmt2.return %ready : !firrtl.uint<1>
  } control {
    cmt2.proc.static_repeat 4 {
      cmt2.proc.enable @mac
    }
  }
}
```

## 7. Summary

CMT2-proc will adopt Calyx's timing system with adaptations:

1. **Signature-level timing**: Port attributes like `{go}`, `{stable}`, `{latency=n}` on method signatures
2. **Call-site timing guards**: `{timing = [i,j]}` attributes on call arguments and results
3. **Compile-time validation**: Verify call timing matches method contracts
4. **Enhanced passes**: TimingInference, TimingValidation, improved StaticPromotion

This combines CMT2's GAA semantics with Calyx's cycle-precise timing control.
