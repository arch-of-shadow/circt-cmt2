# PyCMT2 User Guide

PyCMT2 is the Python frontend for CMT2, providing a declarative API for hardware design.

**Related Examples:** `examples/PyCMT2/counter.py`, `examples/PyCMT2/gcd.py`

---

## Installation

After building CIRCT with Python bindings:

```bash
export PYTHONPATH=$CIRCT_BUILD/tools/circt/python_packages/circt_core
```

Import:
```python
from circt.pycmt2 import Circuit, UInt, SInt
from circt.pycmt2.stl import Reg, FIFO, Memory
```

---

## Circuit and Modules

### Creating a Circuit

```python
circuit = Circuit("MyDesign")
```

### Defining Modules

```python
with circuit.module("MyModule") as m:
    # Ports
    clk = m.clock()
    rst = m.reset()

    # Module body...
```

### Module Ports

| Method | Description |
|--------|-------------|
| `m.clock(name="clk")` | Clock input |
| `m.reset(name="rst")` | Reset input |
| `m.input(name, type)` | Custom input port |
| `m.output(name, type)` | Custom output port |

---

## Types

```python
from circt.pycmt2 import UInt, SInt, ClockType, ResetType

UInt(32)         # 32-bit unsigned
SInt(16)         # 16-bit signed
UInt(1)          # Boolean (1-bit)
ClockType()      # Clock
ResetType()      # Synchronous reset
```

---

## Instances

### STL Components

```python
from circt.pycmt2.stl import Reg, FIFO, Memory

# Create module types
reg32 = Reg.create(circuit, 32)
fifo = FIFO.create(circuit, 32, depth=4)
mem = Memory.create(circuit, data_width=32, depth=256)

# Instantiate
with circuit.module("MyModule") as m:
    clk, rst = m.clock(), m.reset()
    counter = m.instance(reg32, "counter", clk=clk, rst=rst)
    buffer = m.instance(fifo, "buffer", clk=clk, rst=rst)
```

### External Modules

```python
with circuit.external_module("CustomALU") as alu:
    alu.clock("clk")
    alu.value("add", args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[("sum", UInt(32))])
    alu.value("sub", args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[("diff", UInt(32))])
```

**Simulation note:** external modules only define *bindings* (port contracts).
If the extern is not backed by ModuleLibrary, you must provide its RTL when
building a simulation workspace:

```python
from circt.pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, "sim")
ws.add_external_rtl("CustomALU.sv", "module CustomALU(...); /* ... */ endmodule")
```

End-to-end reference: `examples/PyCMT2/external_module_custom_rtl.py`.

---

## Rules

Rules are atomic state transitions:

```python
with m.rule("my_rule") as rule:
    with rule.guard() as g:
        # Guard condition (when rule can fire)
        ...
    with rule.body() as body:
        # Actions (atomic)
        ...
```

### Guard Patterns

```python
# Always fire
with rule.guard() as g:
    g.always()

# Never fire (disabled)
with rule.guard() as g:
    g.never()

# Conditional
with rule.guard() as g:
    enabled = g.call(enable_reg, "read")
    g.returns(enabled)

# Compound condition
with rule.guard() as g:
    a = g.call(reg_a, "read")
    b = g.call(reg_b, "read")
    g.returns(g.and_(g.gt(a, b), g.lt(a, g.const(100, 32))))
```

### Body Operations

```python
with rule.body() as body:
    # Read
    val = body.call(reg, "read")

    # Arithmetic
    sum = body.add(val, body.const(1, 32))
    diff = body.sub(val, body.const(1, 32))
    prod = body.mul(val, body.const(2, 32))

    # Bitwise
    masked = body.and_(val, body.const(0xFF, 32))
    combined = body.or_(val, body.const(0x100, 32))
    inverted = body.not_(val)

    # Comparison
    is_zero = body.eq(val, body.const(0, 32))
    is_big = body.gt(val, body.const(100, 32))

    # Conditional (mux)
    result = body.mux(is_zero, body.const(1, 32), sum)

    # Write
    body.call(reg, "write", result)
```

---

## Methods

Methods are callable actions:

```python
with m.method("store", args=[("data", UInt(32))]) as meth:
    with meth.guard() as g:
        full = g.call(fifo, "is_full")
        g.returns(g.not_(full))
    with meth.body() as body:
        body.call(fifo, "enqueue", body.arg("data"))
```

### With Return Values

```python
with m.method("compute",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[UInt(32)]) as meth:
    with meth.guard() as g:
        g.always()
    with meth.body() as body:
        result = body.add(body.arg("a"), body.arg("b"))
        body.returns(result)
```

