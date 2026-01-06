# PyCMT2 Design

Internal design documentation for the PyCMT2 Python frontend.

---

## Overview

PyCMT2 is a Python embedded DSL (EDSL) for CMT2, providing:

- **Context manager API** for structured hardware definition
- **Type system** with runtime validation
- **STL components** (Reg, Wire, FIFO, Memory)
- **Full pipeline**: Python → CMT2 MLIR → FIRRTL → Verilog
- **Simulation workspace** for Verilator integration

---

## Architecture

```
User Python Code
         │
         ▼
┌─────────────────────────────────────┐
│  PyCMT2 Builders                    │
│  (Circuit, Module, Rule, Method...) │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│  CIRCT Python Bindings              │
│  (circt.dialects.cmt2)              │
└─────────────────────────────────────┘
         │
         ▼
    MLIR IR → Passes → FIRRTL → Verilog
```

---

## Package Structure

```
lib/Bindings/Python/pycmt2/
├── __init__.py           # Main exports
├── circuit.py            # Circuit builder
├── module.py             # Module builder
├── builders.py           # Base builder, expressions
├── function_builders.py  # Guard, body builders
├── proc_builders.py      # Procedural control
├── external_module.py    # External FIRRTL bindings
├── types.py              # Type system
├── signals.py            # Signal wrapper
├── refs.py               # References
├── stl.py                # Standard library
├── simulation.py         # SimulationWorkspace
├── testbench.py          # Testbench generation
├── interpreter.py        # Hardware interpreter
├── location.py           # Source location tracking
└── diagnostics.py        # Error reporting
```

**Total**: ~7,600 lines of Python

---

## Core Components

### Circuit

Top-level container for modules.

```python
class Circuit:
    def __init__(self, name: str):
        self._context = ir.Context()
        self._module = ir.Module.create(self._context.loc)
        self._circuit_op = cmt2.CircuitOp(...)

    def module(self, name: str) -> ModuleBuilder:
        """Create a CMT2 module."""

    def external_module(self, name: str) -> ExternalModuleBuilder:
        """Create an external FIRRTL module binding."""

    def emit_mlir(self) -> str:
        """Emit CMT2 MLIR."""

    def emit_firrtl(self) -> str:
        """Run passes and emit FIRRTL."""

    def emit_verilog(self) -> str:
        """Full pipeline to Verilog."""
```

### Module

CMT2 module with rules, methods, values.

```python
class ModuleBuilder:
    def clock(self) -> Signal:
        """Add clock port."""

    def reset(self) -> Signal:
        """Add reset port."""

    def instance(self, module, name, **kwargs) -> InstanceRef:
        """Instantiate a module."""

    def rule(self, name: str) -> RuleBuilder:
        """Create a rule."""

    def method(self, name: str, args=[], returns=[],
               static_latency=None, interval=None) -> MethodBuilder:
        """Create a method."""

    def value(self, name: str, returns=[]) -> ValueBuilder:
        """Create a value."""

    def step(self, name: str) -> StepBuilder:
        """Create a dynamic step."""

    def static_step(self, latency: int, name: str,
                    interval: int = None) -> StaticStepBuilder:
        """Create a static step."""

    def proc_rule(self, name: str) -> ProcRuleBuilder:
        """Create a procedural rule."""
```

### Function Builders

Guard and body regions for rules/methods/values.

```python
class GuardBuilder:
    def always(self):
        """Return constant true."""

    def call(self, instance, method, *args) -> Signal:
        """Call method and return result."""

    def returns(self, value: Signal):
        """Return guard condition."""

class BodyBuilder:
    def call(self, instance, method, *args) -> Signal:
        """Call method."""

    def arg(self, name: str) -> Signal:
        """Get method argument."""

    def const(self, value: int, width: int) -> Signal:
        """Create constant."""

    def add(self, a, b) -> Signal:
        """Add two signals."""

    # ... other operations

    def returns(self, *values):
        """Return values."""
```

### Procedural Control

Multi-cycle operations with control flow.

```python
class StepBuilder:
    def call(self, instance, method, *args) -> Signal:
        """Call method in step."""

    def done(self, condition: Signal):
        """Signal step completion."""

class StaticStepBuilder:
    def call(self, instance, method, *args,
             arg_timing=None, result_timing=None) -> Signal:
        """Call with optional timing."""

class ControlBuilder:
    def seq(self) -> SeqBuilder:
        """Sequential composition."""

    def par(self) -> ParBuilder:
        """Parallel composition."""

    def if_(self, condition) -> IfBuilder:
        """Conditional."""

    def while_(self, condition) -> WhileBuilder:
        """Loop."""

    def static_repeat(self, count, body_latency) -> StaticRepeatBuilder:
        """Fixed-iteration loop."""

    def static_if(self, condition, then_lat, else_lat) -> StaticIfBuilder:
        """Conditional with known latencies."""

    def enable(self, step_ref):
        """Enable a step."""
```

---

## Type System

