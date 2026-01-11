#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Function-like builders (Rule, Method, Value) for PyCMT2 EDSL."""

from __future__ import annotations

from contextlib import contextmanager
from typing import TYPE_CHECKING, Iterator

from .types import Cmt2Type, UInt, Bool
from .signals import Signal
from .builders import RegionBuilder
from .refs import MethodRef, ValueRef, RuleRef
from .location import get_python_location, PythonLocation

if TYPE_CHECKING:
    from .module import ModuleBuilder


class GuardBuilder(RegionBuilder):
    """Builder for guard regions.

    Guards are regions that must return a boolean condition. The guard
    determines when a rule/method/value can fire.

    Example:
        with rule.guard() as g:
            ready = g.call(reg, reg.not_empty)
            g.returns(ready)
    """

    def __init__(self, parent, block, loc, ctx):
        super().__init__(block, loc, ctx)
        self._parent = parent
        self._result: Signal | None = None

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None and self._result is None:
            raise ValueError(
                "Guard region must set a result using guard.returns(condition) "
                "or guard.always()/guard.never()"
            )
        super().__exit__(exc_type, exc_val, exc_tb)
        return False

    def returns(self, condition: Signal) -> None:
        """Set the guard condition.

        Args:
            condition: A boolean signal (UInt<1>).

        Raises:
            TypeError: If condition is not a boolean signal.
        """
        if not isinstance(condition.type, UInt) or condition.type.width != 1:
            raise TypeError(
                f"Guard condition must be Bool (UInt<1>), got {condition.type}"
            )
        self._result = condition
        self._emit_guard_return(condition)

    def _emit_guard_return(self, condition: Signal):
        """Emit the return operation for the guard."""
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            cmt2.ReturnOp([condition.value], loc=self._loc)

    def always(self) -> None:
        """Guard that always fires (returns true)."""
        self.returns(self.const(1, 1))

    def never(self) -> None:
        """Guard that never fires (returns false)."""
        self.returns(self.const(0, 1))


class BodyBuilder(RegionBuilder):
    """Builder for body regions.

    Body regions contain the main logic for rules/methods/values.
    They can call methods, perform computations, and return values.

    Example:
        with rule.body() as b:
            val = b.call(reg, reg.read)
            new_val = val + b.const(1, 32)
            b.call(reg, reg.write, new_val)
    """

    def __init__(
        self,
        parent,
        block,
        loc,
        ctx,
        args: list[tuple[str, Signal]] | None = None,
        return_types: list[Cmt2Type] | None = None,
    ):
        super().__init__(block, loc, ctx)
        self._parent = parent
        self._args = {name: sig for name, sig in (args or [])}
        self._return_types = return_types or []
        self._results: list[Signal] = []
        self._has_return = False

    def __exit__(self, exc_type, exc_val, exc_tb):
        # Ensure body block has a return even if empty (for rules)
        if exc_type is None and not self._has_return:
            self._emit_body_return()
        super().__exit__(exc_type, exc_val, exc_tb)
        return False

    def arg(self, name: str) -> Signal:
        """Get an argument by name.

        Args:
            name: The argument name.

        Returns:
            The Signal for the argument.

        Raises:
            KeyError: If the argument doesn't exist.
        """
        if name not in self._args:
            raise KeyError(f"No argument named '{name}'")
        return self._args[name]

    def returns(self, *values: Signal) -> None:
        """Return values from the body.

        Args:
            *values: Signals to return.

        Note:
            If return type hints are specified and the value widths don't match,
            the values are automatically converted (truncated or padded) to match
            the expected return types.
        """
        # Convert values to match expected return types if specified
        converted_values = list(values)
        if self._return_types and len(self._return_types) == len(values):
            for i, (val, expected_type) in enumerate(zip(values, self._return_types)):
                expected_width = expected_type.bit_width()
                actual_width = val.type.bit_width()
                if actual_width != expected_width:
                    converted_values[i] = self.convert_width(val, expected_width)

        self._results = converted_values
        self._has_return = True
        self._emit_body_return(*converted_values)

    def _emit_body_return(self, *values: Signal):
        """Emit the return operation for the body."""
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            cmt2.ReturnOp([v.value for v in values], loc=self._loc)

    @contextmanager
    def if_(self, condition: Signal) -> Iterator[IfBuilder]:
        """Conditional execution.

        Args:
            condition: Boolean condition.

        Yields:
            An IfBuilder for then/else branches.

        Example:
            with body.if_(cond) as if_:
                with if_.then_() as then_b:
                    then_b.call(reg, reg.write, val1)
                with if_.else_() as else_b:
                    else_b.call(reg, reg.write, val2)
        """
        builder = IfBuilder(self, condition)
        yield builder
        builder._finalize()


