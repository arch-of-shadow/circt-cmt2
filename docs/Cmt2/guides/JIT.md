# Cmt2 JIT — Stacked on PyCMT2

This document describes the **current, supported** Cmt2 “JIT” layer:
`python/cmt2/jit`.

## What JIT is (and is not)

JIT is a **thin syntax layer** that makes PyCMT2 more ergonomic for writing
rules/methods/values in Python:

- **In scope (JIT)**:
  - Module context manager (`with jit.module(...)`)
  - Rule/method/value decorators (`@jit.rule`, `@jit.method`, `@jit.value`)
  - Clear guard/body separation (`with r.guard:` / `with r.body:`)
  - Attribute-based instance calls via `SignalRef`:
    - `reg.read` (property-like read)
    - `reg.write(x)` (call)
    - `reg.next = x` (write shortcut)

- **Out of scope (owned by PyCMT2)**:
  - MLIR construction and dialect semantics
  - Lowering / codegen / export (`emit_mlir`, `emit_firrtl`, `emit_verilog`, …)
  - Simulation workspace generation (Verilator) and execution
  - Testbench DSL and waveform generation
  - Any “hardware backend” features (FPGA flows, physical constraints, CDC, multi-clock domains)

The layering is intentionally simple:

```
User code (JIT syntax)
  -> PyCMT2 builders (Circuit/ModuleBuilder/RuleBuilder/BodyBuilder/GuardBuilder)
      -> CIRCT / MLIR (Cmt2 + FIRRTL + HW + SV)
          -> Verilog + SimulationWorkspace + Testbench
```

## Requirements

JIT requires **CIRCT Python bindings** (PyCMT2):

```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python python3 -c "import cmt2, circt.pycmt2"
```

## API: elaboration + modules + rules

```python
import cmt2.jit as jit
from circt.pycmt2 import Circuit, UInt
from cmt2.stl import Reg

@jit.elaborate
def counter(width: int = 32):
    circuit = Circuit("Counter")

    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width), clk=clk, rst=rst)

        @jit.rule(m)
        def increment(r):
            with r.guard:
                r.always()

            with r.body:
                count.next = count.read + 1

        @jit.value(m)
        def get_count(r) -> UInt[width]:
            with r.guard:
                r.always()

            with r.body:
                r.returns(count.read)

    return circuit
```

### Name inference notes

Several JIT helpers infer symbol names from the assignment target, e.g.:

- `x = m.instance(...)`
- `with m.step() as s: ...`

This depends on Python stack inspection. If a name cannot be inferred (common
cases include dynamic container construction or unusual assignment patterns),
use `alias=...` explicitly.

### Interfaces (decl/def/bind)

Interfaces are defined at **circuit scope** (signatures), then used inside
modules via declarations/definitions and instance bindings:

- `with circuit.interface() as Reader`: define interface functions (`method`/`value`) with name inference
- `reader = m.interface_decl(Reader)`: declare an interface instance in a module (name inferred)
- `read_x = m.interface_def(Reader).bind(x.instance, x.instance.read, getData)`: define a mapping (no strings)
- `m.instance(child, interface_bindings={read_x: reader.decl}, ...)`: bind on instantiation

In JIT, interface decls are wrapped as `InterfaceRef`:

```python
reader = m.interface_decl(Reader)
data = reader.getData          # value (property-like)
writer = m.interface_decl(Writer)
writer.store(data)             # method (callable)
```

For an end-to-end reference, see `examples/JIT/interface_hello.py`.

### Methods (action methods with arguments)

JIT exposes method arguments as attributes on the context *inside* `guard`/`body`
(matching PyCMT2’s builder behavior).

```python
@jit.method(m)
def write(r, data: UInt[32]) -> None:
    with r.guard:
        r.always()

    with r.body:
        count.write(data)
```

### Values with arguments

Values are read-only and follow the ready/data contract. Values may also take
arguments; in that case they are called like a function (not as a property):

