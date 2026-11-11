# CMT2 Quick Start

This guide gets you started with CMT2 using the PyCMT2 Python frontend.

---

## Prerequisites

Build CIRCT with Python bindings:

```bash
ninja -C build
```

Set the Python path:

```bash
export PYTHONPATH=$PWD/build/tools/circt/python_packages/circt_core
```

---

## Hello World: A Simple Counter

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg

# Create circuit
circuit = Circuit("HelloWorld")

# Create a 32-bit register module from STL
reg32 = Reg.create(circuit, 32)

with circuit.module("Counter") as m:
    # Declare ports
    clk = m.clock()
    rst = m.reset()

    # Instantiate register
    count = m.instance(reg32, "count", clk=clk, rst=rst)

    # Rule: increment counter every cycle
    with m.rule("increment") as rule:
        with rule.guard() as g:
            g.always()  # Always enabled
        with rule.body() as body:
            val = body.call(count, "read")
            new_val = body.add(val, body.const(1, 32))
            body.call(count, "write", new_val)

    # Value: expose current count
    with m.value("get_count", returns=[UInt(32)]) as val:
        with val.guard() as g:
            g.always()
        with val.body() as body:
            body.returns(body.call(count, "read"))

# Print generated MLIR
print(circuit.emit_mlir())

# Generate Verilog
print(circuit.emit_verilog())
```

---

## Running the Example

The repository includes a runnable counter example:

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/counter.py
```

---

## Understanding the Code

### Circuit and Module

```python
circuit = Circuit("HelloWorld")  # Top-level circuit
with circuit.module("Counter") as m:  # Define a module
    ...
```

### STL Components

```python
reg32 = Reg.create(circuit, 32)  # 32-bit register
count = m.instance(reg32, "count", clk=clk, rst=rst)  # Instantiate
```

The STL (Standard Library) provides pre-built components:
- `Reg(width)` - Register with read/write methods
- `FIFO(width, depth)` - FIFO buffer
- `Memory(width, depth)` - RAM module

### Rules

Rules are the basic scheduling unit in GAA. They have:
- **Guard**: Boolean condition for when the rule can fire
- **Body**: Atomic actions executed when the rule fires

```python
with m.rule("increment") as rule:
    with rule.guard() as g:
        g.always()  # Guard is always true
    with rule.body() as body:
        # Atomic body - read, compute, write
        val = body.call(count, "read")
        body.call(count, "write", body.add(val, body.const(1, 32)))
```

### Values

Values are read-only methods that expose data:

```python
with m.value("get_count", returns=[UInt(32)]) as val:
    with val.guard() as g:
        g.always()
    with val.body() as body:
        body.returns(body.call(count, "read"))
```

---

## Next Steps

| Topic | Document |
|-------|----------|
| GAA concepts | [Concepts.md](../features/Concepts.md) |
| Full PyCMT2 API | [PyCMT2-Guide.md](PyCMT2-Guide.md) |
| Multi-cycle operations | [MultiCycle.md](../features/MultiCycle.md) |
| Simulation | [Debugging.md](Debugging.md) |
| More examples | [Examples.md](../examples/Examples.md) |

---

## Common Patterns

### Conditional Guard

```python
with m.rule("increment_if_enabled") as rule:
    with rule.guard() as g:
        enabled = g.call(enable_reg, "read")
        g.returns(enabled)  # Only fire when enabled
    with rule.body() as body:
        ...
```

### Method with Arguments

```python
with m.method("add", args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[UInt(32)]) as meth:
    with meth.guard() as g:
        g.always()
    with meth.body() as body:
        result = body.add(body.arg("a"), body.arg("b"))
        body.returns(result)
```

### Conditional Logic

```python
with rule.body() as body:
    val = body.call(count, "read")
    # Mux: if val > 10 then 0 else val + 1
    cond = body.gt(val, body.const(10, 32))
    new_val = body.mux(cond, body.const(0, 32), body.add(val, body.const(1, 32)))
    body.call(count, "write", new_val)
```

---

## Running with Simulation

For simulation with Verilator, see the [gcd.py](../../../examples/PyCMT2/gcd.py) example:

```python
from circt.pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, "sim_output")
ws.build()
success, output = ws.run()
print(output)
```
