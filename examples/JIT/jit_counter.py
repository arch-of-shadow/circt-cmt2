#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
JIT Counter (E2E) — JIT syntax stacked on PyCMT2.

This example:
- builds the design using `cmt2.jit`
- generates a Verilator workspace via PyCMT2 `SimulationWorkspace`
- runs a PyCMT2 `Testbench` sequence to validate behavior

Usage:
  cd circt-cmt2/build
  PYTHONPATH=tools/circt/python_packages/circt_core:../python \\
    python3 ../examples/JIT/jit_counter.py
"""

from __future__ import annotations

import shutil
from pathlib import Path

import cmt2.jit as jit
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from cmt2.stl import Reg, clear_stl_registry


@jit.elaborate
def build_counter(width: int = 32) -> Circuit:
    clear_stl_registry()
    circuit = Circuit("JitCounterE2E")

    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(Reg.create(circuit, width, init=0), clk=clk, rst=rst)

        @jit.rule(m)
        def increment(r):
            with r.guard:
                r.always()
            with r.body:
                count.next = count.read + 1

        @jit.value(m)
        def get_count(r) -> UInt[width]:
            with r.guard:
                r.always()
            with r.body:
                r.returns(count.read)

    return circuit


def main() -> int:
    print("\n" + "=" * 60)
    print("E2E (JIT): Counter")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_jit_counter"
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    circuit = build_counter(width=32)

    tb = Testbench(circuit, auto_debug_ports=False)
    with tb.sequence("basic") as seq:
        seq.reset(5)
        seq.expect("get_count_res0", 0, "count starts at 0 after reset")
        for i in range(1, 6):
            seq.wait(1)
            seq.expect("get_count_res0", i, f"count increments to {i}")

    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)
    ws.generate_with_testbench(tb)
    ok, out = ws.build_and_run()
    print(out)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