```python
# Basic types
UInt(width: int)      # Unsigned integer
SInt(width: int)      # Signed integer
Clock                 # Clock signal
Reset                 # Synchronous reset
Bool = UInt(1)        # Boolean alias

# Compound types (planned)
Bundle(fields: dict)  # Record type
Vector(elem, size)    # Array type
```

### Signal Wrapper

```python
class Signal:
    def __init__(self, value: ir.Value, width: int):
        self._value = value
        self._width = width

    # Arithmetic
    def __add__(self, other) -> Signal: ...
    def __sub__(self, other) -> Signal: ...
    def __mul__(self, other) -> Signal: ...

    # Comparison
    def __eq__(self, other) -> Signal: ...
    def __lt__(self, other) -> Signal: ...

    # Bitwise
    def __and__(self, other) -> Signal: ...
    def __or__(self, other) -> Signal: ...
    def __invert__(self) -> Signal: ...

    # Bit manipulation
    def bits(self, high: int, low: int) -> Signal: ...
```

---

## STL Components

### Registry

STL modules are cached to avoid duplication.

```python
_stl_registry: Dict[str, ir.Operation] = {}

def clear_stl_registry():
    """Clear for new circuit."""
    _stl_registry.clear()
```

### Reg

Register with read/write methods.

```python
class Reg:
    @staticmethod
    def create(circuit: Circuit, width: int) -> ExternalModuleRef:
        """Create or get cached register module."""

    # Methods:
    # - read() -> UInt(width)
    # - write(data: UInt(width))
    # Scheduling: sequenceBefore(read, write)
```

### Wire

Combinational wire.

```python
class Wire:
    @staticmethod
    def create(circuit: Circuit, width: int) -> ExternalModuleRef:
        """Create wire module."""

    # Methods:
    # - read() -> UInt(width)
    # - write(data: UInt(width))
    # Scheduling: conflictFree(read, write)
```

### FIFO

FIFO queue with configurable depth.

```python
class FIFO:
    @staticmethod
    def create(circuit: Circuit, width: int, depth: int = 1):
        """Create FIFO module."""

    # Methods:
    # - enq(data) - Enqueue
    # - deq() -> data - Dequeue
    # - first() -> data - Peek
    # - notEmpty() -> bool
    # - notFull() -> bool
```

### Memory

Synchronous memory.

```python
class Memory:
    @staticmethod
    def create(circuit: Circuit, data_width: int,
               addr_width: int, depth: int):
        """Create memory module."""

    # Methods:
    # - read(addr) -> data
    # - write(addr, data)
```

---

## Timing Support

### Method Timing

```python
with m.method("multiply",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[UInt(64)],
              static_latency=4,    # Total latency
              interval=2) as meth: # Initiation interval
    ...
```

### Call Timing

```python
with m.static_step(6, "compute") as step:
    result = step.call(mult, "multiply", a, b,
                       arg_timing=[(0, 1), (0, 1)],
                       result_timing=[(4, 5)])
```

### External Module Timing

```python
with circuit.external_module("Multiplier") as mult:
    mult.method("multiply",
                args=[("a", UInt(32)), ("b", UInt(32))],
                returns=[UInt(64)],
                static_latency=4,
                interval=2)
```

---

## Simulation Workspace

Generates Verilator project.

```python
class SimulationWorkspace:
    def __init__(self, circuit: Circuit, output_dir: str):
        ...

    def generate_placeholder(self):
        """Generate with placeholder testbench."""

    def build(self) -> bool:
        """Run Verilator build."""

    def run(self) -> Tuple[bool, str]:
        """Run simulation."""
```

### Generated Structure

```
output_dir/
├── rtl/           # Verilog files
├── tb/            # Testbench
├── build/         # Verilator output
├── waves/         # VCD files
├── Makefile
└── README.md
```

---

## Source Location Tracking

Python source locations propagate to Verilog.

```python
def get_python_location() -> PyLocation:
    """Get current Python source location."""
    frame = inspect.currentframe().f_back
    return PyLocation(
        file=frame.f_code.co_filename,
        line=frame.f_lineno,
        col=0
    )

# Usage in builder
loc = get_python_location()
op = cmt2.RuleOp(..., loc=loc.to_mlir())
```

---

## Diagnostics

Multi-level error reporting.

```python
class DiagnosticLevel(Enum):
    ERROR = auto()
    WARNING = auto()
    INFO = auto()
    DEBUG = auto()

def emit_error(loc: PyLocation, message: str,
               notes: List[str] = None,
               hint: str = None) -> Diagnostic:
    """Emit error diagnostic."""

class DiagnosticHandler:
    """Context manager for collecting diagnostics."""

    def has_errors(self) -> bool: ...
    def error_count(self) -> int: ...
    def format_summary(self) -> str: ...
```

---

## Design Principles

1. **Context managers** for scoped regions
2. **Strong typing** with runtime validation
3. **Object references** instead of strings
4. **JIT naming** from Python variables
5. **No continuous assignment** (CMT2-only ops)
6. **Region-aware guards**

---

## See Also

- [PyCMT2-Guide.md](PyCMT2-Guide.md) - User guide
- [Examples.md](Examples.md) - Example designs
- [STL.md](STL.md) - Standard library
