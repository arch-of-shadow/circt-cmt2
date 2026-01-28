# CMT2 Type System

**Status:** Implemented  
**Version:** 1.0  
**Last Updated:** 2026-01-28

---

## Overview

The CMT2 type system defines hardware signal types with explicit bit widths and signedness. This addresses the [PL expert concern](../review-pl-expert.md) about underspecified type inference in hardware DSLs.

## Core Types

### SignalType (Base Class)

All hardware types inherit from `SignalType`, which provides:
- `width: int` - The bit width (always positive)
- `is_signed: bool` - Signedness property
- `is_unsigned: bool` - Convenience property (inverse of is_signed)

### UInt(width)

Unsigned integer type. Values range from `0` to `2^width - 1`.

```python
from cmt2.types import UInt

# Create an 8-bit unsigned integer type
t = UInt(8)
assert t.width == 8
assert not t.is_signed

# Alternative syntax (Python 3.9+)
t = UInt[8]
```

**Constraints:**
- Width must be positive (`width > 0`)

### SInt(width)

Signed two's-complement integer type. Values range from `-2^(width-1)` to `2^(width-1) - 1`.

```python
from cmt2.types import SInt

# Create an 8-bit signed integer type
t = SInt(8)
assert t.width == 8
assert t.is_signed

# Alternative syntax
t = SInt[8]
```

**Constraints:**
- Width must be at least 2 (`width >= 2`) to accommodate sign bit and at least one value bit

### Bits(width)

Untyped bit vector. Used for raw bits without numeric interpretation (e.g., control signals, protocol data).

```python
from cmt2.types import Bits

# Create an 8-bit untyped vector
t = Bits(8)
assert t.width == 8
assert not t.is_signed

# Alternative syntax
t = Bits[8]
```

**Constraints:**
- Width must be positive (`width > 0`)

## Type Promotion Rules

Type promotion determines the result type when combining signals of different types. CMT2 follows hardware design conventions with explicit, predictable rules.

### Width Inference

| Operation | Width Formula | Example |
|-----------|---------------|---------|
| `add`, `sub` | `max(w1, w2) + 1` | `UInt(8) + UInt(16)` → `UInt(17)` |
| `mul` | `w1 + w2` | `UInt(8) * UInt(8)` → `UInt(16)` |
| `div`, `mod` | `w1` (dividend) | `UInt(16) / UInt(8)` → `UInt(16)` |
| `bitwise` | `max(w1, w2)` | `UInt(8) & UInt(16)` → `UInt(16)` |
| `concat` | `w1 + w2` | `UInt(8).concat(UInt(16))` → `UInt(24)` |
| `lshift` | `w1 + 2^min(w2,6) - 1` | `UInt(8) << UInt(4)` → large width |
| `rshift` | `w1` | `UInt(16) >> UInt(2)` → `UInt(16)` |
| `compare` | `1` | `UInt(8) < UInt(16)` → `Bits(1)` |
| `neg` | `w + 1` | `-UInt(8)` → `SInt(9)` |
| `invert` | `w` | `~UInt(8)` → `UInt(8)` |
| `abs` | `w` | `abs(SInt(8))` → `UInt(8)` |

### Signedness Rules

| Operation | Result Signedness |
|-----------|-------------------|
| Arithmetic (`+`, `-`, `*`, `/`, `%`) | Signed if **any** operand is signed |
| Bitwise (`&`, `\|`, `^`) | Signed only if **both** operands are signed |
| Comparison (`<`, `<=`, `>`, `>=`, `==`, `!=`) | Always unsigned (`Bits(1)`) |
| Shift (`<<`, `>>`) | Same as the value being shifted |
| Concatenation | Always unsigned |
| Negation (`-`) | Always signed |
| Absolute value (`abs`) | Always unsigned |

### Promotion Examples

```python
from cmt2.types import UInt, SInt, Bits

# Example 1: Same type addition
result = UInt(8) + UInt(16)
# Result: UInt(17) - max(8, 16) + 1 = 17

# Example 2: Mixed signedness
result = SInt(8) + UInt(8)
# Result: SInt(9) - signed because SInt is signed, max(8,8)+1=9

# Example 3: Multiplication
result = UInt(8) * UInt(8)
# Result: UInt(16) - 8 + 8 = 16

# Example 4: Bitwise operations
result = SInt(8) & UInt(8)
# Result: UInt(8) - unsigned because not both are signed

result = SInt(8) & SInt(16)
# Result: SInt(16) - signed because both are signed

# Example 5: Comparisons
result = UInt(8) < UInt(16)
# Result: Bits(1) - comparisons always return 1-bit

# Example 6: Negation
result = -UInt(8)
# Result: SInt(9) - negation always produces signed, width+1

# Example 7: Concatenation
result = UInt(8).concat(UInt(16))
# Result: UInt(24) - 8 + 16 = 24, always unsigned
```

## Why These Rules?

### Addition Width: `max(w1, w2) + 1`

