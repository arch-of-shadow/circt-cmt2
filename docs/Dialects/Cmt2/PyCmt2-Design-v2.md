# PyCmt2: Python Frontend for CMT2 (Revised Design)

## Overview

This document outlines the revised design for a Python frontend for the CMT2 dialect, incorporating feedback on API design preferences:

1. **Context manager-based API** (primary) over decorator/lambda patterns
2. **Strong typing** with Python type hints and runtime validation
3. **Region-aware guards** - guards are always regions, not simple expressions
4. **No continuous assignment** - only CMT2-supported operations
5. **JIT-based naming** - use Python introspection to reduce naming boilerplate
6. **Directive support** - scheduling directives (sequenceBefore, conflict, conflictFree)
7. **Object references** - avoid string indexing for groups/methods

---

## Part 1: Type System Design

### 1.1 Core Types

```python
from __future__ import annotations
from typing import TypeVar, Generic, overload, Literal, Union
from dataclasses import dataclass
from abc import ABC, abstractmethod

# Width type variable for parameterized integer types
W = TypeVar('W', bound=int)

@dataclass(frozen=True)
class Cmt2Type(ABC):
    """Base class for all CMT2 types."""

    @abstractmethod
    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        """Convert to FIRRTL MLIR type."""
        ...

    @abstractmethod
    def bit_width(self) -> int:
        """Return the bit width of this type."""
        ...

@dataclass(frozen=True)
class UInt(Cmt2Type):
    """Unsigned integer type with static width."""
    width: int

    def __post_init__(self):
        if self.width <= 0:
            raise ValueError(f"Width must be positive, got {self.width}")

    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        return firrtl.uint_type(ctx.mlir_ctx, self.width)

    def bit_width(self) -> int:
        return self.width

@dataclass(frozen=True)
class SInt(Cmt2Type):
    """Signed integer type with static width."""
    width: int

    def __post_init__(self):
        if self.width <= 0:
            raise ValueError(f"Width must be positive, got {self.width}")

    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        return firrtl.sint_type(ctx.mlir_ctx, self.width)

    def bit_width(self) -> int:
        return self.width

@dataclass(frozen=True)
class ClockType(Cmt2Type):
    """Clock signal type."""

    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        return firrtl.clock_type(ctx.mlir_ctx)

    def bit_width(self) -> int:
        return 1

@dataclass(frozen=True)
class ResetType(Cmt2Type):
    """Synchronous reset type."""

    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        return firrtl.uint_type(ctx.mlir_ctx, 1)

    def bit_width(self) -> int:
        return 1

@dataclass(frozen=True)
class AsyncResetType(Cmt2Type):
    """Asynchronous reset type."""

    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        return firrtl.async_reset_type(ctx.mlir_ctx)

    def bit_width(self) -> int:
        return 1

@dataclass(frozen=True)
class Bundle(Cmt2Type):
    """Bundle (struct) type with named fields."""
    fields: tuple[tuple[str, Cmt2Type, bool], ...]  # (name, type, is_flip)

    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        return firrtl.bundle_type(ctx.mlir_ctx, [
            (name, ty.to_firrtl_type(ctx), flip)
            for name, ty, flip in self.fields
        ])

    def bit_width(self) -> int:
        return sum(ty.bit_width() for _, ty, _ in self.fields)

@dataclass(frozen=True)
class Vector(Cmt2Type):
    """Vector (array) type."""
    element: Cmt2Type
    size: int

    def to_firrtl_type(self, ctx: 'Context') -> 'MlirType':
        return firrtl.vector_type(ctx.mlir_ctx,
                                   self.element.to_firrtl_type(ctx),
                                   self.size)

    def bit_width(self) -> int:
        return self.element.bit_width() * self.size

# Type aliases for common patterns
Clock = ClockType()
Reset = ResetType()
AsyncReset = AsyncResetType()
Bool = UInt(1)
```

### 1.2 Typed Signal Wrappers

