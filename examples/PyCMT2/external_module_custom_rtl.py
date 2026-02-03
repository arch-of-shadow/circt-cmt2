#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
External Module Example (custom RTL) using PyCMT2 EDSL

This example validates user-defined `Circuit.external_module(...)` bindings by
providing a matching SystemVerilog implementation via
`SimulationWorkspace.add_external_rtl(...)` and running an end-to-end Verilator
simulation with the Testbench DSL.

Usage:
  PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
    python3 examples/PyCMT2/external_module_custom_rtl.py
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def create_circuit() -> Circuit:
    circuit = Circuit("ExternalModuleCustomRTL")

    rtl_path = Path(__file__).resolve().parents[1] / "rtl" / "Accum32.sv"

    # Define a user external module (not backed by ModuleLibrary).
    #
    # This extern behaves like a simple accumulator register:
    # - read(): value returning the current sum
    # - add(x): action method adding x into the sum each cycle it is enabled
    with circuit.external_module("Accum32") as acc:
        acc.clock("clk")
        acc.reset("rst")
        acc.rtl_path(rtl_path)
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

    with circuit.module("Top") as m:
        clk = m.clock()
        rst = m.reset()

        accum = m.instance(circuit._external_modules["Accum32"], "accum", clk=clk, rst=rst)

        with m.rule("tick") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                b.call(accum, "add", b.const(1, 32))

        with m.value("get_count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                b.returns(b.call(accum, "read"))

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
