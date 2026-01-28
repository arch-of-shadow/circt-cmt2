# CMT2 JIT v2 - Thin Layer on PyCMT2

## Architecture

JIT v2 is a thin syntax sugar layer on top of PyCMT2. It does NOT duplicate PyCMT2 functionality.

```
User Code
    ↓
CMT2 JIT v2 (decorators, helpers)
    ↓
PyCMT2 (Circuit, ModuleBuilder, DataflowBuilder)
    ↓
CIRCT Python Bindings (MLIR)
```

## Design Principles

1. **No Duplication**: All MLIR construction goes through PyCMT2
2. **Agile Syntax**: Decorator-based instead of verbose context managers
3. **Native PyCMT2**: Returns PyCMT2 Circuit objects directly
4. **Optional**: Can mix JIT v2 with raw PyCMT2

## Quick Example

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg
import cmt2.jit_v2 as jit

@jit.elaborate
def counter(width: int = 32):
    circuit = Circuit("Counter")
    
    @jit.module(circuit, "Counter")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        
        @jit.rule(m, "increment")
        def _(r):
            @r.guard
            def _(g): g.always()
            @r.body  
            def _(b): b.call(count, "write", b.call(count, "read") + 1)
    
    return circuit
```