```python
from typing import TypeVar, Generic

T = TypeVar('T', bound=Cmt2Type)

class Signal(Generic[T]):
    """A typed signal wrapper that tracks MLIR value and type."""

    __slots__ = ('_value', '_type', '_builder')

    def __init__(self, value: 'MlirValue', ty: T, builder: 'RegionBuilder'):
        self._value = value
        self._type = ty
        self._builder = builder

    @property
    def value(self) -> 'MlirValue':
        """Get the underlying MLIR value."""
        return self._value

    @property
    def type(self) -> T:
        """Get the CMT2 type."""
        return self._type

    # Arithmetic operators (only for integer types)
    def __add__(self, other: 'Signal[UInt] | Signal[SInt] | int') -> 'Signal':
        return self._builder.add(self, other)

    def __sub__(self, other: 'Signal[UInt] | Signal[SInt] | int') -> 'Signal':
        return self._builder.sub(self, other)

    def __mul__(self, other: 'Signal[UInt] | Signal[SInt] | int') -> 'Signal':
        return self._builder.mul(self, other)

    # Bitwise operators
    def __and__(self, other: 'Signal | int') -> 'Signal':
        return self._builder.and_(self, other)

    def __or__(self, other: 'Signal | int') -> 'Signal':
        return self._builder.or_(self, other)

    def __xor__(self, other: 'Signal | int') -> 'Signal':
        return self._builder.xor_(self, other)

    def __invert__(self) -> 'Signal':
        return self._builder.not_(self)

    # Comparison operators (return Signal[Bool])
    def __eq__(self, other: 'Signal | int') -> 'Signal[UInt]':  # type: ignore
        return self._builder.eq(self, other)

    def __ne__(self, other: 'Signal | int') -> 'Signal[UInt]':  # type: ignore
        return self._builder.neq(self, other)

    def __lt__(self, other: 'Signal | int') -> 'Signal[UInt]':
        return self._builder.lt(self, other)

    def __le__(self, other: 'Signal | int') -> 'Signal[UInt]':
        return self._builder.le(self, other)

    def __gt__(self, other: 'Signal | int') -> 'Signal[UInt]':
        return self._builder.gt(self, other)

    def __ge__(self, other: 'Signal | int') -> 'Signal[UInt]':
        return self._builder.ge(self, other)

    # Bit extraction
    def __getitem__(self, key: int | slice) -> 'Signal[UInt]':
        if isinstance(key, slice):
            return self._builder.bits(self, key.start, key.stop)
        return self._builder.bit(self, key)

    # Concatenation
    def concat(self, *others: 'Signal') -> 'Signal[UInt]':
        return self._builder.concat(self, *others)
```

---

## Part 2: Context Manager-Based API

### 2.1 Core Builder Architecture

```python
from contextlib import contextmanager
from contextvars import ContextVar
import inspect

# Context variable to track the current builder
_current_builder: ContextVar['RegionBuilder | None'] = ContextVar('current_builder', default=None)

def get_current_builder() -> 'RegionBuilder':
    """Get the current region builder or raise if none."""
    builder = _current_builder.get()
    if builder is None:
        raise RuntimeError("No active region builder. Use within a context manager.")
    return builder

class RegionBuilder:
    """Base builder for regions that can contain expressions."""

    def __init__(self, mlir_builder: 'MlirOpBuilder', loc: 'MlirLocation', ctx: 'Context'):
        self._mlir_builder = mlir_builder
        self._loc = loc
        self._ctx = ctx
        self._parent_builder: RegionBuilder | None = None

    def __enter__(self) -> 'RegionBuilder':
        self._parent_builder = _current_builder.get()
        _current_builder.set(self)
        return self

    def __exit__(self, *args):
        _current_builder.set(self._parent_builder)

    # Expression building methods
    def const(self, value: int, width: int) -> Signal[UInt]:
        """Create a constant unsigned integer."""
        mlir_val = self._create_const(value, width)
        return Signal(mlir_val, UInt(width), self)

    def add(self, a: Signal, b: Signal | int) -> Signal:
        """Add two signals."""
        if isinstance(b, int):
            b = self.const(b, a.type.bit_width())
        mlir_val = self._create_add(a.value, b.value)
        # Result type depends on input types
        result_width = max(a.type.bit_width(), b.type.bit_width()) + 1
        return Signal(mlir_val, UInt(result_width), self)

    # ... other expression builders

    def call(self, target: 'Instance | Self', method: 'MethodRef | ValueRef',
             *args: Signal) -> tuple[Signal, ...] | Signal | None:
        """Call a method or value on an instance or self."""
        # Implementation details
        ...
```

### 2.2 Circuit and Module Builders

