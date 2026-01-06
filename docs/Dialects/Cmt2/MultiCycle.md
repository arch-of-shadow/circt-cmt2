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

## Method Timing

### Static Latency

Declare method latency:

```python
with m.method("multiply",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[UInt(64)],
              static_latency=4) as meth:
    # Method completes 4 cycles after call
    ...
```

### Pipelined Method

```python
with m.method("pipelined_mult",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[UInt(64)],
              static_latency=4,
              interval=1) as meth:
    # 4-cycle latency, can accept new call every cycle
    ...
```

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

    # Start method with timing
    with m.method("start",
                  args=[("a", UInt(32)), ("b", UInt(32))],
                  static_latency=2,
                  interval=2) as meth:
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

## Next Steps

- [Operations.md](Operations.md) - All CMT2 operations
- [Attributes.md](Attributes.md) - Attribute reference
- [Passes.md](Passes.md) - Transformation passes
