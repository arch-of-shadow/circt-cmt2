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

The while loop has a condition region that supports `cmt2.call` for reading
dynamic values:

```mlir
cmt2.proc.while {
    // Condition region - can use cmt2.call
    %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
    %c0 = firrtl.constant 0 : !firrtl.uint<32>
    %cond = firrtl.neq %val, %c0 : ...
    cmt2.proc.while_cond %cond : !firrtl.uint<1>
} do {
    cmt2.proc.enable @body_step
    cmt2.proc.yield
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

### Parallel Control: Per-Branch FSM Design

For true parallel execution, each branch of a `par` construct requires its own FSM register. This section describes the complete design.

#### Problem Statement

A single FSM register can only be in one state at a time. For parallel branches with multi-step control flow:

```mlir
cmt2.proc.par {
  cmt2.proc.seq {       // Branch 0: needs states 1,2,3
    cmt2.proc.enable @A
    cmt2.proc.enable @B
  }
  cmt2.proc.seq {       // Branch 1: needs states 4,5
    cmt2.proc.enable @C
  }
}
```

Both branches must progress **simultaneously**, but a single FSM cannot be in state 1 AND state 4 at the same time.

#### Solution: Hierarchical FSM Structure

Each `par` block generates:
1. **Main FSM states**: fork, join (managed by parent FSM)
2. **Branch FSM registers**: One per branch, independent progression
3. **Done signals**: Per-branch completion indicators

```
Main FSM: ... → fork_state → join_state → ...
                    │              ▲
                    │              │ (all branch_done signals)
                    ▼              │
         ┌─────────────────────────┴───────────────────┐
         │                                             │
    Branch 0 FSM                              Branch 1 FSM
    ┌─────────────┐                          ┌─────────────┐
    │ 0: idle     │                          │ 0: idle     │
    │ 1: enable A │                          │ 1: enable C │
    │ 2: enable B │                          │ 0: done     │
    │ 0: done     │                          └─────────────┘
    └─────────────┘
```

#### State Machine Semantics

**Fork Phase** (Main FSM enters fork_state):
```
// When main FSM transitions to fork_state:
branch_0_fsm <= 1;   // Initialize branch 0 to first state
branch_1_fsm <= 1;   // Initialize branch 1 to first state
```

**Execution Phase** (Main FSM stays in fork_state):
```
// Each branch FSM advances independently based on its control flow:

// Branch 0 FSM transitions:
if (branch_0_fsm == 1 && step_A_done) branch_0_fsm <= 2;
if (branch_0_fsm == 2 && step_B_done) branch_0_fsm <= 0;  // done

// Branch 1 FSM transitions:
if (branch_1_fsm == 1 && step_C_done) branch_1_fsm <= 0;  // done
```

**Join Phase** (Main FSM transitions from fork_state to join_state):
```
// Transition condition: all branches complete
join_guard = (branch_0_fsm == 0) && (branch_1_fsm == 0);
if (main_fsm == fork_state && join_guard) main_fsm <= join_state;
```

#### Branch FSM State Encoding

Each branch FSM uses:
- **State 0**: Idle/Done state (branch not active or completed)
- **States 1..N**: Active states for branch control flow

The branch is:
- **Idle**: `branch_fsm == 0` before fork
- **Active**: `branch_fsm != 0` during execution
- **Done**: `branch_fsm == 0` after completion

#### Nested Parallelism

For nested `par` inside a branch:
```mlir
cmt2.proc.par {            // Outer par
  cmt2.proc.seq {          // Branch 0
    cmt2.proc.enable @A
    cmt2.proc.par {        // Inner par (nested)
      cmt2.proc.enable @B
      cmt2.proc.enable @C
    }
  }
  cmt2.proc.enable @D      // Branch 1
}
```

Each level of nesting generates its own branch FSM registers:
```
Main FSM
├── outer_branch_0_fsm
│   └── inner_branch_0_fsm, inner_branch_1_fsm
└── outer_branch_1_fsm
```

The naming convention: `__par_{par_id}_branch_{branch_idx}_fsm`

#### Generated Hardware

For `par { seq { enable @A; enable @B }; enable @C }`:

```mlir
// Branch FSM registers
cmt2.instance @__par_0_branch_0_fsm = @Reg<2>(...) // 2-bit for states 0,1,2
cmt2.instance @__par_0_branch_1_fsm = @Reg<1>(...) // 1-bit for states 0,1

// Branch 0 tick rule
cmt2.rule @__par_0_branch_0_tick () -> () {
  %fsm = cmt2.call @__par_0_branch_0_fsm @read() : ...
  %active = firrtl.neq %fsm, %c0 : ...
  cmt2.return %active : !firrtl.uint<1>
} {
  %fsm = cmt2.call @__par_0_branch_0_fsm @read() : ...
  // State 1 -> 2 when A done
  // State 2 -> 0 when B done
  ...
}

// Branch 0 enable rules
cmt2.rule @__par_0_branch_0_enable_A () -> () {
  %fsm = cmt2.call @__par_0_branch_0_fsm @read() : ...
  %in_state_1 = firrtl.eq %fsm, %c1 : ...
  cmt2.return %in_state_1 : !firrtl.uint<1>
} {
  // Execute step A actions
  ...
}

// Branch done value (for join)
cmt2.value @__par_0_branch_0_done () -> (!firrtl.uint<1>) {
  cmt2.return
} {
  %fsm = cmt2.call @__par_0_branch_0_fsm @read() : ...
  %done = firrtl.eq %fsm, %c0 : ...
  cmt2.return %done : !firrtl.uint<1>
}

