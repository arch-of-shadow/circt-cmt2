# CMT2 Procedural Layer Design

Internal design documentation for the CMT2 procedural control layer.

---

## Overview

The procedural layer extends CMT2's GAA semantics with multi-cycle operations. It provides:

- **Steps**: Units of execution (dynamic or static)
- **Control flow**: seq, par, if, while, static_repeat, static_if
- **FSM compilation**: Automatic state machine generation
- **Timing**: Cycle-precise scheduling for static control

---

## Architecture

```
Procedural Layer (proc.*)
         │
         ▼
┌─────────────────────────────────────┐
│  Static Compilation                 │
│  (timing inference, FSM allocation) │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│  TDCC (Top-Down Compile Control)    │
│  (FSM generation for dynamic ctrl)  │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│  ProcStmtToAction                   │
│  (Convert to GAA action rules)      │
└─────────────────────────────────────┘
         │
         ▼
    GAA Layer (rule, method, value)
```

---

## Operations

### Steps

**Dynamic Step** (`proc.step`):
```mlir
cmt2.proc.step @wait_data {
    %ready = cmt2.call @fifo @has_data() : () -> !firrtl.uint<1>
    cmt2.proc.step_done %ready : !firrtl.uint<1>
    // Actions when enabled...
}
```
- Completion determined at runtime via `step_done`
- Used for variable-latency operations

**Static Step** (`proc.static_step`):
```mlir
cmt2.proc.static_step @compute<4> {
    // Fixed 4-cycle latency
    %r = cmt2.call @mult @multiply(%a, %b) : ...
} {interval = #cmt2.interval<2>}  // Optional pipelining
```
- Compile-time known latency
- Optional initiation interval for pipelining

### Control Flow

**Sequential** (`proc.seq`):
```mlir
cmt2.proc.seq {
    cmt2.proc.enable @step_a
    cmt2.proc.enable @step_b  // After step_a completes
}
```

**Parallel** (`proc.par`):
```mlir
cmt2.proc.par {
    cmt2.proc.enable @step_a  // Concurrent
    cmt2.proc.enable @step_b  // Concurrent
}
// Both must complete before continuing
```

**Conditional** (`proc.if`):
```mlir
cmt2.proc.if %cond {
    cmt2.proc.enable @then_step
} else {
    cmt2.proc.enable @else_step
}
```

**Loop** (`proc.while`):
```mlir
cmt2.proc.while %cond {
    cmt2.proc.enable @body_step
}
```

**Static Repeat** (`proc.static_repeat`):
```mlir
cmt2.proc.static_repeat 4<16> {
    // 4 iterations, 4 cycles each = 16 total
    cmt2.proc.enable @loop_step
}
```

**Static If** (`proc.static_if`):
```mlir
cmt2.proc.static_if %cond<4, 4> {
    cmt2.proc.enable @then_step  // 4 cycles
} else {
    cmt2.proc.enable @else_step  // 4 cycles
}
```

### Procedural Functions

**Procedural Rule** (`proc.rule`):
```mlir
cmt2.proc.rule @compute () -> () {
    %guard = ...
    cmt2.return %guard : !firrtl.uint<1>
} control {
    cmt2.proc.seq {
        cmt2.proc.enable @load
        cmt2.proc.enable @process
        cmt2.proc.enable @store
    }
    cmt2.proc.control_end
}
```

**Procedural Method** (`proc.method`):
```mlir
cmt2.proc.method @transform (%in: !firrtl.uint<32>) -> (!firrtl.uint<32>)
    attributes {static_latency = 8 : i64} {
    %ready = ...
    cmt2.return %ready : !firrtl.uint<1>
} control {
    cmt2.proc.seq { ... }
    cmt2.proc.control_end
}
```

---

## Compilation Pipeline

### Phase 1: Static Optimization

```
Input: proc.static_step, proc.static_repeat, proc.static_if
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-static-inference               │
│  • Compute latencies bottom-up       │
│  • seq: sum of children              │
│  • par: max of children              │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-static-promotion               │
│  • Promote dynamic→static if known   │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-timing-inference               │
│  • Infer arg/result timing           │
│  • Propagate through call graph      │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-timing-validation              │
│  • Verify timing constraints         │
│  • Check pipeline spacing            │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-static-fsm-allocation          │
│  • Map cycles to FSM states          │
│  • Choose encoding (binary/one-hot)  │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-compile-static                 │
│  • Generate FSM wrapper for static   │
│  • Add tick rule, done value         │
│  • Apply early-reset optimization    │
└──────────────────────────────────────┘
```

### Phase 2: Dynamic Compilation (TDCC)

```
Input: Mixed static wrappers and dynamic control
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-compile-invoke                 │
│  • Convert proc.invoke to step+enable│
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-tdcc (Top-Down Compile Control)│
│  • Assign NODE_ID to each enable     │
│  • Build execution schedule          │
│  • Compute state transitions         │
│  • Generate FSM for control tree     │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-proc-stmt-to-action            │
│  • Convert proc statements to rules  │
│  • Generate FSM tick/done rules      │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│  cmt2-proc-to-gaa                    │
│  • Final cleanup                     │
│  • Convert proc.rule → rule          │
│  • Convert proc.method → method      │
└──────────────────────────────────────┘
```

---

## TDCC Algorithm

Top-Down Compile Control assigns FSM states to control nodes.

### Node ID Assignment

