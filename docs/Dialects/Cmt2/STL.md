# Standard Library (STL)

The CMT2 Standard Library provides pre-built hardware components with ready-enable interfaces.

**Related Examples:** `examples/PyCMT2/gcd.py`, `examples/PyCMT2/pipeline_fifo_testbench.py`

---

## Overview

STL components:
- Have predefined interfaces (methods and values)
- Generate synthesizable RTL
- Integrate with simulation infrastructure

---

## Available Components

| Component | Description |
|-----------|-------------|
| `Reg` | Single register with read/write |
| `Wire` | Combinational wire |
| `FIFO` | First-in-first-out buffer |
| `FIFO1Push` | Single-element FIFO (push interface) |
| `FIFO1Pull` | Single-element FIFO (pull interface) |
| `Memory` | RAM module |

---

## Reg (Register)

```python
from circt.pycmt2.stl import Reg

# Create 32-bit register module
reg32 = Reg.create(circuit, 32)

# Instantiate
with circuit.module("MyModule") as m:
    clk, rst = m.clock(), m.reset()
    counter = m.instance(reg32, "counter", clk=clk, rst=rst)
```

### Interface

| Method/Value | Type | Description |
|--------------|------|-------------|
| `read()` | Value | Read current value |
| `write(data)` | Method | Write new value |

### Scheduling

```python
reg.sequence_before("read", "write")  # Read sees old value
```

### Example

```python
with m.rule("increment") as rule:
    with rule.guard() as g:
        g.always()
    with rule.body() as body:
        val = body.call(counter, "read")
        body.call(counter, "write", body.add(val, body.const(1, 32)))
```

---

## FIFO (Buffer)

```python
from circt.pycmt2.stl import FIFO

# Create 32-bit, depth-4 FIFO
fifo = FIFO.create(circuit, width=32, depth=4)

# Instantiate
buffer = m.instance(fifo, "buffer", clk=clk, rst=rst)
```

### Interface

| Method/Value | Type | Description |
|--------------|------|-------------|
| `enqueue(data)` | Method | Add data to FIFO |
| `dequeue()` | Method | Remove and return front |
| `first()` | Value | Peek at front (no remove) |
| `is_empty()` | Value | True if empty |
| `is_full()` | Value | True if full |

### Scheduling

```python
fifo.conflict("enqueue", "dequeue")  # Cannot do both in same cycle
fifo.conflict_free("is_empty", "is_full")  # Can check both
```

### Example

```python
# Producer rule
with m.rule("produce") as rule:
    with rule.guard() as g:
        full = g.call(buffer, "is_full")
        g.returns(g.not_(full))
    with rule.body() as body:
        body.call(buffer, "enqueue", body.call(data_reg, "read"))

# Consumer rule
with m.rule("consume") as rule:
    with rule.guard() as g:
        empty = g.call(buffer, "is_empty")
        g.returns(g.not_(empty))
    with rule.body() as body:
        data = body.call(buffer, "dequeue")
        body.call(result_reg, "write", data)
```

---

## Memory

```python
from circt.pycmt2.stl import Memory

# Create 32-bit data, 256-entry memory
mem = Memory.create(circuit, data_width=32, depth=256)

# Instantiate
ram = m.instance(mem, "ram", clk=clk, rst=rst)
```

### Interface

| Method/Value | Type | Description |
|--------------|------|-------------|
| `read(addr)` | Value | Read from address |
| `write(addr, data)` | Method | Write to address |

### Example

```python
# Read from memory
with m.value("load", args=[("addr", UInt(8))], returns=[UInt(32)]) as val:
    with val.guard() as g:
        g.always()
    with val.body() as body:
        data = body.call(ram, "read", body.arg("addr"))
        body.returns(data)

# Write to memory
with m.method("store", args=[("addr", UInt(8)), ("data", UInt(32))]) as meth:
    with meth.guard() as g:
        g.always()
    with meth.body() as body:
        body.call(ram, "write", body.arg("addr"), body.arg("data"))
```

