#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
JIT FIFO (E2E) — rule scheduling + STL + simulation.

This example uses two rules (produce/consume) communicating through a PyCMT2 FIFO.
It uses module-level precedence to make arbitration deterministic.

Usage:
  cd circt-cmt2/build
  PYTHONPATH=tools/circt/python_packages/circt_core:../python \\
    python3 ../examples/JIT/jit_fifo.py
"""

from __future__ import annotations

import shutil
from pathlib import Path

import cmt2.jit as jit
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from cmt2.stl import FIFO, Reg, clear_stl_registry


@jit.elaborate
def build_fifo_demo(width: int = 8, depth: int = 2) -> Circuit:
    clear_stl_registry()
    circuit = Circuit("JitFifoE2E")

    with jit.module(circuit, "FifoDemo") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(FIFO.create(circuit, width, depth=depth), "fifo", clk=clk, rst=rst)
        src = m.instance(Reg.create(circuit, width, init=0), "src", clk=clk, rst=rst)
        last = m.instance(Reg.create(circuit, width, init=0), "last", clk=clk, rst=rst)

        @jit.rule(m)
        def produce(r):
            with r.guard:
                r.ready(r.not_(fifo.full))
            with r.body:
                val = src.read
                fifo.enq(val)
                src.next = val + 1

        @jit.rule(m)
        def consume(r):
            with r.guard:
                r.ready(r.not_(fifo.empty))
            with r.body:
                last.next = fifo.deq()

        # Deterministic arbitration when both rules are enabled.
        m.builder.precedence(consume._cmt2_ref, produce._cmt2_ref)

        @jit.value(m, returns=[UInt(width)])
        def get_last(r):
            with r.guard:
                r.always()
            with r.body:
                r.returns(last.read)

    return circuit


def main() -> int:
    print("\n" + "=" * 60)
    print("E2E (JIT): FIFO demo")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_jit_fifo"
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    circuit = build_fifo_demo(width=8, depth=2)

    tb = Testbench(circuit, auto_debug_ports=False)
    with tb.sequence("basic") as seq:
        seq.reset(5)
        seq.expect("get_last_res0", 0)

        # This demo intentionally keeps I/O-free rules (no module inputs) and
        # uses FIFO.full/empty to guard enq/deq. The resulting schedule is stable
        # under the current compiler/scheduler.
        seq.wait(12)
        seq.expect("get_last_res0", 10)

    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)
    ws.generate_with_testbench(tb)
    ok, out = ws.build_and_run()
    print(out)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
