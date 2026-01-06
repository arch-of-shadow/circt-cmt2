# PyCMT2: Python Frontend for CMT2

## Overview

PyCMT2 is a Python embedded DSL (EDSL) for the CMT2 dialect, providing a Pythonic interface for hardware design with Guarded Atomic Actions (GAA) semantics.

**Key Features:**
- Context manager-based API for structured hardware definition
- Strong typing with runtime validation
- STL modules (Reg, Wire) with auto-generated RTL
- Full pipeline: Python → CMT2 MLIR → FIRRTL → Verilog
- Simulation workspace generation for Verilator

## Quick Start

```bash
# Run examples
./scripts/pycmt2.sh examples/PyCMT2/counter_example.py
./scripts/pycmt2.sh examples/PyCMT2/gcd_example.py

# List available examples
./scripts/pycmt2.sh --list

# Interactive Python with pycmt2
./scripts/pycmt2.sh -i
```

## Architecture

```
User Code (pycmt2 EDSL)
         │
         ▼
PyCMT2 Builders (Circuit, Module, Rule, Method, Value, Control)
         │
         ▼
CIRCT Python Bindings (circt.dialects.cmt2)
         │
         ▼
MLIR IR → Passes → FIRRTL → Verilog
```

## Package Structure

```
lib/Bindings/Python/pycmt2/
├── __init__.py          # Main exports
├── circuit.py           # Circuit builder
├── module.py            # Module builder
├── builders.py          # Base builder, expression operations
├── function_builders.py # Guard, Body, Rule, Method, Value builders
├── proc_builders.py     # Procedural control (Step, ProcRule, Control)
├── external_module.py   # External FIRRTL module bindings
├── types.py             # Type system (UInt, SInt, Clock, Reset)
├── signals.py           # Signal wrapper with operators
├── refs.py              # Object references (MethodRef, ValueRef, StepRef)
├── stl.py               # Standard library (Reg, Wire)
├── simulation.py        # SimulationWorkspace for Verilator
├── testbench.py         # Testbench DSL
└── location.py          # Python source location tracking
```

## Core API

### Circuit and Module

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg

circuit = Circuit("MyDesign")

# Create STL register module
reg_mod = Reg.create(circuit, 32)

with circuit.module("Counter") as m:
    clk = m.clock()
    rst = m.reset()

    count = m.instance(reg_mod, "count", clk=clk, rst=rst)

    # Rule: increment counter
    with m.rule("increment") as rule:
        with rule.guard() as g:
            g.always()
        with rule.body() as body:
            val = body.call(count, "read")
            new_val = body.add(val, body.const(1, 32))
            body.call(count, "write", new_val)

    # Value: read counter
    with m.value("get_count", returns=[UInt(32)]) as val:
        with val.guard() as g:
            g.always()
        with val.body() as body:
            body.returns(body.call(count, "read"))