### With Timing Attributes

```python
with m.method("multiply",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[UInt(64)],
              static_latency=4,   # 4-cycle latency
              interval=2) as meth:  # Can accept new call every 2 cycles
    ...
```

---

## Values

Values are read-only methods:

```python
with m.value("get_count", returns=[UInt(32)]) as val:
    with val.guard() as g:
        g.always()
    with val.body() as body:
        body.returns(body.call(counter, "read"))
```

---

## Output Generation

### MLIR

```python
mlir = circuit.emit_mlir()
print(mlir)
```

### FIRRTL

```python
firrtl = circuit.emit_firrtl()
print(firrtl)
```

### Verilog

```python
verilog = circuit.emit_verilog()
print(verilog)
```

---

## Simulation

### SimulationWorkspace

```python
from circt.pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, "sim_output")
ws.build()
success, output = ws.run()
print(output)
```

### Custom Testbench

```python
TESTBENCH_CPP = '''
#include "VMyModule.h"
#include "verilated.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    auto dut = new VMyModule();

    dut->clk = 0;
    dut->rst = 1;

    // Reset
    for (int i = 0; i < 5; i++) {
        dut->clk = !dut->clk;
        dut->eval();
    }
    dut->rst = 0;

    // Run
    for (int i = 0; i < 100; i++) {
        dut->clk = !dut->clk;
        dut->eval();
    }

    delete dut;
    return 0;
}
'''

tb_file = ws.workspace_dir / "tb" / "testbench.cpp"
tb_file.write_text(TESTBENCH_CPP)
```

---

## Complete Example: GCD

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry

clear_stl_registry()
circuit = Circuit("GCDCircuit")
reg32 = Reg.create(circuit, 32)

with circuit.module("GCD") as m:
    clk, rst = m.clock(), m.reset()

    reg_a = m.instance(reg32, "reg_a", clk=clk, rst=rst)
    reg_b = m.instance(reg32, "reg_b", clk=clk, rst=rst)

    # Rule: subtract smaller from larger
    with m.rule("step") as rule:
        with rule.guard() as g:
            a = g.call(reg_a, "read")
            b = g.call(reg_b, "read")
            g.returns(g.and_(g.neq(a, b), g.neq(b, g.const(0, 32))))
        with rule.body() as body:
            a = body.call(reg_a, "read")
            b = body.call(reg_b, "read")
            a_gt_b = body.gt(a, b)
            new_a = body.mux(a_gt_b, body.sub(a, b), a)
            new_b = body.mux(a_gt_b, b, body.sub(b, a))
            body.call(reg_a, "write", new_a)
            body.call(reg_b, "write", new_b)

    # Method: load initial values
    with m.method("load", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
        with meth.guard() as g:
            g.always()
        with meth.body() as body:
            body.call(reg_a, "write", body.arg("a"))
            body.call(reg_b, "write", body.arg("b"))

    # Value: read result
    with m.value("result", returns=[UInt(32)]) as val:
        with val.guard() as g:
            a = g.call(reg_a, "read")
            b = g.call(reg_b, "read")
            g.returns(g.eq(a, b))  # Ready when a == b
        with val.body() as body:
            body.returns(body.call(reg_a, "read"))
```

**Full example:** `examples/PyCMT2/gcd.py`

---

## API Reference

### BodyBuilder Operations

| Operation | Description |
|-----------|-------------|
| `const(value, width)` | Create constant |
| `arg(name)` | Access method argument |
| `call(inst, method, *args)` | Call method/value |
| `add(a, b)` | Addition |
| `sub(a, b)` | Subtraction |
| `mul(a, b)` | Multiplication |
| `and_(a, b)` | Bitwise AND |
| `or_(a, b)` | Bitwise OR |
| `xor_(a, b)` | Bitwise XOR |
| `not_(a)` | Bitwise NOT |
| `eq(a, b)` | Equal |
| `neq(a, b)` | Not equal |
| `lt(a, b)` | Less than |
| `le(a, b)` | Less or equal |
| `gt(a, b)` | Greater than |
| `ge(a, b)` | Greater or equal |
| `mux(cond, t, f)` | Conditional select |
| `shl(a, n)` | Shift left |
| `shr(a, n)` | Shift right |
| `bits(a, hi, lo)` | Bit extraction |
| `cat(a, b)` | Concatenation |
| `returns(*vals)` | Return values |

---

## Next Steps

- [MultiCycle.md](../features/MultiCycle.md) - Multi-cycle operations and timing
- [STL.md](../features/STL.md) - Standard library components
- [Examples.md](../examples/Examples.md) - More examples
