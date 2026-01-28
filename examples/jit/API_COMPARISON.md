# CMT2 JIT API Comparison

## Counter Example - Same Hardware, Different APIs

### JIT v1 (Original) - High Boilerplate

```python
import cmt2
from cmt2.pycmt2_integration import CircuitBuilder

@cmt2.elaborate
def counter(width: int = 32):
    # CircuitBuilder duplicates PyCMT2 functionality
    builder = CircuitBuilder("Counter")
    
    with builder.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance_reg(width, "count", clk=clk, rst=rst)
        
        # Verbose nested with statements
        with m.rule("increment") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                # String-based method names
                val = b.call(count, "read")
                b.call(count, "write", b.add(val, b.const(1, width)))
    
    return builder.circuit
```

**Issues:**
- ❌ CircuitBuilder duplicates PyCMT2 Circuit
- ❌ Deep nesting (with inside with inside with...)
- ❌ String method names ("read", "write")
- ❌ High boilerplate

---

### JIT v2 (Agile) - Decorator-Based

```python
import cmt2.jit_v2 as jit
from circt.pycmt2 import Circuit, Reg

@jit.elaborate
def counter(width: int = 32):
    # Native PyCMT2 - no duplication
    circuit = Circuit("Counter")
    
    @jit.module(circuit, "Counter")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        
        # Decorator-based, but still has `def _` boilerplate
        @jit.rule(m, "increment")  # String name required
        def _(rule):
            @rule.guard
            def _(g): g.always()
            @rule.body
            def _(b):
                # Still string-based method calls
                val = b.call(count, "read")
                b.call(count, "write", b.add(val, b.const(1, width)))
    
    return circuit
```

**Improvements:**
- ✅ Native PyCMT2 Circuit - no duplication
- ✅ Decorator-based syntax
- ❌ Still has `def _` boilerplate
- ❌ Still string-based method names

---

### JIT v3 (Zero-Boilerplate) - Recommended

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
        
        # Auto-inferred name from function: "increment"
        @jit.rule(m)
        def increment(guard, body):
            guard.always()
            # Zero boilerplate! Attribute-based method calls
            count.next = count.read + 1
    
    return circuit
```

**Improvements:**
- ✅ Native PyCMT2 Circuit - no duplication
- ✅ No `def _` boilerplate
- ✅ Auto-inferred names from functions
- ✅ Attribute-based methods: `count.read`, `count.next =`
- ✅ Direct method calls: `fifo.enq()`, `fifo.deq()`

---

## Feature Comparison

| Feature | JIT v1 | JIT v2 | JIT v3 |
|---------|--------|--------|--------|
| **Duplication** | High (CircuitBuilder) | Zero | Zero |
| **Boilerplate** | Very High | Medium | Minimal |
| **Rule Definition** | Nested `with` | `@rule` + `def _` | `@rule` function |
| **Name Inference** | ❌ Manual | ❌ Manual | ✅ From function |
| **Method Calls** | `b.call(obj, "method")` | `b.call(obj, "method")` | `obj.method()` |
| **Write Shortcut** | `b.call(obj, "write", val)` | `b.call(obj, "write", val)` | `obj.next = val` |
| **String Methods** | Required | Required | ❌ None! |
| **Dataflow Support** | ❌ | ✅ | ✅ |
| **Readability** | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## Method Call Comparison

### Read Operation

```python
# v1 - String-based
val = b.call(count, "read")

# v2 - String-based
val = b.call(count, "read")

# v3 - Attribute-based (clean!)
val = count.read
```

### Write Operation

```python
# v1 - String-based
b.call(count, "write", new_val)

# v2 - String-based
b.call(count, "write", new_val)

# v3 - Shortcut syntax (clean!)
count.next = new_val
# Or explicit:
count.write(new_val)
```

### FIFO Enqueue

```python
# v1 - String-based
b.call(fifo, "enq", data)

# v2 - String-based
b.call(fifo, "enq", data)

# v3 - Direct method call (clean!)
fifo.enq(data)
```

---

## Rule Definition Comparison

### Simple Rule

```python
# v1 - Nested with statements
with m.rule("increment") as rule:
    with rule.guard() as g:
        g.always()
    with rule.body() as b:
        val = b.call(count, "read")
        b.call(count, "write", b.add(val, b.const(1, 32)))

# v2 - Decorators with boilerplate
@jit.rule(m, "increment")
def _(rule):
    @rule.guard
    def _(g): g.always()
    @rule.body
    def _(b):
        val = b.call(count, "read")
        b.call(count, "write", b.add(val, b.const(1, 32)))

# v3 - Clean function with auto-inferred name
@jit.rule(m)
def increment(guard, body):
    guard.always()
    count.next = count.read + 1
```

### Conditional Rule

```python
# v3 - Clean and readable
@jit.rule(m)
def conditional_update(guard, body):
    guard.equals(enable, body.const(1, 1))
    count.next = count.read + step_size
```

---

## Recommendation

| Use Case | Recommended API |
|----------|----------------|
| New designs | **JIT v3** ✅ |
| Existing v1 code | Migrate to v3 |
| Existing v2 code | Migrate to v3 |
| Learning CMT2 | Start with v3 |

**JIT v3 is the future of CMT2 Python design.**

---

## Migration Guide

### From v1 to v3

1. Replace `CircuitBuilder` with native `Circuit`
2. Replace nested `with` statements with `@jit.rule`
3. Replace string method calls with attribute access
4. Let rule names be inferred from function names

### From v2 to v3

1. Remove `def _` boilerplate
2. Remove explicit rule names (auto-inferred)
3. Replace `b.call(obj, "method")` with `obj.method()`
4. Use `obj.next = value` for writes
