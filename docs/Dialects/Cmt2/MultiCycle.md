# Multi-Cycle Operations

CMT2 supports **multi-cycle operations** through procedural control constructs. This enables complex algorithms spanning multiple clock cycles with both dynamic and static timing.

**Related Examples:** `examples/PyCMT2/proc.py`, `examples/PyCMT2/static_proc.py`, `examples/PyCMT2/timing.py`

---

## Overview

CMT2 provides two control modes:

| Mode | Latency | Done Signal | Use Case |
|------|---------|-------------|----------|
| **Dynamic** | Runtime | Explicit | Variable-length operations |
| **Static** | Compile-time | Implicit | Fixed-latency pipelines |

---

## Steps

### Dynamic Steps

Dynamic steps have **runtime-determined completion**:

```python
with m.step("wait_for_data") as step:
    data_ready = step.call(fifo, "has_data")
    step.done(data_ready)  # Complete when data available

    # Actions when enabled
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
| `step.done(cond)` | Signal completion when `cond` is true (dynamic only) |
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

> **Implementation Note:** Proc lowering is implemented via TDCC → ProcStmtToAction → ProcToGAA passes. Circuits using proc rules must include register modules with widths matching FSM requirements (e.g., `Reg.create(circuit, 3)` for 5-8 state FSMs). See Development-Tracker.md for current status.

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

Specify when arguments are driven and results captured:

```mlir
cmt2.call @mem @read(%addr) {
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
        step.done(step.const(1, 1))

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

Multi-cycle control is lowered through these passes:

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

**For comprehensive documentation, see:** [tmp/PrecedenceHandling.md](tmp/PrecedenceHandling.md)

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

**Critical Limitation:** Timing attributes on CallOps (`arg_timing`, `result_timing`) are **not preserved** through the procedural lowering pipeline (TDCC → ProcStmtToAction → ProcToGAA).

| Pass | Timing Support |
|------|----------------|
| `CompileStatic` | ✓ Uses timing for hardware generation |
| `TDCC` | ✗ Ignores timing - FSM based on control flow only |
| `ProcStmtToAction` | ✗ Creates CallOps with empty timing arrays |
| `ProcToGAA` | ✗ No timing propagation |

**Workaround:** Use `static_step` with inline calls for timing-critical operations. The timing within static steps is preserved by the `CompileStatic` pass.

```python
# WORKS: Timing preserved in static_step
with m.static_step(4, "timed_mult") as step:
    result = step.call(mult, "multiply", a, b,
                      arg_timing=[(0, 1), (0, 1)],
                      result_timing=[(3, 4)])

# LIMITATION: Timing in proc.step bodies may be lost during TDCC lowering
with m.step("dynamic_op") as step:
    result = step.call(mult, "multiply", a, b,
                      arg_timing=[(0, 1), (0, 1)])  # Timing may be ignored
    step.done(...)
```

**Future Work:** See `Development-Tracker.md` tasks TL2-TL7 for planned timing preservation improvements.

---

## Next Steps

- [Operations.md](Operations.md) - All CMT2 operations
- [Attributes.md](Attributes.md) - Attribute reference
- [Passes.md](Passes.md) - Transformation passes
