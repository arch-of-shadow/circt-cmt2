# Multi-Cycle Operations

CMT2 supports **multi-cycle operations** through procedural control constructs. This enables complex algorithms spanning multiple clock cycles with both dynamic and static timing.

**Related Examples:**
- Proc control: `examples/PyCMT2/proc.py`, `examples/PyCMT2/proc_testbench.py`, `examples/PyCMT2/timing.py`
- Call-site timing (call_timing/arg_timing/result_timing inside static_step): `examples/PyCMT2/static_step_call_timing_window.py`
- Dataflow/Pipeline: `examples/PyCMT2/comprehensive_dataflow_example.py`, `examples/PyCMT2/dataflow_forkjoin.py`
- Comprehensive: `examples/PyCMT2/comprehensive_example.py`

---

## Overview

CMT2 provides two complementary paradigms for multi-cycle operations:

### Procedural Control (proc rules)

FSM-based control flow for sequential, parallel, and looping operations:

| Mode | Latency | Done Signal | Use Case |
|------|---------|-------------|----------|
| **Dynamic** | Runtime | Implicit (advance-on-fire) | Variable readiness / backpressure |
| **Static** | Compile-time | Implicit | Fixed-latency pipelines |

### Dataflow/Pipeline (proc.dataflow)

Token-based synchronization for producer-consumer pipelines:

| Mode | Hardware | Use Case |
|------|----------|----------|
| **Latency-Sensitive (LS)** | Shift registers | Fixed-timing pipelines |
| **Latency-Insensitive (LI)** | FIFOs | Variable-timing, decoupled stages |

Both paradigms can be combined: dataflow tasks can contain proc control internally.

---

## Timing Contracts (Static Latency + Interval)

Static steps and static methods can carry **cycle-precise timing contracts**:

- `static_latency`: how many cycles from *start* to *completion*
- `interval`: how often a new iteration can start (initiation interval)

ASCII timeline example (`static_latency=5`, `interval=2`):

```
cycle:   0 1 2 3 4 5 6 7 8 9
start:   S   S   S   S   S
done:            D   D   D
         ^^^^^ latency=5 ^^^^^
start cadence: every 2 cycles (interval=2)
```

Where this shows up:

- User-facing attributes and contracts: `docs/Cmt2/reference/Attributes.md`
- Implementation pointers: `docs/Cmt2/features/Lowering.md`

Proc control overview: `docs/Cmt2/features/Proc.md`.

---

## Steps

### Dynamic Steps

Dynamic steps have **runtime-determined readiness**: a step may stall for an
arbitrary number of cycles until its internal calls are ready; once the step
fires, it completes in that cycle and the proc FSM advances unconditionally.

```python
with m.step("dequeue_when_ready") as step:
    # If `dequeue` is not ready, the step simply doesn't fire and the FSM stays
    # in this state.
    data = step.call(fifo, "dequeue")
    step.call(result_reg, "write", data)
```

### Static Steps

Static steps have **compile-time known latency**:

```python
with m.static_step(4, "multiply") as step:
    # 4-cycle fixed latency
    a = step.call(reg_a, "read")
    b = step.call(reg_b, "read")
    product = step.mul(a, b)
    step.call(reg_result, "write", product)
```

**How multi-cycle execution works:** a `static_step(L)` lowers to an FSM segment with **L cycles**. Operations in the step body are **scheduled to specific cycles** within that segment.

- By default, a `step.call(...)` is enabled only for the **first cycle** of the step (`[0, 1)`), and the remaining cycles simply advance the step latency.
- `call_timing` controls when a call is **issued** (enable pulse), `arg_timing` specifies when arguments are **valid**, and `result_timing` specifies when results are **captured**. Multicycle calls are issued once and observed later; they are not repeatedly invoked. See: `examples/PyCMT2/static_step_call_timing_window.py`.

### Pipelined Static Steps

For pipelined operations with **initiation interval**:

```python
with m.static_step(8, "pipelined_mult", interval=2) as step:
    # 8-cycle latency, can start new operation every 2 cycles
    a = step.call(reg_a, "read")
    b = step.call(reg_b, "read")
    step.call(reg_result, "write", step.mul(a, b))
```

| Parameter | Description |
|-----------|-------------|
| `latency` | Total cycles from start to completion |
| `interval` | Initiation interval (minimum cycles between starts) |

### Step Methods

| Method | Description |
|--------|-------------|
| `step.call(inst, method, *args)` | Call method on instance |
| `step.const(value, width)` | Create constant |
| `step.add/sub/mul/...` | Arithmetic operations |

