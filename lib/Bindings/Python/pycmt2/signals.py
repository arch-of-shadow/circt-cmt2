#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Signal wrapper for PyCMT2 EDSL."""

from __future__ import annotations

from typing import TYPE_CHECKING, Generic, TypeVar, overload

from .types import Cmt2Type, UInt

if TYPE_CHECKING:
    from circt.ir import Value as MlirValue
    from .builders import RegionBuilder

T = TypeVar("T", bound=Cmt2Type)


class Signal(Generic[T]):
    """A typed signal wrapper that tracks MLIR value and CMT2 type.

    Signals support operator overloading for arithmetic, bitwise, and
    comparison operations. Operations return new Signal instances.
    """

    __slots__ = ("_value", "_type", "_builder")

    def __init__(self, value: MlirValue, ty: T, builder: RegionBuilder):
        self._value = value
        self._type = ty
        self._builder = builder

    @property
    def value(self) -> MlirValue:
        """Get the underlying MLIR value."""
        return self._value

    @property
    def type(self) -> T:
        """Get the CMT2 type."""
        return self._type

    # Arithmetic operators
    def __add__(self, other: Signal | int) -> Signal:
        """Add two signals or a signal and an integer."""
        return self._builder.add(self, other)

    def __radd__(self, other: int) -> Signal:
        """Right add for integer + signal."""
        return self._builder.add(self, other)

    def __sub__(self, other: Signal | int) -> Signal:
        """Subtract signals or a signal and an integer."""
        return self._builder.sub(self, other)

    def __rsub__(self, other: int) -> Signal:
        """Right subtract for integer - signal."""
        return self._builder.rsub(other, self)

    def __mul__(self, other: Signal | int) -> Signal:
        """Multiply two signals or a signal and an integer."""
        return self._builder.mul(self, other)

    def __rmul__(self, other: int) -> Signal:
        """Right multiply for integer * signal."""
        return self._builder.mul(self, other)

    # Bitwise operators
    def __and__(self, other: Signal | int) -> Signal:
        """Bitwise AND."""
        return self._builder.and_(self, other)

    def __rand__(self, other: int) -> Signal:
        """Right bitwise AND."""
        return self._builder.and_(self, other)

    def __or__(self, other: Signal | int) -> Signal:
        """Bitwise OR."""
        return self._builder.or_(self, other)

    def __ror__(self, other: int) -> Signal:
        """Right bitwise OR."""
        return self._builder.or_(self, other)

    def __xor__(self, other: Signal | int) -> Signal:
        """Bitwise XOR."""
        return self._builder.xor_(self, other)

    def __rxor__(self, other: int) -> Signal:
        """Right bitwise XOR."""
        return self._builder.xor_(self, other)

    def __invert__(self) -> Signal:
        """Bitwise NOT."""
        return self._builder.not_(self)

    def __lshift__(self, amount: Signal | int) -> Signal:
        """Left shift."""
        return self._builder.shl(self, amount)

    def __rshift__(self, amount: Signal | int) -> Signal:
        """Right shift (logical for unsigned, arithmetic for signed)."""
        return self._builder.shr(self, amount)

    # Comparison operators (return Signal[UInt(1)] aka Bool)
    def __eq__(self, other: Signal | int) -> Signal[UInt]:  # type: ignore[override]
        """Equality comparison."""
        return self._builder.eq(self, other)

    def __ne__(self, other: Signal | int) -> Signal[UInt]:  # type: ignore[override]
        """Inequality comparison."""
        return self._builder.neq(self, other)

    def __lt__(self, other: Signal | int) -> Signal[UInt]:
        """Less than comparison."""
        return self._builder.lt(self, other)

    def __le__(self, other: Signal | int) -> Signal[UInt]:
        """Less than or equal comparison."""
        return self._builder.le(self, other)

    def __gt__(self, other: Signal | int) -> Signal[UInt]:
        """Greater than comparison."""
        return self._builder.gt(self, other)

    def __ge__(self, other: Signal | int) -> Signal[UInt]:
        """Greater than or equal comparison."""
        return self._builder.ge(self, other)

    # Bit extraction
    @overload
    def __getitem__(self, key: int) -> Signal[UInt]: ...

    @overload
    def __getitem__(self, key: slice) -> Signal[UInt]: ...

    def __getitem__(self, key: int | slice) -> Signal[UInt]:
        """Bit extraction: signal[7:0] or signal[3]."""
        if isinstance(key, slice):
            # slice.start is high bit, slice.stop is low bit (inclusive)
            high = key.start if key.start is not None else self._type.bit_width() - 1
            low = key.stop if key.stop is not None else 0
            return self._builder.bits(self, high, low)
        return self._builder.bit(self, key)

    # Concatenation
    def concat(self, *others: Signal) -> Signal[UInt]:
        """Concatenate signals (self is MSB, last arg is LSB)."""
        return self._builder.concat(self, *others)

    # Reduction operations
    def reduce_and(self) -> Signal[UInt]:
        """Reduce AND (all bits are 1)."""
        return self._builder.reduce_and(self)

    def reduce_or(self) -> Signal[UInt]:
        """Reduce OR (any bit is 1)."""
        return self._builder.reduce_or(self)

    def reduce_xor(self) -> Signal[UInt]:
        """Reduce XOR (odd number of 1 bits)."""
        return self._builder.reduce_xor(self)

    # Conversion
    def as_uint(self) -> Signal[UInt]:
        """Convert to unsigned interpretation."""
        return self._builder.as_uint(self)

    def as_sint(self) -> Signal:
        """Convert to signed interpretation."""
        return self._builder.as_sint(self)

    def pad(self, width: int) -> Signal:
        """Zero-extend or sign-extend to given width."""
        return self._builder.pad(self, width)

    def __repr__(self) -> str:
        return f"Signal({self._type})"
