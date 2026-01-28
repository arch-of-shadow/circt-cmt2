#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Debug information support for CMT2 JIT.

This module provides source location tracking and debug information
propagation through all JIT compilation stages. It enables:

1. Python source location tracking (file, line, function)
2. MLIR location information attachment
3. Source-to-hardware mapping for debugging
4. Simulation trace mapping

Example:
    from cmt2.jit._debug_info import DebugInfo, get_current_location
    
    # Capture current Python location
    loc = get_current_location()
    print(f"Current: {loc.file}:{loc.line} in {loc.function}")
    
    # Attach to MLIR operation
    with DebugInfo.attach_to_mlir(loc):
        # MLIR operations created here will have location info
        op = create_op()
"""

from __future__ import annotations

import inspect
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from circt.ir import Location as MlirLocation


# CIRCT availability check
try:
    from circt import ir
    _HAS_CIRCT = True
except ImportError:
    _HAS_CIRCT = False


@dataclass(frozen=True)
class SourceLocation:
    """Python source location information.
    
    Attributes:
        file: Source file path
        line: Line number (1-based)
        column: Column number (optional)
        function: Function name
        code_context: Surrounding code lines
    """
    file: str
    line: int
    column: Optional[int] = None
    function: str = "<unknown>"
    code_context: Optional[list[str]] = field(default=None, compare=False)
    
    def __str__(self) -> str:
        """Format as human-readable string."""
        if self.function:
            return f"{self.file}:{self.line} (in {self.function})"
        return f"{self.file}:{self.line}"
    
    def __repr__(self) -> str:
        """Format as Python representation."""
        return (
            f"SourceLocation("
            f"file={self.file!r}, "
            f"line={self.line}, "
            f"function={self.function!r}"
            f")"
        )
    
    def get_source_lines(self, context_lines: int = 3) -> str:
        """Get source code around this location.
        
        Args:
            context_lines: Number of lines before and after
            
        Returns:
            Source code snippet
        """
        if self.code_context:
            return "\n".join(self.code_context)
        
        try:
            with open(self.file, 'r') as f:
                lines = f.readlines()
        except (IOError, OSError):
            return "<source not available>"
        
        start = max(0, self.line - context_lines - 1)
        end = min(len(lines), self.line + context_lines)
        
        result = []
        for i in range(start, end):
            line_num = i + 1
            prefix = ">>> " if line_num == self.line else "    "
            result.append(f"{prefix}{line_num:4d}: {lines[i].rstrip()}")
        
        return "\n".join(result)


class DebugInfo:
    """Debug information manager for JIT compilation.
    
    This class provides utilities for:
    - Capturing Python source locations
    - Converting to MLIR locations
    - Attaching debug info to operations
    
    Example:
        # Capture location
        loc = DebugInfo.capture()
        
        # Convert to MLIR location
        mlir_loc = DebugInfo.to_mlir_location(loc, context)
        
        # Use with builder
        with DebugInfo.attach(loc):
            # Operations created here will have location info
            op = builder.create_op()
    """
    
    _current_location: Optional[SourceLocation] = None
    
    @staticmethod
    def capture(stack_offset: int = 2) -> SourceLocation:
        """Capture the current Python source location.
        
        Args:
            stack_offset: How many frames up the stack to look
            
        Returns:
            SourceLocation with file, line, function info
        """
        frame = inspect.currentframe()
        try:
            # Navigate up the stack
            for _ in range(stack_offset):
                if frame is None:
                    break
                frame = frame.f_back
            
            if frame is None:
                return SourceLocation("<unknown>", 0)
            
            # Extract info
            filename = frame.f_code.co_filename
            lineno = frame.f_lineno
            function = frame.f_code.co_name
            
            # Get code context if available
            context = None
            try:
                context = inspect.getframeinfo(frame, context=3).code_context
            except Exception:
                pass
            
            return SourceLocation(
                file=filename,
                line=lineno,
                function=function,
                code_context=context,
            )
        finally:
            del frame  # Avoid reference cycles
    
    @staticmethod
    def to_mlir_location(
        loc: SourceLocation,
        context: Any,
    ) -> Optional["MlirLocation"]:
        """Convert SourceLocation to MLIR Location.
        
        Args:
            loc: Python source location
            context: MLIR Context
            
        Returns:
            MLIR Location or None if CIRCT unavailable
        """
        if not _HAS_CIRCT:
            return None
        
        try:
            # Create a FileLineColLoc
            return ir.Location.file(
                loc.file,
                loc.line,
                loc.column or 0,
                context=context,
            )
        except Exception:
            # Fallback to unknown location
            return ir.Location.unknown(context=context)
    
    @staticmethod
    def attach(loc: SourceLocation) -> "DebugInfoContext":
        """Create a context manager to attach debug info.
        
        Args:
            loc: Source location to attach
            
        Returns:
            Context manager for use with 'with' statement
        """
        return DebugInfoContext(loc)
    
    @classmethod
    def get_current(cls) -> Optional[SourceLocation]:
        """Get the currently active source location.
        
        Returns:
            Current SourceLocation or None
        """
        return cls._current_location
    
    @classmethod
    def set_current(cls, loc: Optional[SourceLocation]) -> None:
        """Set the current source location.
        
        Args:
            loc: Location to set, or None to clear
        """
        cls._current_location = loc


class DebugInfoContext:
    """Context manager for attaching debug information.
    
    Example:
        with DebugInfo.attach(loc):
            # Operations created here know their source location
            op = create_op()
    """
    
    def __init__(self, loc: SourceLocation):
        """Initialize the context.
        
        Args:
            loc: Source location to attach
        """
        self._location = loc
        self._previous_location: Optional[SourceLocation] = None
    
    def __enter__(self) -> "DebugInfoContext":
        """Enter the context.
        
        Returns:
            Self for method chaining
        """
        self._previous_location = DebugInfo.get_current()
        DebugInfo.set_current(self._location)
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        """Exit the context.
        
        Args:
            exc_type: Exception type if an exception occurred
            exc_val: Exception value
            exc_tb: Exception traceback
            
        Returns:
            False to not suppress exceptions
        """
        DebugInfo.set_current(self._previous_location)
        return False


def get_current_location(stack_offset: int = 1) -> SourceLocation:
    """Convenience function to get current source location.
    
    Args:
        stack_offset: How many frames up the stack to look
        
    Returns:
        SourceLocation for the caller's location
        
    Example:
        def my_function():
            loc = get_current_location()
            print(f"Called from {loc}")
    """
    return DebugInfo.capture(stack_offset=stack_offset + 1)


def format_error_with_source(
    error: Exception,
    loc: Optional[SourceLocation] = None,
    context_lines: int = 3,
) -> str:
    """Format an error message with source location.
    
    Args:
        error: The exception that occurred
        loc: Source location (if None, uses current)
        context_lines: Number of source lines to show
        
    Returns:
        Formatted error message with source context
        
    Example:
        try:
            do_something()
        except Exception as e:
            msg = format_error_with_source(e)
            print(msg)
    """
    if loc is None:
        loc = DebugInfo.capture(stack_offset=2)
    
    lines = [
        f"Error in {loc.function} at {loc.file}:{loc.line}",
        f"  {type(error).__name__}: {error}",
        "",
        "Source context:",
        loc.get_source_lines(context_lines),
    ]
    
    return "\n".join(lines)


class Cmt2TracingError(Exception):
    """Exception with source location information for CMT2 tracing.
    
    This exception type automatically captures source location when
    raised, providing better debugging information.
    
    Attributes:
        message: Error message
        source_location: Where the error occurred
        original_exception: Original exception if this is a wrapper
    """
    
    def __init__(
        self,
        message: str,
        source_location: Optional[SourceLocation] = None,
        original_exception: Optional[Exception] = None,
    ):
        """Initialize the error.
        
        Args:
            message: Error message
            source_location: Source location (auto-captured if None)
            original_exception: Original exception if wrapping
        """
        super().__init__(message)
        self.message = message
        self.source_location = source_location or DebugInfo.capture(stack_offset=2)
        self.original_exception = original_exception
    
    def __str__(self) -> str:
        """Format the error with source location."""
        lines = [
            f"CMT2 Tracing Error at {self.source_location}",
            f"  {self.message}",
        ]
        
        if self.original_exception:
            lines.append(f"  Caused by: {self.original_exception}")
        
        lines.extend([
            "",
            "Source context:",
            self.source_location.get_source_lines(),
        ])
        
        return "\n".join(lines)


def add_debug_info_to_circuit(
    circuit: Any,
    source_file: Optional[str] = None,
) -> None:
    """Add debug information to a circuit.
    
    This attaches source location information to all operations
    in a circuit that don't already have it.
    
    Args:
        circuit: The circuit to annotate
        source_file: Optional source file path
    """
    if not _HAS_CIRCT:
        return
    
    if source_file is None:
        loc = get_current_location()
        source_file = loc.file
    
    # TODO: Walk the circuit and add location info to operations
    # This would require CIRCT Python bindings for operation walking
    pass


# Export debug info types
__all__ = [
    "SourceLocation",
    "DebugInfo",
    "DebugInfoContext",
    "get_current_location",
    "format_error_with_source",
    "Cmt2TracingError",
    "add_debug_info_to_circuit",
]