---

## Procedural Rules

Rules with multi-cycle control flow:

```python
with m.proc_rule("compute") as rule:
    with rule.guard() as g:
        busy = g.call(busy_reg, "read")
        g.returns(busy)

    with rule.control() as ctrl:
        with ctrl.seq() as seq:
            seq.enable(m._steps["load_step"].ref())
            seq.enable(m._steps["compute_step"].ref())
            seq.enable(m._steps["store_step"].ref())
```

---

## Control Flow Constructs

### Sequential (seq)

Execute steps one after another:

```python
with ctrl.seq() as seq:
    seq.enable(step_a.ref())  # First
    seq.enable(step_b.ref())  # Then
    seq.enable(step_c.ref())  # Finally
```

### Parallel (par)

Execute steps concurrently:

```python
with ctrl.par() as par:
    par.enable(step_a.ref())  # Concurrent
    par.enable(step_b.ref())  # Concurrent
# Both must complete before continuing
```

### Conditional (if)

```python
with ctrl.if_(condition) as if_ctrl:
    with if_ctrl.then_() as then_:
        then_.enable(step_if_true.ref())
    with if_ctrl.else_() as else_:
        else_.enable(step_if_false.ref())
```

### Loop (while)

```python
with ctrl.while_(loop_condition) as loop:
    with loop.body() as body:
        body.enable(step_body.ref())
```

### Static Repeat

Fixed-iteration loop with known latency:

```python
with ctrl.static_repeat(4, body_latency=5) as loop:
    # 4 iterations, each taking 5 cycles
    # Total latency: 4 * 5 = 20 cycles
    with loop.seq() as seq:
        seq.enable(step_a.ref())  # 2 cycles
        seq.enable(step_b.ref())  # 3 cycles
```

### Static If

Conditional with known branch latencies:

```python
with ctrl.static_if(condition, then_latency=3, else_latency=3) as if_ctrl:
    with if_ctrl.then_() as then_:
        then_.enable(step_then.ref())  # 3 cycles
    with if_ctrl.else_() as else_:
        else_.enable(step_else.ref())  # 3 cycles
```

---

## Hardware Execution Model

This section describes how procedural control constructs execute in hardware after compilation. Understanding the execution model is essential for writing efficient multi-cycle operations.

> **Implementation Note:** Proc lowering is implemented via TDCC → ProcStmtToAction → ProcToGAA passes.

### FSM-Based Execution

All procedural control (`proc.rule`) compiles to a finite state machine (FSM):

```
┌─────────────────────────────────────────────────────────────┐
│  proc.rule FSM Execution                                    │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│    ┌─────────┐   guard=1   ┌─────────┐   done   ┌─────────┐│
│    │  IDLE   │────────────>│ RUNNING │─────────>│ RETURN  ││
│    │ state=0 │             │state=1..N│         │ to IDLE ││
│    └─────────┘             └─────────┘          └─────────┘│
│         ↑                        │                    │    │
│         └────────────────────────┴────────────────────┘    │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

- **IDLE (state 0)**: Guard is evaluated. If true, FSM advances to first step.
- **RUNNING (states 1..N)**: Steps execute according to control structure.
- **RETURN TO IDLE**: After final step completes, FSM returns to state 0.

### Step Execution Timing

The key performance difference is between **static** and **dynamic** steps:

#### Dynamic Steps (`proc.step`)

Dynamic steps use explicit done signals, requiring **2 cycles per step**:

```
Cycle N:   FSM enables step (go=1), step executes, step asserts done=1
Cycle N+1: FSM sees done=1, transitions to next state
```

| Phase | Cycle | Action |
|-------|-------|--------|
| Execute | N | Step body runs, done signal asserted |
| Transition | N+1 | FSM detects done, advances state |

**Total: 2 cycles per dynamic step**

#### Static Steps (`proc.static_step`)

Static steps have compile-time known latency, enabling **L cycles per step**:

```
Cycle N..N+L-1: Step executes for exactly L cycles
Cycle N+L:     FSM automatically transitions (no done signal needed)
```

| Latency | Cycles | Notes |
|---------|--------|-------|
| 1 | 1 | Ideal for simple operations |
| L | L | Exact latency, no overhead |

**Recommendation:** Use `static_step(1, "name")` for 1-cycle operations instead of dynamic `step()`.

### Control Construct Overhead

#### Sequential Composition (`proc.seq`)

Steps execute one after another. Total cycles = sum of step cycles.

```python
with ctrl.seq() as seq:
    seq.enable(step_a.ref())  # 2 cycles (dynamic) or L cycles (static)
    seq.enable(step_b.ref())  # 2 cycles (dynamic) or L cycles (static)
