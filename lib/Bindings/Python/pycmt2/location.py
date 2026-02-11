#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Python source location capture for PyCMT2.

This module provides utilities for capturing Python source locations
and converting them to MLIR locations, enabling source-level debugging
of generated hardware.

Example:
    from pycmt2.location import get_python_location

    class MyBuilder:
        def __init__(self):
            # Capture location at construction time
            self._python_loc = get_python_location(depth=1)

        def get_mlir_location(self, ctx):
            return self._python_loc.to_mlir_location(ctx)
"""

from __future__ import annotations

import sys
import traceback
from contextlib import contextmanager
from contextvars import ContextVar
from dataclasses import dataclass
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    from circt.ir import Context, Location


@dataclass
class PythonLocation:
    """Represents a location in Python source code.

    This class captures file, line, column, and function information
    from Python source code and can convert it to MLIR locations.

    Attributes:
        filename: The source file path.
        line: The line number (1-indexed).
        column: The column number (0 for Python, which doesn't track columns).
        function: The function name where the location was captured.
    """
    filename: str
    line: int
    column: int
    function: str

    def to_mlir_location(self, ctx: Context) -> Location:
        """Convert to an MLIR FileLineColLoc.

        Args:
            ctx: The MLIR context to create the location in.

        Returns:
            An MLIR Location representing this Python source location.
        """
        from circt.ir import Location
        return Location.file(self.filename, self.line, self.column, context=ctx)

    def to_fused_location(self, ctx: Context, other: Location) -> Location:
        """Create a fused location combining this with another location.

        This is useful for preserving both the original Python location
        and generated/transformed locations.

        Args:
            ctx: The MLIR context.
            other: Another location to fuse with.

        Returns:
            A fused MLIR Location.
        """
        from circt.ir import Location
        python_loc = self.to_mlir_location(ctx)
        return Location.fused([python_loc, other], context=ctx)

    def __str__(self) -> str:
        """Format as a human-readable string."""
        return f"{self.filename}:{self.line} in {self.function}()"

    def __repr__(self) -> str:
        return f"PythonLocation({self.filename!r}, {self.line}, {self.column}, {self.function!r})"


def get_python_location(depth: int = 1) -> PythonLocation:
    """Capture the current Python source location.

    This function inspects the call stack to capture the source location
    of the calling code. This enables tracing generated hardware back
    to the Python source that created it.

    Args:
        depth: Number of stack frames to skip (0 = this function,
               1 = caller, 2 = caller's caller, etc.).

    Returns:
        A PythonLocation with the captured source information.

    Example:
        def create_rule(self, name):
            # depth=1 captures the location where create_rule was called
            loc = get_python_location(depth=1)
            ...
    """
    try:
        # Add 1 to skip get_python_location itself
        frame = sys._getframe(depth + 1)
        return PythonLocation(
            filename=frame.f_code.co_filename,
            line=frame.f_lineno,
            column=0,  # Python doesn't track column numbers
            function=frame.f_code.co_name,
        )
    except (ValueError, AttributeError):
        # Fallback if frame inspection fails
        return PythonLocation(
            filename="<unknown>",
            line=0,
            column=0,
            function="<unknown>",
        )


def get_python_location_from_traceback() -> list[PythonLocation]:
    """Capture the full Python traceback as a list of locations.

    This is useful for debugging to see the complete call stack
    that led to a particular operation.

    Returns:
        A list of PythonLocation objects, from innermost to outermost frame.
    """
    locations = []
    for frame_info in traceback.extract_stack()[:-1]:  # Exclude this function
        locations.append(PythonLocation(
            filename=frame_info.filename,
            line=frame_info.lineno,
            column=0,
            function=frame_info.name,
        ))
    return locations


class LocationTracker:
    """Context manager for tracking source locations.

    This class provides a convenient way to capture and manage
    source locations for a scope of operations.

    Example:
        with LocationTracker() as tracker:
            # All operations in this block use the captured location
            rule = create_rule("my_rule")
            tracker.attach(rule)
    """

    def __init__(self, depth: int = 1):
        """Create a location tracker.

        Args:
            depth: Stack depth to capture (additional frames above __enter__).
        """
        self._depth = depth
        self._location: Optional[PythonLocation] = None

    def __enter__(self) -> LocationTracker:
        # Capture location at entry (add 1 for __enter__ frame)
        self._location = get_python_location(depth=self._depth + 1)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

    @property
    def location(self) -> Optional[PythonLocation]:
        """Get the captured location."""
        return self._location

    def get_mlir_location(self, ctx: Context) -> Location:
        """Get the MLIR location for the captured Python location.

        Args:
            ctx: The MLIR context.

        Returns:
            An MLIR Location, or Location.unknown if no location captured.
        """
        from circt.ir import Location
        if self._location:
            return self._location.to_mlir_location(ctx)
        return Location.unknown(context=ctx)


def format_location_chain(locations: list[PythonLocation], max_depth: int = 5) -> str:
    """Format a chain of locations for error messages.

    Args:
        locations: List of locations from innermost to outermost.
        max_depth: Maximum number of locations to show.

    Returns:
        A formatted string showing the location chain.

    Example output:
        at example.py:15 in create_counter()
        at example.py:42 in main()
        at example.py:50 in <module>
    """
    lines = []
    for i, loc in enumerate(locations[:max_depth]):
        indent = "  " * i
        lines.append(f"{indent}at {loc}")

    if len(locations) > max_depth:
        lines.append(f"  ... ({len(locations) - max_depth} more frames)")


# -----------------------------------------------------------------------------
# Default elaboration location plumbing (used by cmt2.jit.elaborate)
# -----------------------------------------------------------------------------

_DEFAULT_PYTHON_LOCATION: ContextVar[PythonLocation | None] = ContextVar(
    "pycmt2_default_python_location", default=None
)


@contextmanager
def default_python_location(depth: int = 1):
    """Temporarily set a default Python location for new IR roots.

    This is intentionally lightweight: it only affects places where PyCMT2
    would otherwise use `Location.unknown` (e.g., circuit/module containers).
    """
    token = _DEFAULT_PYTHON_LOCATION.set(get_python_location(depth=depth + 1))
    try:
        yield
    finally:
        _DEFAULT_PYTHON_LOCATION.reset(token)


def get_default_mlir_location(ctx: "Context") -> "Location":
    """Get the current default MLIR location (or unknown)."""
    from circt.ir import Location

    loc = _DEFAULT_PYTHON_LOCATION.get()
    if loc is None:
        return Location.unknown(context=ctx)
    return loc.to_mlir_location(ctx)

    return "\n".join(lines)