```python
class Circuit:
    """Top-level circuit container."""

    def __init__(self, name: str | None = None):
        # JIT: If name is None, infer from variable assignment
        self._name = name
        self._ctx = Context()
        self._modules: dict[str, Module] = {}
        self._external_modules: dict[str, ExternalModule] = {}
        self._interfaces: dict[str, Interface] = {}

        # Create MLIR circuit op
        self._op = self._ctx.create_circuit_op(self._resolve_name())

    def _resolve_name(self) -> str:
        """Resolve name using JIT introspection if not provided."""
        if self._name is not None:
            return self._name
        # Use frame introspection to find variable name
        frame = inspect.currentframe()
        if frame and frame.f_back and frame.f_back.f_back:
            outer_frame = frame.f_back.f_back
            # Look for assignment target in bytecode
            # This is a simplified version; full implementation uses dis module
            for name, val in outer_frame.f_locals.items():
                if val is self:
                    return name
        return "Circuit"

    @contextmanager
    def module(self, name: str | None = None) -> 'ModuleBuilder':
        """Create a module within this circuit."""
        builder = ModuleBuilder(self, name)
        yield builder
        builder._finalize()
        self._modules[builder.name] = builder._module

    @contextmanager
    def external_module(self, firrtl_name: str, name: str | None = None) -> 'ExternalModuleBuilder':
        """Create an external FIRRTL module binding."""
        builder = ExternalModuleBuilder(self, firrtl_name, name)
        yield builder
        builder._finalize()
        self._external_modules[builder.name] = builder._module

    @contextmanager
    def interface(self, name: str | None = None) -> 'InterfaceBuilder':
        """Define an interface."""
        builder = InterfaceBuilder(self, name)
        yield builder
        builder._finalize()
        self._interfaces[builder.name] = builder._interface

    def emit_mlir(self) -> str:
        """Emit the circuit as MLIR text."""
        return self._ctx.emit_mlir(self._op)

    def to_verilog(self) -> str:
        """Run the full pipeline and emit Verilog."""
        return self._ctx.run_pipeline_and_emit_verilog(self._op)


class ModuleBuilder:
    """Builder for CMT2 modules."""

    def __init__(self, circuit: Circuit, name: str | None = None):
        self._circuit = circuit
        self._name = name
        self._args: list[tuple[str, Cmt2Type, Signal]] = []
        self._instances: dict[str, Instance] = {}
        self._rules: dict[str, Rule] = {}
        self._methods: dict[str, Method] = {}
        self._values: dict[str, Value] = {}
        self._proc_rules: dict[str, ProcRule] = {}
        self._proc_methods: dict[str, ProcMethod] = {}
        self._groups: dict[str, Group] = {}

        # Scheduling directives
        self._sequence_before: list[tuple[MethodRef | ValueRef, MethodRef | ValueRef]] = []
        self._conflict: list[tuple[MethodRef | ValueRef, MethodRef | ValueRef]] = []
        self._conflict_free: list[tuple[MethodRef | ValueRef, MethodRef | ValueRef]] = []

    @property
    def name(self) -> str:
        if self._name is None:
            # JIT: resolve from context
            self._name = self._resolve_name_from_context()
        return self._name

    # Port declarations
    def clock(self, name: str = "clk") -> Signal[ClockType]:
        """Add a clock port."""
        sig = self._add_port(name, Clock)
        return sig

    def reset(self, name: str = "rst") -> Signal[ResetType]:
        """Add a reset port."""
        sig = self._add_port(name, Reset)
        return sig

    def input(self, name: str, ty: Cmt2Type) -> Signal:
        """Add an input port."""
        return self._add_port(name, ty)

    def output(self, name: str, ty: Cmt2Type) -> Signal:
        """Add an output port."""
        return self._add_port(name, ty, is_output=True)

    # Instance creation
    def instance(self, module: 'Module | ExternalModule',
                 name: str | None = None,
                 interface_bindings: dict['InterfaceDecl', 'InterfaceDef'] | None = None,
                 **port_connections) -> 'Instance':
        """Create an instance of another module."""
        inst_name = name if name else self._generate_instance_name(module)
        inst = Instance(inst_name, module, port_connections, interface_bindings)
        self._instances[inst_name] = inst
        return inst

    # Function-like operations with context managers
    @contextmanager
    def rule(self, name: str | None = None) -> 'RuleBuilder':
        """Define a rule."""
        builder = RuleBuilder(self, name)
        yield builder
        builder._finalize()
        self._rules[builder.name] = builder._rule

    @contextmanager
    def method(self, name: str | None = None,
               args: list[tuple[str, Cmt2Type]] | None = None,
               returns: list[Cmt2Type] | None = None) -> 'MethodBuilder':
        """Define a method."""
        builder = MethodBuilder(self, name, args or [], returns or [])
        yield builder
        builder._finalize()
        self._methods[builder.name] = builder._method

    @contextmanager
    def value(self, name: str | None = None,
              returns: list[Cmt2Type] | None = None) -> 'ValueBuilder':
        """Define a value method."""
        builder = ValueBuilder(self, name, returns or [])
        yield builder
        builder._finalize()
        self._values[builder.name] = builder._value

    # Procedural operations
    @contextmanager
    def proc_rule(self, name: str | None = None) -> 'ProcRuleBuilder':
        """Define a procedural rule with multi-cycle control."""
        builder = ProcRuleBuilder(self, name)
        yield builder
        builder._finalize()
        self._proc_rules[builder.name] = builder._proc_rule

    @contextmanager
    def proc_method(self, name: str | None = None,
                    args: list[tuple[str, Cmt2Type]] | None = None,
                    returns: list[Cmt2Type] | None = None) -> 'ProcMethodBuilder':
        """Define a procedural method."""
        builder = ProcMethodBuilder(self, name, args or [], returns or [])
        yield builder
        builder._finalize()
        self._proc_methods[builder.name] = builder._proc_method

    @contextmanager
    def group(self, name: str | None = None) -> 'GroupBuilder':
        """Define a procedural group."""
        builder = GroupBuilder(self, name)
        yield builder
        builder._finalize()
        self._groups[builder.name] = builder._group

    @contextmanager
    def static_group(self, latency: int, name: str | None = None) -> 'StaticGroupBuilder':
        """Define a static latency group."""
        builder = StaticGroupBuilder(self, name, latency)
        yield builder
        builder._finalize()
        self._groups[builder.name] = builder._group

    # Scheduling directives using object references
    def sequence_before(self, before: 'MethodRef | ValueRef',
                        after: 'MethodRef | ValueRef') -> 'ModuleBuilder':
        """Declare that 'before' must execute before 'after'."""
        self._sequence_before.append((before, after))
        return self

    def conflict(self, a: 'MethodRef | ValueRef',
                 b: 'MethodRef | ValueRef') -> 'ModuleBuilder':
        """Declare that 'a' and 'b' conflict (cannot execute together)."""
        self._conflict.append((a, b))
        return self

    def conflict_free(self, a: 'MethodRef | ValueRef',
                      b: 'MethodRef | ValueRef') -> 'ModuleBuilder':
        """Declare that 'a' and 'b' are conflict-free."""
        self._conflict_free.append((a, b))
        return self

    # Interface support
    def interface_decl(self, interface: 'Interface',
                       name: str | None = None) -> 'InterfaceDecl':
        """Declare an interface (to be bound during instantiation)."""
        decl = InterfaceDecl(interface, name or interface.name)
        return decl

    def interface_def(self, interface: 'Interface',
                      bindings: dict[str, tuple['Instance', 'MethodRef | ValueRef']],
                      name: str | None = None) -> 'InterfaceDef':
        """Define an interface implementation."""
        defn = InterfaceDef(interface, bindings, name or interface.name)
        return defn
```

