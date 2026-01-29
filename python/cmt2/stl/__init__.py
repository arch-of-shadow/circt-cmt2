#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Cmt2 STL (PyCMT2 wrapper).

This module intentionally does *not* re-implement an STL. It re-exports PyCMT2's
STL factories so `cmt2` stays stacked on `circt.pycmt2` without feature
duplication.

Use:
  - `from cmt2.stl import Reg, FIFO, Memory`
  - or directly `from circt.pycmt2.stl import ...`
"""

from __future__ import annotations

__all__: list[str] = []

try:
    from circt.pycmt2.stl import (  # type: ignore
        Reg,
        Wire,
        FIFO,
        FIFO1Push,
        FIFO1Pull,
        FIFO2I,
        ShiftReg,
        Memory,
        get_stl_rtl_files,
        add_stl_rtl_to_workspace,
        clear_stl_registry,
    )

    __all__.extend(
        [
            "Reg",
            "Wire",
            "FIFO",
            "FIFO1Push",
            "FIFO1Pull",
            "FIFO2I",
            "ShiftReg",
            "Memory",
            "get_stl_rtl_files",
            "add_stl_rtl_to_workspace",
            "clear_stl_registry",
        ]
    )
except ImportError as e:  # pragma: no cover
    raise ImportError(
        "cmt2.stl requires CIRCT Python bindings (circt.pycmt2). "
        "Set PYTHONPATH to include circt_core."
    ) from e