# Total: sum of all step cycles
```

| Steps | Dynamic | Static (L=1 each) |
|-------|---------|-------------------|
| 2 | 4 cycles | 2 cycles |
| 3 | 6 cycles | 3 cycles |
| N | 2N cycles | N cycles |

#### Parallel Composition (`proc.par`)

Steps execute concurrently. Total cycles = max of branch cycles.

```python
with ctrl.par() as par:
    par.enable(step_a.ref())  # Branch A
    par.enable(step_b.ref())  # Branch B
# Total: max(branch_A_cycles, branch_B_cycles)
```

**Important:** Each parallel branch gets its own FSM. Fork-join synchronization adds no extra cycles.

#### Static Repeat (`proc.static_repeat`)

Fixed iteration count with no condition overhead:

```python
with ctrl.static_repeat(N, body_latency=L) as loop:
    loop.enable(step.ref())  # L cycles per iteration
# Total: N × L cycles
```

| Iterations | Body Latency | Total |
|------------|--------------|-------|
| 4 | 1 (static) | 4 cycles |
| 4 | 2 (dynamic) | 8 cycles |
| N | L | N × L cycles |

#### While Loop (`proc.while`)

Dynamic condition requires re-evaluation each iteration:

```python
with ctrl.while_(condition) as loop:
    loop.enable(step.ref())  # Body execution
# Total: iterations × (condition_eval + body_cycles)
```

| Component | Cycles | Notes |
|-----------|--------|-------|
| Condition eval | 1 | Each iteration |
| Body (dynamic step) | 2 | Done signal overhead |
| **Per iteration** | **3** | Minimum for dynamic body |
| Body (static step) | L | No done overhead |
| **Per iteration** | **1 + L** | With static body |

**Current Limitation:** While loops require condition re-evaluation, adding 1 cycle overhead per iteration.

### Performance Comparison

| Construct | 4 Iterations × 1-op | Notes |
|-----------|---------------------|-------|
| static_repeat + static_step(1) | 4 cycles | Optimal |
| static_repeat + dynamic step | 8 cycles | 2x overhead |
| while + dynamic step | 12 cycles | Condition + done overhead |
| while + static_step(1) | 8 cycles | Condition overhead only |

### Step Selection Guidelines

Choose the right step type for your use case:

| Use Case | Recommended | Reason |
|----------|-------------|--------|
| Fixed-latency operation | `static_step(L)` | No done signal overhead |
| Simple 1-cycle operation | `static_step(1)` | Minimal FSM states |
| Data-dependent completion | `step()` (dynamic) | Needs done signal |
| External module handshake | `step()` (dynamic) | Waits for ready/valid |
| Pipeline stage | `static_step(1)` | Predictable timing |

### Loop Selection Guidelines

| Use Case | Recommended | Reason |
|----------|-------------|--------|
| Known iteration count | `static_repeat` | No condition overhead |
| Data-dependent termination | `while` | Needs condition check |
| Pipeline filling/draining | `static_repeat` | Known count |
| Search/find operations | `while` | Unknown termination |

### Example: Optimal vs Suboptimal

```python
# SUBOPTIMAL: 8 cycles for 4 iterations
with ctrl.static_repeat(4) as loop:
    with loop.seq() as seq:
        seq.enable(dynamic_step.ref())  # 2 cycles each = 8 total

# OPTIMAL: 4 cycles for 4 iterations
with ctrl.static_repeat(4) as loop:
    with loop.seq() as seq:
        seq.enable(static_step_1cycle.ref())  # 1 cycle each = 4 total
```

### Testbench Verification

The execution model is verified by `examples/PyCMT2/proc_testbench.py` which includes:

| Test | Construct | Expected Cycles |
|------|-----------|-----------------|
| `test_static_only` | static_repeat + static_step | Exact latency |
| `test_dynamic_only` | seq + dynamic steps | 2× step count |
| `test_mixed` | static + dynamic | Sum of latencies |
| `test_parallel` | par | Max of branches |

Run the testbench to verify timing:
```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 \
    ../examples/PyCMT2/proc_testbench.py
