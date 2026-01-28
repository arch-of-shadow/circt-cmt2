"""CMT2 Hardware Type System.

This module defines the hardware signal types used in CMT2 designs:
- UInt(width): Unsigned integer
- SInt(width): Signed integer  
- Bits(width): Untyped bits

Example:
    >>> from cmt2.types import UInt, SInt, Bits
    >>> 
    >>> # Create types
    >>> t1 = UInt(8)
    >>> t2 = SInt(16)
    >>> 
    >>> # Type promotion
    >>> t3 = t1 + t2  # Results in SInt(17)
"""

from cmt2.types._core import (
    SignalType,
    UInt,
    SInt,
    Bits,
    TypeError,
)

__all__ = [
    "SignalType",
    "UInt",
    "SInt", 
    "Bits",
    "TypeError",
]
