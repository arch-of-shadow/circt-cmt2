#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Static-step call timing demo (arg_timing / result_timing) + E2E simulation.

Goal
----
Show that `arg_timing` and `result_timing` on `step.call(...)` affect the
generated SV by changing *which FSM states* a call is enabled in a
`proc.static_step`.

We use a small external module, `EnableWindowCounter`, whose counter increments
on every cycle its `run_enable` is asserted. By controlling the call's enable
window via timing attributes, we can validate the effect in simulation.

What to expect
--------------
We run 3 static steps back-to-back (each is 6 cycles long):

1) No timing attributes on the call:
   - defaults to start=0, end=1 => enable window is 1 cycle
2) Only result_timing=[(5, 6)]:
   - start defaults to 0, end becomes 6 => enable window is 6 cycles
3) arg_timing=[(2, 3)] and result_timing=[(5, 6)]:
   - start=2, end=6 => enable window is 4 cycles

The external counter therefore reaches:
  after step 1:  1
  after step 2:  7
  after step 3: 11

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core \\
      python3 ../examples/PyCMT2/static_step_call_timing_window.py
"""

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.testbench import Testbench


def build_circuit() -> Circuit:
    clear_stl_registry()

    circuit = Circuit("StaticStepCallTimingWindowDemo")

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
            returns=[("dummy_out", UInt(1))],
        )
        counter_mod.value("count", returns=[("out", UInt(32))])

    reg2 = Reg.create(circuit, 2, init=0)

    with circuit.module("TimingWindowTop") as m:
        clk = m.clock()
        rst = m.reset()

        counter = m.instance(counter_mod, "counter", clk=clk, rst=rst)
        phase = m.instance(reg2, "phase", clk=clk, rst=rst)

        # ------------------------------------------------------------
        # Step 1: default timing (no call timing attributes)
        # Call enable window defaults to [0, 1) => 1 cycle.
        # ------------------------------------------------------------
        with m.static_step(6, "default_call") as step:
            step.call(counter, "run", step.const(1, 1))
            # Advance phase at the last cycle of the step.
            step.call(phase, "write", step.const(1, 2), arg_timing=[(5, 6)])

        # ------------------------------------------------------------
        # Step 2: only result_timing
        # start defaults to 0; end becomes max(result_timing.end)=6
        # enable window => [0, 6) => 6 cycles.
        # ------------------------------------------------------------
        with m.static_step(6, "result_timing_only") as step:
            step.call(counter, "run", step.const(1, 1), result_timing=[(5, 6)])
            step.call(phase, "write", step.const(2, 2), arg_timing=[(5, 6)])

        # ------------------------------------------------------------
        # Step 3: arg_timing + result_timing
        # start becomes 2; end becomes 6 => enable window [2, 6) => 4 cycles.
        # ------------------------------------------------------------
        with m.static_step(6, "arg_and_result_timing") as step:
            step.call(
                counter,
                "run",
                step.const(1, 1),
                arg_timing=[(2, 3)],
                result_timing=[(5, 6)],
            )
            step.call(phase, "write", step.const(3, 2), arg_timing=[(5, 6)])

        # ------------------------------------------------------------
        # Proc rules: execute the 3 steps in sequence based on `phase`.
        # ------------------------------------------------------------
        with m.proc_rule("run_default") as rule:
            with rule.guard() as g:
                p = g.call(phase, "read")
                g.returns(g.eq(p, g.const(0, 2)))
            with rule.control() as ctrl:
                ctrl.enable(m._steps["default_call"].ref())

        with m.proc_rule("run_result_only") as rule:
            with rule.guard() as g:
                p = g.call(phase, "read")
                g.returns(g.eq(p, g.const(1, 2)))
            with rule.control() as ctrl:
                ctrl.enable(m._steps["result_timing_only"].ref())

        with m.proc_rule("run_arg_and_result") as rule:
            with rule.guard() as g:
                p = g.call(phase, "read")
                g.returns(g.eq(p, g.const(2, 2)))
            with rule.control() as ctrl:
                ctrl.enable(m._steps["arg_and_result_timing"].ref())

        # Observability for the testbench.
        with m.value("get_phase", returns=[UInt(2)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                p = b.call(phase, "read")
                b.returns(p)

        with m.value("get_count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                c = b.call(counter, "count")
                b.returns(c)

    return circuit


def build_testbench(circuit: Circuit) -> Testbench:
    tb = Testbench(circuit, auto_debug_ports=False)

    with tb.sequence("timing_windows") as seq:
        seq.reset(5)

        # After step 1, phase becomes 1 and count must be 1.
        seq.wait_condition("dut->get_phase_res0 == 1", timeout=100)
        seq.expect("get_count_res0", 1, "default call enable window should be 1 cycle")

        # After step 2, phase becomes 2 and count must be 7 (1 + 6).
        seq.wait_condition("dut->get_phase_res0 == 2", timeout=100)
        seq.expect("get_count_res0", 7, "result_timing should extend enable window to 6 cycles")

        # After step 3, phase becomes 3 and count must be 11 (7 + 4).
        seq.wait_condition("dut->get_phase_res0 == 3", timeout=100)
        seq.expect("get_count_res0", 11, "arg_timing should shift start to reduce window to 4 cycles")

        seq.print("Final phase", "get_phase_res0")
        seq.print("Final count", "get_count_res0")

    return tb


def main() -> int:
    print("=" * 70)
    print("StaticStep call timing demo (arg_timing/result_timing)")
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

