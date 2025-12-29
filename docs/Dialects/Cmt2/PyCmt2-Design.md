# PyCmt2: Python Frontend for CMT2

## Overview

This document outlines the design for a Python frontend for the CMT2 dialect, providing:

1. **Low-level CIRCT Python bindings** for the CMT2 dialect (MLIR integration)
2. **High-level Python EDSL** for user-friendly hardware design (similar to calyx-py)

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     User Code (pycmt2 EDSL)                         │
│  circuit = Circuit("MyDesign")                                      │
│  mod = circuit.module("Counter")                                    │
│  reg = mod.reg(32, "count")                                         │
│  mod.rule("increment", guard=lambda: True, body=...)                │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    PyCmt2 Builder Layer                              │
│  CircuitBuilder, ModuleBuilder, RuleBuilder, ControlBuilder         │
│  - Pythonic API with context managers                               │
│  - Type inference and validation                                    │
│  - Automatic resource management                                    │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│               CIRCT Python Bindings (circt.dialects.cmt2)           │
│  - Auto-generated from Cmt2Ops.td                                   │
│  - Custom type/attribute bindings (Cmt2Module.cpp)                  │
│  - Direct MLIR operation construction                               │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        MLIR C API / IR                              │
│  - Module, Operation, Value, Type, Attribute                        │
│  - InsertionPoint, Location                                         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Part 1: CIRCT Python Bindings for CMT2

### 1.1 Files to Create

| File | Description |
|------|-------------|
| `lib/Bindings/Python/Cmt2Module.cpp` | C++ nanobind module for CMT2 types/attributes |
| `lib/Bindings/Python/dialects/Cmt2Ops.td` | TableGen wrapper for Python op generation |
| `lib/Bindings/Python/dialects/cmt2.py` | Python dialect module with custom wrappers |

### 1.2 C++ Bindings (Cmt2Module.cpp)

```cpp
#include "CIRCTModules.h"
#include "circt-c/Dialect/Cmt2.h"
#include "mlir/Bindings/Python/NanobindAdaptors.h"

namespace nb = nanobind;
using namespace circt;
using namespace mlir::python::nanobind_adaptors;

void circt::python::populateDialectCmt2Submodule(nb::module_ &m) {
  m.doc() = "CMT2 dialect Python native extension";

  // Register dialect
  m.def(
    "register_dialect",
    [](MlirContext ctx, bool load) {
      MlirDialectHandle handle = mlirGetDialectHandle__cmt2__();
      mlirDialectHandleRegisterDialect(handle, ctx);
      if (load)
        mlirDialectHandleLoadDialect(handle, ctx);
    },
    nb::arg("context"), nb::arg("load") = true);
}
```

### 1.3 TableGen for Python Op Generation (dialects/Cmt2Ops.td)

```tablegen
#ifndef PYTHON_BINDINGS_CMT2_OPS
#define PYTHON_BINDINGS_CMT2_OPS

include "circt/Dialect/Cmt2/Cmt2Ops.td"

#endif // PYTHON_BINDINGS_CMT2_OPS
```

### 1.4 Python Dialect Module (dialects/cmt2.py)

```python
from __future__ import annotations
from .._mlir_libs._circt._cmt2 import *
from ..dialects._ods_common import _cext as _ods_cext
from ..ir import *
from ._cmt2_ops_gen import *
from ._cmt2_ops_gen import _Dialect
from .. import support

@_ods_cext.register_operation(_Dialect, replace=True)
class CircuitOp(CircuitOp):
    """CMT2 Circuit operation wrapper."""

    def __init__(self, *, loc=None, ip=None):
        super().__init__(loc=loc, ip=ip)

    @property
    def body(self):
        return self.regions[0].blocks[0]

@_ods_cext.register_operation(_Dialect, replace=True)
class ModuleOp(ModuleOp):
    """CMT2 Module operation wrapper."""

    def __init__(self, name, input_ports=[], output_ports=[], *,
                 loc=None, ip=None):
        # Build module type and create operation
        ...

    @property
    def body(self):
        return self.regions[0].blocks[0]

# Similar wrappers for RuleOp, MethodOp, ValueOp, CallOp, etc.
```

### 1.5 CMakeLists.txt Updates

```cmake
# In lib/Bindings/Python/CMakeLists.txt

# Add to PYTHON_BINDINGS_SOURCES
set(PYTHON_BINDINGS_SOURCES
  ...
  Cmt2Module.cpp
)

# Add to PYTHON_BINDINGS_LINK_LIBS
set(PYTHON_BINDINGS_LINK_LIBS
  ...
  CIRCTCAPICmt2
)

# Add dialect binding
declare_mlir_dialect_python_bindings(
  ADD_TO_PARENT CIRCTBindingsPythonSources.Dialects
  ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
  TD_FILE dialects/Cmt2Ops.td
  SOURCES
    dialects/cmt2.py
  DIALECT_NAME cmt2)
```

