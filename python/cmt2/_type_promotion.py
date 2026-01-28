"""Type promotion rules for CMT2 hardware types.

This module implements hardware-aware type promotion following SystemVerilog
and hardware design conventions. It handles:
- Width inference for arithmetic operations
- Signedness propagation rules
- Type coercion between different signal types

Example:
    >>> from cmt2.types import UInt, SInt, Bits
    >>> from cmt2._type_promotion import promote_types, Operation
    >>> 
    >>> # Addition: max(w1, w2) + 1
    >>> promote_types(UInt(8), UInt(16), Operation.ADD)
    UInt(17)
    >>> 
    >>> # Multiplication: w1 + w2
    >>> promote_types(UInt(8), UInt(8), Operation.MUL)
    UInt(16)
    >>> 
    >>> # Mixed signedness: result is signed
    >>> promote_types(SInt(8), UInt(8), Operation.ADD)
    SInt(9)
"""

from __future__ import annotations

from enum import Enum, auto
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from cmt2.types import SignalType


class Operation(Enum):
    """Hardware operations that require type promotion."""
    # Arithmetic operations
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    MOD = auto()
    
    # Bitwise operations
    BITWISE = auto()  # AND, OR, XOR
    
    # Shift operations
    LSHIFT = auto()
    RSHIFT = auto()
    
    # Comparison operations
    COMPARE = auto()
    
    # Concatenation
    CONCAT = auto()
    
    # Unary operations
    NEG = auto()
    INVERT = auto()
    ABS = auto()


def _result_signedness(op: Operation, t1_signed: bool, t2_signed: bool = False) -> bool:
    """Determine the result signedness for an operation.
    
    Rules:
    - Comparisons always return unsigned (1-bit) result
    - Bitwise ops on mixed types: unsigned unless both signed
    - Arithmetic on mixed types: signed if any operand is signed
    - Shifts: same as the value being shifted (first operand)
    
    Args:
        op: The operation being performed.
        t1_signed: Whether the first operand is signed.
        t2_signed: Whether the second operand is signed (for binary ops).
    
    Returns:
        True if the result should be signed.
    """
    if op == Operation.COMPARE:
        # Comparisons return 1-bit unsigned
        return False
    
    if op in (Operation.LSHIFT, Operation.RSHIFT):
        # Result has same signedness as the value being shifted
        return t1_signed
    
    if op == Operation.BITWISE:
        # Bitwise: signed only if both are signed
        return t1_signed and t2_signed
    
    if op == Operation.CONCAT:
        # Concatenation is always unsigned
        return False
    
    # Arithmetic operations: signed if any operand is signed
    # This ensures proper sign extension
    return t1_signed or t2_signed