cd proc_testbench_workspace
make && make run
```

---

## Procedural Methods (proc_method)

**CRITICAL**: Timing attributes (`static_latency`, `interval`) are **only valid for procedural operations** that use multi-cycle control flow. Atomic methods (`cmt2.method`) are always single-cycle and **cannot have timing attributes**.

### Atomic vs Procedural Methods

| Aspect | `cmt2.method` (Atomic) | `cmt2.proc.method` (Procedural) |
|--------|------------------------|--------------------------------|
| **Execution** | Single cycle | Multi-cycle |
| **Regions** | Guard + Body | Guard + Control |
| **Body** | Atomic actions | Procedural control flow |
| **Timing Attributes** | ❌ NOT ALLOWED | ✓ Required for static scheduling |
| **Protocol** | Ready-Enable | Start + FSM execution |

### proc_method Definition

```python
# CORRECT: Timing attributes on proc_method with procedural control
with m.proc_method("multiply",
                   args=[("a", UInt(32)), ("b", UInt(32))],
                   returns=[UInt(64)],
                   static_latency=8,   # Total latency in cycles
                   interval=2) as meth:  # Initiation interval (pipelined)
    with meth.guard() as g:
        g.always()  # Always ready (for pipelined method)

    with meth.control() as ctrl:
        with ctrl.seq() as seq:
            # 8-cycle pipeline: 4 stages × 2 cycles each
            seq.enable(m._steps["mult_stage1"].ref())  # 2 cycles
            seq.enable(m._steps["mult_stage2"].ref())  # 2 cycles
            seq.enable(m._steps["mult_stage3"].ref())  # 2 cycles
            seq.enable(m._steps["mult_stage4"].ref())  # 2 cycles
```

### Timing Attribute Constraints

**The `static_latency` must equal the sum of latencies in the control region:**

```
static_latency = sum of step latencies in control flow
```

Example validation:
```python
# static_latency=6 must match control flow: 2 + 4 = 6
with m.proc_method("compute", ..., static_latency=6) as meth:
    with meth.control() as ctrl:
        with ctrl.seq() as seq:
            seq.enable(step_a.ref())  # 2-cycle static_step
            seq.enable(step_b.ref())  # 4-cycle static_step
            # Total: 6 cycles ✓
```

**Interval constraint**: `interval ≤ static_latency`
- Cannot start faster than completion time
- `interval=1` means fully pipelined (new input every cycle)

### Implementation Status

| Layer | Status | Notes |
|-------|--------|-------|
| **MLIR (TableGen)** | ✓ Complete | `ProcMethodOp` with `static_latency`, `interval` attributes |
| **Python Bindings** | ✓ Complete | `ProcMethodBuilder` supports timing parameters |
| **Validation** | ✓ Partial | Rejects timing on atomic methods and CallOps outside static_step |

**Python API:**
```python
# proc_method with timing attributes
with m.proc_method("multiply",
                   args=[("a", UInt(32)), ("b", UInt(32))],
                   returns=[UInt(64)],
                   static_latency=4,  # Total cycles
                   interval=2) as meth:  # Initiation interval
    # Properties available: meth.is_static, meth.latency, meth.interval, meth.is_pipelined
    ...