### 2.3 Guard Region Builder (Region-Aware)

Guards are always regions in CMT2. The context manager ensures proper region construction:

```python
class GuardBuilder(RegionBuilder):
    """Builder for guard regions that must return a boolean condition."""

    def __init__(self, parent: 'FunctionLikeBuilder', mlir_builder: 'MlirOpBuilder',
                 loc: 'MlirLocation', ctx: 'Context'):
        super().__init__(mlir_builder, loc, ctx)
        self._parent = parent
        self._result: Signal[UInt] | None = None

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None and self._result is None:
            raise ValueError("Guard region must set a result using guard.returns(condition)")
        super().__exit__(exc_type, exc_val, exc_tb)

    def returns(self, condition: Signal[UInt]) -> None:
        """Set the guard condition (must be Bool/UInt<1>)."""
        if not isinstance(condition.type, UInt) or condition.type.width != 1:
            raise TypeError(f"Guard condition must be Bool (UInt<1>), got {condition.type}")
        self._result = condition
        self._emit_guard_return(condition)

    # Convenience methods for common guard patterns
    def always(self) -> None:
        """Guard that always fires (returns true)."""
        self.returns(self.const(1, 1))

    def never(self) -> None:
        """Guard that never fires (returns false)."""
        self.returns(self.const(0, 1))


class BodyBuilder(RegionBuilder):
    """Builder for body regions of rules/methods/values."""

    def __init__(self, parent: 'FunctionLikeBuilder', mlir_builder: 'MlirOpBuilder',
                 loc: 'MlirLocation', ctx: 'Context',
                 args: list[tuple[str, Signal]] | None = None):
        super().__init__(mlir_builder, loc, ctx)
        self._parent = parent
        self._args = {name: sig for name, sig in (args or [])}
        self._results: list[Signal] = []

    def arg(self, name: str) -> Signal:
        """Get an argument by name."""
        if name not in self._args:
            raise KeyError(f"No argument named '{name}'")
        return self._args[name]

    def returns(self, *values: Signal) -> None:
        """Return values from this body."""
        self._results = list(values)
        self._emit_body_return(*values)

    # Conditional execution
    @contextmanager
    def if_(self, condition: Signal[UInt]) -> 'IfBuilder':
        """Conditional execution."""
        builder = IfBuilder(self, condition)
        yield builder
        builder._finalize()

    # Method/value calls - using object references, not strings
    def call(self, instance: 'Instance | Self', method: 'MethodRef | ValueRef',
             *args: Signal) -> tuple[Signal, ...]:
        """Call a method or value on an instance."""
        return self._emit_call(instance, method, args)
```

### 2.4 Rule/Method/Value Builders

