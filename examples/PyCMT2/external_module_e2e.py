#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
PyCMT2 External Module E2E Example
=================================

This example validates external FIRRTL modules (`cmt2.module.extern.firrtl`) end
to end, including:
  - `cmt2.bind.value` with input arguments
  - SystemVerilog workspace generation
  - Verilator compilation and simulation

Usage (run from repo root build dir):
  PYTHONPATH=tools/circt/python_packages/circt_core \\
    python3 ../examples/PyCMT2/external_module_e2e.py
"""

from __future__ import annotations

import inspect
import shutil
import sys
from pathlib import Path

import circt  # noqa: F401 (core CIRCT bindings; provided via PYTHONPATH)


def _import_pycmt2():
    """Import PyCMT2, preferring the in-tree sources when needed.

    The generated python package (`circt.pycmt2`) may lag behind the in-tree
    sources if bindings haven't been rebuilt after editing Python files.
    """
    from circt import pycmt2 as pkg

    if "rtl" in inspect.signature(pkg.Circuit.external_module).parameters:
        return pkg

    repo_root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo_root / "lib" / "Bindings" / "Python"))
    import pycmt2 as in_tree  # type: ignore[import-not-found]

    return in_tree


pycmt2 = _import_pycmt2()
Circuit = pycmt2.Circuit
UInt = pycmt2.UInt
SimulationWorkspace = pycmt2.SimulationWorkspace
Reg = pycmt2.Reg
Testbench = pycmt2.Testbench
clear_stl_registry = pycmt2.clear_stl_registry


def build_circuit() -> Circuit:
    clear_stl_registry()

    circuit = Circuit("ExternalModuleE2E")

    alu_rtl = Path(__file__).resolve().parent / "rtl" / "ALU.sv"
    with circuit.external_module("ALU", rtl=alu_rtl) as alu_mod:
        alu_mod.value(
            "add",
            args=[("a", UInt(8)), ("b", UInt(8))],
            returns=[("out", UInt(8))],
        )

    reg8 = Reg.create(circuit, 8, init=0)

    with circuit.module("Top") as m:
        clk = m.clock()
        rst = m.reset()

        a_reg = m.instance(reg8, "a_reg", clk=clk, rst=rst)
        b_reg = m.instance(reg8, "b_reg", clk=clk, rst=rst)

        alu = m.instance(alu_mod, "alu")

        with m.method("load", args=[("a", UInt(8)), ("b", UInt(8))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(a_reg, "write", body.arg("a"))
                body.call(b_reg, "write", body.arg("b"))

        with m.value("result", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                a = body.call(a_reg, "read")
                b = body.call(b_reg, "read")
                s = body.call(alu, "add", a, b)
                body.returns(s)

    return circuit


def build_testbench(circuit: Circuit) -> Testbench:
    tb = Testbench(circuit, auto_debug_ports=False)

    vectors = [
        (0, 0, 0),
        (1, 2, 3),
        (7, 9, 16),
        (250, 10, 4),  # wraparound
    ]

    with tb.sequence("external_module_e2e") as seq:
        seq.reset(5)
        for a, b, expect in vectors:
            seq.expect("load_ready", 1)
            seq.drive("load_a", a)
            seq.drive("load_b", b)
            seq.drive("load_enable", 1)
            seq.wait(1)
            seq.drive("load_enable", 0)
            seq.wait(2)
            seq.expect("result_ready", 1)
            seq.expect("result_res0", expect)

    return tb


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    sim_dir = script_dir / "sim_external_module_e2e"
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    circuit = build_circuit()
    tb = build_testbench(circuit)

    ws = SimulationWorkspace(circuit, sim_dir)
    ws.generate_with_testbench(tb)
    success, output = ws.build_and_run()
    print(output)
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