---

## FIFO1Push / FIFO1Pull

Single-element FIFOs optimized for latency-insensitive design:

```python
from circt.pycmt2.stl import FIFO1Push, FIFO1Pull

# Push-side interface (producer)
push_fifo = FIFO1Push.create(circuit, 32)

# Pull-side interface (consumer)
pull_fifo = FIFO1Pull.create(circuit, 32)
```

### FIFO1Push Interface

| Method/Value | Type | Description |
|--------------|------|-------------|
| `push(data)` | Method | Push data |
| `can_push()` | Value | True if can accept data |

### FIFO1Pull Interface

| Method/Value | Type | Description |
|--------------|------|-------------|
| `pull()` | Method | Pull data |
| `can_pull()` | Value | True if data available |
| `peek()` | Value | Look at data without removing |

---

## Clearing STL Registry

When creating multiple circuits, clear the registry:

```python
from circt.pycmt2.stl import clear_stl_registry

clear_stl_registry()
circuit = Circuit("NewDesign")
```

---

## RTL Files

STL generates RTL files for simulation:

```python
from circt.pycmt2.stl import get_stl_rtl_files

# Get list of generated RTL files
rtl_files = get_stl_rtl_files()
for f in rtl_files:
    print(f)
```

### Integration with SimulationWorkspace

```python
from circt.pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, "sim_output")
# STL RTL files are automatically included
ws.build()
```

---

## Custom External Modules

Create your own reusable modules:

```python
with circuit.external_module("CustomALU") as alu:
    alu.clock("clk")
    alu.reset("rst")

    alu.value("add",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[("sum", UInt(32))])

    alu.value("sub",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[("diff", UInt(32))])

    # With timing
    alu.method("multiply",
               args=[("a", UInt(32)), ("b", UInt(32))],
               returns=[("product", UInt(64))],
               static_latency=3)

    # Scheduling constraints
    alu.conflict_free("add", "sub")
```

---

## Complete Example

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, FIFO, clear_stl_registry

clear_stl_registry()
circuit = Circuit("Pipeline")

reg32 = Reg.create(circuit, 32)
fifo32 = FIFO.create(circuit, 32, depth=4)

with circuit.module("Pipeline") as m:
    clk, rst = m.clock(), m.reset()

    input_fifo = m.instance(fifo32, "input", clk=clk, rst=rst)
    output_fifo = m.instance(fifo32, "output", clk=clk, rst=rst)
    accum = m.instance(reg32, "accum", clk=clk, rst=rst)

    # Rule: process data
    with m.rule("process") as rule:
        with rule.guard() as g:
            in_empty = g.call(input_fifo, "is_empty")
            out_full = g.call(output_fifo, "is_full")
            g.returns(g.and_(g.not_(in_empty), g.not_(out_full)))
        with rule.body() as body:
            data = body.call(input_fifo, "dequeue")
            acc = body.call(accum, "read")
            new_acc = body.add(acc, data)
            body.call(accum, "write", new_acc)
            body.call(output_fifo, "enqueue", new_acc)

    # Method: push input
    with m.method("push", args=[("data", UInt(32))]) as meth:
        with meth.guard() as g:
            full = g.call(input_fifo, "is_full")
            g.returns(g.not_(full))
        with meth.body() as body:
            body.call(input_fifo, "enqueue", body.arg("data"))

    # Method: pop output
    with m.method("pop", returns=[UInt(32)]) as meth:
        with meth.guard() as g:
            empty = g.call(output_fifo, "is_empty")
            g.returns(g.not_(empty))
        with meth.body() as body:
            body.returns(body.call(output_fifo, "dequeue"))
```

---

## Next Steps

- [PyCMT2-Guide.md](PyCMT2-Guide.md) - Full Python API
- [Examples.md](Examples.md) - More examples
- [Debugging.md](Debugging.md) - Simulation and debugging