class IfBuilder:
    """Builder for conditional execution."""

    def __init__(self, parent: BodyBuilder, condition: Signal):
        self._parent = parent
        self._condition = condition
        self._then_builder: BodyBuilder | None = None
        self._else_builder: BodyBuilder | None = None
        self._op = None

    @contextmanager
    def then_(self) -> Iterator[BodyBuilder]:
        """The 'then' branch."""
        from circt.ir import InsertionPoint, Block
        from circt.dialects import cmt2

        # Create the if op
        with InsertionPoint(self._parent._block):
            self._op = cmt2.IfOp(
                [],  # no results for now
                self._condition.value,
                loc=self._parent._loc,
            )

        # Create then block
        then_block = Block.create_at_start(self._op.thenRegion)
        self._then_builder = BodyBuilder(
            self._parent._parent,
            then_block,
            self._parent._loc,
            self._parent._ctx,
        )
        with self._then_builder as b:
            yield b

    @contextmanager
    def else_(self) -> Iterator[BodyBuilder]:
        """The 'else' branch (optional)."""
        from circt.ir import Block

        if self._op is None:
            raise RuntimeError("Must call then_() before else_()")

        # Create else block
        else_block = Block.create_at_start(self._op.elseRegion)
        self._else_builder = BodyBuilder(
            self._parent._parent,
            else_block,
            self._parent._loc,
            self._parent._ctx,
        )
        with self._else_builder as b:
            yield b

    def _finalize(self):
        """Finalize the if operation."""
        pass