```python
class RuleBuilder:
    """Builder for CMT2 rules."""

    def __init__(self, module: ModuleBuilder, name: str | None):
        self._module = module
        self._name = name
        self._guard_builder: GuardBuilder | None = None
        self._body_builder: BodyBuilder | None = None

    @property
    def name(self) -> str:
        if self._name is None:
            self._name = self._resolve_name_from_context()
        return self._name

    @contextmanager
    def guard(self) -> GuardBuilder:
        """Enter the guard region."""
        self._guard_builder = GuardBuilder(self, ...)
        with self._guard_builder as g:
            yield g

    @contextmanager
    def body(self) -> BodyBuilder:
        """Enter the body region."""
        self._body_builder = BodyBuilder(self, ...)
        with self._body_builder as b:
            yield b

    def _finalize(self):
        """Finalize the rule construction."""
        if self._guard_builder is None:
            raise ValueError(f"Rule '{self.name}' must have a guard region")
        if self._body_builder is None:
            raise ValueError(f"Rule '{self.name}' must have a body region")


class MethodBuilder:
    """Builder for CMT2 methods."""

    def __init__(self, module: ModuleBuilder, name: str | None,
                 args: list[tuple[str, Cmt2Type]], returns: list[Cmt2Type]):
        self._module = module
        self._name = name
        self._arg_types = args
        self._return_types = returns
        self._guard_builder: GuardBuilder | None = None
        self._body_builder: BodyBuilder | None = None

    @property
    def name(self) -> str:
        if self._name is None:
            self._name = self._resolve_name_from_context()
        return self._name

    @contextmanager
    def guard(self) -> GuardBuilder:
        """Enter the guard region (has access to method arguments)."""
        self._guard_builder = GuardBuilder(self, ...)
        with self._guard_builder as g:
            # Expose arguments in guard region
            for arg_name, _ in self._arg_types:
                setattr(g, arg_name, g._args[arg_name])
            yield g

    @contextmanager
    def body(self) -> BodyBuilder:
        """Enter the body region (has access to method arguments)."""
        self._body_builder = BodyBuilder(self, ..., args=self._make_arg_signals())
        with self._body_builder as b:
            yield b

    def ref(self) -> 'MethodRef':
        """Get a reference to this method for scheduling directives."""
        return MethodRef(self)


class ValueBuilder:
    """Builder for CMT2 value methods."""

    def __init__(self, module: ModuleBuilder, name: str | None,
                 returns: list[Cmt2Type]):
        self._module = module
        self._name = name
        self._return_types = returns
        self._guard_builder: GuardBuilder | None = None
        self._body_builder: BodyBuilder | None = None

    @contextmanager
    def guard(self) -> GuardBuilder:
        """Enter the guard region."""
        self._guard_builder = GuardBuilder(self, ...)
        with self._guard_builder as g:
            yield g

    @contextmanager
    def body(self) -> BodyBuilder:
        """Enter the body region."""
        self._body_builder = BodyBuilder(self, ...)
        with self._body_builder as b:
            yield b

    def ref(self) -> 'ValueRef':
        """Get a reference to this value for scheduling directives."""
        return ValueRef(self)
```

---

## Part 3: Procedural Control API

### 3.1 Group and Control Builders

```python
class GroupBuilder(RegionBuilder):
    """Builder for procedural groups (go-done interface)."""

    def __init__(self, module: ModuleBuilder, name: str | None):
        super().__init__(...)
        self._module = module
        self._name = name
        self._done_set = False

    @property
    def name(self) -> str:
        if self._name is None:
            self._name = self._resolve_name_from_context()
        return self._name

    def done(self, condition: Signal[UInt]) -> None:
        """Signal that the group is done."""
        if not isinstance(condition.type, UInt) or condition.type.width != 1:
            raise TypeError("Done condition must be Bool")
        self._emit_group_done(condition)
        self._done_set = True

    def ref(self) -> 'GroupRef':
        """Get a reference to this group for control flow."""
        return GroupRef(self)


class ControlBuilder:
    """Builder for procedural control flow."""

    def __init__(self, parent: 'ProcRuleBuilder | ProcMethodBuilder'):
        self._parent = parent
        self._mlir_builder = parent._control_builder

    @contextmanager
    def seq(self) -> 'ControlBuilder':
        """Sequential composition - execute children one after another."""
        seq_builder = ControlBuilder(self._parent)
        seq_builder._enter_seq()
        yield seq_builder
        seq_builder._exit_seq()

    @contextmanager
    def par(self) -> 'ControlBuilder':
        """Parallel composition - execute children concurrently."""
        par_builder = ControlBuilder(self._parent)
        par_builder._enter_par()
        yield par_builder
        par_builder._exit_par()

    @contextmanager
    def if_(self, condition: Signal[UInt]) -> 'IfControlBuilder':
        """Conditional control flow."""
        if_builder = IfControlBuilder(self, condition)
        yield if_builder
        if_builder._finalize()

    @contextmanager
    def while_(self, condition: Signal[UInt]) -> 'ControlBuilder':
        """While loop control flow."""
        while_builder = ControlBuilder(self._parent)
        while_builder._enter_while(condition)
        yield while_builder
        while_builder._exit_while()

    # Enable groups using object references, not strings
    def enable(self, group: 'GroupRef') -> None:
        """Enable a group."""
        self._emit_enable(group)

    def invoke(self, instance: 'Instance', method: 'MethodRef',
               *args: Signal) -> tuple[Signal, ...]:
        """Invoke a method within procedural control."""
        return self._emit_invoke(instance, method, args)


class IfControlBuilder:
    """Builder for conditional control flow with then/else branches."""

    def __init__(self, parent: ControlBuilder, condition: Signal[UInt]):
        self._parent = parent
        self._condition = condition
        self._then_builder: ControlBuilder | None = None
        self._else_builder: ControlBuilder | None = None

    @contextmanager
    def then_(self) -> ControlBuilder:
        """The 'then' branch."""
        self._then_builder = ControlBuilder(self._parent._parent)
        yield self._then_builder

    @contextmanager
    def else_(self) -> ControlBuilder:
        """The 'else' branch (optional)."""
        self._else_builder = ControlBuilder(self._parent._parent)
        yield self._else_builder


class ProcRuleBuilder:
    """Builder for procedural rules with guard and control regions."""

    def __init__(self, module: ModuleBuilder, name: str | None):
        self._module = module
        self._name = name
        self._guard_builder: GuardBuilder | None = None
        self._control_builder: ControlBuilder | None = None

    @contextmanager
    def guard(self) -> GuardBuilder:
        """Enter the guard region."""
        self._guard_builder = GuardBuilder(self, ...)
        with self._guard_builder as g:
            yield g

    @contextmanager
    def control(self) -> ControlBuilder:
        """Enter the control region."""
        self._control_builder = ControlBuilder(self)
        yield self._control_builder
```

