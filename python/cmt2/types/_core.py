"""Core hardware type definitions for CMT2.

This module defines the base type system for hardware signals, including
unsigned integers (UInt), signed integers (SInt), and untyped bits (Bits).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Union, Optional, Any


class TypeError(Exception):
    """Exception raised for type-related errors in CMT2."""
    pass


@dataclass(frozen=True)
class SignalType:
    """Base class for all hardware signal types.
    
    All signal types have a defined bit width and signedness.
    
    Attributes:
        width: The bit width of the signal (must be positive).
    
    Example:
        >>> t = UInt(8)
        >>> t.width
        8
        >>> t.is_signed
        False
    """
    width: int
    
    def __post_init__(self):
        if not isinstance(self.width, int):
            raise TypeError(f"Width must be an integer, got {type(self.width).__name__}")
        if self.width <= 0:
            raise TypeError(f"Width must be positive, got {self.width}")
    
    @property
    def is_signed(self) -> bool:
        """Returns True if this is a signed type."""
        raise NotImplementedError
    
    @property
    def is_unsigned(self) -> bool:
        """Returns True if this is an unsigned type."""
        return not self.is_signed
    
    def __repr__(self) -> str:
        return f"{self.__class__.__name__}({self.width})"
    
    def __eq__(self, other: object) -> bool:
        if not isinstance(other, SignalType):
            return NotImplemented
        return self.width == other.width and self.is_signed == other.is_signed
    
    def __hash__(self) -> int:
        return hash((self.__class__.__name__, self.width))
    
    # Arithmetic operations - these delegate to type promotion rules
    def __add__(self, other: SignalType) -> SignalType:
        """Return type for addition: max(w1, w2) + 1"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.ADD)
    
    def __sub__(self, other: SignalType) -> SignalType:
        """Return type for subtraction: max(w1, w2) + 1"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.SUB)
    
    def __mul__(self, other: SignalType) -> SignalType:
        """Return type for multiplication: w1 + w2"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.MUL)
    
    def __truediv__(self, other: SignalType) -> SignalType:
        """Return type for division: same as dividend (TODO: verify)"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.DIV)
    
    def __floordiv__(self, other: SignalType) -> SignalType:
        """Return type for floor division: same as dividend"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.DIV)
    
    def __mod__(self, other: SignalType) -> SignalType:
        """Return type for modulo: same as divisor"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.MOD)
    
    def __and__(self, other: SignalType) -> SignalType:
        """Return type for bitwise AND: max(w1, w2)"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.BITWISE)
    
    def __or__(self, other: SignalType) -> SignalType:
        """Return type for bitwise OR: max(w1, w2)"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.BITWISE)
    
    def __xor__(self, other: SignalType) -> SignalType:
        """Return type for bitwise XOR: max(w1, w2)"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.BITWISE)
    
    def __lshift__(self, other: SignalType) -> SignalType:
        """Return type for left shift: w1 + (2^w2 - 1) max shift, simplified to w1 + 2^w2 - 1"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.LSHIFT)
    
    def __rshift__(self, other: SignalType) -> SignalType:
        """Return type for right shift: w1 (same width)"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.RSHIFT)
    
    def __lt__(self, other: SignalType) -> Bits:
        """Comparison returns 1-bit result"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.COMPARE)
    
    def __le__(self, other: SignalType) -> Bits:
        """Comparison returns 1-bit result"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.COMPARE)
    
    def __gt__(self, other: SignalType) -> Bits:
        """Comparison returns 1-bit result"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.COMPARE)
    
    def __ge__(self, other: SignalType) -> Bits:
        """Comparison returns 1-bit result"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.COMPARE)
    
    def __eq__(self, other: SignalType) -> Bits:  # type: ignore[override]
        """Equality comparison returns 1-bit result"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.COMPARE)
    
    def __ne__(self, other: SignalType) -> Bits:  # type: ignore[override]
        """Inequality comparison returns 1-bit result"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.COMPARE)
    
    def __neg__(self) -> SignalType:
        """Unary negation: width + 1"""
        from cmt2._type_promotion import promote_unary, Operation
        return promote_unary(self, Operation.NEG)
    
    def __invert__(self) -> SignalType:
        """Unary bitwise NOT: same width"""
        from cmt2._type_promotion import promote_unary, Operation
        return promote_unary(self, Operation.INVERT)
    
    def __abs__(self) -> SignalType:
        """Absolute value: same width (for SInt) or same (for UInt)"""
        from cmt2._type_promotion import promote_unary, Operation
        return promote_unary(self, Operation.ABS)
    
    def concat(self, other: SignalType) -> SignalType:
        """Concatenation: w1 + w2"""
        from cmt2._type_promotion import promote_types, Operation
        return promote_types(self, other, Operation.CONCAT)


@dataclass(frozen=True, repr=False)
class UInt(SignalType):
    """Unsigned integer type.
    
    Represents an unsigned integer with a fixed bit width.
    Values range from 0 to 2^width - 1.
    
    Args:
        width: The bit width (must be positive).
    
    Example:
        >>> t = UInt(8)
        >>> t.width
        8
        >>> t.is_signed
        False
        >>> 
        >>> # Type promotion with another UInt
        >>> t2 = UInt(16)
        >>> result = t + t2  # UInt(17)
        >>> 
        >>> # Type promotion with SInt
        >>> s = SInt(8)
        >>> result = t + s  # SInt(17)
    """
    
    @property
    def is_signed(self) -> bool:
        return False
    
    def __getitem__(self, width: int) -> UInt:
        """Allow syntax: UInt[8] -> UInt(8)"""
        return UInt(width)
    
    def __repr__(self) -> str:
        return f"UInt({self.width})"


@dataclass(frozen=True, repr=False)
class SInt(SignalType):
    """Signed integer type.
    
    Represents a signed two's-complement integer with a fixed bit width.
    Values range from -2^(width-1) to 2^(width-1) - 1.
    
    Args:
        width: The bit width (must be positive, typically >= 2).
    
    Example:
        >>> t = SInt(8)
        >>> t.width
        8
        >>> t.is_signed
        True
        >>> 
        >>> # Type promotion with UInt
        >>> u = UInt(16)
        >>> result = t + u  # SInt(17)
    """
    
    def __post_init__(self):
        super().__post_init__()
        if self.width < 2:
            raise TypeError(f"SInt width must be at least 2 (for sign bit), got {self.width}")
    
    @property
    def is_signed(self) -> bool:
        return True
    
    def __getitem__(self, width: int) -> SInt:
        """Allow syntax: SInt[8] -> SInt(8)"""
        return SInt(width)
    
    def __repr__(self) -> str:
        return f"SInt({self.width})"


@dataclass(frozen=True, repr=False)
class Bits(SignalType):
    """Untyped bits.
    
    Represents a collection of bits without numeric interpretation.
    Used for raw bit vectors, control signals, and protocol data.
    
    Args:
        width: The bit width (must be positive).
    
    Example:
        >>> t = Bits(8)
        >>> t.width
        8
        >>> t.is_signed
        False
        >>> 
        >>> # Bits with UInt
        >>> u = UInt(16)
        >>> result = t + u  # Bits(17) - but typically avoided
    """
    
    @property
    def is_signed(self) -> bool:
        return False
    
    def __getitem__(self, width: int) -> Bits:
        """Allow syntax: Bits[8] -> Bits(8)"""
        return Bits(width)
    
    def __repr__(self) -> str:
        return f"Bits({self.width})"
