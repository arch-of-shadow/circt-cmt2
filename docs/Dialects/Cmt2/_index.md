# CMT2 Dialect

CMT2 is a rule-based hardware description dialect implementing **Guarded Atomic Actions (GAA)** semantics with **One-Rule-At-A-Time (ORAAT)** execution.

**Last Updated:** 2026-01-06

---

## Documentation Overview

### Getting Started

| Document | Description |
|----------|-------------|
| [QuickStart.md](QuickStart.md) | Introduction and first example |
| [Concepts.md](Concepts.md) | GAA semantics, rules, methods, values |

### User Guides

| Document | Description |
|----------|-------------|
| [PyCMT2-Guide.md](PyCMT2-Guide.md) | Python frontend - recommended for new users |
| [ECMT2-Guide.md](ECMT2-Guide.md) | C++ embedded DSL |
| [MultiCycle.md](MultiCycle.md) | Multi-cycle operations, timing, and pipelining |
| [STL.md](STL.md) | Standard library components (Reg, FIFO, Memory) |

### Reference

| Document | Description |
|----------|-------------|
| [Operations.md](Operations.md) | All CMT2 operations |
| [Attributes.md](Attributes.md) | Timing and scheduling attributes |
| [Passes.md](Passes.md) | Transformation and analysis passes |
| [Examples.md](Examples.md) | Example index with descriptions |

### Development

| Document | Description |
|----------|-------------|
| [Debugging.md](Debugging.md) | Debugging tools and techniques |
| [Development-Tracker.md](Development-Tracker.md) | Ongoing work and TODOs |

### Design Documents

| Document | Description |
|----------|-------------|
| [RationaleCmt2.md](RationaleCmt2.md) | Design rationale and GAA semantics |
| [Cmt2Proc-Design.md](Cmt2Proc-Design.md) | Procedural control design |
| [Cmt2ProcVsCalyx.md](Cmt2ProcVsCalyx.md) | Comparison with Calyx |
| [PyCmt2-Design.md](PyCmt2-Design.md) | PyCMT2 implementation design |

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

Run examples from the repository:

```bash
cd circt-cmt2/build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/gcd.py
```

See [Examples.md](Examples.md) for the full list.