---

## Part 4: Object References (No String Indexing)

### 4.1 Reference Types

```python
class MethodRef:
    """Reference to a method for scheduling and calls."""

    __slots__ = ('_builder', '_instance')

    def __init__(self, builder: MethodBuilder, instance: 'Instance | None' = None):
        self._builder = builder
        self._instance = instance  # None means @this

    @property
    def name(self) -> str:
        return self._builder.name

    @property
    def instance(self) -> 'Instance | None':
        return self._instance

    def __repr__(self) -> str:
        if self._instance:
            return f"MethodRef(@{self._instance.name}::{self.name})"
        return f"MethodRef(@this::{self.name})"


class ValueRef:
    """Reference to a value method for scheduling and calls."""

    __slots__ = ('_builder', '_instance')

    def __init__(self, builder: ValueBuilder, instance: 'Instance | None' = None):
        self._builder = builder
        self._instance = instance

    @property
    def name(self) -> str:
        return self._builder.name


class GroupRef:
    """Reference to a group for control flow."""

    __slots__ = ('_builder',)

    def __init__(self, builder: GroupBuilder):
        self._builder = builder

    @property
    def name(self) -> str:
        return self._builder.name


class Instance:
    """An instance of a module with typed method/value access."""

    def __init__(self, name: str, module: 'Module | ExternalModule',
                 port_connections: dict[str, Signal],
                 interface_bindings: dict['InterfaceDecl', 'InterfaceDef'] | None):
        self._name = name
        self._module = module
        self._ports = port_connections
        self._interface_bindings = interface_bindings or {}

    @property
    def name(self) -> str:
        return self._name

    def method(self, name: str) -> MethodRef:
        """Get a reference to a method on this instance."""
        method_builder = self._module._methods.get(name)
        if method_builder is None:
            raise KeyError(f"Module '{self._module.name}' has no method '{name}'")
        return MethodRef(method_builder, self)

    def value(self, name: str) -> ValueRef:
        """Get a reference to a value on this instance."""
        value_builder = self._module._values.get(name)
        if value_builder is None:
            raise KeyError(f"Module '{self._module.name}' has no value '{name}'")
        return ValueRef(value_builder, self)

    # Alternative: attribute-style access for methods/values
    def __getattr__(self, name: str) -> MethodRef | ValueRef:
        if name.startswith('_'):
            raise AttributeError(name)
        if name in self._module._methods:
            return MethodRef(self._module._methods[name], self)
        if name in self._module._values:
            return ValueRef(self._module._values[name], self)
        raise AttributeError(f"'{self._module.name}' has no method or value '{name}'")
```

---

## Part 5: JIT Naming Support

### 5.1 Frame Introspection for Auto-Naming

```python
import dis
import sys

def _get_assignment_target() -> str | None:
    """
    Use bytecode introspection to find the variable name being assigned to.

    Example:
        my_rule = builder.rule()  # Returns "my_rule"
    """
    frame = sys._getframe(2)  # Caller's caller
    code = frame.f_code

    # Get the bytecode instruction at the current position
    instructions = list(dis.get_instructions(code))

    # Find the STORE_NAME/STORE_FAST that follows the current call
    # This is a simplified implementation; production code would be more robust
    for i, instr in enumerate(instructions):
        if instr.offset >= frame.f_lasti:
            # Look for STORE_* in the next few instructions
            for j in range(i, min(i + 5, len(instructions))):
                next_instr = instructions[j]
                if next_instr.opname in ('STORE_NAME', 'STORE_FAST', 'STORE_GLOBAL'):
                    return next_instr.argval

    return None


class AutoNamed:
    """Mixin for objects that support automatic naming via JIT."""

    _name: str | None

    def _resolve_name_from_context(self) -> str:
        """Resolve name from assignment context or generate one."""
        name = _get_assignment_target()
        if name:
            return name
        # Fallback: generate a unique name
        return f"_{type(self).__name__}_{id(self):x}"
```