### 1.6 C API Requirements

Need to create `include/circt-c/Dialect/Cmt2.h` and `lib/CAPI/Dialect/Cmt2.cpp`:

```c
// include/circt-c/Dialect/Cmt2.h
#ifndef CIRCT_C_DIALECT_CMT2_H
#define CIRCT_C_DIALECT_CMT2_H

#include "mlir-c/IR.h"

#ifdef __cplusplus
extern "C" {
#endif

MLIR_DECLARE_CAPI_DIALECT_REGISTRATION(Cmt2, cmt2);

#ifdef __cplusplus
}
#endif

#endif // CIRCT_C_DIALECT_CMT2_H
```

---

## Part 2: High-Level Python EDSL (pycmt2)

### 2.1 Design Principles

1. **Pythonic API**: Use context managers, decorators, and operator overloading
2. **Type Safety**: Leverage Python type hints and runtime validation
3. **Composability**: Support modular design with reusable components
4. **Familiar Patterns**: Similar to calyx-py and PyRTL for easy adoption

### 2.2 Package Structure

```
pycmt2/
├── __init__.py           # Main exports
├── builder.py            # Core builder classes
├── types.py              # Type wrappers (UInt, SInt, Clock, Reset)
├── signals.py            # Signal and wire abstractions
├── control.py            # Procedural control builders
├── stl.py                # Standard library (Reg, FIFO, Memory)
├── ast.py                # Internal AST representation (optional)
└── utils.py              # Utility functions
```

### 2.3 Core API Design

#### 2.3.1 Circuit and Module Builders

```python
from pycmt2 import Circuit, Module, UInt, Clock, Reset

# Create a circuit
circuit = Circuit("MyDesign")

# Add a module
@circuit.module
class Counter(Module):
    # Module ports
    clk = Clock()
    rst = Reset()
    count_out = UInt(32).output()

    def build(self):
        # Internal state
        self.count = self.reg(32, init=0)

        # Define rules
        @self.rule
        def increment(self):
            return (
                guard=lambda: True,
                body=lambda: self.count.write(self.count.read() + 1)
            )

        # Connect output
        self.count_out <<= self.count.read()
```

#### 2.3.2 Builder Pattern (Alternative Style)

```python
from pycmt2.builder import Builder, const

prog = Builder("MyDesign")

# Create module
counter = prog.module("Counter")

# Add ports
clk = counter.clock("clk")
rst = counter.reset("rst")
count_out = counter.output("count_out", 32)

# Add register from STL
count_reg = counter.reg(32, "count", init=0)

# Define rule with context manager
with counter.rule("increment") as rule:
    rule.guard(const(1, 1))  # Always enabled

    with rule.body():
        val = count_reg.read()
        next_val = val + const(1, 32)
        count_reg.write(next_val)

# Continuous assignment
with counter.continuous:
    count_out <<= count_reg.read()

# Emit MLIR
print(prog.emit_mlir())
```

#### 2.3.3 Procedural Control API

```python
from pycmt2.builder import Builder
from pycmt2.control import seq, par, if_, while_

prog = Builder("ProcExample")
mod = prog.module("SeqModule")

clk = mod.clock("clk")
rst = mod.reset("rst")

# Create register
reg = mod.reg(32, "data")

# Define groups
@mod.group("load")
def load_group(g):
    val = reg.read()
    g.done(const(1, 1))

@mod.group("store")
def store_group(g):
    reg.write(const(42, 32))
    g.done(const(1, 1))

# Define procedural rule
@mod.proc_rule("main")
def main_rule(r):
    r.guard(const(1, 1))

    # Control: seq { par { load; } store; }
    r.control(
        seq(
            par("load"),
            "store"
        )
    )
```

#### 2.3.4 External Module Binding

```python
from pycmt2.builder import Builder

prog = Builder("ExternExample")

# Define external FIRRTL module
reg_mod = prog.extern_firrtl(
    name="Reg32",
    firrtl_name="Reg32",
    ports={
        "clk": ("clock", Clock()),
        "rst": ("reset", Reset()),
    },
    bindings={
        "read": ValueBinding(
            ready="read_ready",
            results=["read_data"],
            result_types=[UInt(32)]
        ),
        "write": MethodBinding(
            enable="write_enable",
            ready="write_ready",
            arguments=["write_data"],
            arg_types=[UInt(32)]
        )
    },
    scheduling={
        "sequence_before": [("read", "write")],
        "conflict": [("write", "write")],
    }
)

# Use in module
mod = prog.module("User")
clk = mod.clock("clk")
rst = mod.reset("rst")

reg_inst = mod.instance("my_reg", reg_mod, clk=clk, rst=rst)

@mod.rule("use_reg")
def use_reg(r):
    r.guard(const(1, 1))
    with r.body():
        val = reg_inst.read()
        reg_inst.write(val + 1)
```

