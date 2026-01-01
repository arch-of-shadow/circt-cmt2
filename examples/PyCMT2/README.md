# PyCMT2 Examples

This directory contains examples demonstrating the PyCMT2 Python EDSL for CMT2 hardware design.

## Prerequisites

Build CIRCT with Python bindings enabled:

```bash
cd circt-cmt2/build
ninja
```

## Running Examples

### Option 1: Run Individual Examples

```bash
cd circt-cmt2/build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/counter_example.py
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/proc_example.py
```

### Option 2: Run All Examples

```bash
cd circt-cmt2/build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/run_examples.py
```

## Examples

### counter_example.py

A simple counter module demonstrating:
- Circuit and module creation with JIT naming
- Clock and reset ports
- Input ports with custom types
- Rules with guards and bodies
- Value methods for reading state

### proc_example.py

A multi-cycle ALU demonstrating procedural control:
- Groups for atomic operations
- Static latency groups
- Procedural rules with control flow
- Sequential control composition

## API Overview

### Circuit

```python
from circt.pycmt2 import Circuit

# Create circuit (name inferred from variable or explicit)
circuit = Circuit("MyCircuit")

# Create modules
with circuit.module("Counter") as mod:
    clk = mod.clock("clk")
    rst = mod.reset("rst")
    # ...

# Emit MLIR IR
print(circuit.emit_mlir())
```

### Types

```python
from circt.pycmt2 import UInt, SInt, Clock, Reset, Bundle, Vector

# Integer types
data = UInt(32)      # 32-bit unsigned
signed = SInt(16)    # 16-bit signed

# Clock and reset
clock = Clock        # Clock type
reset = Reset        # Reset type (1-bit uint)

# Composite types
bundle = Bundle([("valid", UInt(1)), ("data", UInt(32))])
vector = Vector(UInt(8), 4)  # 4-element vector of UInt(8)
```

### Rules

```python
with mod.rule("increment") as rule:
    with rule.guard() as g:
        g.always()  # or g.returns(condition)
    with rule.body() as b:
        # Perform actions
        pass
```

### Methods and Values

```python
# Action method (can modify state)
with mod.method("write", args=[("data", UInt(32))]) as meth:
    with meth.guard() as g:
        g.always()
    with meth.body() as b:
        data = b.arg("data")
        # ...

# Value method (read-only)
with mod.value("read", returns=[UInt(32)]) as val:
    with val.guard() as g:
        g.always()
    with val.body() as b:
        result = b.const(0, 32)
        b.returns(result)
```

### Procedural Control

```python
# Step (go-done interface)
with mod.step("load") as grp:
    # ... operations
    grp.done(condition)

# Static latency group
with mod.static_step(4, "compute") as sgrp:
    # ... 4-cycle operation
    pass

# Procedural rule with control
with mod.proc_rule("execute") as rule:
    with rule.guard() as g:
        g.always()
    with rule.control() as ctrl:
        with ctrl.seq():
            ctrl.enable(load.ref())
            ctrl.enable(compute.ref())
```