### 5.2 Usage Examples with JIT Naming

```python
from pycmt2 import Circuit, UInt, Clock, Reset

# Name is inferred as "counter_circuit"
counter_circuit = Circuit()

with counter_circuit.module() as mod:  # Name inferred as "mod"
    clk = mod.clock()
    rst = mod.reset()

    # Create a register instance
    count_reg = mod.instance(Reg(32, init=0))  # Name inferred as "count_reg"

    # Rule name inferred from context manager variable
    with mod.rule() as increment:  # Name is "increment"
        with increment.guard() as g:
            g.always()

        with increment.body() as b:
            val = b.call(count_reg, count_reg.read)
            next_val = val + b.const(1, 32)
            b.call(count_reg, count_reg.write, next_val)

# Explicit naming still works
with counter_circuit.module("Counter") as counter:
    ...
```

---

## Part 6: Scheduling Directives

### 6.1 Directive API

```python
class ModuleBuilder:
    # ... (previous code)

    def sequence_before(self, before: MethodRef | ValueRef,
                        after: MethodRef | ValueRef) -> 'ModuleBuilder':
        """
        Declare that 'before' must sequence before 'after'.

        This means if both are called in the same rule, 'before' observes
        state from before 'after' executes.

        Example:
            with mod.method() as read:
                ...
            with mod.method() as write:
                ...
            mod.sequence_before(read.ref(), write.ref())
        """
        self._sequence_before.append((before, after))
        return self

    def conflict(self, a: MethodRef | ValueRef,
                 b: MethodRef | ValueRef) -> 'ModuleBuilder':
        """
        Declare that 'a' and 'b' conflict.

        They cannot be called together in the same rule firing.

        Example:
            mod.conflict(write.ref(), write.ref())  # Write-write conflict
        """
        self._conflict.append((a, b))
        return self

    def conflict_free(self, a: MethodRef | ValueRef,
                      b: MethodRef | ValueRef) -> 'ModuleBuilder':
        """
        Declare that 'a' and 'b' are conflict-free.

        They can execute concurrently without conflict.

        Example:
            mod.conflict_free(read.ref(), read.ref())  # Multiple reads OK
        """
        self._conflict_free.append((a, b))
        return self
```

### 6.2 Directives for External Modules

```python
class ExternalModuleBuilder:
    """Builder for external FIRRTL module bindings."""

    def __init__(self, circuit: Circuit, firrtl_name: str, name: str | None):
        self._circuit = circuit
        self._firrtl_name = firrtl_name
        self._name = name
        self._args: list[tuple[str, Cmt2Type]] = []
        self._bare_bindings: list[tuple[str, str]] = []
        self._method_bindings: dict[str, MethodBinding] = {}
        self._value_bindings: dict[str, ValueBinding] = {}

        # Scheduling
        self._sequence_before: list[tuple[str, str]] = []
        self._conflict: list[tuple[str, str]] = []
        self._conflict_free: list[tuple[str, str]] = []

    def arg(self, name: str, ty: Cmt2Type) -> 'ExternalModuleBuilder':
        """Add a module argument."""
        self._args.append((name, ty))
        return self

    def bind_clock(self, arg_name: str, port_name: str) -> 'ExternalModuleBuilder':
        """Bind a clock argument to a port."""
        self._bare_bindings.append((arg_name, port_name))
        return self

    def bind_reset(self, arg_name: str, port_name: str) -> 'ExternalModuleBuilder':
        """Bind a reset argument to a port."""
        self._bare_bindings.append((arg_name, port_name))
        return self

    def bind_method(self, name: str, *,
                    enable: str | None = None,
                    ready: str | None = None,
                    args: list[str] | None = None,
                    results: list[str] | None = None,
                    arg_types: list[Cmt2Type] | None = None,
                    result_types: list[Cmt2Type] | None = None) -> 'MethodRef':
        """Bind a method to FIRRTL ports."""
        binding = MethodBinding(
            enable=enable, ready=ready,
            args=args or [], results=results or [],
            arg_types=arg_types or [], result_types=result_types or []
        )
        self._method_bindings[name] = binding
        return MethodRef(...)  # Return reference for scheduling

    def bind_value(self, name: str, *,
                   ready: str | None = None,
                   args: list[str] | None = None,
                   results: list[str] | None = None,
                   arg_types: list[Cmt2Type] | None = None,
                   result_types: list[Cmt2Type] | None = None) -> 'ValueRef':
        """Bind a value to FIRRTL ports."""
        binding = ValueBinding(
            ready=ready,
            args=args or [], results=results or [],
            arg_types=arg_types or [], result_types=result_types or []
        )
        self._value_bindings[name] = binding
        return ValueRef(...)

    # Scheduling using method names for external modules
    def sequence_before(self, before: str, after: str) -> 'ExternalModuleBuilder':
        """Declare sequence_before relationship."""
        self._sequence_before.append((before, after))
        return self

    def conflict(self, a: str, b: str) -> 'ExternalModuleBuilder':
        """Declare conflict relationship."""
        self._conflict.append((a, b))
        return self

    def conflict_free(self, a: str, b: str) -> 'ExternalModuleBuilder':
        """Declare conflict-free relationship."""
        self._conflict_free.append((a, b))
        return self
```

