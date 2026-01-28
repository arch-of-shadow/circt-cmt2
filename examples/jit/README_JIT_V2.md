# CMT2 JIT v2 Examples

These examples demonstrate the new **JIT v2** architecture - a thin layer on top of PyCMT2 that provides agile syntax without duplicating functionality.

## Architecture

```
User Code
    ↓
JIT v2 (thin decorators: @elaborate, @rule, @dataflow)
    ↓
PyCMT2 (Circuit, ModuleBuilder, DataflowBuilder)
    ↓
CIRCT Python Bindings (MLIR construction)
```

## Key Differences from JIT v1

| Aspect | JIT v1 | JIT v2 |
|--------|--------|--------|
| Circuit creation | `CircuitBuilder` wrapper | Native PyCMT2 `Circuit` |
| Module definition | `with builder.module() as m` | `@jit.module(circuit, name)` |
| Rule definition | Nested `with` statements | `@jit.rule` + `@guard`/`@body` |
| Dataflow | Separate implementation | Thin wrapper on PyCMT2 |
| Duplication | Replicated PyCMT2 | Zero duplication |

## Examples

### 1. Counter (jit_v2_counter.py)

Basic counter using agile syntax:

```python
@jit.elaborate
def counter_v2(width: int = 32):
    circuit = Circuit("CounterV2")
    
    @jit.module(circuit, "Counter")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        
        @jit.rule(m, "increment")
        def _(rule):
            @rule.guard
            def _(g): g.always()
            @rule.body
            def _(b): b.call(count, "write", b.call(count, "read") + 1)
    
    return circuit
```

### 2. Fork-Join Dataflow (jit_v2_dataflow.py)

Fork-join pipeline using `@forkjoin` decorator:

```python
@jit.forkjoin(m, "parallel", UInt(32), UInt(33))
def build_parallel(fjp):
    @fjp.source
    def source(task, data): return data
    
    @fjp.branch("add_one")
    def add_one(task, data): return task.add(data, task.const(1, 32))
    
    @fjp.branch("add_two")
    def add_two(task, data): return task.add(data, task.const(2, 32))
    
    @fjp.sink
    def sink(task, results):
        a, b = results
        return task.add(a, b)
```

### 3. Banked GEMM (jit_v2_banked_gemm.py)

Tiled matrix multiplication with banked memories and dataflow tasks.

### 4. Nested Dataflow (jit_v2_nested_dataflow.py)

Hierarchical dataflows with outer and inner pipelines.

## Running Examples

```bash
# Set PYTHONPATH
export PYTHONPATH=$CIRCT_BUILD/tools/circt/python_packages/circt_core:../../python

# Run examples
python jit_v2_counter.py
python jit_v2_dataflow.py
python jit_v2_banked_gemm.py
python jit_v2_nested_dataflow.py
```

## Migration from JIT v1

JIT v1 (in `python/cmt2/jit/`) is deprecated. New designs should use JIT v2:

```python
# OLD (v1) - verbose, duplicated functionality
from cmt2 import elaborate
from cmt2.pycmt2_integration import CircuitBuilder

@elaborate
def design():
    builder = CircuitBuilder("Name")  # Wrapper!
    with builder.module("Mod") as m:
        with m.rule("r") as r:
            with r.guard() as g: g.always()
            with r.body() as b: b.call(...)

# NEW (v2) - thin, agile, no duplication
import cmt2.jit_v2 as jit
from circt.pycmt2 import Circuit

@jit.elaborate
def design():
    circuit = Circuit("Name")  # Native PyCMT2!
    @jit.module(circuit, "Mod")
    def build(m):
        @jit.rule(m, "r")
        def _(r):
            @r.guard
            def _(g): g.always()
            @r.body
            def _(b): b.call(...)
```

## Available Decorators

### Core
- `@jit.elaborate` - Circuit elaboration with caching
- `@jit.simulate` - Simulation runner
- `@jit.module(circuit, name)` - Module definition
- `@jit.rule(module, name)` - Rule definition
- `@jit.method(module, name, args, returns)` - Action method
- `@jit.value(module, name, returns)` - Value method

### Dataflow
- `@jit.dataflow(module, name, args, returns)` - Dataflow pipeline
- `@jit.task(df, name, tokens_in, timing)` - Dataflow task
- `@jit.forkjoin(module, name, data_type, output_type)` - Fork-join pipeline

## Benefits

1. **No Duplication**: All MLIR construction through PyCMT2
2. **Agile Syntax**: Decorator-based instead of nested `with`
3. **Composable**: Mix JIT v2 decorators with raw PyCMT2
4. **Maintainable**: Thin layer is easier to maintain
5. **Compatible**: Works with all PyCMT2 features
