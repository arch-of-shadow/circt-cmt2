#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT wrappers for PyCMT2 external modules."""

from __future__ import annotations

from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator


def _infer_assignment_name(*, depth: int) -> str | None:
    try:
        from circt.pycmt2.circuit import _get_assignment_target
    except Exception:
        _get_assignment_target = None

    if _get_assignment_target is None:
        return None
    return _get_assignment_target(depth=depth)


@contextmanager
def external_module(
    circuit: Any,
    name: str | None = None,
    *,
    alias: str | None = None,
    rtl_path: str | Path | None = None,
    rtl_filename: str | None = None,
) -> Iterator[Any]:
    """Define a PyCMT2 external module with optional RTL attachment.

    This is a thin convenience wrapper around `Circuit.external_module(...)`:
    - Adds name inference compatible with JIT wrappers (or `alias=...`).
    - Optionally records an RTL file path on the external module builder, so
      `SimulationWorkspace` can stage it automatically.

    Args:
        circuit: A `circt.pycmt2.Circuit`.
        name: Optional explicit symbol name (avoid; prefer inference/alias).
        alias: Explicit name to use when inference is unavailable.
        rtl_path: Optional path to a Verilog/SystemVerilog file implementing
            the external module (for custom externs not backed by ModuleLibrary).
        rtl_filename: Optional filename to use under the simulation workspace
            `rtl/` directory (defaults to basename of `rtl_path`).
    """
    if name is not None and alias is not None:
        raise TypeError("Use either `name` or `alias`, not both")

    ext_name = name or alias or _infer_assignment_name(depth=4) or _infer_assignment_name(depth=5)
    if not ext_name:
        raise TypeError("Cannot infer external module name; pass `name=...` or `alias=...`")

    with circuit.external_module(ext_name) as ext:
        if rtl_path is not None:
            ext.rtl_path(rtl_path, filename=rtl_filename)
        yield ext
