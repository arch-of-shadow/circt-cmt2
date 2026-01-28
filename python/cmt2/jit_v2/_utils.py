#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Utility functions for JIT v2.
"""

from __future__ import annotations

from typing import Any, TypeVar

# Type variable for generic functions
T = TypeVar("T")


class StaticType:
    """Marker class for static (compile-time) arguments.
    
    Example:
        def design(width: Annotated[int, static] = 32):
            ...
    """
    
    def __class_getitem__(cls, item: T) -> T:
        """Allow static[T] syntax in type annotations."""
        return item


# Singleton instance for easy access
static = StaticType


def enable_operator_overloading():
    """Enable operator overloading for PyCMT2 builders.
    
    This patches PyCMT2 builders to support Python operators
    like +, -, *, etc. on signals.
    
    Example:
        jit.enable_operator_overloading()
        
        # Now you can use:
        result = signal_a + signal_b  # Instead of b.add(a, b)
    
    Returns:
        True if successful, False if PyCMT2 not available
    """
    try:
        from circt.pycmt2.builders import RegionBuilder
        
        # Add operator methods to RegionBuilder if not present
        if not hasattr(RegionBuilder, '__add__'):
            RegionBuilder.__add__ = lambda self, other: self.add(self._get_lhs(), other)
        if not hasattr(RegionBuilder, '__sub__'):
            RegionBuilder.__sub__ = lambda self, other: self.sub(self._get_lhs(), other)
        if not hasattr(RegionBuilder, '__mul__'):
            RegionBuilder.__mul__ = lambda self, other: self.mul(self._get_lhs(), other)
        
        return True
    except ImportError:
        return False


class CacheInfo:
    """Cache information for JIT compilation.
    
    Attributes:
        hits: Number of cache hits
        misses: Number of cache misses
        size: Current cache size
    """
    
    def __init__(self):
        self.hits = 0
        self.misses = 0
        self.size = 0
    
    def __repr__(self) -> str:
        return f"CacheInfo(hits={self.hits}, misses={self.misses}, size={self.size})"


# Global cache info
cache_info = CacheInfo()
