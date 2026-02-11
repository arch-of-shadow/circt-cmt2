#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT STL: thin re-export of PyCMT2 STL.

This module exists to make the intended layering explicit:
`cmt2.jit` provides syntax; `circt.pycmt2` (via `cmt2.stl`) provides the STL.
"""

from __future__ import annotations

from cmt2.stl import (
    FIFO,
    FIFO1Pull,
    FIFO1Push,
    FIFO2I,
    Memory,
    Reg,
    ShiftReg,
    Wire,
    add_stl_rtl_to_workspace,
    clear_stl_registry,
    get_stl_rtl_files,
)

__all__ = [
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