```

### MLIR Examples

**Static proc_method** (compile-time latency):
```mlir
cmt2.proc.method @multiply(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> (!firrtl.uint<64>) {
  %c1 = firrtl.constant 1 : !firrtl.uint<1>
  cmt2.return %c1 : !firrtl.uint<1>
} control {
  cmt2.proc.seq {
    cmt2.proc.enable @mult_stage1
    cmt2.proc.enable @mult_stage2
  }
  cmt2.proc.control_end
} {static_latency = 8 : i64, interval = #cmt2.interval<2>}
```

**Dynamic proc_method** (runtime completion):
```mlir
cmt2.proc.method @search(%key: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
  %idle = cmt2.call @fsm @isIdle() : () -> !firrtl.uint<1>
  cmt2.return %idle : !firrtl.uint<1>
} control {
  cmt2.proc.while {
    %found = cmt2.call @table @contains(%key) : (!firrtl.uint<32>) -> !firrtl.uint<1>
    %not_found = firrtl.not %found : !firrtl.uint<1>
    cmt2.yield %not_found : !firrtl.uint<1>
  } do {
    cmt2.proc.enable @check_next
  }
  cmt2.proc.control_end
}
// No static_latency - completion time unknown at compile time
```

### Lowering (ProcToGAA Pass)

`cmt2.proc.method` lowers to:

1. **FSM state register** - Tracks execution progress
2. **Argument registers** - Hold inputs across cycles
3. **Result register** - Stores output value
4. **Entry method** - Atomic method that latches args and starts FSM
5. **GAA rules** - One per control flow state transition
6. **Result value** - Read result when FSM is idle

For static methods, FSM is optimized:
- Counter-based state machine (known sequence)
- No done signal needed (completion at cycle N guaranteed)
- Enables static scheduling in callers

---

## External Module Timing

### Value with Latency

```python
with circuit.external_module("Memory") as mem:
    mem.clock("clk")
    mem.reset("rst")

    # Read takes 2 cycles
    mem.value("read",
              args=[("addr", UInt(8))],
              returns=[("data", UInt(32))],
              static_latency=2)
```

### Method with Timing

```python
    # Write: 4 cycles, can pipeline every 2 cycles
    mem.method("write",
               args=[("addr", UInt(8)), ("data", UInt(32))],
               static_latency=4,
               interval=2)
```

---

## Timing Attributes (MLIR)

### TimingIntervalAttr

Half-open cycle interval `[start, end)`:

```mlir
#cmt2.timing<[0, 4]>  // Cycles 0, 1, 2, 3
```

### LatencyAttr

Port latency:

```mlir
#cmt2.latency<2>  // Available at cycle 2
```

### IntervalAttr

Initiation interval:

```mlir
#cmt2.interval<2>  // Can start every 2 cycles
```

### Call-Site Timing

Specify when a call is issued, when arguments are valid, and when results are captured:

```mlir
cmt2.call @mem @read(%addr) {
    call_timing = #cmt2.timing<[0, 1]>,     // Issue call at cycle 0
    arg_timing = [#cmt2.timing<[0, 1]>],    // Drive addr at cycle 0
    result_timing = [#cmt2.timing<[2, 3]>]  // Capture result at cycle 2
} : (!firrtl.uint<8>) -> !firrtl.uint<32>
```

---

## Complete Example

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry

clear_stl_registry()
circuit = Circuit("DotProductPipeline")
reg32 = Reg.create(circuit, 32)
reg1 = Reg.create(circuit, 1)

with circuit.module("DotProduct") as m:
    clk, rst = m.clock(), m.reset()

    reg_a = m.instance(reg32, "reg_a", clk=clk, rst=rst)
    reg_b = m.instance(reg32, "reg_b", clk=clk, rst=rst)
    reg_product = m.instance(reg32, "reg_product", clk=clk, rst=rst)
    reg_accum = m.instance(reg32, "reg_accum", clk=clk, rst=rst)
    busy = m.instance(reg1, "busy", clk=clk, rst=rst)

    # Static step: load (1 cycle)
    with m.static_step(1, "load") as step:
        pass  # Data already in registers

    # Static step: multiply (3 cycles)
    with m.static_step(3, "multiply") as step:
        a = step.call(reg_a, "read")
        b = step.call(reg_b, "read")
        step.call(reg_product, "write", step.mul(a, b))

    # Static step: accumulate (1 cycle)
    with m.static_step(1, "accumulate") as step:
        prod = step.call(reg_product, "read")
        acc = step.call(reg_accum, "read")
        step.call(reg_accum, "write", step.add(acc, prod))

    # Dynamic step: finish
    with m.step("finish") as step:
        step.call(busy, "write", step.const(0, 1))

    # Procedural rule with static control
    with m.proc_rule("compute") as rule:
        with rule.guard() as g:
            is_busy = g.call(busy, "read")
            g.returns(is_busy)

        with rule.control() as ctrl:
            with ctrl.seq() as seq:
                seq.enable(m._steps["load"].ref())

                # 4 iterations: total = 4 * (3 + 1) = 16 cycles
                with seq.static_repeat(4, body_latency=4) as loop:
                    with loop.seq() as inner:
                        inner.enable(m._steps["multiply"].ref())
                        inner.enable(m._steps["accumulate"].ref())

                seq.enable(m._steps["finish"].ref())

    # Start method (atomic - NO timing attributes)
    # Atomic methods complete in 1 cycle, timing attributes not allowed
    with m.method("start",
                  args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
        with meth.guard() as g:
            is_busy = g.call(busy, "read")
            g.returns(g.not_(is_busy))
        with meth.body() as body:
            body.call(reg_a, "write", body.arg("a"))
            body.call(reg_b, "write", body.arg("b"))
            body.call(busy, "write", body.const(1, 1))
            body.call(reg_accum, "write", body.const(0, 32))

    # Result value
    with m.value("result", returns=[UInt(32)]) as val:
        with val.guard() as g:
            is_busy = g.call(busy, "read")
            g.returns(g.not_(is_busy))
        with val.body() as body:
            body.returns(body.call(reg_accum, "read"))
```

---

## Compilation Pipeline

### Procedural Control Pipeline

Multi-cycle proc control is lowered through these passes:

1. **cmt2-compile-invoke**: Converts `proc.invoke` to steps
2. **cmt2-tdcc**: Top-Down Compile Control - generates FSM
3. **cmt2-static-inference**: Infers latencies
4. **cmt2-static-promotion**: Promotes dynamic to static where possible
5. **cmt2-timing-inference**: Infer timing from method signatures
6. **cmt2-timing-validation**: Verify timing constraints
7. **cmt2-static-fsm-allocation**: Map cycles to FSM states
8. **cmt2-compile-static**: Generate optimized FSM hardware
9. **cmt2-proc-stmt-to-action**: Converts to FSM-based rules
10. **cmt2-proc-to-gaa**: Final conversion to GAA rules

### Dataflow Pipeline

Dataflow/pipeline constructs are lowered through:

1. **cmt2-dataflow-lowering**: Convert `proc.dataflow` to rules with tokens
2. **cmt2-token-lowering**: Analyze tokens, classify LS/LI, infer FIFO depths
3. **cmt2-token-rtl-gen**: Generate storage modules, rewrite token ops to calls
4. **cmt2-to-firrtl**: Standard conversion (no token ops remain)

### Combined Pipeline

For designs using both proc control and dataflow:

```
cmt2-compile-invoke → cmt2-tdcc → cmt2-proc-stmt-to-action →
cmt2-dataflow-lowering → cmt2-token-lowering → cmt2-token-rtl-gen →
cmt2-proc-to-gaa → cmt2-to-firrtl
```

---

## FSM Generation

The TDCC pass generates a finite state machine:

```
State 0: Idle
State 1: load_step active
State 2: multiply_step (cycle 1)
State 3: multiply_step (cycle 2)
State 4: multiply_step (cycle 3)
State 5: accumulate_step active
State 6: finish_step active
→ Return to State 0
```

### FSM Encoding

| States | Encoding | Bits |
|--------|----------|------|
| ≤8 | One-hot | N bits |
| >8 | Binary | log2(N) bits |

---

## Precedence Handling During Proc Lowering

When procedural rules are lowered to GAA rules, multiple FSM state rules are generated that need proper precedence relationships.

**See also:** [Scheduling.md](Scheduling.md) (conflicts and precedence)

Key points:
- Generated rules include FSM operations AND cloned step body operations
- Current implementation handles FSM conflicts via mutually exclusive guards
- Step body conflicts and cross-module constraints need additional handling

---

## Timing Validation

The compiler validates timing constraints:

### Timing Within Step Bounds

```
error: call timing [5, 6) exceeds step latency 4
    → Timing must be within [0, step_latency)
```

### Pipelined Call Spacing

```
error: pipelined calls to @multiply must be at least 2 cycles apart
    → Calls at cycles 0 and 1 violate interval=2
```

### Result Timing

```
error: result timing [1, 2) before method output latency 3
    → Cannot capture result before it's available
```

---

## Known Limitations

### Timing in Procedural Lowering Pipeline

`call_timing` / `arg_timing` / `result_timing` are **only legal inside** `cmt2.proc.static_step`. They are **consumed** by the static-step lowering pipeline to derive per-cycle scheduling actions (issue vs capture); after that, timing becomes **implicit** in the FSM state structure and the attributes are dropped. Per-cycle call clones are tagged with `call_ty = "Enable"|"GetRes"` so SV lowering can avoid re-issuing multicycle calls.

| Pass | Timing Support |
|------|----------------|
| `CompileStatic` | ✓ Consumes `call_timing`/`arg_timing`/`result_timing` and drops them after legalization |
| `TDCC` | ✗ Control-flow FSM only (does not reschedule calls) |
| `StaticFSMAllocation` | ✓ Computes `state_assignments` for per-cycle call actions (Enable/GetRes) |
| `ProcStmtToAction` | ✓ Clones scheduled calls per FSM cycle and tags clones with `call_ty` |
| `ProcToGAA` | ✓ Preserves behavior via per-state rules (no timing attrs needed) |

**Current restrictions (documented for future relaxation):**
- `call_timing` must be a single-cycle interval.
- `arg_timing[i]` must match `call_timing` (no implicit “hold regs”).
- `result_timing` must be a single-cycle interval shared by all results, and must match the callee’s declared static latency from the call start (no implicit “stable-until-next-call” modeling).

**Practical guidance:** for cycle-precise call behavior, put the call inside a `static_step` and use `call_timing` (issue), `arg_timing` (arg validity), and `result_timing` (capture). For an E2E demo (SV + simulation), see `examples/PyCMT2/static_step_call_timing_window.py`.

```python
# WORKS: Timing preserved in static_step
with m.static_step(4, "timed_mult") as step:
    result = step.call(mult, "multiply", a, b,
                      call_timing=(0, 1),
                      arg_timing=[(0, 1), (0, 1)],
                      result_timing=[(3, 4)])

# LIMITATION: Timing in proc.step bodies is not currently supported
with m.step("dynamic_op") as step:
    result = step.call(mult, "multiply", a, b,
                      arg_timing=[(0, 1), (0, 1)])  # Timing may be ignored
```

**Future Work:** Timing preservation and richer scheduling contracts are ongoing; track current status in the source tree (passes + TODOs).

---

## Dataflow/Pipeline Operations

CMT2 supports **dataflow/pipeline operations** through token-based synchronization. This enables pipelined hardware generation with explicit producer-consumer relationships.

**Related Examples:** `examples/PyCMT2/comprehensive_dataflow_example.py`, `examples/PyCMT2/dataflow_forkjoin.py`, `examples/PyCMT2/division_pipeline.py`

**Status:** Fully implemented and tested (16+ E2E simulations verified)

---

### Token-Based Synchronization

Dataflow uses **SyncTokens** as first-class SSA values for synchronization:

| Concept | Description |
|---------|-------------|
| **SyncToken** | SSA value representing synchronization point + optional data |
| **Multi-consumer** | Token used by multiple tasks; implies fork/broadcast pattern |
| **Latency-Sensitive (LS)** | Fixed timing, shift register impl, consumers must be ready |
| **Latency-Insensitive (LI)** | Variable timing, FIFO impl, waits until consumed |

```mlir
// SyncToken type with data payload
!cmt2.sync_token<data = !firrtl.uint<32>>
```

### proc.dataflow Construct

The unified `proc.dataflow` construct supports linear pipelines and fork-join patterns:

```python
with m.dataflow("pipeline", args=[("input", UInt(32))], returns=[UInt(32)]) as df:
    # Task 0: source (creates token from input)
    with df.task("source") as t:
        tok = t.create_token(df.input)
        t.yield_tokens(tok)

    # Task 1: process (consumes and produces token)
    with df.task("process", tokens_in=[tok]) as t:
        data = t.token_data(tok)
        result = t.add(data, t.const(10, 32))
        out_tok = t.create_token(result)
        t.yield_tokens(out_tok)

    # Task 2: sink (returns final result)
    with df.task("sink", tokens_in=[out_tok]) as t:
        result = t.token_data(out_tok)
        t.return_values(result)
```

### Fork-Join Pattern

Multiple tasks can consume the same token (fork) and a task can wait for multiple tokens (join):

```python
with m.dataflow("fork_join", args=[("x", UInt(16))], returns=[UInt(16)]) as df:
    # Source produces one token
    with df.task("source") as t:
        tok = t.create_token(df.x)
        t.yield_tokens(tok)

    # Fork: both tasks receive the same token
    with df.task("branch_a", tokens_in=[tok]) as t:
        data = t.token_data(tok)
        result = t.mul(data, t.const(2, 16))
        tok_a = t.create_token(result)
        t.yield_tokens(tok_a)

    with df.task("branch_b", tokens_in=[tok]) as t:
        data = t.token_data(tok)
        result = t.add(data, t.const(100, 16))
        tok_b = t.create_token(result)
        t.yield_tokens(tok_b)

    # Join: wait for both branches
    with df.task("combine", tokens_in=[tok_a, tok_b]) as t:
        a = t.token_data(tok_a)
        b = t.token_data(tok_b)
        result = t.add(a, b)
        t.return_values(result)
```

### Token Operations

| Operation | MLIR | Description |
|-----------|------|-------------|
| Create | `cmt2.token.create %data` | Create token with data payload |
| Valid | `cmt2.token.valid %tok` | Check if token is valid (for guards) |
| Data | `cmt2.token.data %tok` | Extract data from token |
| Join | `cmt2.token.join %a, %b` | Join multiple tokens (AND of valids) |

### Hardware Generation

Tokens are lowered to storage modules by the `cmt2-token-rtl-gen` pass:

| Token Mode | Hardware | Usage |
|------------|----------|-------|
| LS (latency-sensitive) | Shift register | Fixed timing, cycle-synchronized |
| LI (latency-insensitive) | FIFO | Variable timing, handshake-based |

```mlir
// Before TokenRTLGen:
cmt2.rule @producer() tokens_out(!cmt2.sync_token<data = !firrtl.uint<32>>) {
  %tok = cmt2.token.create %data : ...
}

// After TokenRTLGen:
cmt2.instance @__tok_0 = @ShiftReg_w32_d1(%clk, %rst)
cmt2.rule @producer() {
  cmt2.call @__tok_0 @write(%data) : ...
}
```

### Pass Pipeline

```
cmt2-dataflow-lowering → cmt2-token-lowering → cmt2-token-rtl-gen → cmt2-to-firrtl
```

| Pass | Purpose |
|------|---------|
| `cmt2-dataflow-lowering` | Convert proc.dataflow to rules with tokens |
| `cmt2-token-lowering` | Analyze tokens, infer FIFO depths |
| `cmt2-token-rtl-gen` | Generate storage modules, rewrite token ops |
| `cmt2-to-firrtl` | Standard CMT2 to FIRRTL conversion |

---

## Integration: Dataflow Tasks with Proc Control

Multi-cycle proc control can be used **inside dataflow tasks** for complex pipeline stages. This enables patterns like:

- Tasks with internal `static_repeat` loops
- Tasks with data-dependent `while` loops
- Tasks calling external modules with timing constraints

### Task with Internal Control Flow

```python
with m.dataflow("iterative_pipeline", args=[("input", UInt(32))], returns=[UInt(32)]) as df:
    # Task with internal loop (4 iterations, 1 cycle each)
    with df.task("iterative_stage", timing=(0, 4)) as t:
        state = df.input
        with t.static_repeat(4):
            state = t.call(iter_step, "process", state)
        tok = t.create_token(state)
        t.yield_tokens(tok)

    # Simple single-cycle task
    with df.task("final", tokens_in=[tok], timing=(4, 5)) as t:
        result = t.token_data(tok)
        t.return_values(result)
```

### Timing Modes

| Task Body | Token Mode | Timing |
|-----------|------------|--------|
| Combinational logic | LS | Immediate |
| `static_step(N)` | LS | N cycles, known at compile time |
| `static_repeat` | LS | Iterations × body latency |
| `while` loop | LI | Data-dependent, requires FIFO |

### FSM Composition

When tasks have internal FSMs, multiple FSM levels exist:

```
Dataflow Level: Token-based scheduling
    task0 ──token──> task1 ──token──> task2
      │                │                │
      ▼                ▼                ▼
  ┌───────┐        ┌───────┐        ┌───────┐
  │FSM_t0 │        │FSM_t1 │        │FSM_t2 │   Task-level FSMs
  │(loop) │        │(seq)  │        │(comb) │
  └───────┘        └───────┘        └───────┘
```

### Stall Controller

For hybrid LS/LI designs, stall controllers manage timing boundaries:

- All registers in LS region gated by global stall
- Task FSM registers included in stall gating
- LI FIFOs provide ready/valid signals to stall controller

**Compatibility Status (Phase 7):**

| Scenario | Status |
|----------|--------|
| Static tasks (single-cycle) | ✓ Complete |
| Static tasks (multi-cycle, fixed timing) | ✓ Complete |
| Dynamic tasks (while loops) | ✓ Complete (auto-infers LI mode) |
| Mixed LS/LI regions | ✓ Complete |
| Tasks calling external modules with timing | ✓ Complete |
| Nested dataflow | Deferred (flatten manually) |

---

## Timing Attribute Unification

### Timing Systems

| System | Attribute | Meaning |
|--------|-----------|---------|
| Dataflow | `#cmt2.timing<[start, end]>` | Task active interval |
| Proc | `static_latency` | Total cycles to complete |
| Proc | `interval` | Initiation interval |
| Call | `arg_timing`, `result_timing` | Per-call port timing |

### Unified Rules

Task timing attributes integrate with proc timing:

```mlir
// Task with timing = equivalent to static_latency
cmt2.dataflow.task @t {timing = #cmt2.timing<[2, 5]>}
// Equivalent to: static_latency = 3 (cycles 2, 3, 4)

// Task without timing = dynamic (LI mode)
cmt2.dataflow.task @t {mode = "li"}
// Completion determined by internal FSM
```

---

## Next Steps

- [Operations.md](../reference/Operations.md) - All CMT2 operations
- [Attributes.md](../reference/Attributes.md) - Attribute reference
- [Passes.md](../reference/Passes.md) - Transformation passes
- [Lowering.md](Lowering.md) - Where to find lowering implementation entry points
- [Dataflow.md](Dataflow.md) - Decoupled pipelines and token/task model