Hardware addition can overflow. For example, adding two 8-bit unsigned values:
- Maximum value: `255 + 255 = 510`
- 510 requires 9 bits (`2^9 - 1 = 511`)
- So `UInt(8) + UInt(8)` → `UInt(9)`

This prevents silent overflow bugs common in hardware design.

### Multiplication Width: `w1 + w2`

Multiplication produces a result that may need all bits:
- `255 * 255 = 65025`
- Requires 16 bits (`2^16 - 1 = 65535`)
- So `UInt(8) * UInt(8)` → `UInt(16)`

This provides full precision without overflow.

### Signedness: Signed if Any Operand is Signed

When mixing signed and unsigned operands, the result is signed to ensure correct arithmetic semantics:
- `SInt(8) + UInt(8)` treats the UInt as signed for the operation
- This matches SystemVerilog's behavior and prevents subtle bugs

## Implementation Details

### Type Classes

```python
from dataclasses import dataclass

@dataclass(frozen=True)
class UInt(SignalType):
    width: int
    
    @property
    def is_signed(self) -> bool:
        return False
```

Types are immutable and hashable, enabling use as dictionary keys and set elements.

### Type Promotion Interface

```python
from cmt2._type_promotion import promote_types, Operation

# Low-level promotion API
result_type = promote_types(UInt(8), UInt(16), Operation.ADD)
# Returns: UInt(17)
```

### Operator Overloading

Types support arithmetic operators that compute result types:

```python
# These all return the promoted type
result = UInt(8) + UInt(16)   # UInt(17)
result = UInt(8) * UInt(8)    # UInt(16)
result = SInt(8) - UInt(8)    # SInt(9)
```

## Comparison with Other Systems

### SystemVerilog

SystemVerilog has implicit type promotion with width extension rules. CMT2 makes these explicit:

```systemverilog
// SystemVerilog: implicit rules
logic [7:0] a;
logic [15:0] b;
logic [16:0] c = a + b;  // Zero-extend a, result is 17 bits
```

```python
# CMT2: explicit rules
a = UInt(8)
b = UInt(16)
c = a + b  # UInt(17), zero-extension implicit in hardware
```

### Chisel/Scala

Chisel uses `UInt` and `SInt` with similar width inference:

```scala
// Chisel
val a = UInt(8.W)
val b = UInt(16.W)
val c = a + b  // UInt(17.W)
```

```python
# CMT2
a = UInt(8)
b = UInt(16)
c = a + b  # UInt(17)
```

### JAX (for reference)

JAX's type promotion inspired CMT2's design, but hardware has different constraints:

```python
# JAX: values have types
import jax.numpy as jnp
a = jnp.array([1], dtype=jnp.uint8)
b = jnp.array([1], dtype=jnp.uint16)
c = a + b  # uint16 (different rule!)

# CMT2: types are separate from values
a = UInt(8)
b = UInt(16)
c = a + b  # UInt(17) - hardware needs extra bit
```

## Best Practices

### 1. Use Explicit Types for Module Interfaces

```python
from cmt2.types import UInt, SInt

def my_module(data: UInt[8], coeff: SInt[16]) -> UInt[24]:
    """Explicit types make interfaces clear."""
    result = data * coeff  # Type is computed automatically
    return result
```

### 2. Be Aware of Width Growth

```python
# Chain of operations can cause width explosion
a = UInt(8)
b = a * a      # UInt(16)
c = b * b      # UInt(32)
d = c * c      # UInt(64) - very wide!

# Consider explicit truncation when appropriate
d = UInt(32)(c * c)  # Truncate to 32 bits
```

### 3. Handle Mixed Signedness Carefully

```python
# Signed + Unsigned = Signed
a = SInt(8)
b = UInt(8)
c = a + b  # SInt(9)

# If you want unsigned result, be explicit
c = UInt(9)(a + b)  # Cast to unsigned
```

### 4. Use Bits for Control Signals

```python
from cmt2.types import Bits

# Control signals don't need arithmetic
valid = Bits(1)
ready = Bits(1)
handshake = valid & ready  # Bits(1), no sign issues
```

## Future Extensions

### Fixed-Point Types

Future versions may add `UFixed(width, frac_width)` and `SFixed(width, frac_width)` for fixed-point arithmetic with automatic binary point alignment.

### Bundle/Struct Types

See [Task 2.3](../Implementation-Plan.md) for bundle type support:

```python
class AXI4Bundle(cmt2.Bundle):
    addr = cmt2.Field(UInt(32))
    data = cmt2.Field(UInt(128))
    valid = cmt2.Field(Bits(1))
```

### Vector/Array Types

Vector types for arrays of signals:

```python
from cmt2.types import Vector, UInt

# Array of 16 8-bit unsigned values
data = Vector(UInt(8), 16)
```

## References

- [Implementation Plan](../Implementation-Plan.md) - Task 1.3
- [PL Expert Review](../review-pl-expert.md) - Type system concerns
- SystemVerilog IEEE 1800-2017 - Type promotion rules
- Chisel/FIRRTL documentation
