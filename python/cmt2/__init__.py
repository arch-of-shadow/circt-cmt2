#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Cmt2 Python package (JIT only).

This repo intentionally keeps the Cmt2 "JIT" layer *thin*:
- `cmt2.jit` provides ergonomic Python syntax for building designs.
- PyCMT2 (`circt.pycmt2`) owns MLIR construction, lowering/codegen, simulation,
  and testbench infrastructure.

All legacy JIT layers (v1/v2), pass pipelines, custom backends, clock-domain/CDC
systems, and physical-design constraint APIs are removed to avoid feature
duplication with PyCMT2.
"""

from __future__ import annotations

__version__ = "0.3.0"

from . import jit

__all__ = [
    "jit",
]

# Optional convenience re-exports from PyCMT2 (no duplication; these are the
# authoritative types/builders used by JIT).
try:
    from circt.pycmt2 import (
        Circuit,
        UInt,
        SInt,
        ClockType,
        ResetType,
        AsyncResetType,
        Bundle,
        Vector,
        Clock,
        Reset,
        AsyncReset,
        Bool,
        SyncToken,
        Signal,
        ModuleBuilder,
        InterfaceBuilder,
        InterfaceDefBuilder,
        InterfaceDecl,
        SimulationWorkspace,
        Testbench,
    )

    __all__.extend(
        [
            "Circuit",
            "UInt",
            "SInt",
            "ClockType",
            "ResetType",
            "AsyncResetType",
            "Bundle",
            "Vector",
            "Clock",
            "Reset",
            "AsyncReset",
            "Bool",
            "SyncToken",
            "Signal",
            "ModuleBuilder",
            "InterfaceBuilder",
            "InterfaceDefBuilder",
            "InterfaceDecl",
            "SimulationWorkspace",
            "Testbench",
        ]
    )
except ImportError:
    # CIRCT Python bindings may not be available in all environments.
    pass