### 2.4 Type System

```python
# pycmt2/types.py

from typing import Optional, Union
from dataclasses import dataclass

@dataclass
class Type:
    """Base type class."""
    pass

@dataclass
class UInt(Type):
    """Unsigned integer type."""
    width: int

    def __post_init__(self):
        assert self.width > 0, "Width must be positive"

@dataclass
class SInt(Type):
    """Signed integer type."""
    width: int

@dataclass
class Clock(Type):
    """Clock type."""
    pass

@dataclass
class Reset(Type):
    """Reset type (synchronous, 1-bit)."""
    pass

@dataclass
class AsyncReset(Type):
    """Asynchronous reset type."""
    pass

@dataclass
class Bundle(Type):
    """Bundle (struct) type."""
    fields: dict[str, Type]

@dataclass
class Vector(Type):
    """Vector (array) type."""
    element_type: Type
    size: int
```

### 2.5 Signal Abstractions

```python
# pycmt2/signals.py

class Signal:
    """Base signal class with operator overloading."""

    def __init__(self, type_: Type, name: str, builder):
        self._type = type_
        self._name = name
        self._builder = builder

    def __add__(self, other):
        return self._builder.add(self, other)

    def __sub__(self, other):
        return self._builder.sub(self, other)

    def __and__(self, other):
        return self._builder.and_(self, other)

    def __or__(self, other):
        return self._builder.or_(self, other)

    def __xor__(self, other):
        return self._builder.xor_(self, other)

    def __invert__(self):
        return self._builder.not_(self)

    def __lshift__(self, amount):
        """Connect syntax: out <<= value"""
        self._builder.connect(self, amount)
        return self

    def __getitem__(self, key):
        """Bit extraction: signal[7:0] or signal[3]"""
        if isinstance(key, slice):
            return self._builder.bits(self, key.start, key.stop)
        return self._builder.bit(self, key)

    def __eq__(self, other):
        return self._builder.eq(self, other)

    def __ne__(self, other):
        return self._builder.neq(self, other)

    def __lt__(self, other):
        return self._builder.lt(self, other)

    def __le__(self, other):
        return self._builder.le(self, other)

    def __gt__(self, other):
        return self._builder.gt(self, other)

    def __ge__(self, other):
        return self._builder.ge(self, other)
```

### 2.6 Standard Library Components

```python
# pycmt2/stl.py

def Reg(width: int, init: int = 0):
    """Create a register component."""
    return STLComponent("Reg", {"width": width, "init": init})

def Wire(width: int):
    """Create a wire component."""
    return STLComponent("Wire", {"width": width})

def FIFO(width: int, depth: int):
    """Create a FIFO component."""
    return STLComponent("FIFO", {"width": width, "depth": depth})

def Memory(width: int, depth: int, read_ports: int = 1, write_ports: int = 1):
    """Create a memory component."""
    return STLComponent("Memory", {
        "width": width,
        "depth": depth,
        "read_ports": read_ports,
        "write_ports": write_ports
    })
```

### 2.7 Control Flow Helpers

```python
# pycmt2/control.py

def seq(*children):
    """Sequential composition."""
    return SeqControl(children)

def par(*children):
    """Parallel composition."""
    return ParControl(children)

def if_(cond, then_body, else_body=None):
    """Conditional execution."""
    return IfControl(cond, then_body, else_body)

def while_(cond, body):
    """While loop."""
    return WhileControl(cond, body)

def enable(group_name):
    """Enable a group."""
    return EnableControl(group_name)

def invoke(instance, method, **kwargs):
    """Invoke a method on an instance."""
    return InvokeControl(instance, method, kwargs)
```

---

## Part 3: Implementation Plan

### Phase 1: CIRCT Python Bindings (Foundation)

1. Create C API for CMT2 dialect (`circt-c/Dialect/Cmt2.h`)
2. Create nanobind module (`Cmt2Module.cpp`)
3. Add TableGen wrapper for Python op generation
4. Create `cmt2.py` dialect module with operation wrappers
5. Update CMakeLists.txt

**Deliverables:**
- `circt.dialects.cmt2` module
- Ability to construct CMT2 IR from Python

### Phase 2: Core Builder Layer

1. Implement `Builder` class with MLIR context management
2. Implement `ModuleBuilder` with port and body management
3. Implement signal/value classes with operator overloading
4. Implement basic type system

**Deliverables:**
- Basic circuit/module construction
- Port declaration and connection

### Phase 3: Function-Like Operations

