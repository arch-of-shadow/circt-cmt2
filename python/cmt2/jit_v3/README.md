# CMT2 JIT v3 - Zero-Boilerplate API

## Overview

JIT v3 is the **recommended** API for CMT2 hardware design. It provides:

- ✅ **Zero duplication** - Uses native PyCMT2 Circuit
- ✅ **Zero boilerplate** - No `def _` tokens
- ✅ **No strings** - Attribute-based method calls
- ✅ **Auto-inferred names** - From function definitions
- ✅ **Clean syntax** - Pythonic hardware design

## Quick Start

```python
import cmt2.jit_v3 as jit
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg

@jit.elaborate
def counter(width: int = 32):
    circuit = Circuit("Counter")
    
    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        
        # Rule name auto-inferred as "increment"
        @jit.rule(m)
        def increment(guard, body):
            guard.always()
            count.next = count.read + 1  # Clean attribute access!
    
    return circuit

# Run it
circuit = counter(width=16)
print(circuit.emit_mlir())
```

## Key Features

### 1. Auto-Inferred Names

```python
@jit.rule(m)  # Name is "increment" (from function name)
def increment(guard, body):
    ...

@jit.rule(m, name="custom")  # Explicit override
def my_rule(guard, body):
    ...
```

### 2. Attribute-Based Method Calls

```python
# Old way (strings)
b.call(count, "read")
b.call(count, "write", value)
b.call(fifo, "enq", data)

# New way (attributes)
count.read           # Read method
count.write(value)   # Write method
count.next = value   # Shortcut for write
fifo.enq(data)       # Direct method call
fifo.deq()           # Direct method call
fifo.notFull         # Status method
```

### 3. Clean Rule Definition

```python
@jit.rule(m)
def increment(guard, body):
    guard.always()
    count.next = count.read + 1
```

Instead of:
```python
@jit.rule(m, "increment")
def _(rule):
    @rule.guard
    def _(g): g.always()
    @rule.body
    def _(b):
        val = b.call(count, "read")
        b.call(count, "write", b.add(val, b.const(1, 32)))
```

## API Reference

### Core Decorators

- `@jit.elaborate` - Circuit elaboration
- `@jit.simulate` - Simulation runner
- `@jit.rule(module)` - Rule definition (auto-named)
- `@jit.method(module, args, returns)` - Action method
- `@jit.value(module, returns)` - Value method

### Module Context

- `with jit.module(circuit, name) as m` - Module definition

### Instance Creation

- `m.instance(module, name, **kwargs)` - Creates instance wrapped with SignalRef

### Method Access

All instances support attribute-based method access:

- `instance.read` - Read method reference
- `instance.write(value)` - Write method call
- `instance.next = value` - Write shortcut
- `instance.enq(data)` - Enqueue (for FIFOs)
- `instance.deq()` - Dequeue (for FIFOs)
- `instance.notFull` - Status check
- `instance.notEmpty` - Status check

## Examples

### Counter with Enable

```python
@jit.elaborate
def counter(width: int = 32):
    circuit = Circuit("Counter")
    
    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        enable = m.input("enable", UInt(1))
        
        @jit.rule(m)
        def increment(guard, body):
            guard.equals(enable, body.const(1, 1))
            count.next = count.read + 1
        
        @jit.value(m, returns=[UInt(width)])
        def get_count(guard, body):
            guard.always()
            body.returns(count.read)
    
    return circuit
```

### FIFO

```python
@jit.elaborate
def fifo(data_width: int = 32, depth: int = 8):
    from circt.pycmt2.stl import FIFO
    
    circuit = Circuit("FIFO")
    
    with jit.module(circuit, "FIFO") as m:
        clk = m.clock()
        rst = m.reset()
        fifo = m.instance(FIFO.create(circuit, UInt(data_width), depth), "fifo", clk=clk, rst=rst)
        
        @jit.rule(m)
        def do_enqueue(guard, body):
            guard.equals(fifo.notFull, body.const(1, 1))
            data_in = m.input("data_in", UInt(data_width))
            fifo.enq(data_in)
        
        @jit.rule(m)
        def do_dequeue(guard, body):
            guard.equals(fifo.notEmpty, body.const(1, 1))
            fifo.deq()
        
        @jit.value(m, returns=[UInt(32)])
        def get_count(guard, body):
            guard.always()
            body.returns(fifo.count)
    
    return circuit
```

## Comparison with Previous Versions

| Aspect | JIT v1 | JIT v2 | JIT v3 |
|--------|--------|--------|--------|
| Duplication | High | Zero | Zero |
| Boilerplate | Very High | Medium | Minimal |
| Strings | Required | Required | ❌ None |
| Names | Manual | Manual | Auto-inferred |
| Read | `b.call(c, "read")` | `b.call(c, "read")` | `c.read` |
| Write | `b.call(c, "write", v)` | `b.call(c, "write", v)` | `c.next = v` |

## Migration

### From v1/v2 to v3

1. Replace `CircuitBuilder` with native `Circuit`
2. Replace nested `with` or `@module` with `with jit.module()`
3. Remove explicit rule names (auto-inferred)
4. Replace `b.call(obj, "method")` with `obj.method()`
5. Use `obj.next = value` for writes

## Files

- `__init__.py` - Module exports
- `_method_ref.py` - Method reference system for attribute access
- `_ast_decorators.py` - AST-based decorators with auto-inference
- `_module.py` - Module context manager with SignalRef wrapping

## Status

✅ **STABLE** - Recommended for new designs