```

### Methods with Arguments

```python
with m.method("load", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
    with meth.guard() as g:
        g.always()
    with meth.body() as body:
        a_in = body.arg("a")
        b_in = body.arg("b")
        body.call(reg_a, "write", a_in)
        body.call(reg_b, "write", b_in)
```

### Procedural Control

```python
# Dynamic step (explicit done signal)
with m.step("load") as step:
    val = step.call(reg, "read")
    step.call(other_reg, "write", val)
    step.done(step.const(1, 1))

# Static step (fixed latency)
with m.static_step(3, "delay") as step:
    # 3-cycle delay, no done needed
    pass

# Procedural rule with control flow
with m.proc_rule("compute") as rule:
    with rule.guard() as g:
        g.always()
    with rule.control() as ctrl:
        with ctrl.seq():
            ctrl.enable(m._steps["load"].ref())
            with ctrl.while_(condition) as loop:
                loop.enable(m._steps["process"].ref())
            ctrl.enable(m._steps["store"].ref())
```

### Control Flow Constructs

| Construct | Description |
|-----------|-------------|
| `ctrl.seq()` | Sequential composition |
| `ctrl.par()` | Parallel composition |
| `ctrl.if_(cond)` | Conditional with `.then_()` and `.else_()` |
| `ctrl.while_(cond)` | While loop |
| `ctrl.static_if(cond, then_lat, else_lat)` | Static conditional with known branch latencies |
| `ctrl.static_repeat(count, body_lat)` | Static loop with fixed iterations |
| `ctrl.enable(step_ref)` | Enable a step |
| `ctrl.invoke(instance, method, *args)` | Invoke method within control |

### Static Timing Features

PyCMT2 supports cycle-precise timing for static control:

#### Methods with Static Timing

```python
# Method with 4-cycle latency, can accept new call every 3 cycles (pipelined)
with m.method("multiply",
              args=[("a", UInt(32)), ("b", UInt(32))],
              returns=[UInt(32)],
              static_latency=4,
              interval=3) as meth:
    with meth.guard() as g:
        g.always()
    with meth.body() as body:
        body.returns(body.mul(body.arg("a"), body.arg("b")))
```

#### Calls with Timing Annotations

```python
with m.static_step(6, "compute") as step:
    # Specify when args are driven and result is captured
    result = step.call(mult, "multiply", a, b,
                       arg_timing=[(0, 1), (0, 1)],    # args at cycle 0
                       result_timing=[(4, 5)])         # result at cycle 4
```

#### Static Control Flow

```python
with rule.control() as ctrl:
    with ctrl.seq():
        # Fixed 4-iteration loop
        with ctrl.static_repeat(4, body_latency=3) as loop:
            loop.enable(step_3cycle.ref())  # total = 4 * 3 = 12 cycles

        # Static conditional with known branch latencies
        with ctrl.static_if(cond, then_latency=5, else_latency=3) as sif:
            with sif.then_() as then_ctrl:
                then_ctrl.enable(branch_a.ref())
            with sif.else_() as else_ctrl:
                else_ctrl.enable(branch_b.ref())
        # Total latency = max(5, 3) = 5 cycles
```

#### Timing Summary

| Feature | API | Effect |
|---------|-----|--------|
| Method latency | `method(..., static_latency=N)` | Method takes N cycles |
| Pipeline interval | `method(..., interval=M)` | New call every M cycles |
| Arg timing | `call(..., arg_timing=[(s,e)])` | Args driven at cycles [s,e) |
| Result timing | `call(..., result_timing=[(s,e)])` | Results captured at [s,e) |
| Static loop | `static_repeat(N, body_lat=L)` | Total = N × L cycles |
| Static if | `static_if(c, then_lat=T, else_lat=E)` | Total = max(T, E) cycles |

### External Modules

```python
with circuit.external_module("Reg32") as reg:
    reg.clock("clk")
    reg.reset("rst")
    reg.value("read", returns=[UInt(32)])
    reg.method("write", args=[("data", UInt(32))])
    reg.sequence_before("read", "write")
```

### STL Modules

```python
from circt.pycmt2.stl import Reg, Wire

# Create register (auto-generates RTL)
reg_mod = Reg.create(circuit, 32)  # 32-bit register
reg1_mod = Reg.create(circuit, 1)   # 1-bit register

# Create wire
wire_mod = Wire.create(circuit, 32)

# Instantiate
reg = m.instance(reg_mod, "my_reg", clk=clk, rst=rst)
```

## Emission Methods

```python
# Emit CMT2 MLIR
mlir = circuit.emit_mlir()

# Emit FIRRTL (runs CMT2 passes)
firrtl = circuit.emit_firrtl()

# Emit Verilog (full pipeline)
verilog = circuit.to_verilog()
```

## Simulation Workspace

```python
from circt.pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, "sim_output")

# Generate with placeholder testbench
ws.generate_placeholder()

# Build and run
ws.build()
success, output = ws.run()
```

Generated structure:
```
sim_output/
├── rtl/           # Verilog files + STL modules
├── tb/            # Testbench (placeholder or DSL-generated)
├── build/         # Verilator build output
├── waves/         # VCD waveforms
├── Makefile       # Build automation
└── README.md      # Instructions
```

## Type System

| Type | Description |
|------|-------------|
| `UInt(width)` | Unsigned integer |
| `SInt(width)` | Signed integer |
| `Clock` | Clock signal |
| `Reset` | Synchronous reset |
| `Bool` | Alias for `UInt(1)` |

## Expression Operations

Available in guard and body builders:

| Category | Operations |
|----------|------------|
| Arithmetic | `add`, `sub`, `mul`, `div`, `mod` |
| Comparison | `eq`, `neq`, `lt`, `le`, `gt`, `ge` |
| Bitwise | `and_`, `or_`, `xor_`, `not_` |
| Bit manipulation | `bits`, `pad`, `truncate`, `concat` |
| Control | `mux` (conditional select) |
| Constants | `const(value, width)` |

## Python Source Location Tracking

PyCMT2 automatically captures Python source locations and propagates them to the generated Verilog as comments, enabling traceability from RTL back to Python source.

## Diagnostics System

PyCMT2 includes a multi-level diagnostic system for error reporting with source context.

### Diagnostic Levels

| Level | Usage | MLIR Mapping |
|-------|-------|--------------|
| `ERROR` | Fatal issues that prevent compilation | `emitError` |
| `WARNING` | Non-fatal issues that may affect behavior | `emitWarning` |
| `INFO` | Informational messages during compilation | `emitRemark` |
| `DEBUG` | Detailed debugging information | `LLVM_DEBUG` |

### Basic Usage

```python
from circt.pycmt2 import (
    emit_error, emit_warning, emit_info, emit_debug,
    get_python_location, DiagnosticHandler
)

# Emit diagnostics with Python source location
loc = get_python_location()
error = emit_error(loc, "method 'write' requires exactly 1 argument",
                   notes=["expected: write(data: UInt<32>)", "got: write()"],
                   hint="add the missing data argument")
