#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Builder infrastructure for PyCMT2 EDSL.

This module provides the base RegionBuilder class and context management
for building CMT2 IR.
"""

from __future__ import annotations

from contextlib import contextmanager
from contextvars import ContextVar
from typing import TYPE_CHECKING, Iterator

from .signals import Signal
from .types import Cmt2Type, UInt, SInt, Bool

if TYPE_CHECKING:
    from circt.ir import (
        InsertionPoint,
        Location,
        OpView,
        Value as MlirValue,
        Block,
    )
    from .refs import MethodRef, ValueRef, Instance

# Context variable to track the current region builder
_current_builder: ContextVar[RegionBuilder | None] = ContextVar(
    "current_builder", default=None
)


def get_current_builder() -> RegionBuilder:
    """Get the current region builder or raise if none.

    Returns:
        The currently active RegionBuilder.

    Raises:
        RuntimeError: If not within a region builder context.
    """
    builder = _current_builder.get()
    if builder is None:
        raise RuntimeError(
            "No active region builder. Use within a context manager (e.g., with rule.body() as b:)"
        )
    return builder


class RegionBuilder:
    """Base builder for regions that can contain expressions.

    RegionBuilder provides the foundation for building CMT2 IR within
    regions (guard, body, control). It manages the MLIR insertion point
    and provides methods for creating operations.
    """

    def __init__(
        self,
        block: Block,
        loc: Location,
        ctx,
    ):
        self._block = block
        self._loc = loc
        self._ctx = ctx
        self._parent_builder: RegionBuilder | None = None

    def __enter__(self) -> RegionBuilder:
        self._parent_builder = _current_builder.get()
        _current_builder.set(self)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        _current_builder.set(self._parent_builder)
        return False

    @property
    def block(self) -> Block:
        """Get the MLIR block being built."""
        return self._block

    @property
    def loc(self) -> Location:
        """Get the current MLIR location."""
        return self._loc

    # Expression building methods

    def const(self, value: int, width: int) -> Signal[UInt]:
        """Create a constant unsigned integer.

        Args:
            value: The integer value.
            width: The bit width.

        Returns:
            A Signal wrapping the constant.
        """
        from circt.ir import IntegerAttr, IntegerType, InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            # Create a FIRRTL constant
            # For FIRRTL, we need an unsigned integer type attribute, not signless
            ty = firrtl.UIntType.get(self._ctx.mlir_context, width)
            int_ty = IntegerType.get_unsigned(width)
            attr = IntegerAttr.get(int_ty, value)
            const_op = firrtl.ConstantOp(ty, attr, loc=self._loc)
            return Signal(const_op.result, UInt(width), self)

    def _ensure_signal(self, val: Signal | int, width: int | None = None) -> Signal:
        """Convert an int to a Signal if needed."""
        if isinstance(val, Signal):
            return val
        if width is None:
            raise ValueError("Cannot infer width for integer constant")
        return self.const(val, width)

    def add(self, a: Signal, b: Signal | int) -> Signal:
        """Add two signals.

        Note: FIRRTL addition increases width by 1. The result type reflects
        this wider width. Use truncate() or bits() if you need the original width.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            input_width = max(a.type.bit_width(), b_sig.type.bit_width())
            # FIRRTL add returns width+1
            add_op = firrtl.AddPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(add_op.result, UInt(input_width + 1), self)

    def sub(self, a: Signal, b: Signal | int) -> Signal:
        """Subtract two signals.

        Note: FIRRTL subtraction increases width by 1. The result type reflects
        this wider width. Use truncate() or bits() if you need the original width.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            input_width = max(a.type.bit_width(), b_sig.type.bit_width())
            # FIRRTL sub returns width+1
            sub_op = firrtl.SubPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(sub_op.result, UInt(input_width + 1), self)

    def rsub(self, a: int, b: Signal) -> Signal:
        """Subtract signal from integer (a - b)."""
        a_sig = self.const(a, b.type.bit_width())
        return self.sub(a_sig, b)

    def mul(self, a: Signal, b: Signal | int) -> Signal:
        """Multiply two signals."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            result_width = a.type.bit_width() + b_sig.type.bit_width()
            mul_op = firrtl.MulPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(mul_op.result, UInt(result_width), self)

    def and_(self, a: Signal, b: Signal | int) -> Signal:
        """Bitwise AND."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            result_width = max(a.type.bit_width(), b_sig.type.bit_width())
            and_op = firrtl.AndPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(and_op.result, UInt(result_width), self)

    def or_(self, a: Signal, b: Signal | int) -> Signal:
        """Bitwise OR."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            result_width = max(a.type.bit_width(), b_sig.type.bit_width())
            or_op = firrtl.OrPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(or_op.result, UInt(result_width), self)

    def xor_(self, a: Signal, b: Signal | int) -> Signal:
        """Bitwise XOR."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            result_width = max(a.type.bit_width(), b_sig.type.bit_width())
            xor_op = firrtl.XorPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(xor_op.result, UInt(result_width), self)

    def not_(self, a: Signal) -> Signal:
        """Bitwise NOT."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            not_op = firrtl.NotPrimOp(a.value, loc=self._loc)
            return Signal(not_op.result, a.type, self)

    def shl(self, a: Signal, amount: Signal | int) -> Signal:
        """Left shift."""
        from circt.ir import InsertionPoint, IntegerAttr, IntegerType, Operation
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            if isinstance(amount, int):
                result_width = a.type.bit_width() + amount
                # Use Operation.create to ensure correct widened result type
                # (The Python bindings' ShlPrimOp doesn't properly infer the wider type).
                result_ty = firrtl.UIntType.get(self._ctx.mlir_context, result_width)
                op = Operation.create(
                    "firrtl.shl",
                    results=[result_ty],
                    operands=[a.value],
                    attributes={"amount": IntegerAttr.get(IntegerType.get_signless(32), amount)},
                    loc=self._loc
                )
            else:
                shl_op = firrtl.DShlPrimOp(a.value, amount.value, loc=self._loc)
                result_width = a.type.bit_width() + (2 ** amount.type.bit_width() - 1)
                return Signal(shl_op.result, UInt(result_width), self)
            return Signal(op.result, UInt(result_width), self)

    def shr(self, a: Signal, amount: Signal | int) -> Signal:
        """Right shift."""
        from circt.ir import InsertionPoint, IntegerAttr, IntegerType, Operation
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            if isinstance(amount, int):
                # Use Operation.create to ensure correct result type
                # (The Python bindings' ShrPrimOp doesn't properly infer the narrower type)
                result_width = max(1, a.type.bit_width() - amount)
                result_ty = firrtl.UIntType.get(self._ctx.mlir_context, result_width)
                op = Operation.create(
                    "firrtl.shr",
                    results=[result_ty],
                    operands=[a.value],
                    attributes={"amount": IntegerAttr.get(IntegerType.get_signless(32), amount)},
                    loc=self._loc
                )
                return Signal(op.result, UInt(result_width), self)
            else:
                shr_op = firrtl.DShrPrimOp(a.value, amount.value, loc=self._loc)
                result_width = a.type.bit_width()
                return Signal(shr_op.result, UInt(result_width), self)

    def eq(self, a: Signal, b: Signal | int) -> Signal[UInt]:
        """Equality comparison."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            eq_op = firrtl.EQPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(eq_op.result, Bool, self)

    def neq(self, a: Signal, b: Signal | int) -> Signal[UInt]:
        """Inequality comparison."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            neq_op = firrtl.NEQPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(neq_op.result, Bool, self)

    def lt(self, a: Signal, b: Signal | int) -> Signal[UInt]:
        """Less than comparison."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            lt_op = firrtl.LTPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(lt_op.result, Bool, self)

    def le(self, a: Signal, b: Signal | int) -> Signal[UInt]:
        """Less than or equal comparison."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            leq_op = firrtl.LEQPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(leq_op.result, Bool, self)

    def gt(self, a: Signal, b: Signal | int) -> Signal[UInt]:
        """Greater than comparison."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            gt_op = firrtl.GTPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(gt_op.result, Bool, self)

    def ge(self, a: Signal, b: Signal | int) -> Signal[UInt]:
        """Greater than or equal comparison."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            geq_op = firrtl.GEQPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(geq_op.result, Bool, self)

    def bit(self, a: Signal, idx: int) -> Signal[UInt]:
        """Extract a single bit."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            bits_op = firrtl.BitsPrimOp(a.value, idx, idx, loc=self._loc)
            return Signal(bits_op.result, UInt(1), self)

    def bits(self, a: Signal, high: int, low: int) -> Signal[UInt]:
        """Extract a range of bits (inclusive)."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            width = high - low + 1
            bits_op = firrtl.BitsPrimOp(a.value, high, low, loc=self._loc)
            return Signal(bits_op.result, UInt(width), self)

    def concat(self, *signals: Signal) -> Signal[UInt]:
        """Concatenate signals (first is MSB)."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        if len(signals) < 2:
            raise ValueError("concat requires at least 2 signals")

        with InsertionPoint(self._block):
            # FIRRTL cat is binary, so we need to chain them
            result = signals[0]
            total_width = result.type.bit_width()
            for sig in signals[1:]:
                total_width += sig.type.bit_width()
                cat_op = firrtl.CatPrimOp(result.value, sig.value, loc=self._loc)
                result = Signal(cat_op.result, UInt(total_width), self)
            return result

    def reduce_and(self, a: Signal) -> Signal[UInt]:
        """Reduce AND."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            andr_op = firrtl.AndRPrimOp(a.value, loc=self._loc)
            return Signal(andr_op.result, Bool, self)

    def reduce_or(self, a: Signal) -> Signal[UInt]:
        """Reduce OR."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            orr_op = firrtl.OrRPrimOp(a.value, loc=self._loc)
            return Signal(orr_op.result, Bool, self)

    def reduce_xor(self, a: Signal) -> Signal[UInt]:
        """Reduce XOR."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            xorr_op = firrtl.XorRPrimOp(a.value, loc=self._loc)
            return Signal(xorr_op.result, Bool, self)

    def as_uint(self, a: Signal) -> Signal[UInt]:
        """Convert to unsigned."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            asuint_op = firrtl.AsUIntPrimOp(a.value, loc=self._loc)
            return Signal(asuint_op.result, UInt(a.type.bit_width()), self)

    def as_sint(self, a: Signal) -> Signal:
        """Convert to signed."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            assint_op = firrtl.AsSIntPrimOp(a.value, loc=self._loc)
            return Signal(assint_op.result, SInt(a.type.bit_width()), self)

    def pad(self, a: Signal, width: int) -> Signal:
        """Zero-extend or sign-extend to given width."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            pad_op = firrtl.PadPrimOp(a.value, width, loc=self._loc)
            if isinstance(a.type, SInt):
                return Signal(pad_op.result, SInt(width), self)
            return Signal(pad_op.result, UInt(width), self)

    def truncate(self, a: Signal, width: int) -> Signal:
        """Truncate signal to given width (extract low bits)."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        if width >= a.type.bit_width():
            return a  # No truncation needed
        with InsertionPoint(self._block):
            bits_op = firrtl.BitsPrimOp(a.value, width - 1, 0, loc=self._loc)
            if isinstance(a.type, SInt):
                return Signal(bits_op.result, SInt(width), self)
            return Signal(bits_op.result, UInt(width), self)

    def convert_width(self, a: Signal, width: int) -> Signal:
        """Convert signal to given width (truncate or pad as needed)."""
        current_width = a.type.bit_width()
        if current_width == width:
            return a
        elif current_width > width:
            return self.truncate(a, width)
        else:
            return self.pad(a, width)

    def mux(self, cond: Signal, t: Signal, f: Signal) -> Signal:
        """Multiplexer: if cond then t else f.

        Note: If t and f have different widths, they are converted to the
        maximum width before the mux operation.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        # Convert t and f to the same width (maximum of both)
        t_width = t.type.bit_width()
        f_width = f.type.bit_width()
        max_width = max(t_width, f_width)

        if t_width != max_width:
            t = self.convert_width(t, max_width)
        if f_width != max_width:
            f = self.convert_width(f, max_width)

        with InsertionPoint(self._block):
            mux_op = firrtl.MuxPrimOp(cond.value, t.value, f.value, loc=self._loc)
            return Signal(mux_op.result, UInt(max_width), self)

    def call(
        self,
        target,
        method_or_value,
        *args: Signal,
        arg_timing: list[tuple[int, int]] | None = None,
        result_timing: list[tuple[int, int]] | None = None,
    ) -> tuple[Signal, ...] | Signal | None:
        """Call a method or value on an instance.

        Args:
            target: The instance to call on (Instance object), or None for @this.
            method_or_value: A MethodRef, ValueRef, or string method name.
            *args: Arguments to pass.
            arg_timing: Optional list of (start, end) cycle timing for each argument.
                        Each tuple specifies a half-open interval [start, end).
                        Example: [(0, 1), (0, 1)] means both args driven at cycle 0.
            result_timing: Optional list of (start, end) cycle timing for each result.
                           Example: [(4, 5)] means result captured at cycle 4.

        Returns:
            The return values as a tuple of Signals, a single Signal,
            or None if there are no return values.

        Example with timing (for static steps):
            with mod.static_step(6, "compute") as step:
                result = step.call(mult, "multiply", a, b,
                    arg_timing=[(0, 1), (0, 1)],   # args at cycle 0
                    result_timing=[(4, 5)])        # result at cycle 4
        """
        from circt.ir import InsertionPoint, FlatSymbolRefAttr, StringAttr
        from circt.dialects import cmt2
        from .refs import MethodRef, ValueRef, Instance
        from .external_module import ExternalModuleBuilder

        with InsertionPoint(self._block):
            # Determine the callee symbol (instance name)
            if target is None:
                callee = FlatSymbolRefAttr.get("this")
                instance_module = None
            elif isinstance(target, Instance):
                callee = FlatSymbolRefAttr.get(target.name)
                instance_module = target._module
            else:
                callee = FlatSymbolRefAttr.get(target.name)
                instance_module = getattr(target, "_ext_module", None)

            # Determine method/value symbol
            if isinstance(method_or_value, (MethodRef, ValueRef)):
                method_name = method_or_value.name
                method_builder = method_or_value.builder
            elif isinstance(method_or_value, str):
                method_name = method_or_value
                method_builder = None
            else:
                raise TypeError(f"Expected MethodRef, ValueRef, or str, got {type(method_or_value)}")

            method_sym = FlatSymbolRefAttr.get(method_name)

            # Get result types and argument types from method/value builder or external module
            result_types = []
            cmt2_return_types = []
            cmt2_arg_types = []

            if method_builder is not None and hasattr(method_builder, "_return_types"):
                cmt2_return_types = method_builder._return_types
                if hasattr(method_builder, "_arg_types"):
                    cmt2_arg_types = method_builder._arg_types
            elif isinstance(instance_module, ExternalModuleBuilder):
                # Look up types from external module - try value first, then method
                cmt2_return_types = instance_module.get_value_return_types(method_name)
                cmt2_arg_types = instance_module.get_value_arg_types(method_name)
                if not cmt2_return_types and not cmt2_arg_types:
                    cmt2_return_types = instance_module.get_method_return_types(method_name)
                    cmt2_arg_types = instance_module.get_method_arg_types(method_name)
            elif instance_module is not None:
                # Look up types from CMT2 module (ModuleBuilder)
                # Check _values first, then _methods
                if hasattr(instance_module, "_values") and method_name in instance_module._values:
                    val_builder = instance_module._values[method_name]
                    cmt2_return_types = val_builder._return_types
                    cmt2_arg_types = []  # Values don't have args
                elif hasattr(instance_module, "_methods") and method_name in instance_module._methods:
                    meth_builder = instance_module._methods[method_name]
                    cmt2_return_types = meth_builder._return_types
                    cmt2_arg_types = [ty for _, ty in meth_builder._arg_types]

            result_types = [
                ty.to_firrtl_type(self._ctx.mlir_context)
                for ty in cmt2_return_types
            ]

            # Convert argument widths if needed
            converted_args = list(args)
            if cmt2_arg_types and len(cmt2_arg_types) == len(args):
                for i, (arg, expected_type) in enumerate(zip(args, cmt2_arg_types)):
                    expected_width = expected_type.bit_width()
                    actual_width = arg.type.bit_width()
                    if actual_width != expected_width:
                        converted_args[i] = self.convert_width(arg, expected_width)

            # Create the call
            input_values = [arg.value for arg in converted_args]

            # Build timing attributes if provided
            call_attrs = {}
            if arg_timing is not None:
                from circt.ir import ArrayAttr, Attribute
                timing_attrs = []
                for start, end in arg_timing:
                    # Parse the timing attribute from string representation
                    attr_str = f"#cmt2.timing<[{start}, {end}]>"
                    attr = Attribute.parse(attr_str, self._ctx.mlir_context)
                    timing_attrs.append(attr)
                call_attrs["arg_timing"] = ArrayAttr.get(timing_attrs)

            if result_timing is not None:
                from circt.ir import ArrayAttr, Attribute
                timing_attrs = []
                for start, end in result_timing:
                    # Parse the timing attribute from string representation
                    attr_str = f"#cmt2.timing<[{start}, {end}]>"
                    attr = Attribute.parse(attr_str, self._ctx.mlir_context)
                    timing_attrs.append(attr)
                call_attrs["result_timing"] = ArrayAttr.get(timing_attrs)

            call_op = cmt2.CallOp(
                result_types,
                input_values,
                callee,
                method_sym,
                loc=self._loc,
            )

            # Set timing attributes after op creation
            for attr_name, attr_value in call_attrs.items():
                call_op.attributes[attr_name] = attr_value

            # Wrap results
            if len(call_op.results) == 0:
                return None
            elif len(call_op.results) == 1:
                ty = cmt2_return_types[0] if cmt2_return_types else UInt(32)
                return Signal(call_op.results[0], ty, self)
            else:
                return_types = cmt2_return_types if cmt2_return_types else [UInt(32)] * len(call_op.results)
                signals = []
                for i, result in enumerate(call_op.results):
                    signals.append(Signal(result, return_types[i], self))
                return tuple(signals)