1. Implement `RuleBuilder` for rules
2. Implement `MethodBuilder` for action methods
3. Implement `ValueBuilder` for value methods
4. Implement guard and body builders

**Deliverables:**
- Rule/method/value definition API
- Call operation support

### Phase 4: Procedural Control

1. Implement `GroupBuilder` for groups
2. Implement control flow builders (seq, par, if, while)
3. Implement `ProcRuleBuilder` for procedural rules
4. Support invoke operations

**Deliverables:**
- Full procedural control API
- Group definition and enabling

### Phase 5: External Modules and STL

1. Implement external module binding API
2. Create standard library wrappers
3. Implement instance management

**Deliverables:**
- External FIRRTL module integration
- Reg, Wire, FIFO, Memory components

### Phase 6: Testing and Documentation

1. Unit tests for all components
2. Integration tests with CIRCT passes
3. Example programs
4. API documentation

---

## Part 4: Example Programs

### 4.1 Simple Counter

```python
from pycmt2 import Circuit, UInt, Clock, Reset

circuit = Circuit("SimpleCounter")

@circuit.module
def Counter(clk: Clock, rst: Reset) -> UInt(32):
    count = Reg(32, init=0)

    @rule
    def increment():
        guard: True
        body: count.write(count.read() + 1)

    return count.read()

# Emit MLIR
print(circuit.emit_mlir())

# Or emit Verilog via CIRCT pipeline
print(circuit.emit_verilog())
```

### 4.2 GCD Module

```python
from pycmt2 import Circuit, UInt, Clock, Reset
from pycmt2.control import seq, while_, if_

circuit = Circuit("GCD")

@circuit.module
def GCDModule(clk: Clock, rst: Reset, a: UInt(32), b: UInt(32)) -> UInt(32):
    reg_a = Reg(32)
    reg_b = Reg(32)

    @proc_rule
    def compute():
        guard: True
        control: seq(
            enable("load"),
            while_(reg_b.read() != 0,
                if_(reg_a.read() >= reg_b.read(),
                    enable("sub_a"),
                    enable("sub_b")
                )
            )
        )

    @group
    def load():
        reg_a.write(a)
        reg_b.write(b)
        done: True

    @group
    def sub_a():
        reg_a.write(reg_a.read() - reg_b.read())
        done: True

    @group
    def sub_b():
        reg_b.write(reg_b.read() - reg_a.read())
        done: True

    @value
    def result():
        guard: compute.idle()
        return reg_a.read()

    return result()
```

### 4.3 FIFO Usage

```python
from pycmt2 import Circuit
from pycmt2.stl import FIFO

circuit = Circuit("FIFOExample")

@circuit.module
def Producer(clk, rst):
    fifo = FIFO(32, depth=4)
    counter = Reg(32, init=0)

    @rule
    def produce():
        guard: fifo.not_full()
        body:
            fifo.enq(counter.read())
            counter.write(counter.read() + 1)

@circuit.module
def Consumer(clk, rst):
    fifo = FIFO(32, depth=4)
    sum_reg = Reg(32, init=0)

    @rule
    def consume():
        guard: fifo.not_empty()
        body:
            val = fifo.deq()
            sum_reg.write(sum_reg.read() + val)
```

---

## Part 5: Integration with CIRCT

### 5.1 Pass Pipeline Invocation

```python
from pycmt2 import Circuit
from circt.passmanager import PassManager

circuit = Circuit("MyDesign")
# ... build circuit ...

# Get MLIR module
mlir_module = circuit.get_module()

# Run passes
pm = PassManager.parse(
    "builtin.module("
    "cmt2-compile-invoke,"
    "cmt2-tdcc,"
    "cmt2-proc-stmt-to-action,"
    "cmt2-proc-to-gaa,"
    "cmt2-inline-modules,"
    "lower-cmt2-to-firrtl"
    ")"
)
pm.run(mlir_module)

# Export to Verilog
from circt.export import export_verilog
verilog = export_verilog(mlir_module)
```

### 5.2 Direct Verilog Generation

```python
circuit = Circuit("MyDesign")
# ... build circuit ...

# One-liner to Verilog
verilog = circuit.to_verilog()

# Or to file
circuit.to_verilog_file("output.sv")
```

---

## Summary

The PyCmt2 design provides:

1. **CIRCT Integration**: Native Python bindings using MLIR infrastructure
2. **Pythonic API**: Context managers, decorators, operator overloading
3. **Type Safety**: Strong typing with runtime validation
4. **Familiar Patterns**: Similar to calyx-py and PyRTL
5. **Full CMT2 Support**: All CMT2 operations including procedural control
6. **Easy Compilation**: Direct pipeline to Verilog

This design balances low-level MLIR access with high-level ergonomics, making CMT2 accessible to Python users while maintaining full compatibility with the CIRCT ecosystem.
