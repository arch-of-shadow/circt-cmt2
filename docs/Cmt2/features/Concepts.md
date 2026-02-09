# CMT2 Concepts

CMT2 implements **Guarded Atomic Actions (GAA)** semantics for hardware design. This document explains the core concepts.

---

## Guarded Atomic Actions (GAA)

GAA is a hardware design paradigm where computation is organized as **rules** - atomic state transitions guarded by boolean conditions.

### One-Rule-At-A-Time (ORAAT)

The key semantic principle: **only one rule fires per clock cycle**. This provides:

- **Atomicity**: Each rule's actions are indivisible
- **Determinism**: Predictable execution despite parallel hardware
- **Composability**: Rules can be reasoned about independently

In practice, the compiler schedules multiple non-conflicting rules to fire concurrently while preserving ORAAT semantics.

---

## Core Abstractions

### Rules

A **rule** is a guarded state transition:

```
rule name:
    guard: boolean condition
    body: atomic actions (reads, computations, writes)
```

Rules fire when:
1. The guard evaluates to true
2. No conflicting rule is also firing
3. The scheduler selects this rule

**Example:**
```python
with m.rule("increment") as rule:
    with rule.guard() as g:
        enabled = g.call(enable_reg, "read")
        g.returns(enabled)
    with rule.body() as body:
        count = body.call(counter, "read")
        body.call(counter, "write", body.add(count, body.const(1, 32)))
```

### Methods

A **method** is an action that can be called by external modules or testbenches:

```
method name(args) -> returns:
    guard: when the method is ready
    body: actions to perform
```

Methods use a **ready-enable protocol**:
- `ready`: Output signal indicating method can accept a call
- `enable`: Input signal to invoke the method

**Example:**
```python
with m.method("write", args=[("data", UInt(32))]) as meth:
    with meth.guard() as g:
        full = g.call(fifo, "is_full")
        g.returns(g.not_(full))  # Ready when not full
    with meth.body() as body:
        body.call(fifo, "enqueue", body.arg("data"))
```

### Values

A **value** is a read-only method that exposes data:

```
value name() -> returns:
    guard: when the value is valid
    body: computation returning the value
```

Values use a **ready-data protocol**:
- `ready`: Output signal indicating data is valid
- `result`: Output data

**Example:**
```python
with m.value("peek", returns=[UInt(32)]) as val:
    with val.guard() as g:
        empty = g.call(fifo, "is_empty")
        g.returns(g.not_(empty))  # Valid when not empty
    with val.body() as body:
        body.returns(body.call(fifo, "front"))
```

---

## Scheduling Constraints

Rules and methods have **scheduling relationships** that determine which can fire together.

### Conflict (`<>`)

Two operations **conflict** if they cannot fire in the same cycle:

```python
# In external module definition
fifo.conflict("enqueue", "dequeue")
```

### Sequence Before (`<`)

One operation must **sequence before** another:

```python
# read must complete before write in same cycle
reg.sequence_before("read", "write")
```

### Conflict-Free (`/`)

Operations can execute in either order or concurrently:

```python
fifo.conflict_free("is_empty", "is_full")
```

---

## Module Composition

### Instances

Modules are composed through **instantiation**:

```python
reg32 = Reg.create(circuit, 32)

with circuit.module("MyModule") as m:
    clk, rst = m.clock(), m.reset()
    counter = m.instance(reg32, "counter", clk=clk, rst=rst)
```

### Interfaces

Modules communicate through **interfaces** - collections of methods and values:

```python
# External module defines interface
with circuit.external_module("FIFO32") as fifo:
    fifo.clock("clk")
    fifo.reset("rst")
    fifo.method("enqueue", args=[("data", UInt(32))])
    fifo.method("dequeue", returns=[("data", UInt(32))])
    fifo.value("is_empty", returns=[("empty", UInt(1))])
    fifo.value("is_full", returns=[("full", UInt(1))])
    fifo.conflict("enqueue", "dequeue")
```

---

## Execution Model

### Clock Cycle Execution

Each clock cycle:

1. **Guard Evaluation**: All rule guards are evaluated
2. **Conflict Resolution**: Scheduler selects non-conflicting rules
3. **Atomic Execution**: Selected rules execute atomically
4. **State Update**: Register values update at clock edge

### Rule Ordering

Within a rule's body, operations execute in **program order**:

```python
with rule.body() as body:
    a = body.call(reg_a, "read")   # First
    b = body.call(reg_b, "read")   # Second
    body.call(reg_a, "write", b)   # Third
    body.call(reg_b, "write", a)   # Fourth (swap completes)
```

### Read-Before-Write

By default, reads see the **old value** and writes take effect at the **clock edge**:

```python
# This swaps values correctly
a = body.call(reg_a, "read")  # Reads old A
b = body.call(reg_b, "read")  # Reads old B
body.call(reg_a, "write", b)  # Writes B to A (at clock edge)
body.call(reg_b, "write", a)  # Writes A to B (at clock edge)
```

---

## Multi-Cycle Operations

For operations spanning multiple cycles, use **procedural control**:

### Dynamic Control

Operations with runtime-determined completion:

```python
with m.step("dequeue_when_ready") as step:
    # If `dequeue` is not ready, the step doesn't fire and the FSM stays put.
    data = step.call(fifo, "dequeue")
    step.call(result_reg, "write", data)
```

### Static Control

Operations with compile-time-known latency:

```python
with m.static_step(4, "multiply") as step:
    # 4-cycle fixed latency - no runtime done signal
    a = step.call(reg_a, "read")
    b = step.call(reg_b, "read")
    step.call(reg_result, "write", step.mul(a, b))
```

See [MultiCycle.md](MultiCycle.md) for details.

---

## Type System

CMT2 uses FIRRTL types:

| Type | Description | PyCMT2 |
|------|-------------|--------|
| Unsigned integer | N-bit unsigned | `UInt(N)` |
| Signed integer | N-bit signed | `SInt(N)` |
| Clock | Clock signal | `ClockType()` |
| Reset | Sync reset | `ResetType()` |
| Async Reset | Async reset | `AsyncResetType()` |
| Bundle | Struct-like | `Bundle(...)` |
| Vector | Array | `Vector(T, N)` |

---

## Summary

| Concept | Purpose |
|---------|---------|
| **Rule** | Guarded atomic state transition |
| **Method** | Callable action with ready-enable |
| **Value** | Read-only data with ready-data |
| **Instance** | Module composition |
| **Conflict** | Scheduling constraint |
| **ORAAT** | One rule fires per cycle |

---

## Next Steps

- [PyCMT2-Guide.md](../guides/PyCMT2-Guide.md) - Full Python API
- [MultiCycle.md](MultiCycle.md) - Multi-cycle operations and timing