def _compute_width(op: Operation, w1: int, w2: int = 0) -> int:
    """Compute the result width for an operation.
    
    Width rules:
    - ADD/SUB: max(w1, w2) + 1 (to prevent overflow)
    - MUL: w1 + w2 (full precision)
    - DIV/MOD: w1 (same as dividend)
    - BITWISE: max(w1, w2) (no width change)
    - LSHIFT: w1 + 2^w2 - 1 (worst case), or w1 + (1 << w2) - 1
      Simplified: typically w1 + (2^shift_width - 1), but practically
      often just w1 if shift amount is known
    - RSHIFT: w1 (same width, but may lose bits)
    - COMPARE: 1 (boolean result)
    - CONCAT: w1 + w2 (combined width)
    - NEG: w1 + 1 (negation can overflow)
    - INVERT: w1 (same width)
    - ABS: w1 (same width)
    
    Args:
        op: The operation being performed.
        w1: Width of first operand.
        w2: Width of second operand (for binary ops).
    
    Returns:
        The resulting bit width.
    """
    if op == Operation.ADD:
        # Addition can overflow: need max + 1 bit
        return max(w1, w2) + 1
    
    if op == Operation.SUB:
        # Subtraction can underflow: need max + 1 bit
        return max(w1, w2) + 1
    
    if op == Operation.MUL:
        # Multiplication: full precision is w1 + w2
        return w1 + w2
    
    if op in (Operation.DIV, Operation.MOD):
        # Division/Modulo result fits in dividend width
        return w1
    
    if op == Operation.BITWISE:
        # Bitwise operations: maximum width
        return max(w1, w2)
    
    if op == Operation.LSHIFT:
        # Left shift: w1 + (2^w2 - 1) in worst case
        # For practical purposes, often limited to w1 + constant
        # Here we use a simplified model: w1 + (1 << min(w2, 6)) - 1
        # to avoid excessive width growth
        max_shift = min(w2, 6)  # Cap at 64-bit shift amount
        return w1 + (1 << max_shift) - 1
    
    if op == Operation.RSHIFT:
        # Right shift: same width (may lose MSBs)
        return w1
    
    if op == Operation.COMPARE:
        # Comparisons return 1-bit result
        return 1
    
    if op == Operation.CONCAT:
        # Concatenation: sum of widths
        return w1 + w2
    
    if op == Operation.NEG:
        # Negation: w + 1 (e.g., -(-128) = 128 needs 9 bits for 8-bit input)
        return w1 + 1
    
    if op == Operation.INVERT:
        # Bitwise invert: same width
        return w1
    
    if op == Operation.ABS:
        # Absolute value: same width (e.g., abs(-128) for 8-bit is still 8 bits: 128)
        return w1
    
    raise ValueError(f"Unknown operation: {op}")


def promote_types(t1: SignalType, t2: SignalType, op: Operation) -> SignalType:
    """Promote two types according to hardware type promotion rules.
    
    This function determines the result type when performing an operation
    on two signals of potentially different types and widths.
    
    Width rules:
    - ADD/SUB: max(w1, w2) + 1 (prevent overflow)
    - MUL: w1 + w2 (full precision)
    - DIV/MOD: w1 (dividend width)
    - BITWISE: max(w1, w2)
    - COMPARE: 1 (boolean)
    - CONCAT: w1 + w2
    
    Signedness rules:
    - Arithmetic: signed if any operand is signed
    - Bitwise: signed only if both are signed
    - Comparisons: always unsigned (1-bit)
    - Shifts: same as value being shifted
    - Concatenation: always unsigned
    
    Args:
        t1: First operand type.
        t2: Second operand type.
        op: The operation being performed.
    
    Returns:
        The promoted result type.
    
    Example:
        >>> from cmt2.types import UInt, SInt
        >>> from cmt2._type_promotion import promote_types, Operation
        >>> 
        >>> # UInt + UInt = UInt with +1 width
        >>> promote_types(UInt(8), UInt(16), Operation.ADD)
        UInt(17)
        >>> 
        >>> # SInt + UInt = SInt with +1 width
        >>> promote_types(SInt(8), UInt(16), Operation.ADD)
        SInt(17)
        >>> 
        >>> # Multiplication
        >>> promote_types(UInt(8), UInt(8), Operation.MUL)
        UInt(16)
    """
    from cmt2.types import UInt, SInt, Bits
    
    # Compute result width
    result_width = _compute_width(op, t1.width, t2.width)
    
    # Compute result signedness
    result_signed = _result_signedness(op, t1.is_signed, t2.is_signed)
    
    # Construct result type
    if op == Operation.COMPARE:
        # Comparisons always return Bits(1)
        return Bits(1)
    
    if result_signed:
        # Need at least 2 bits for signed (1 sign + 1 value)
        return SInt(max(result_width, 2))
    else:
        return UInt(result_width)