```python
@jit.value(m)
def add1(r, x: UInt[8]) -> UInt[8]:
    with r.guard:
        r.always()
    with r.body:
        r.returns(x + 1)

with some_rule.body:
    y = inst.add1(j)  # callable value-with-args
```

### Typed signatures (no `args=[("x", ...)]`)

JIT never asks you to define argument/return types via string/tuple lists.
Instead it uses Python annotations:

```python
@jit.method(m)
def add1(r, x: UInt[32]) -> UInt[32]:
    with r.guard:
        r.always()
    with r.body:
        r.returns(x + 1)
```

Dynamic-width annotations like `UInt[width]` are supported (the annotation is
evaluated in the definer’s frame during elaboration).

## Scheduling hooks (still PyCMT2)

JIT attaches the underlying PyCMT2 reference object to decorated callables:

- `fn._cmt2_ref`: a `RuleRef` / `MethodRef` / `ValueRef`
- `fn._cmt2_name`: the inferred/explicit name
- `fn.ref()`: convenience alias returning `fn._cmt2_ref`

This enables using PyCMT2 scheduling APIs (e.g. precedence) without JIT
re-implementing anything:

```python
@jit.rule(m)
def a(r): ...

@jit.rule(m)
def b(r): ...

m.builder.precedence(a._cmt2_ref, b._cmt2_ref)
```

## Dataflow pipelines

Use `@jit.dataflow` to define typed dataflow pipelines without `args=[...]`:

```python
@jit.dataflow(m, interval=1)
def pipe(df, x: UInt[16]) -> UInt[16]:
    b = df._df
    with b.task("stage0") as t0:
        tok = t0.create_token(x, UInt[16])
        t0.yield_tokens(tok)
    with b.task("out", tokens_in=[tok]) as t1:
        t1.return_values(t1.token_data(tok))
```

## STL usage (no duplication)

JIT does **not** define an STL. It re-exports PyCMT2 STL factories:

- `cmt2.stl` (recommended): thin wrapper around `circt.pycmt2.stl`
- `cmt2.jit.stl`: convenience re-export of `cmt2.stl`

Example:

```python
from cmt2.stl import Reg, FIFO

reg_mod = Reg.create(circuit, 32, init=0)
fifo_mod = FIFO.create(circuit, 32, depth=2)

reg = m.instance(reg_mod, clk=clk, rst=rst)   # name inferred from `reg = ...`
fifo = m.instance(fifo_mod, clk=clk, rst=rst) # name inferred from `fifo = ...`
```

## Codegen / simulation / testbenches (PyCMT2)

JIT builds a normal PyCMT2 `Circuit`, so you use PyCMT2 for everything after
elaboration:

```python
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench

circuit = counter(width=32)

# Generate SystemVerilog directly:
sv = circuit.emit_verilog()

# Or create a runnable Verilator workspace:
ws = SimulationWorkspace(circuit, "./sim_counter", debug_ports=True)

tb = Testbench(circuit)
with tb.sequence("basic") as seq:
    seq.reset(5)
    seq.wait(10)

ws.generate_with_testbench(tb)
ok, out = ws.build_and_run()
assert ok, out
```

### Testbench notes

- Use `seq.eval()` to re-evaluate combinational outputs after `seq.drive(...)`
  when you need to sample signals within the same cycle.
- For interface decls, keep a typed `InterfaceDecl` handle from elaboration and
  pass it to the testbench (no string lookup):
  `circuit, h = build(); writer = tb.interface_decl(h.writer); seq.call_interface(writer, store, ...)`.
  This models an *outgoing* call from the DUT (the DUT drives `*_enable/*_arg*`,
  the testbench drives `*_ready/*_res*`).

## Validation (JIT feature coverage)

The recommended way to validate JIT coverage is to run the JIT reimplementation
of the full PyCMT2 E2E suite:

```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going
```

## Types

JIT uses the **PyCMT2 type system** (see [TypeSystem.md](../reference/TypeSystem.md)).
Use those types for ports, method args, and method returns.
