# CMT2 Hardware Control Flow

**Status:** Implementation Ready  
**Last Updated:** 2026-01-28  
**Related:** [CMT2 JIT Implementation Plan](./Implementation-Plan.md)

---

## Overview

This document describes CMT2's hardware control flow constructs that address the fundamental distinction between **trace-time** (Python execution during elaboration) and **hardware-time** (runtime circuit behavior).

### The Problem

Hardware designers using Python-based HDLs often confuse Python's control flow with hardware control flow:

```python
@cmt2.elaborate
def design(data: UInt(8)):
    # WRONG: This won't create hardware!
    if data > 10:  # Python if - evaluated at trace time!
        result = data + 1
    else:
        result = data - 1
    
    # At elaboration time, 'data' is a tracer object, not a value.
    # 'data > 10' returns a tracer, which is truthy.
    # So only the 'if' branch is executed, the 'else' is never traced!
```

### The Solution

CMT2 provides explicit hardware control flow constructs:

| Python (Trace-Time) | CMT2 (Hardware-Time) | Hardware Generated |
|---------------------|----------------------|-------------------|
| `if condition:` | `with when(condition):` | Multiplexer |
| `if/else` | `with when(): / with otherwise():` | 2:1 Mux |
| `match/case` | `with switch(expr): / with case():` | Mux Tree |
| `for i in range(n):` | `for i in unroll(range(n)):` | N parallel instances |

---

## Trace-Time vs Hardware-Time

### Trace-Time (Elaboration)

Python code executes during elaboration to build the circuit structure:

```python
@cmt2.elaborate
def parameterized_design(width: int, use_fifo: bool):
    circuit = Circuit("Design")
    
    # Trace-time: Python if executes at elaboration
    # This decides which components to instantiate
    if use_fifo:  # ✓ Correct use of Python if
        fifo = FIFO.create(circuit, width)
    else:
        buffer = Wire.create(circuit, width)
    
    # ... instantiate components based on use_fifo ...
    
    return circuit

# Create two different circuits
design_with_fifo = parameterized_design(32, use_fifo=True)
design_without_fifo = parameterized_design(32, use_fifo=False)
```

### Hardware-Time (Runtime)

Hardware control flow creates circuits that make decisions at runtime:

```python
@cmt2.elaborate
def design():
    circuit = Circuit("Design")
    
    with circuit.module("Top") as m:
        clk, rst = m.clock(), m.reset()
        reg = m.instance(Reg.create(circuit, 32), "reg", clk=clk, rst=rst)
        
        with m.rule("update") as r:
            with r.body() as body:
                val = body.call(reg, "read")
                
                # Hardware-time: Creates a multiplexer!
                with cmt2.when(val == 0):  # ✓ Correct use of when
                    body.call(reg, "write", body.const(1, 32))
                with cmt2.otherwise():
                    body.call(reg, "write", body.add(val, body.const(1, 32)))
    
    return circuit
```

**Generated Hardware:**
```verilog
// Conceptual Verilog for when/otherwise
always @(posedge clk) begin
    if (val == 0)  // Hardware comparison
        reg <= 1;
    else
        reg <= val + 1;
end
```

---

## Hardware Conditionals: `when` / `otherwise`

### Basic Usage

```python
from cmt2 import when, otherwise

with cmt2.when(condition):
    # Assignments here are conditionally executed
    result = true_value

with cmt2.otherwise():
    # Assignments here execute when condition is false
    result = false_value
```

### Example: Counter with Reset

```python
@cmt2.elaborate
def counter_with_reset():
    circuit = Circuit("Counter")
    
    with circuit.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, 32), "count", clk=clk, rst=rst)
        reset_signal = m.input("reset", UInt(1))
        
        with m.rule("update") as r:
            with r.body() as body:
                current = body.call(count, "read")
                
                with cmt2.when(reset_signal):
                    # Reset to 0
                    body.call(count, "write", body.const(0, 32))
                with cmt2.otherwise():
                    # Increment
                    next_val = body.add(current, body.const(1, 32))
                    body.call(count, "write", next_val)
    
    return circuit
```

