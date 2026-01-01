#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 Diagnostic System for Python.

This module provides a multi-level diagnostic reporting system for PyCMT2
with complete source location tracking. Diagnostics emitted by CMT2 passes
can be captured, formatted, and displayed with Python source context.

Diagnostic Levels:
    - ERROR: Fatal errors that prevent compilation
    - WARNING: Non-fatal issues that may affect correctness
    - INFO: Informational messages about compilation progress
    - DEBUG: Detailed debugging information

Example:
    from pycmt2.diagnostics import DiagnosticHandler, DiagnosticLevel

    # Capture diagnostics during compilation
    with DiagnosticHandler() as handler:
        verilog = circuit.to_verilog()

        if handler.has_errors():
            print("Compilation failed!")
            for diag in handler.get_diagnostics():
                print(diag.format())

    # Or use the simple API
    from pycmt2.diagnostics import emit_error, emit_warning

    emit_error(location, "type mismatch", notes=["expected UInt<32>"])
"""

from __future__ import annotations

import sys
import traceback
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import TYPE_CHECKING, Callable, List, Optional, Tuple

if TYPE_CHECKING:
    from circt.ir import Context, Location

from .location import PythonLocation, get_python_location


class DiagnosticLevel(IntEnum):
    """Severity levels for diagnostics."""
    ERROR = 0
    WARNING = 1
    INFO = 2
    DEBUG = 3

    def __str__(self) -> str:
        return self.name.lower()

    @property
    def prefix(self) -> str:
        """Get the prefix for formatting."""
        prefixes = {
            DiagnosticLevel.ERROR: "error",
            DiagnosticLevel.WARNING: "warning",
            DiagnosticLevel.INFO: "info",
            DiagnosticLevel.DEBUG: "debug",
        }
        return prefixes[self]

    @property
    def color_code(self) -> str:
        """Get ANSI color code for terminal output."""
        colors = {
            DiagnosticLevel.ERROR: "\033[1;31m",    # Bold red
            DiagnosticLevel.WARNING: "\033[1;33m",  # Bold yellow
            DiagnosticLevel.INFO: "\033[1;36m",     # Bold cyan
            DiagnosticLevel.DEBUG: "\033[0;37m",    # Gray
        }
        return colors[self]


@dataclass
class Note:
    """A note attached to a diagnostic."""
    message: str
    location: Optional[PythonLocation] = None

    def format(self, color: bool = False) -> str:
        """Format the note for display."""
        loc_str = ""
        if self.location:
            loc_str = f"{self.location.filename}:{self.location.line}: "
        return f"  note: {loc_str}{self.message}"


@dataclass
class Diagnostic:
    """A diagnostic message with location and notes.

    Attributes:
        level: The severity level (ERROR, WARNING, INFO, DEBUG).
        message: The main diagnostic message.
        location: The Python source location where the issue was detected.
        notes: Additional notes providing context or hints.
        hint: A suggestion for how to fix the issue.
        mlir_location: The original MLIR location string (if available).
    """
    level: DiagnosticLevel
    message: str
    location: Optional[PythonLocation] = None
    notes: List[Note] = field(default_factory=list)
    hint: Optional[str] = None
    mlir_location: Optional[str] = None

    def add_note(self, message: str,
                 location: Optional[PythonLocation] = None) -> Diagnostic:
        """Add a note to this diagnostic."""
        self.notes.append(Note(message, location))
        return self

    def set_hint(self, hint: str) -> Diagnostic:
        """Set a hint for fixing the issue."""
        self.hint = hint
        return self

    def format(self, color: bool = True, show_mlir_loc: bool = False) -> str:
        """Format the diagnostic for display.

        Args:
            color: Whether to use ANSI color codes.
            show_mlir_loc: Whether to show the MLIR location.

        Returns:
            Formatted diagnostic string.
        """
        lines = []

        # Reset code
        reset = "\033[0m" if color else ""
        level_color = self.level.color_code if color else ""

        # Location prefix
        loc_str = ""
        if self.location:
            loc_str = f"{self.location.filename}:{self.location.line}: "

        # Main message
        lines.append(
            f"{loc_str}{level_color}{self.level.prefix}:{reset} {self.message}"
        )

        # MLIR location if requested
        if show_mlir_loc and self.mlir_location:
            lines.append(f"  mlir: {self.mlir_location}")

        # Notes
        for note in self.notes:
            lines.append(note.format(color))

        # Hint
        if self.hint:
            hint_color = "\033[1;32m" if color else ""  # Bold green
            lines.append(f"  {hint_color}hint:{reset} {self.hint}")

        return "\n".join(lines)

    def __str__(self) -> str:
        return self.format(color=False)


def emit_error(location: Optional[PythonLocation], message: str,
               notes: Optional[List[str]] = None,
               hint: Optional[str] = None) -> Diagnostic:
    """Emit an error diagnostic.

    Args:
        location: The Python source location.
        message: The error message.
        notes: Optional list of note messages.
        hint: Optional hint for fixing the issue.

    Returns:
        The created Diagnostic.
    """
    diag = Diagnostic(DiagnosticLevel.ERROR, message, location)
    if notes:
        for note in notes:
            diag.add_note(note)
    if hint:
        diag.set_hint(hint)
    return diag


def emit_warning(location: Optional[PythonLocation], message: str,
                 notes: Optional[List[str]] = None,
                 hint: Optional[str] = None) -> Diagnostic:
    """Emit a warning diagnostic."""
    diag = Diagnostic(DiagnosticLevel.WARNING, message, location)
    if notes:
        for note in notes:
            diag.add_note(note)
    if hint:
        diag.set_hint(hint)
    return diag


def emit_info(location: Optional[PythonLocation], message: str,
              notes: Optional[List[str]] = None) -> Diagnostic:
    """Emit an info diagnostic."""
    diag = Diagnostic(DiagnosticLevel.INFO, message, location)
    if notes:
        for note in notes:
            diag.add_note(note)
    return diag


def emit_debug(location: Optional[PythonLocation], message: str) -> Diagnostic:
    """Emit a debug diagnostic."""
    return Diagnostic(DiagnosticLevel.DEBUG, message, location)


class DiagnosticHandler:
    """A context manager for capturing diagnostics during compilation.

    This handler can be used to capture diagnostics emitted by CMT2 passes
    during compilation and format them with Python source context.

    Example:
        with DiagnosticHandler() as handler:
            verilog = circuit.to_verilog()

        for diag in handler.get_diagnostics():
            print(diag.format())

        if handler.has_errors():
            sys.exit(1)
    """

    def __init__(self, min_level: DiagnosticLevel = DiagnosticLevel.INFO,
                 capture_debug: bool = False):
        """Create a diagnostic handler.

        Args:
            min_level: Minimum level to capture (default: INFO).
            capture_debug: Whether to capture DEBUG level diagnostics.
        """
        self._min_level = min_level
        self._capture_debug = capture_debug
        self._diagnostics: List[Diagnostic] = []
        self._error_count = 0
        self._warning_count = 0
        self._info_count = 0
        self._debug_count = 0

    def __enter__(self) -> DiagnosticHandler:
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

    def add_diagnostic(self, diag: Diagnostic) -> None:
        """Add a diagnostic to the handler."""
        if diag.level == DiagnosticLevel.DEBUG and not self._capture_debug:
            return
        if diag.level > self._min_level:
            return

        self._diagnostics.append(diag)

        if diag.level == DiagnosticLevel.ERROR:
            self._error_count += 1
        elif diag.level == DiagnosticLevel.WARNING:
            self._warning_count += 1
        elif diag.level == DiagnosticLevel.INFO:
            self._info_count += 1
        elif diag.level == DiagnosticLevel.DEBUG:
            self._debug_count += 1

    def get_diagnostics(self, level: Optional[DiagnosticLevel] = None) -> List[Diagnostic]:
        """Get captured diagnostics, optionally filtered by level."""
        if level is None:
            return self._diagnostics.copy()
        return [d for d in self._diagnostics if d.level == level]

    def has_errors(self) -> bool:
        """Check if any errors were captured."""
        return self._error_count > 0

    def has_warnings(self) -> bool:
        """Check if any warnings were captured."""
        return self._warning_count > 0

    @property
    def error_count(self) -> int:
        return self._error_count

    @property
    def warning_count(self) -> int:
        return self._warning_count

    @property
    def info_count(self) -> int:
        return self._info_count

    @property
    def debug_count(self) -> int:
        return self._debug_count

    def format_summary(self) -> str:
        """Get a summary of diagnostics."""
        parts = []
        if self._error_count > 0:
            parts.append(f"{self._error_count} error(s)")
        if self._warning_count > 0:
            parts.append(f"{self._warning_count} warning(s)")
        if self._info_count > 0:
            parts.append(f"{self._info_count} info")
        if parts:
            return ", ".join(parts) + " generated"
        return "no diagnostics"

    def print_all(self, file=None, color: bool = True) -> None:
        """Print all diagnostics to a file (default: stderr)."""
        if file is None:
            file = sys.stderr

        for diag in self._diagnostics:
            print(diag.format(color=color), file=file)

        if self._diagnostics:
            print(self.format_summary(), file=file)


def parse_mlir_location(loc_str: str) -> Optional[PythonLocation]:
    """Parse an MLIR FileLineColLoc string to extract Python location.

    Args:
        loc_str: MLIR location string like 'loc("file.py":10:5)'

    Returns:
        PythonLocation if the location is a Python file, None otherwise.
    """
    import re

    # Match patterns like loc("file.py":10:5) or "file.py":10:5
    patterns = [
        r'loc\("([^"]+)":(\d+):(\d+)\)',
        r'"([^"]+)":(\d+):(\d+)',
        r'([^:]+):(\d+):(\d+)',
    ]

    for pattern in patterns:
        match = re.search(pattern, loc_str)
        if match:
            filename = match.group(1)
            line = int(match.group(2))
            column = int(match.group(3))

            if filename.endswith('.py') or filename.endswith('.pyw'):
                return PythonLocation(filename, line, column, "<unknown>")

    return None


def format_diagnostic_with_source(diag: Diagnostic, context_lines: int = 2) -> str:
    """Format a diagnostic with source code context.

    Args:
        diag: The diagnostic to format.
        context_lines: Number of context lines before/after the error line.

    Returns:
        Formatted string with source context.
    """
    lines = [diag.format(color=True)]

    if diag.location and diag.location.filename != "<unknown>":
        try:
            source_path = Path(diag.location.filename)
            if source_path.exists():
                with open(source_path) as f:
                    source_lines = f.readlines()

                target_line = diag.location.line
                start = max(0, target_line - context_lines - 1)
                end = min(len(source_lines), target_line + context_lines)

                lines.append("")
                for i in range(start, end):
                    line_num = i + 1
                    prefix = ">" if line_num == target_line else " "
                    lines.append(f"  {prefix} {line_num:4d} | {source_lines[i].rstrip()}")

                    # Add caret for column if available
                    if line_num == target_line and diag.location.column > 0:
                        caret_line = " " * (diag.location.column + 10) + "^"
                        lines.append(caret_line)
        except (IOError, OSError):
            pass  # Skip source context if file can't be read

    return "\n".join(lines)


# Type conversion error helpers

def type_mismatch_error(location: Optional[PythonLocation],
                        expected: str, actual: str,
                        context: str = "") -> Diagnostic:
    """Create a type mismatch error diagnostic.

    Args:
        location: Source location.
        expected: Expected type string.
        actual: Actual type string.
        context: Additional context (e.g., "in method argument").

    Returns:
        Diagnostic for the type mismatch.
    """
    msg = f"type mismatch: expected {expected}, got {actual}"
    if context:
        msg = f"{context}: {msg}"

    return emit_error(location, msg,
                      notes=[f"expected: {expected}", f"actual: {actual}"],
                      hint="check argument types match method signature")


def undefined_reference_error(location: Optional[PythonLocation],
                               kind: str, name: str,
                               container: str = "") -> Diagnostic:
    """Create an undefined reference error diagnostic.

    Args:
        location: Source location.
        kind: Kind of reference (method, value, module, instance).
        name: Name of the undefined reference.
        container: Container where it was looked up.

    Returns:
        Diagnostic for the undefined reference.
    """
    msg = f"undefined {kind} '{name}'"
    if container:
        msg += f" in {container}"

    hints = {
        "method": "check spelling or ensure the method is defined",
        "value": "check spelling or ensure the value is defined",
        "module": "check that the module is defined in the circuit",
        "instance": "check that the instance exists in the module",
    }

    return emit_error(location, msg,
                      hint=hints.get(kind, "check the definition exists"))


def scheduling_conflict_warning(location: Optional[PythonLocation],
                                 method1: str, method2: str,
                                 reason: str = "") -> Diagnostic:
    """Create a scheduling conflict warning diagnostic.

    Args:
        location: Source location.
        method1: First conflicting method.
        method2: Second conflicting method.
        reason: Reason for the conflict.

    Returns:
        Diagnostic for the scheduling conflict.
    """
    msg = f"scheduling conflict between '{method1}' and '{method2}'"
    if reason:
        msg += f": {reason}"

    return emit_warning(location, msg,
                        notes=["these methods cannot execute in the same cycle"],
                        hint="consider explicit sequencing or use different steps")