```
control {
  seq {                 // NODE_ID = 0
    enable @A;          // NODE_ID = 1
    par {               // NODE_ID = 2
      enable @B;        // NODE_ID = 3
      enable @C;        // NODE_ID = 4
    }
    enable @D;          // NODE_ID = 5
  }
}
```

### State Transitions

For each control construct:

**seq**:
```
state[0] → A_done → state[1]
state[1] → B_done && C_done → state[2]
state[2] → D_done → state[0] (done)
```

**par**:
```
All children enabled simultaneously
Wait for all done signals
```

**if**:
```
cond=1 → then branch
cond=0 → else branch
```

**while**:
```
cond=1 → body, return to check
cond=0 → exit
```

### Generated FSM

```mlir
// FSM register
cmt2.instance @__fsm = @Reg<log2(num_states)>(...)

// Tick rule: advance FSM
cmt2.rule @__fsm_tick () -> () {
    // Guard: any transition condition true
    ...
} {
    // Body: update FSM state based on current state and done signals
    ...
}

// Step enable rules: enable step when in correct state
cmt2.rule @A__enable () -> () {
    // Guard: FSM state == A's state
    ...
} {
    // Enable step A's actions
    ...
}
```

---

## Static Control Compilation

### CompileStatic Transformation

Static steps are wrapped with FSM logic:

**Before:**
```mlir
cmt2.proc.static_step @compute<4> {
    %r = cmt2.call @mult @multiply(%a, %b) : ...
}
```

**After:**
```mlir
// FSM register
cmt2.instance @__fsm_compute = @Reg1(...)

// Tick rule
cmt2.rule @compute__tick () -> () {
    %fsm = cmt2.call @__fsm_compute @read() : ...
    %max = firrtl.constant 3 : ...
    %lt = firrtl.lt %fsm, %max : ...
    cmt2.return %lt : !firrtl.uint<1>
} {
    %fsm = cmt2.call @__fsm_compute @read() : ...
    %one = firrtl.constant 1 : ...
    %next = firrtl.add %fsm, %one : ...
    cmt2.call @__fsm_compute @write(%next) : ...
    cmt2.return
}

// Done value
cmt2.value @compute__done () -> (!firrtl.uint<1>) {
    cmt2.return
} {
    %fsm = cmt2.call @__fsm_compute @read() : ...
    %max = firrtl.constant 3 : ...
    %done = firrtl.geq %fsm, %max : ...
    cmt2.return %done : !firrtl.uint<1>
}

// Start rule
cmt2.rule @compute__start () -> () {
    // Enable when go signal and FSM idle
    ...
} {
    // Reset FSM, start operation
    ...
}

// Original step with wrapper_generated marker
cmt2.proc.static_step @compute<4> { ... } {wrapper_generated}
```

### Early-Reset Optimization

For efficiency, the FSM can reset one cycle early:

```
Without early-reset: state = 0,1,2,3 (done at 3)
With early-reset:    state = 0,1,2,0 (done when state goes 0)
```

This saves one cycle in back-to-back operations.

---

## FSM Encoding

### Binary Encoding

For >8 states:
```
states = 16 → bits = 4
state[3:0] encodes 0-15
```

### One-Hot Encoding

For ≤8 states:
```
states = 4 → bits = 4
state = 0001, 0010, 0100, 1000
```

One-hot benefits:
- Simpler decode logic
- Better timing
- Lower power for small FSMs

---

## Timing Validation

### Constraint Checking

**Call timing within bounds:**
```mlir
cmt2.proc.static_step @compute<4> {
    cmt2.call @mult @op(%a) {arg_timing = [#cmt2.timing<[5, 6]>]} : ...
    // ERROR: cycle 5 exceeds step latency 4
}
```

**Pipelined call spacing:**
```mlir
cmt2.proc.static_step @pipe<10> {
    cmt2.call @mult @op(%a) {arg_timing = [#cmt2.timing<[0, 1]>]} : ...
    cmt2.call @mult @op(%b) {arg_timing = [#cmt2.timing<[1, 2]>]} : ...
    // ERROR: interval=3 requires 3 cycles between calls
}
```

**Result availability:**
```mlir
cmt2.call @mult @op(%a) {
    arg_timing = [#cmt2.timing<[0, 1]>],
    result_timing = [#cmt2.timing<[2, 3]>]
} : ...
// ERROR: method has output latency 4, cannot capture at cycle 2
```

---

## Implementation Files

| File | Purpose | Lines |
|------|---------|-------|
| `TDCC.cpp` | Top-Down Compile Control | 1,300 |
| `ProcStmtToAction.cpp` | Statement to action rules | 1,000 |
| `CompileStatic.cpp` | Static FSM wrapper | 900 |
| `StaticInference.cpp` | Latency inference | 400 |
| `StaticFSMAllocation.cpp` | FSM state allocation | 450 |
| `TimingInference.cpp` | Timing inference | 280 |
| `TimingValidation.cpp` | Timing validation | 290 |
| `CompileInvoke.cpp` | Invoke lowering | 160 |
| `ProcToGAA.cpp` | Final GAA conversion | 150 |

---

## See Also

- [MultiCycle.md](MultiCycle.md) - User guide for multi-cycle operations
- [Passes.md](Passes.md) - Complete pass reference
- [Cmt2ProcVsCalyx.md](Cmt2ProcVsCalyx.md) - Comparison with Calyx