### Chained Conditionals

Use multiple `when()` blocks for if-elif-else chains:

```python
with cmt2.when(opcode == 0):   # if
    result = a + b

with cmt2.when(opcode == 1):   # elif
    result = a - b

with cmt2.when(opcode == 2):   # elif
    result = a * b

with cmt2.otherwise():         # else
    result = 0
```

**Note:** For multiple cases on the same expression, use `switch` instead.

---

## Hardware Switch: `switch` / `case`

### Basic Usage

The `switch` statement creates an efficient multiplexer tree for multi-way branching:

```python
from cmt2 import switch

with cmt2.switch(expression) as sw:
    with sw.case(0):
        result = a + b
    with sw.case(1):
        result = a - b
    with sw.case(2):
        result = a * b
    with sw.default():
        result = 0
```

### Example: ALU

```python
@cmt2.elaborate
def alu(width: int):
    circuit = Circuit("ALU")
    
    with circuit.module("ALU") as m:
        a = m.input("a", UInt(width))
        b = m.input("b", UInt(width))
        opcode = m.input("opcode", UInt(3))
        result = m.output("result", UInt(width))
        
        with m.rule("compute") as r:
            with r.body() as body:
                with cmt2.switch(opcode) as sw:
                    with sw.case(0):  # ADD
                        body.assign(result, body.add(a, b))
                    with sw.case(1):  # SUB
                        body.assign(result, body.sub(a, b))
                    with sw.case(2):  # AND
                        body.assign(result, body.and_(a, b))
                    with sw.case(3):  # OR
                        body.assign(result, body.or_(a, b))
                    with sw.case(4):  # XOR
                        body.assign(result, body.xor_(a, b))
                    with sw.default():  # Default to 0
                        body.assign(result, body.const(0, width))
    
    return circuit
```

### Switch vs Nested When

Use `switch` instead of nested `when` for:
- **Readability**: Clear intent of multi-way selection
- **Efficiency**: Better hardware generation (single mux tree vs. nested muxes)
- **Exhaustiveness**: `default` case ensures all values are handled

```python
# Less efficient: Nested when creates priority encoder
with cmt2.when(opcode == 0):
    result = a
with cmt2.otherwise():
    with cmt2.when(opcode == 1):
        result = b
    with cmt2.otherwise():
        with cmt2.when(opcode == 2):
            result = c
        with cmt2.otherwise():
            result = d  # Default

# More efficient: switch creates balanced mux tree
with cmt2.switch(opcode) as sw:
    with sw.case(0): result = a
    with sw.case(1): result = b
    with sw.case(2): result = c
    with sw.default(): result = d
```

---

## Unrolled Loops: `unroll`

### Basic Usage

The `unroll` function creates parallel hardware instances:

```python
from cmt2 import unroll

# Creates 4 parallel multipliers
for i in cmt2.unroll(range(4)):
    products[i] = inputs[i] * coefficients[i]
```

### Example: FIR Filter

```python
@cmt2.elaborate
def fir_filter(width: int, taps: int):
    """FIR filter with parallel multiply-accumulate."""
    circuit = Circuit("FIRFilter")
    
    # Coefficients (known at elaboration)
    coefficients = [0.1, 0.15, 0.5, 0.15, 0.1]
    
    with circuit.module("FIR") as m:
        clk, rst = m.clock(), m.reset()
        
        # Create tap registers
        tap_regs = []
        for i in cmt2.unroll(range(taps)):  # Unroll: parallel hardware
            reg = m.instance(Reg.create(circuit, width), f"tap{i}", clk=clk, rst=rst)
            tap_regs.append(reg)
        
        # Shift input through taps
        with m.rule("shift") as r:
            with r.guard() as g:
                g.always()
            with r.body() as body:
                for i in cmt2.unroll(range(taps - 1, 0, -1)):
                    prev_val = body.call(tap_regs[i - 1], "read")
                    body.call(tap_regs[i], "write", prev_val)
        
        # Compute weighted sum (parallel)
        with m.rule("compute") as r:
            with r.guard() as g:
                g.always()
            with r.body() as body:
                # Multiply each tap by its coefficient (parallel)
                products = []
                for i in cmt2.unroll(range(taps)):
                    tap_val = body.call(tap_regs[i], "read")
                    coeff = body.const(int(coefficients[i] * 256), width)
                    products.append(body.mul(tap_val, coeff))
                
                # Sum all products (tree structure)
                result = products[0]
                for i in cmt2.unroll(range(1, taps)):
                    result = body.add(result, products[i])
    
    return circuit
```

