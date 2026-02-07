#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Static-step call timing demo (call_timing / arg_timing / result_timing) + E2E simulation.

Goal
----
Demonstrate multicycle call semantics inside `cmt2.proc.static_step`:

- `call_timing` issues the call (a 1-cycle enable pulse).
- `arg_timing` specifies when call arguments are valid.
- `result_timing` specifies when the call result is valid/captured.

The example uses a small external module, `EnableWindowCounter`, which:
- accepts a 1-cycle `run_enable` pulse, and
- increments an internal counter exactly once after a fixed latency (LAT=6).

We run 2 static steps back-to-back:
1) Start at cycle 0, capture at cycle 6.
2) Start at cycle 2, capture at cycle 8.

Expected behavior:
- The counter increments once per call (not once per cycle of the static step).
- The captured snapshot matches the counter value at the capture cycle.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core \\
      python3 ../examples/PyCMT2/static_step_call_timing_window.py
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

import inspect

import circt  # noqa: F401 (core CIRCT bindings; provided via PYTHONPATH)


def _import_pycmt2():
    """Import PyCMT2, preferring the in-tree sources when needed."""
    from circt import pycmt2 as pkg
    import importlib

    try:
        builders_mod = importlib.import_module("circt.pycmt2.builders")
        if "call_timing" in inspect.signature(builders_mod.RegionBuilder.call).parameters:
            return pkg
    except Exception:
        pass

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

    circuit = Circuit("StaticStepCallTimingDemo")

    rtl_dir = Path(__file__).resolve().parent / "rtl"
    with circuit.external_module(
        "EnableWindowCounter", rtl=rtl_dir / "EnableWindowCounter.sv"
    ) as counter_mod:
        counter_mod.clock("clk")
        counter_mod.reset("rst")
        counter_mod.method(
            "run",
            enable_name="run_enable",
            ready_name="run_ready",
            args=[("dummy", UInt(1))],
            returns=[("run_out", UInt(32))],
            static_latency=6,
        )

    reg2 = Reg.create(circuit, 2, init=0)
    reg32 = Reg.create(circuit, 32, init=0)

    with circuit.module("TimingWindowTop") as m:
        clk = m.clock()
        rst = m.reset()

        counter = m.instance(counter_mod, "counter", clk=clk, rst=rst)
        phase = m.instance(reg2, "phase", clk=clk, rst=rst)
        snapshot = m.instance(reg32, "snapshot", clk=clk, rst=rst)

        # ------------------------------------------------------------
        # Step 0: issue at cycle 0, capture at cycle 6 (LAT=6)
        # ------------------------------------------------------------
        with m.static_step(7, "start0_capture6") as step:
            y = step.call(
                counter,
                "run",
                step.const(1, 1),
                call_timing=(0, 1),
                arg_timing=[(0, 1)],
                result_timing=[(6, 7)],
            )
            step.call(
                snapshot,
                "write",
                y,
                call_timing=(6, 7),
                arg_timing=[(6, 7)],
            )
            step.call(
                phase,
                "write",
                step.const(1, 2),
                call_timing=(6, 7),
                arg_timing=[(6, 7)],
            )

        # ------------------------------------------------------------
        # Step 1: issue at cycle 2, capture at cycle 8 (LAT=6)
        # ------------------------------------------------------------
        with m.static_step(9, "start2_capture8") as step:
            y = step.call(
                counter,
                "run",
                step.const(1, 1),
                call_timing=(2, 3),
                arg_timing=[(2, 3)],
                result_timing=[(8, 9)],
            )
            step.call(
                snapshot,
                "write",
                y,
                call_timing=(8, 9),
                arg_timing=[(8, 9)],
            )
            step.call(
                phase,
                "write",
                step.const(2, 2),
                call_timing=(8, 9),
                arg_timing=[(8, 9)],
            )

        # ------------------------------------------------------------
        # Proc rules: execute the steps in sequence based on `phase`.
        # ------------------------------------------------------------
        with m.proc_rule("run_step0") as rule:
            with rule.guard() as g:
                p = g.call(phase, "read")
                g.returns(g.eq(p, g.const(0, 2)))
            with rule.control() as ctrl:
                ctrl.enable(m._steps["start0_capture6"].ref())

        with m.proc_rule("run_step1") as rule:
            with rule.guard() as g:
                p = g.call(phase, "read")
                g.returns(g.eq(p, g.const(1, 2)))
            with rule.control() as ctrl:
                ctrl.enable(m._steps["start2_capture8"].ref())

        # Observability for the testbench.
        with m.value("get_phase", returns=[UInt(2)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                p = b.call(phase, "read")
                b.returns(p)

        with m.value("get_snapshot", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                s = b.call(snapshot, "read")
                b.returns(s)

    return circuit


def build_testbench(circuit: Circuit) -> Testbench:
    tb = Testbench(circuit, auto_debug_ports=False)

    with tb.sequence("timing_windows") as seq:
        seq.reset(5)

        # After step 0, phase becomes 1 and snapshot/count must be 1.
        seq.wait_condition("dut->get_phase_res0 == 1", timeout=200)
        seq.expect("get_snapshot_res0", 1, "capture at cycle 6 should see count=1")

        # After step 1, phase becomes 2 and snapshot/count must be 2.
        seq.wait_condition("dut->get_phase_res0 == 2", timeout=400)
        seq.expect("get_snapshot_res0", 2, "shifted start still increments once")

        seq.print("Final phase", "get_phase_res0")
        seq.print("Final snapshot", "get_snapshot_res0")

    return tb


def main() -> int:
    print("=" * 70)
    print("StaticStep call timing demo (call_timing/arg_timing/result_timing)")
    print("=" * 70)

    circuit = build_circuit()
    tb = build_testbench(circuit)

    sim_dir = Path("static_step_call_timing_window_workspace")
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=False)
    ws.generate_with_testbench(tb)

    if not ws.build():
        return 1

    success, output = ws.run()
    print(output)
    print(f"Waveforms: {sim_dir / 'waves' / 'TimingWindowTop.vcd'}")
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
