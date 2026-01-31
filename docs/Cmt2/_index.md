# CMT2 Dialect

CMT2 is a rule-based hardware description dialect implementing **Guarded Atomic Actions (GAA)** semantics with **One-Rule-At-A-Time (ORAAT)** execution.

**Last Updated:** 2026-01-27

---

## Core Model

- **Rules**: guarded atomic state updates scheduled each cycle.
- **Methods**: action interfaces (ready/enable) that can update state.
- **Values**: read-only interfaces (ready/data) that expose results.

## Documentation Overview


### Start Here

| Document | Description |
|----------|-------------|
| [QuickStart.md](guides/QuickStart.md) | Introduction and first example |
| [Concepts.md](features/Concepts.md) | GAA semantics, rules, methods, values |
| [Examples.md](examples/Examples.md) | Runnable examples (PyCMT2 + JIT) |

### Feature Tour

| Feature | Where to Read |
|---------|---------------|
| Proc control (multi-cycle control flow) | [Proc.md](features/Proc.md), [MultiCycle.md](features/MultiCycle.md), [Lowering.md](features/Lowering.md) |
| Multi-cycle timing (latency/interval) | [MultiCycle.md](features/MultiCycle.md), [Attributes.md](reference/Attributes.md), [Lowering.md](features/Lowering.md) |
| Dataflow tasks (decoupled stages) | [Dataflow.md](features/Dataflow.md), [MultiCycle.md](features/MultiCycle.md), [Operations.md](reference/Operations.md) |
| Scheduling (conflicts + precedence) | [Scheduling.md](features/Scheduling.md), [Concepts.md](features/Concepts.md) |
| STL components (Reg/FIFO/Memory) | [STL.md](features/STL.md) |
| Lowering (what becomes RTL) | [Lowering.md](features/Lowering.md) |
| Simulation & debugging | [Debugging.md](guides/Debugging.md), [DebugPorts-TestbenchDSL.md](guides/DebugPorts-TestbenchDSL.md) |

### Guides

| Document | Description |
|----------|-------------|
| [PyCMT2-Guide.md](guides/PyCMT2-Guide.md) | Python frontend - recommended for new users |
| [JIT.md](guides/JIT.md) | Typed, ergonomic syntax layer stacked on PyCMT2 |
| [ECMT2-Guide.md](guides/ECMT2-Guide.md) | C++ embedded DSL |
| [MultiCycle.md](features/MultiCycle.md) | Multi-cycle operations, timing, and pipelining |
| [STL.md](features/STL.md) | Standard library components (Reg, FIFO, Memory) |
| [Debugging.md](guides/Debugging.md) | Debugging tools and techniques |
| [DebugPorts-TestbenchDSL.md](guides/DebugPorts-TestbenchDSL.md) | Debug ports and Python testbench DSL |

### Reference

| Document | Description |
|----------|-------------|
| [API-Reference.md](reference/API-Reference.md) | PyCMT2 API documentation (auto-generated) |
| [Operations.md](reference/Operations.md) | All CMT2 operations |
| [Attributes.md](reference/Attributes.md) | Timing and scheduling attributes |
| [TypeSystem.md](reference/TypeSystem.md) | PyCMT2 types (used by JIT and PyCMT2) |
| [Passes.md](reference/Passes.md) | Transformation and analysis passes |
| [Examples.md](examples/Examples.md) | Example index with descriptions |

### Internals

Release-facing explanations live under `docs/Cmt2/features/`.

---

## Quick Example (PyCMT2)

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg

# Create a simple counter
circuit = Circuit("Counter")
reg32 = Reg.create(circuit, 32)

with circuit.module("Counter") as m:
    clk, rst = m.clock(), m.reset()
    count = m.instance(reg32, "count", clk=clk, rst=rst)

    # Rule: increment every cycle
    with m.rule("increment") as rule:
        with rule.guard() as g:
            g.always()
        with rule.body() as body:
            val = body.call(count, "read")
            body.call(count, "write", body.add(val, body.const(1, 32)))

    # Value: read current count
    with m.value("read", returns=[UInt(32)]) as val:
        with val.guard() as g:
            g.always()
        with val.body() as body:
            body.returns(body.call(count, "read"))

# Generate Verilog
verilog = circuit.emit_verilog()
```

---

## Key Features

| Feature | Description |
|---------|-------------|
| **GAA Semantics** | Rules with guards and atomic bodies |
| **Automatic Scheduling** | Conflict detection and rule arbitration |
| **Multi-Cycle Operations** | FSM generation with cycle-precise timing |
| **STL Components** | Pre-built Reg, FIFO, Memory modules |
| **FIRRTL Backend** | Conversion to FIRRTL for synthesis |

---

## Compilation Pipeline

```
PyCMT2/ECMT2 → CMT2 IR
    → Procedural Lowering (TDCC, FSM generation)
    → Static Optimization (timing inference, promotion)
    → Module Inlining
    → FIRRTL Conversion
    → Verilog Generation
```

---

## Examples

See the runnable example index: [Examples.md](examples/Examples.md).

```bash
ninja -C build
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/gcd.py
```

## Validation (Release Confidence)

Run these before release:

```bash
# Cmt2 dialect lit tests
build/bin/llvm-lit -v test/Dialect/Cmt2/

# PyCMT2 curated E2E suite (Verilator)
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/run_examples.py
```
