#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
External Module Example (custom RTL) using Cmt2 JIT stacked on PyCMT2.

This example validates that:
- PyCMT2 external modules (`Circuit.external_module`) can be instantiated and
  called through the JIT attribute-based API.
- Externs not backed by ModuleLibrary can be simulated by attaching an RTL file
  path (`rtl_path=...`) and letting `SimulationWorkspace` stage it automatically.

Usage:
  PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
    python3 examples/JIT/external_module_custom_rtl.py
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

import cmt2.jit as jit

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def create_circuit() -> Circuit:
    circuit = Circuit("ExternalModuleCustomRTLJIT")

    rtl_path = Path(__file__).resolve().parents[1] / "rtl" / "Accum32.sv"

    with jit.external_module(circuit, "Accum32", rtl_path=rtl_path) as acc:
        acc.clock("clk")
        acc.reset("rst")
        acc.value(
            "read",
            ready_name="read_ready",
            returns=[("read_data", UInt(32))],
        )
        acc.method(
            "add",
            enable_name="add_enable",
            ready_name="add_ready",
            args=[("add_data", UInt(32))],
        )
        acc.sequence_before("read", "add")

    accum_mod = circuit._external_modules[acc.name]

    with jit.module(circuit, "Top") as m:
        clk = m.clock()
        rst = m.reset()

        accum = m.instance(accum_mod, clk=clk, rst=rst)

        @jit.rule(m)
        def tick(r):
            with r.guard:
                r.always()
            with r.body:
                one = r.const(1, 32)
                accum.add(one)

        @jit.value(m)
        def get_count(v) -> UInt[32]:
            with v.guard:
                v.always()
            with v.body:
                v.returns(accum.read)

    return circuit


def create_testbench(circuit: Circuit) -> Testbench:
    tb = Testbench(circuit, auto_debug_ports=False)

    with tb.sequence("test_reset") as seq:
        seq.reset(5)
        seq.expect("get_count_res0", 0)

    with tb.sequence("test_increment") as seq:
        seq.reset(5)
        for i in range(1, 6):
            seq.wait(1)
            seq.expect("get_count_res0", i)

    return tb


def run_sim() -> int:
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_external_module_custom_rtl"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    circuit = create_circuit()
    tb = create_testbench(circuit)

    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=False)
    ws.generate_with_testbench(tb)

    if not ws.build():
        return 1

    ok, out = ws.run()
    if not ok:
        print(out)
        return 1

    print("PASSED: All tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(run_sim())