// Main FSM join transition uses branch done signals
// Transition fork -> join when all branches done
```

#### Initialization and Reset

**On module reset:**
- All branch FSM registers reset to 0 (idle)
- Main FSM resets to 0

**On fork (main FSM enters fork_state):**
- Branch FSM registers are set to their start state (1)
- This is done by the fork rule, not the tick rule

```mlir
cmt2.rule @__par_0_fork () -> () {
  %main = cmt2.call @__fsm @read() : ...
  %in_fork = firrtl.eq %main, %fork_state : ...
  %b0 = cmt2.call @__par_0_branch_0_fsm @read() : ...
  %b0_idle = firrtl.eq %b0, %c0 : ...
  %should_init = firrtl.and %in_fork, %b0_idle : ...
  cmt2.return %should_init : !firrtl.uint<1>
} {
  cmt2.call @__par_0_branch_0_fsm @write(%c1) : ...
  cmt2.call @__par_0_branch_1_fsm @write(%c1) : ...
}
```

#### Simple Par Optimization

When all branches are single enables (no nested control flow), an optimization avoids branch FSM registers:

```mlir
// Simple par: all enables share the same state
cmt2.proc.par {
  cmt2.proc.enable @A
  cmt2.proc.enable @B
}
```

Generated as:
- Single state where both @A and @B are enabled simultaneously
- Exit when all done signals are true (for dynamic steps) or after max latency (for static steps)
- No branch FSM registers needed

This optimization is applied when:
1. All children of `par` are `proc.enable` operations (no seq, par, if, while)
2. No nested control structures

#### TDCC Pass Implementation

The TDCC pass handles `par` in three phases:

**Phase 1: State Allocation** (`computeUniqueIdsForOp`)
```cpp
.Case<ProcParOp>([&](ProcParOp par) {
  if (isSimplePar(par)) {
    // All enables get same state
    return allocateSimplePar(par, curState);
  } else {
    // Complex par: reserve fork/join states for main FSM
    // Branch states are handled by branch FSMs (0-indexed internally)
    uint64_t forkState = curState;
    uint64_t joinState = curState + 1;
    stateIds[par] = forkState;
    // Record branch info for later
    parBranchInfo[par] = analyzeBranches(par);
    return joinState + 1;
  }
})
```

**Phase 2: Schedule Building** (`calculateStatesRecur`)
```cpp
.Case<ProcParOp>([&](ProcParOp par) {
  if (isSimplePar(par)) {
    return buildSimpleParSchedule(par, preds);
  } else {
    // Add fork rule: initialize branch FSMs
    // Add branch tick/enable rules (reference branch FSMs)
    // Add join transition: wait for all branch FSMs == 0
    return buildComplexParSchedule(par, preds);
  }
})
```

**Phase 3: Hardware Generation** (`realizeSchedule`)
```cpp
// For each complex par:
for (auto &parInfo : schedule.parBlocks) {
  if (parInfo.needsPerBranchFsm) {
    // Generate branch FSM registers
    for (auto &branch : parInfo.branches) {
      generateBranchFsmRegister(branch);
      generateBranchTickRule(branch);
      generateBranchEnableRules(branch);
      generateBranchDoneValue(branch);
    }
    // Generate fork initialization rule
    generateForkRule(parInfo);
  }
}
```

#### Example: Complete Par Compilation

**Input:**
```mlir
cmt2.proc.rule @example () -> () {
  ...
} control {
  cmt2.proc.seq {
    cmt2.proc.enable @init
    cmt2.proc.par {
      cmt2.proc.seq {
        cmt2.proc.enable @A
        cmt2.proc.enable @B
      }
      cmt2.proc.enable @C
    }
    cmt2.proc.enable @finish
  }
}
```

**Main FSM States:**
```
State 0: Idle
State 1: Enable @init
State 2: Par fork (initialize branch FSMs)
State 3: Par join (wait for branches)
State 4: Enable @finish
State 0: Done (back to idle)
```

**Branch 0 FSM States:**
```
State 0: Idle/Done
State 1: Enable @A
State 2: Enable @B
```

**Branch 1 FSM States:**
```
State 0: Idle/Done
State 1: Enable @C
```

**Generated Transitions:**
```
// Main FSM
State 1 -> 2: init_done
State 2 -> 2: !(branch_0_done && branch_1_done)  // Stay in fork
State 2 -> 3: branch_0_done && branch_1_done     // Go to join
State 3 -> 4: unconditional
State 4 -> 0: finish_done

// Branch 0 FSM
State 0 -> 1: main_fsm == 2 && branch_0_fsm == 0  // Fork init
State 1 -> 2: A_done
State 2 -> 0: B_done

// Branch 1 FSM
State 0 -> 1: main_fsm == 2 && branch_1_fsm == 0  // Fork init
State 1 -> 0: C_done
```

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

## Precedence Handling During Proc Lowering

When procedural rules are lowered to GAA rules, multiple FSM state rules are generated that need proper precedence handling.

**For comprehensive documentation, see:** [tmp/PrecedenceHandling.md](tmp/PrecedenceHandling.md)

The document covers:
- Conflict categories (intra-FSM, step body, cross-module)
- Current implementation analysis
- Concrete example using `proc_pipeline.py`
- Proposed three-phase architecture for systematic handling

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