### Limitations

- **Range only**: `unroll` only accepts `range` objects (bounds must be known at elaboration)
- **Size limit**: Maximum 128 iterations (raises `ValueError` if exceeded)
- **Warning**: Warns if >16 iterations (potential area explosion)

```python
# ✓ Valid: range with constant bounds
for i in cmt2.unroll(range(8)):
    pass

# ✗ Invalid: list (bounds not statically known)
items = [1, 2, 3, 4]
for i in cmt2.unroll(items):  # TypeError!
    pass

# ✗ Invalid: dynamic range
n = some_runtime_value()
for i in cmt2.unroll(range(n)):  # Can't determine n at elaboration
    pass
```

### When to Use Unroll vs Sequential Loops

| Use Case | Construct | Hardware |
|----------|-----------|----------|
| Parallel data processing | `unroll` | N parallel units |
| Fixed number of stages | `unroll` | Pipelined or parallel |
| Variable iteration count | Sequential logic | FSM with counter |
| Large N (>16) | Sequential logic | Single unit, time-multiplexed |

---

## Warnings and Best Practices

### Warnings

The control flow module emits warnings for potentially confusing patterns:

```python
# Warns: Large unroll creates significant hardware
for i in cmt2.unroll(range(100)):  # Warning: Large unroll
    pass

# Error: Range too large
for i in cmt2.unroll(range(200)):  # ValueError: Range too large
    pass
```

### Best Practices

1. **Use Python `if` for elaboration decisions:**
   ```python
   # ✓ Use Python if for static configuration
   if use_fifo:
       buffer = FIFO.create(circuit, width)
   else:
       buffer = Wire.create(circuit, width)
   ```

2. **Use `when` for hardware decisions:**
   ```python
   # ✓ Use when for runtime decisions
   with cmt2.when(buffer_full):
       stall_pipeline()
   ```

3. **Use `switch` for multiple cases:**
   ```python
   # ✓ Use switch for multi-way selection
   with cmt2.switch(opcode) as sw:
       with sw.case(0): result = a + b
       with sw.case(1): result = a - b
       with sw.default(): result = 0
   ```

4. **Use `unroll` for small parallel loops:**
   ```python
   # ✓ Use unroll for parallel hardware (small N)
   for i in cmt2.unroll(range(4)):
       products[i] = inputs[i] * coeffs[i]
   ```

5. **Don't confuse the two:**
   ```python
   # ✗ WRONG: Python if with hardware condition
   if signal > 0:  # Always true during tracing!
       result = a
   else:
       result = b  # Never executed!
   
   # ✓ CORRECT: when with hardware condition
   with cmt2.when(signal > 0):
       result = a
   with cmt2.otherwise():
       result = b
   ```

---

## API Reference

### when(condition)

Create a hardware conditional block.

**Parameters:**
- `condition` (Signal): 1-bit boolean signal controlling the mux

**Returns:** WhenContext for use in with statement

**Example:**
```python
with cmt2.when(enable):
    reg.next = data
```

### otherwise()

Create an else branch for a when statement.

**Returns:** OtherwiseContext for use in with statement

**Raises:** RuntimeError if not following a when() block

**Example:**
```python
with cmt2.when(enable):
    reg.next = data
with cmt2.otherwise():
    reg.next = reg
```

