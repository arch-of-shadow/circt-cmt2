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
            ty = firrtl.UIntType.get(self._ctx.mlir_context, width)
            int_ty = IntegerType.get_signless(width)
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
        """Add two signals."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            result_width = max(a.type.bit_width(), b_sig.type.bit_width()) + 1
            add_op = firrtl.AddPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(add_op.result, UInt(result_width), self)

    def sub(self, a: Signal, b: Signal | int) -> Signal:
        """Subtract two signals."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        b_sig = self._ensure_signal(b, a.type.bit_width())

        with InsertionPoint(self._block):
            result_width = max(a.type.bit_width(), b_sig.type.bit_width()) + 1
            sub_op = firrtl.SubPrimOp(a.value, b_sig.value, loc=self._loc)
            return Signal(sub_op.result, UInt(result_width), self)

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
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            if isinstance(amount, int):
                shl_op = firrtl.ShlPrimOp(a.value, amount, loc=self._loc)
                result_width = a.type.bit_width() + amount
            else:
                shl_op = firrtl.DShlPrimOp(a.value, amount.value, loc=self._loc)
                result_width = a.type.bit_width() + (2 ** amount.type.bit_width() - 1)
            return Signal(shl_op.result, UInt(result_width), self)

    def shr(self, a: Signal, amount: Signal | int) -> Signal:
        """Right shift."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            if isinstance(amount, int):
                shr_op = firrtl.ShrPrimOp(a.value, amount, loc=self._loc)
                result_width = max(1, a.type.bit_width() - amount)
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

    def mux(self, cond: Signal, t: Signal, f: Signal) -> Signal:
        """Multiplexer: if cond then t else f."""
        from circt.ir import InsertionPoint
        from circt.dialects import firrtl

        with InsertionPoint(self._block):
            mux_op = firrtl.MuxPrimOp(cond.value, t.value, f.value, loc=self._loc)
            return Signal(mux_op.result, t.type, self)

    def call(
        self,
        target: Instance | None,
        method_or_value: MethodRef | ValueRef,
        *args: Signal,
    ) -> tuple[Signal, ...] | Signal | None:
        """Call a method or value on an instance.

        Args:
            target: The instance to call on, or None for @this.
            method_or_value: A MethodRef or ValueRef.
            *args: Arguments to pass.

        Returns:
            The return values as a tuple of Signals, a single Signal,
            or None if there are no return values.
        """
        from circt.ir import InsertionPoint, FlatSymbolRefAttr, StringAttr
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            # Determine the callee symbol
            if target is None:
                callee = FlatSymbolRefAttr.get("this")
            else:
                callee = FlatSymbolRefAttr.get(target.name)

            method_sym = FlatSymbolRefAttr.get(method_or_value.name)

            # Get result types from method/value builder if available
            result_types = []
            if method_or_value.builder is not None:
                builder = method_or_value.builder
                if hasattr(builder, "_return_types"):
                    result_types = [
                        ty.to_firrtl_type(self._ctx.mlir_context)
                        for ty in builder._return_types
                    ]

            # Create the call
            input_values = [arg.value for arg in args]
            call_op = cmt2.CallOp(
                result_types,
                input_values,
                callee,
                method_sym,
                loc=self._loc,
            )

            # Wrap results
            if len(call_op.results) == 0:
                return None
            elif len(call_op.results) == 1:
                # Infer type from builder
                if method_or_value.builder and hasattr(method_or_value.builder, "_return_types"):
                    ty = method_or_value.builder._return_types[0]
                else:
                    ty = UInt(32)  # Default
                return Signal(call_op.results[0], ty, self)
            else:
                signals = []
                return_types = (
                    method_or_value.builder._return_types
                    if method_or_value.builder and hasattr(method_or_value.builder, "_return_types")
                    else [UInt(32)] * len(call_op.results)
                )
                for i, result in enumerate(call_op.results):
                    signals.append(Signal(result, return_types[i], self))
                return tuple(signals)
