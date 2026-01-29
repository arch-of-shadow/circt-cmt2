# Cmt2 Type System (PyCMT2)

**Status:** Supported (PyCMT2 is the source of truth)  
**Last Updated:** 2026-01-28

JIT (`cmt2.jit`) is stacked on **PyCMT2**, and uses PyCMT2’s type system
directly. There is no separate `cmt2.types`.

## Scalar types

```python
from circt.pycmt2 import UInt, SInt, Bool, ClockType, ResetType, AsyncResetType

UInt(32)         # unsigned 32-bit integer
SInt(16)         # signed 16-bit integer
Bool()           # 1-bit boolean (UInt(1) semantics)
ClockType()      # clock
ResetType()      # synchronous reset
AsyncResetType() # asynchronous reset
```

## Aggregate types

```python
from circt.pycmt2 import Bundle, Vector, UInt

Vector(UInt(8), 4)  # 4-element vector of UInt<8>

Bundle((
    ("valid", UInt(1), False),
    ("data",  UInt(32), False),
    ("ready", UInt(1), True),   # flip
))
```

## Dataflow token type

```python
from circt.pycmt2 import SyncToken, UInt

SyncToken()                     # void token
SyncToken(UInt(32))             # token carrying UInt<32>
SyncToken(UInt(32), mode="li")  # latency-insensitive token
```

## How JIT uses types

Types are passed directly into PyCMT2 builders (ports, args, returns):

```python
import cmt2.jit as jit
from circt.pycmt2 import Circuit, UInt

@jit.elaborate
def top():
    c = Circuit("Top")
    with jit.module(c, "Top") as m:
        x = m.input("x", UInt(32))

        @jit.value(m, returns=[UInt(32)])
        def id_(r):
            with r.guard:
                r.always()
            with r.body:
                r.returns(x)

    return c
```

Interfaces use the same types for method/value signatures:

```python
with circuit.interface("Writer") as i:
    with i.method("store", args=[("data", UInt(32))], returns=[]) as m:
        ...
```

## Width/sign behavior

Width/sign behavior is defined by the underlying CIRCT/FIRRTL/Cmt2 semantics and
PyCMT2 builder operations (i.e. the IR you build), not by a separate Python-side
promotion system in `cmt2`.