---

## Part 7: Complete Example

```python
from pycmt2 import Circuit, UInt, Clock, Reset

# Create circuit (name inferred as "gcd_circuit")
gcd_circuit = Circuit()

with gcd_circuit.module("GCD") as gcd:
    clk = gcd.clock()
    rst = gcd.reset()

    # Create register instances from STL
    reg_a = gcd.instance(Reg(32))
    reg_b = gcd.instance(Reg(32))

    # Define groups for procedural control
    with gcd.group() as load:
        # Load initial values into registers
        a_in = load.input("a", UInt(32))  # External input
        b_in = load.input("b", UInt(32))
        load.call(reg_a, reg_a.write, a_in)
        load.call(reg_b, reg_b.write, b_in)
        load.done(load.const(1, 1))

    with gcd.group() as sub_a:
        a_val = sub_a.call(reg_a, reg_a.read)
        b_val = sub_a.call(reg_b, reg_b.read)
        sub_a.call(reg_a, reg_a.write, a_val - b_val)
        sub_a.done(sub_a.const(1, 1))

    with gcd.group() as sub_b:
        a_val = sub_b.call(reg_a, reg_a.read)
        b_val = sub_b.call(reg_b, reg_b.read)
        sub_b.call(reg_b, reg_b.write, b_val - a_val)
        sub_b.done(sub_b.const(1, 1))

    # Procedural rule with multi-cycle control
    with gcd.proc_rule("compute") as compute:
        with compute.guard() as g:
            # Ready when input is valid
            g.always()  # Simplified

        with compute.control() as ctrl:
            with ctrl.seq():
                ctrl.enable(load.ref())

                with ctrl.while_(lambda: reg_b.read() != ctrl.const(0, 32)):
                    with ctrl.if_(lambda: reg_a.read() >= reg_b.read()) as cond:
                        with cond.then_():
                            ctrl.enable(sub_a.ref())
                        with cond.else_():
                            ctrl.enable(sub_b.ref())

    # Value to read result
    with gcd.value("result", returns=[UInt(32)]) as result:
        with result.guard() as g:
            # Ready when compute is idle
            idle = compute.idle()  # Check if proc_rule FSM is idle
            g.returns(idle)

        with result.body() as b:
            val = b.call(reg_a, reg_a.read)
            b.returns(val)

# Emit MLIR
print(gcd_circuit.emit_mlir())

# Or directly to Verilog
# print(gcd_circuit.to_verilog())
```

---

## Part 8: Implementation Plan

### Phase 1: Core Type System
1. Implement `Cmt2Type` hierarchy with FIRRTL type conversion
2. Implement `Signal` wrapper with operator overloading
3. Add type validation and inference

### Phase 2: CIRCT Bindings
1. Create C API for CMT2 dialect
2. Create nanobind module with type/attribute bindings
3. Add Python op wrappers

### Phase 3: Builder Infrastructure
1. Implement `Context` with MLIR context management
2. Implement `RegionBuilder` base class
3. Implement `GuardBuilder` and `BodyBuilder`

### Phase 4: Module and Circuit
1. Implement `Circuit` with JIT naming
2. Implement `ModuleBuilder` with port management
3. Implement instance creation and management

### Phase 5: Function-Like Operations
1. Implement `RuleBuilder` with guard/body regions
2. Implement `MethodBuilder` with arguments
3. Implement `ValueBuilder`

### Phase 6: Procedural Control
1. Implement `GroupBuilder` with go-done interface
2. Implement `ControlBuilder` for seq/par/if/while
3. Implement `ProcRuleBuilder` and `ProcMethodBuilder`

### Phase 7: References and Scheduling
1. Implement `MethodRef`, `ValueRef`, `GroupRef`
2. Implement scheduling directive support
3. Add validation for directive constraints

### Phase 8: External Modules and STL
1. Implement `ExternalModuleBuilder`
2. Create STL wrappers (Reg, Wire, FIFO, Memory)
3. Add interface support

### Phase 9: Testing and Polish
1. Unit tests for all components
2. Integration tests with CIRCT passes
3. Example programs
4. API documentation

---

## Summary of Design Decisions

| Concern | Decision |
|---------|----------|
| **API Style** | Context managers primary; decorators deferred |
| **Typing** | Strong typing with `Signal[T]` generics and runtime validation |
| **Guards** | Always regions via `GuardBuilder` context manager |
| **Continuous Assignment** | Not supported (not in CMT2) |
| **Naming** | JIT introspection with explicit name fallback |
| **Directives** | `sequence_before`, `conflict`, `conflict_free` using object refs |
| **References** | `MethodRef`, `ValueRef`, `GroupRef` - no string indexing |
| **Instance Access** | `inst.method("name")` or `inst.name` attribute access |
