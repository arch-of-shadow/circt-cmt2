# JIT v3: Clean API with Clear Guard/Body Separation

JIT v3 provides a Pythonic API for hardware design that balances clarity and conciseness.

## Design Principles

1. **Clear guard/body separation**: Use `with r.guard:` and `with r.body:`
2. **No boilerplate**: No `def _:` nesting required
3. **Auto-inferred names**: Rule/method names from function definitions
4. **No string methods**: Attribute-based access (`count.read` not `b.call(count, "read")`)

## API Overview

### Module Context

```python
with jit.module(circuit, "Counter") as m:
    clk = m.clock()
    rst = m.reset()
    count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
```

### Rules

```python
@jit.rule(m)  # Name: "increment" (auto-inferred)
def increment(r):
    with r.guard:
        r.always()
    
    with r.body:
        count.next = count.read + 1
```

### Value Methods

```python
@jit.value(m, returns=[UInt(32)])  # Name: "get_count"
def get_count(r):
    with r.guard:
        r.always()
    
    with r.body:
        r.returns(count.read)
```

### Action Methods

```python
@jit.method(m, args=[("data", UInt(32))])  # Name: "write"
def write(r, data):
    with r.guard:
        r.always()
    
    with r.body:
        reg.write(data)
```

## Complete Example

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
        
        @jit.rule(m)
        def increment(r):
            with r.guard:
                r.always()
            
            with r.body:
                count.next = count.read + 1
        
        @jit.value(m, returns=[UInt(width)])
        def get_count(r):
            with r.guard:
                r.always()
            
            with r.body:
                r.returns(count.read)
    
    return circuit
```

## Comparison with v2

### v2 (Old)
```python
@jit.rule(m, "increment")  # String name
def _(rule):               # def _ boilerplate
    @rule.guard
    def _(g):              # def _ boilerplate
        g.always()
    @rule.body
    def _(b):              # def _ boilerplate
        val = b.call(count, "read")  # String!
        b.call(count, "write", val + 1)  # String!
```

### v3 (New)
```python
@jit.rule(m)  # Name inferred
def increment(r):
    with r.guard:
        r.always()
    
    with r.body:
        count.next = count.read + 1  # Attribute access
```

## Method Reference System

Instances are automatically wrapped with `SignalRef` to enable attribute access:

```python
count = m.instance(Reg.create(circuit, width), "count", ...)

# These all work:
count.read           # Read method
count.write(value)   # Write method
count.next = value   # Shortcut for write
```

## Name Inference

Names are automatically inferred from function definitions:

```python
@jit.rule(m)
def increment(r): ...  # Name: "increment"

@jit.rule(m, name="custom_name")  # Override
def my_rule(r): ...  # Name: "custom_name"
```

## Available Operations

### Guard Operations (inside `with r.guard:`)
- `r.always()` - Guard always fires
- `r.equals(a, b)` - Guard: a == b
- `r.const(value, width)` - Create constant

### Body Operations (inside `with r.body:`)
- `count.next = value` - Write to register
- `count.read` - Read from register
- `r.returns(value)` - Return value from method
- `r.const(value, width)` - Create constant

## Why This Design?

The v3 API addresses the key issues:

1. **v2 had too much boilerplate**: 3 levels of `def _:` nesting
2. **v3-mixed was unclear**: Guard/body operations mixed together
3. **v3-clean (this) balances both**:
   - Clear separation with `with r.guard:` / `with r.body:`
   - No `def _:` boilerplate
   - Intuitive and Pythonic
