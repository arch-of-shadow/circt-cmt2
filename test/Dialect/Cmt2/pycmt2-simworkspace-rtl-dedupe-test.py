#!/usr/bin/env python3
# RUN: cd %S && PYTHONPATH=%circt_build_path/tools/circt/python_packages/circt_core %python %s | FileCheck %s

"""Regression: SimulationWorkspace must not emit duplicate module defs.

SimulationWorkspace writes a top-level <Top>.sv from Circuit.emit_verilog(), and
may also write additional external RTL files (ModuleLibrary-backed).

This test asserts that no module defined in <Top>.sv is re-defined by any other
SV file written into the workspace's rtl/ directory.
"""

from __future__ import annotations

import re
import tempfile
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry


def extract_defined_modules(verilog: str) -> set[str]:
    return set(re.findall("(?m)^\\s*module\\s+([A-Za-z_][A-Za-z0-9_$]*)\\b", verilog))


def test_no_duplicate_modules_in_workspace_rtl():
    clear_stl_registry()

    circuit = Circuit("TestNoDuplicateModules")
    reg32 = Reg.create(circuit, 32)

    with circuit.module("Top") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(reg32, "count", clk=clk, rst=rst)

        with m.rule("inc") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                val = b.call(count, "read")
                b.call(count, "write", b.add(val, b.const(1, 32)))

    with tempfile.TemporaryDirectory() as td:
        ws_dir = Path(td) / "ws"
        ws = SimulationWorkspace(circuit, ws_dir)
        ws.generate_placeholder()

        rtl_dir = ws_dir / "rtl"
        top_sv = rtl_dir / "Top.sv"
        top_defs = extract_defined_modules(top_sv.read_text())

        external_defs: set[str] = set()
        for sv in sorted(rtl_dir.glob("*.sv")):
            if sv.name == "Top.sv":
                continue
            external_defs |= extract_defined_modules(sv.read_text())

        intersection = top_defs & external_defs

        print(f"TOP_MODULE_DEFS: {len(top_defs)}")
        print(f"EXTERNAL_MODULE_DEFS: {len(external_defs)}")
        print(f"INTERSECTION: {len(intersection)}")
        if intersection:
            print("DUPLICATES:", ", ".join(sorted(intersection)))

        # CHECK: INTERSECTION: 0
        assert not intersection, f"Duplicate module definitions: {sorted(intersection)}"

    print("OK")
    # CHECK: OK


if __name__ == "__main__":
    test_no_duplicate_modules_in_workspace_rtl()