def promote_unary(t: SignalType, op: Operation) -> SignalType:
    """Promote a type for a unary operation.
    
    Args:
        t: The operand type.
        op: The unary operation.
    
    Returns:
        The promoted result type.
    
    Example:
        >>> from cmt2.types import UInt, SInt
        >>> from cmt2._type_promotion import promote_unary, Operation
        >>> 
        >>> # Negation of unsigned -> signed with +1 width
        >>> promote_unary(UInt(8), Operation.NEG)
        SInt(9)
        >>> 
        >>> # Negation of signed -> signed with +1 width
        >>> promote_unary(SInt(8), Operation.NEG)
        SInt(9)
        >>> 
        >>> # Bitwise invert
        >>> promote_unary(UInt(8), Operation.INVERT)
        UInt(8)
    """
    from cmt2.types import UInt, SInt, Bits
    
    # Compute result width
    result_width = _compute_width(op, t.width)
    
    if op == Operation.NEG:
        # Negation always produces signed (even from unsigned)
        return SInt(max(result_width, 2))
    
    if op == Operation.INVERT:
        # Bitwise invert preserves type
        if isinstance(t, SInt):
            return SInt(max(result_width, 2))
        elif isinstance(t, Bits):
            return Bits(result_width)
        else:
            return UInt(result_width)
    
    if op == Operation.ABS:
        # Absolute value: unsigned result
        return UInt(result_width)
    
    raise ValueError(f"Unknown unary operation: {op}")


class TypePromotion:
    """Explicit type promotion rules for hardware operations.
    
    This class provides a namespace for type promotion rule constants
    and helper methods, making the rules self-documenting and testable.
    
    Example:
        >>> TypePromotion.ADD_WIDTH(8, 16)
        17
        >>> TypePromotion.MUL_WIDTH(8, 8)
        16
        >>> TypePromotion.result_signed("add", False, True)
        True
    """
    
    # Width computation lambdas for documentation and testing
    ADD_WIDTH = lambda w1, w2: max(w1, w2) + 1
    SUB_WIDTH = lambda w1, w2: max(w1, w2) + 1
    MUL_WIDTH = lambda w1, w2: w1 + w2
    DIV_WIDTH = lambda w1, w2: w1
    MOD_WIDTH = lambda w1, w2: w2
    BITWISE_WIDTH = lambda w1, w2: max(w1, w2)
    CMP_WIDTH = 1
    CONCAT_WIDTH = lambda w1, w2: w1 + w2
    
    @staticmethod
    def result_signed(op: str, *operand_signed: bool) -> bool:
        """Determine if operation result should be signed.
        
        Args:
            op: Operation name ("add", "sub", "mul", "div", "mod", 
                "bitwise", "compare", "concat").
            *operand_signed: Signedness of each operand.
        
        Returns:
            True if result should be signed.
        """
        if op in ("add", "sub", "mul", "div", "mod"):
            # Arithmetic: signed if any operand is signed
            return any(operand_signed)
        elif op == "bitwise":
            # Bitwise: signed only if both are signed
            return len(operand_signed) >= 2 and all(operand_signed)
        elif op == "compare":
            # Comparisons return unsigned 1-bit
            return False
        elif op == "concat":
            # Concatenation is always unsigned
            return False
        else:
            raise ValueError(f"Unknown operation: {op}")
    
    @staticmethod
    def promote(t1: SignalType, t2: SignalType, op: str) -> SignalType:
        """Convenient interface for type promotion.
        
        Args:
            t1: First operand type.
            t2: Second operand type.
            op: Operation name string.
        
        Returns:
            The promoted result type.
        
        Example:
            >>> from cmt2.types import UInt
            >>> TypePromotion.promote(UInt(8), UInt(16), "add")
            UInt(17)
        """
        op_map = {
            "add": Operation.ADD,
            "sub": Operation.SUB,
            "mul": Operation.MUL,
            "div": Operation.DIV,
            "mod": Operation.MOD,
            "bitwise": Operation.BITWISE,
            "compare": Operation.COMPARE,
            "concat": Operation.CONCAT,
            "lshift": Operation.LSHIFT,
            "rshift": Operation.RSHIFT,
        }
        
        if op not in op_map:
            raise ValueError(f"Unknown operation: {op}")
        
        return promote_types(t1, t2, op_map[op])