class RuleBuilder:
    """Builder for CMT2 rules.

    Rules are the top-level scheduling unit in CMT2. They have a guard
    region (when to fire) and a body region (what to do).

    Example:
        with mod.rule("increment") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                val = b.call(reg, reg.read)
                b.call(reg, reg.write, val + b.const(1, 32))
    """

    def __init__(self, module: ModuleBuilder, name: str | None):
        self._module = module
        self._name = name
        self._guard_builder: GuardBuilder | None = None
        self._body_builder: BodyBuilder | None = None
        self._op = None

        # Capture Python source location for debugging
        # depth=3 to skip: __init__ -> rule() -> contextmanager wrapper -> user code
        self._python_loc = get_python_location(depth=3)

        # Create the rule op
        self._create_rule_op()

    def _create_rule_op(self):
        """Create the MLIR rule operation."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FunctionType, TypeAttr
        from circt.dialects import cmt2

        # Use Python source location for better error messages
        mlir_loc = self._python_loc.to_mlir_location(
            self._module._circuit._ctx.mlir_context
        )

        with InsertionPoint(self._module._op.body):
            # Rule type: () -> ()
            func_type = FunctionType.get([], [])

            self._op = cmt2.RuleOp(
                sym_name=StringAttr.get(self.name),
                function_type=TypeAttr.get(func_type),
                argNames=ArrayAttr.get([]),
                bodyResNames=ArrayAttr.get([]),
                loc=mlir_loc,
            )

            # Create guard and body blocks
            guard_block = Block.create_at_start(self._op.guard)
            body_block = Block.create_at_start(self._op.body)

    @property
    def name(self) -> str:
        """Get the rule name."""
        if self._name is None:
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"rule_{id(self):x}"
        return self._name

    @contextmanager
    def guard(self) -> Iterator[GuardBuilder]:
        """Enter the guard region.

        Yields:
            A GuardBuilder for defining the guard condition.
        """
        guard_block = self._op.guard_block
        self._guard_builder = GuardBuilder(
            self,
            guard_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
        )
        with self._guard_builder as g:
            yield g

    @contextmanager
    def body(self) -> Iterator[BodyBuilder]:
        """Enter the body region.

        Yields:
            A BodyBuilder for defining the rule body.
        """
        body_block = self._op.body_block
        self._body_builder = BodyBuilder(
            self,
            body_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
        )
        with self._body_builder as b:
            yield b

    def ref(self) -> RuleRef:
        """Get a reference to this rule for scheduling directives.

        Returns:
            A RuleRef for use with precedence() and other scheduling directives.
        """
        return RuleRef(self, self.name)

    def _finalize(self):
        """Finalize rule construction."""
        if self._guard_builder is None:
            raise ValueError(f"Rule '{self.name}' must have a guard region")
        if self._body_builder is None:
            raise ValueError(f"Rule '{self.name}' must have a body region")


class MethodBuilder:
    """Builder for CMT2 methods.

    Methods are action functions with a ready-enable protocol. They can
    have arguments, perform side effects, and return values.

    Example:
        with mod.method("write", args=[("data", UInt(32))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as b:
                data = b.arg("data")
                b.call(reg, reg.write, data)
    """

    def __init__(
        self,
        module: ModuleBuilder,
        name: str | None,
        args: list[tuple[str, Cmt2Type]],
        returns: list[Cmt2Type],
    ):
        self._module = module
        self._name = name
        self._arg_types = args
        self._return_types = returns
        self._guard_builder: GuardBuilder | None = None
        self._body_builder: BodyBuilder | None = None
        self._op = None
        self._arg_signals: list[tuple[str, Signal]] = []

        # Capture Python source location for debugging
        # depth=3 to skip: __init__ -> method() -> contextmanager wrapper -> user code
        self._python_loc = get_python_location(depth=3)

        # Create the method op
        self._create_method_op()

    def _create_method_op(self):
        """Create the MLIR method operation.

        Note: Atomic methods do NOT support timing attributes (static_latency, interval).
        Timing is only valid for procedural methods (proc_method) which execute over
        multiple cycles. Atomic methods are always single-cycle by definition.
        """
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FunctionType, TypeAttr

        from circt.dialects import cmt2

        ctx = self._module._circuit._ctx

        # Use Python source location for better error messages
        mlir_loc = self._python_loc.to_mlir_location(ctx.mlir_context)

        with InsertionPoint(self._module._op.body):
            # Build function type
            arg_mlir_types = [ty.to_firrtl_type(ctx.mlir_context) for _, ty in self._arg_types]
            ret_mlir_types = [ty.to_firrtl_type(ctx.mlir_context) for ty in self._return_types]
            func_type = FunctionType.get(arg_mlir_types, ret_mlir_types)

            arg_names = [StringAttr.get(name) for name, _ in self._arg_types]
            body_res_names = [StringAttr.get(f"res{i}") for i in range(len(self._return_types))]

            self._op = cmt2.MethodOp(
                sym_name=StringAttr.get(self.name),
                function_type=TypeAttr.get(func_type),
                argNames=ArrayAttr.get(arg_names),
                bodyResNames=ArrayAttr.get(body_res_names),
                loc=mlir_loc,
            )

            # Create guard and body blocks with arguments
            arg_locs = [mlir_loc] * len(arg_mlir_types)
            guard_block = Block.create_at_start(self._op.guard, arg_mlir_types, arg_locs)
            body_block = Block.create_at_start(self._op.body, arg_mlir_types, arg_locs)

            # Create signals for arguments
            from .builders import RegionBuilder
            dummy_builder = RegionBuilder(body_block, ctx.location, ctx)
            for i, (arg_name, arg_ty) in enumerate(self._arg_types):
                sig = Signal(body_block.arguments[i], arg_ty, dummy_builder)
                self._arg_signals.append((arg_name, sig))

    @property
    def name(self) -> str:
        """Get the method name."""
        if self._name is None:
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"method_{id(self):x}"
        return self._name

    @contextmanager
    def guard(self) -> Iterator[GuardBuilder]:
        """Enter the guard region."""
        guard_block = self._op.guard_block
        self._guard_builder = GuardBuilder(
            self,
            guard_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
        )
        with self._guard_builder as g:
            # Expose arguments in guard
            for arg_name, sig in self._arg_signals:
                setattr(g, arg_name, sig)
            yield g

    @contextmanager
    def body(self) -> Iterator[BodyBuilder]:
        """Enter the body region."""
        body_block = self._op.body_block
        self._body_builder = BodyBuilder(
            self,
            body_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
            args=self._arg_signals,
            return_types=self._return_types,
        )
        with self._body_builder as b:
            yield b

    def ref(self) -> MethodRef:
        """Get a reference to this method for scheduling."""
        return MethodRef(self, None, self.name)

    def _finalize(self):
        """Finalize method construction."""
        if self._guard_builder is None:
            raise ValueError(f"Method '{self.name}' must have a guard region")
        if self._body_builder is None:
            raise ValueError(f"Method '{self.name}' must have a body region")


class ValueBuilder:
    """Builder for CMT2 value methods.

    Value methods are read-only functions that return data. They have
    a guard (ready signal) and a body that computes the return values.

    Example:
        with mod.value("read", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                data = b.call(reg, reg.read)
                b.returns(data)
    """

    def __init__(
        self,
        module: ModuleBuilder,
        name: str | None,
        returns: list[Cmt2Type],
    ):
        self._module = module
        self._name = name
        self._return_types = returns
        self._guard_builder: GuardBuilder | None = None
        self._body_builder: BodyBuilder | None = None
        self._op = None

        # Capture Python source location for debugging
        # depth=3 to skip: __init__ -> value() -> contextmanager wrapper -> user code
        self._python_loc = get_python_location(depth=3)

        # Create the value op
        self._create_value_op()

    def _create_value_op(self):
        """Create the MLIR value operation."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FunctionType, TypeAttr
        from circt.dialects import cmt2

        ctx = self._module._circuit._ctx

        # Use Python source location for better error messages
        mlir_loc = self._python_loc.to_mlir_location(ctx.mlir_context)

        with InsertionPoint(self._module._op.body):
            # Build function type
            ret_mlir_types = [ty.to_firrtl_type(ctx.mlir_context) for ty in self._return_types]
            func_type = FunctionType.get([], ret_mlir_types)

            body_res_names = [StringAttr.get(f"res{i}") for i in range(len(self._return_types))]

            self._op = cmt2.ValueOp(
                sym_name=StringAttr.get(self.name),
                function_type=TypeAttr.get(func_type),
                argNames=ArrayAttr.get([]),
                bodyResNames=ArrayAttr.get(body_res_names),
                loc=mlir_loc,
            )

            # Create guard and body blocks
            guard_block = Block.create_at_start(self._op.guard)
            body_block = Block.create_at_start(self._op.body)

    @property
    def name(self) -> str:
        """Get the value name."""
        if self._name is None:
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"value_{id(self):x}"
        return self._name

    @contextmanager
    def guard(self) -> Iterator[GuardBuilder]:
        """Enter the guard region."""
        guard_block = self._op.guard_block
        self._guard_builder = GuardBuilder(
            self,
            guard_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
        )
        with self._guard_builder as g:
            yield g

    @contextmanager
    def body(self) -> Iterator[BodyBuilder]:
        """Enter the body region."""
        body_block = self._op.body_block
        self._body_builder = BodyBuilder(
            self,
            body_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
            return_types=self._return_types,
        )
        with self._body_builder as b:
            yield b

    def ref(self) -> ValueRef:
        """Get a reference to this value for scheduling."""
        return ValueRef(self, None, self.name)

    def _finalize(self):
        """Finalize value construction."""
        if self._guard_builder is None:
            raise ValueError(f"Value '{self.name}' must have a guard region")
        if self._body_builder is None:
            raise ValueError(f"Value '{self.name}' must have a body region")