print(error.format(color=True))
```

### Specialized Error Helpers

```python
from circt.pycmt2 import (
    type_mismatch_error,
    undefined_reference_error,
    scheduling_conflict_warning
)

# Type mismatch
diag = type_mismatch_error(loc, "UInt<32>", "UInt<16>",
                            "in method 'write' argument 'data'")

# Undefined reference
diag = undefined_reference_error(loc, "method", "wriet",
                                   "module 'Counter'")

# Scheduling conflict
diag = scheduling_conflict_warning(loc, "reg.read", "reg.write",
                                    "in static step 'compute'")
```

### DiagnosticHandler Context Manager

```python
from circt.pycmt2 import DiagnosticHandler, DiagnosticLevel

with DiagnosticHandler(min_level=DiagnosticLevel.DEBUG) as handler:
    # Diagnostics are captured
    handler.add_diagnostic(emit_error(loc, "missing clock argument"))
    handler.add_diagnostic(emit_warning(loc, "unused value 'temp'"))

    # Query captured diagnostics
    print(f"Errors: {handler.error_count}")
    print(f"Warnings: {handler.warning_count}")
    print(f"Has errors: {handler.has_errors()}")
    print(handler.format_summary())  # "1 error, 1 warning"

    for diag in handler.get_diagnostics():
        print(f"[{diag.level.prefix}] {diag.message}")
```

### Source Context Display

```python
from circt.pycmt2 import format_diagnostic_with_source

# Show diagnostic with Python source code context
print(format_diagnostic_with_source(diag, context_lines=2))
```

Output:
```
error: rule guard must return boolean
  --> /path/to/file.py:75:8
    |
 73 |     def create_rule_with_error():
 74 |         loc = get_python_location()
 75 |         return emit_error(loc, "rule guard must return boolean",
    |                ^
 76 |                           notes=["guard returns UInt<32>"])
 77 |
note: guard returns UInt<32> instead of UInt<1>
hint: use comparison operator to produce boolean
```

### C++ Integration

The diagnostic system is integrated into CMT2 passes:

```cpp
#include "circt/Dialect/Cmt2/Transforms/Diagnostics.h"

// In a pass:
return reportConversionError(op, "could not find clock for FSM")
    .note("procedural rules require clock and reset signals")
    .hint("ensure module has clock and reset arguments")
    .withPythonSource()  // Extract Python location from FusedLoc
    .emit();

// Type mismatch
return reportTypeMismatch(op, expectedType, actualType, "in method call")
    .emit();

// Missing definition
return reportMissingDefinition(op, "method", "write", "module Counter")
    .hint("check spelling")
    .emit();

// Scheduling conflict (warning)
(void)reportSchedulingConflict(op, "read", "write", "in static step")
    .note("latency guarantee may be violated")
    .hint("use dynamic step or explicit sequencing")
    .emit();
```

### Diagnostic Handler for Statistics

```cpp
#include "circt/Dialect/Cmt2/Transforms/Diagnostics.h"

// Track diagnostic statistics during a pass
Cmt2DiagnosticHandler handler(ctx);
// ... run operations that may emit diagnostics ...
auto stats = handler.getStats();
llvm::errs() << "Errors: " << stats.errors
             << ", Warnings: " << stats.warnings << "\n";
```

## Examples

| Example | Description |
|---------|-------------|
| `counter_example.py` | Basic counter module |
| `counter_reg_example.py` | Counter with STL Reg |
| `gcd_example.py` | GCD algorithm with procedural control |
| `proc_example.py` | Multi-cycle ALU |
| `proc_comprehensive_example.py` | All procedural features |
| `simulation_workspace_example.py` | Simulation workspace generation |
| `diagnostics_example.py` | Multi-level diagnostic system demo |
| `test_gcd_simulation.py` | GCD with Verilator simulation |
| `test_proc_simulation.py` | ALU with Verilator simulation |

## Helper Script

The `scripts/pycmt2.sh` script simplifies running PyCMT2 examples:

```bash
# Usage
./scripts/pycmt2.sh [OPTIONS] <script.py> [args...]

# Options
-h, --help          Show help
-l, --list          List available examples
-i, --interactive   Start interactive Python
-v, --verbose       Show environment details
-b, --build-dir     Specify build directory
```

## Design Principles

1. **Context managers** for structured regions (modules, rules, guards, bodies)
2. **Strong typing** with `Signal[T]` wrappers and runtime validation
3. **Object references** (`MethodRef`, `ValueRef`, `StepRef`) instead of string indexing
4. **JIT naming** - infer names from Python variable assignments when not specified
5. **No continuous assignment** - only CMT2-supported operations
6. **Region-aware guards** - guards are always regions, not simple expressions

## Related Documentation

- [CMT2 Rationale](RationaleCmt2.md) - GAA semantics and design philosophy
- [ECMT2 C++ EDSL](ecmt2-EDSL.md) - C++ embedded DSL
- [Implementation Tracker](CMT2-Implementation-Tracker.md) - Development progress