### switch(expression)

Create a hardware switch statement.

**Parameters:**
- `expression` (Signal): Signal to switch on

**Returns:** SwitchContext for use in with statement

**Example:**
```python
with cmt2.switch(opcode) as sw:
    with sw.case(0): result = a
    with sw.case(1): result = b
    with sw.default(): result = c
```

### unroll(range)

Unroll a loop at trace time.

**Parameters:**
- `range` (range): Range object to unroll

**Returns:** Iterator yielding each value

**Raises:**
- TypeError: If not a range
- ValueError: If range > 128 elements

**Example:**
```python
for i in cmt2.unroll(range(4)):
    outputs[i] = process(inputs[i])
```

### Utility Functions

```python
# Check if inside when/switch block
if cmt2.control_flow.is_inside_when():
    pass

if cmt2.control_flow.is_inside_switch():
    pass

# Get active when condition
condition = cmt2.control_flow.get_active_condition()
```

---

## Integration with Circuit/Rule API

The control flow constructs integrate with the existing Circuit/Rule API:

```python
from circt.pycmt2 import Circuit, UInt
from cmt2 import when, otherwise, switch, unroll
from cmt2.stl import Reg

circuit = Circuit("Example")

with circuit.module("Top") as m:
    clk, rst = m.clock(), m.reset()
    reg = m.instance(Reg.create(circuit, 32), "reg", clk=clk, rst=rst)
    mode = m.input("mode", UInt(2))
    
    # Value method with switch
    with m.value("read", returns=[UInt(32)]) as val:
        with val.guard() as g:
            g.always()
        with val.body() as body:
            raw = body.call(reg, "read")
            
            # Apply mode-dependent transformation
            with cmt2.switch(mode) as sw:
                with sw.case(0):  # Raw
                    body.returns(raw)
                with sw.case(1):  # Inverted
                    body.returns(body.not_(raw))
                with sw.default():  # Zero
                    body.returns(body.const(0, 32))
```

---

## Common Pitfalls

### Pitfall 1: Truthy Tracers

```python
# WRONG: 'signal' is always truthy (it's a tracer object)
if signal:  # Always True!
    result = a
else:
    result = b  # Never executed!
```

### Pitfall 2: Confusing For Loops

```python
# WRONG: This creates sequential hardware (if it worked)
for i in range(4):  # Python for at trace time
    pass

# CORRECT: This creates parallel hardware
for i in cmt2.unroll(range(4)):
    pass
```

### Pitfall 3: Dynamic Bounds

```python
# WRONG: n is not known at elaboration
def design(n: int):
    for i in cmt2.unroll(range(n)):  # Error!
        pass

# CORRECT: Use static_argnums for elaboration parameters
from typing import Annotated
import cmt2

@cmt2.elaborate
def design(n: Annotated[int, cmt2.static]):
    for i in cmt2.unroll(range(n)):  # OK: n known at elaboration
        pass
```

---

## Future Enhancements

### Potential Additions

1. **`while_loop`**: Bounded hardware loops with FSM generation
2. **`for_seq`**: Sequential (not unrolled) iteration
3. **`priority_encoder`**: Explicit priority selection
4. **`onehot`**: One-hot encoding for state machines

### AST Transform Alternative

For users needing Python control flow to become hardware control flow:

```python
from cmt2 import ast_transform

@ast_transform
def design(data: UInt(8)):
    # Python control flow IS hardware control flow
    if data > 10:  # This becomes a mux!
        result = data + 1
    else:
        result = data - 1
    return result
```

**Trade-offs:**
- ✅ Python control flow becomes hardware
- ✅ Better error messages
- ❌ Can't use dynamic Python features
- ❌ More complex implementation

---

## References

- [Implementation Plan](./Implementation-Plan.md) - Overall JIT implementation
- [PL Expert Review](./review-pl-expert.md) - Detailed control flow analysis
- [PyCMT2 Guide](../guides/PyCMT2-Guide.md) - General PyCMT2 documentation
