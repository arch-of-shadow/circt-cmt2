#!/usr/bin/env python3
"""
Minimal test case to debug nested module proc_rule scheduling issue.

This test checks whether a proc_rule in a submodule executes correctly
when triggered by a parent module's method call.
"""

import sys
import os
import subprocess
from pathlib import Path

# Setup path
build_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
python_pkg_dir = os.path.join(build_dir, "build", "tools", "circt", "python_packages", "circt_core")
if os.path.exists(python_pkg_dir):
    sys.path.insert(0, python_pkg_dir)

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace

def main():
    clear_stl_registry()

    circuit = Circuit("NestedProcDebug")
    reg1_mod = Reg.create(circuit, 1, init=0)
    reg8_mod = Reg.create(circuit, 8, init=0)

    # ========================================
    # Submodule with proc_rule
    # ========================================
    with circuit.module("Calculator") as calc_mod:
        clk = calc_mod.clock()
        rst = calc_mod.reset()

        # Internal state
        input_reg = calc_mod.instance(reg8_mod, "input_reg", clk=clk, rst=rst)
        result_reg = calc_mod.instance(reg8_mod, "result_reg", clk=clk, rst=rst)
        busy_reg = calc_mod.instance(reg1_mod, "busy_reg", clk=clk, rst=rst)

        # Method: start calculation
        with calc_mod.method("start", args=[("data", UInt(8))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy_reg, "read")
                not_busy = g.eq(is_busy, g.const(0, 1))
                g.returns(not_busy)
            with meth.body() as b:
                data = b.arg("data")
                b.call(input_reg, "write", data)
                b.call(busy_reg, "write", b.const(1, 1))

        # Value: get result
        with calc_mod.value("result", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(result_reg, "read")
                b.returns(result)

        # Value: check if done
        with calc_mod.value("done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                is_busy = b.call(busy_reg, "read")
                is_done = b.eq(is_busy, b.const(0, 1))
                b.returns(is_done)

        # Static step: compute result (doubles input)
        with calc_mod.static_step(2, "compute") as step:
            """2-cycle computation: result = input * 2"""
            val = step.call(input_reg, "read")
            result = step.add(val, val)  # input * 2
            step.call(result_reg, "write", result)

        # Static step: clear busy
        with calc_mod.static_step(1, "clear_busy") as step:
            step.call(busy_reg, "write", step.const(0, 1))

        # Proc rule: trigger computation when busy
        with calc_mod.proc_rule("run_calc") as rule:
            """
            This proc_rule should execute when busy_reg is set by start().
            It computes result = input * 2 and clears busy.
            """
            with rule.guard() as g:
                is_busy = g.call(busy_reg, "read")
                g.returns(is_busy)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(calc_mod._steps["compute"].ref())
                    seq.enable(calc_mod._steps["clear_busy"].ref())

    # ========================================
    # Parent module that uses the Calculator
    # ========================================
    with circuit.module("TopModule") as top_mod:
        clk = top_mod.clock()
        rst = top_mod.reset()

        # State
        data_reg = top_mod.instance(reg8_mod, "data_reg", clk=clk, rst=rst)
        started_reg = top_mod.instance(reg1_mod, "started_reg", clk=clk, rst=rst)
        done_reg = top_mod.instance(reg1_mod, "done_reg", clk=clk, rst=rst)

        # Submodule instance
        calc = top_mod.instance(calc_mod, "calc", clk=clk, rst=rst)

        # Method: start processing
        with top_mod.method("start", args=[("data", UInt(8))]) as meth:
            with meth.guard() as g:
                is_started = g.call(started_reg, "read")
                not_started = g.eq(is_started, g.const(0, 1))
                g.returns(not_started)
            with meth.body() as b:
                data = b.arg("data")
                b.call(data_reg, "write", data)
                b.call(started_reg, "write", b.const(1, 1))
                b.call(done_reg, "write", b.const(0, 1))
                # Call submodule start
                b.call(calc, "start", data)

        # Value: get result
        with top_mod.value("result", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(calc, "result")
                b.returns(result)

        # Value: is done
        with top_mod.value("is_done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                done = b.call(done_reg, "read")
                b.returns(done)

        # Rule: wait for calc to complete and mark done
        with top_mod.rule("wait_for_calc") as rule:
            with rule.guard() as g:
                is_started = g.call(started_reg, "read")
                calc_done = g.call(calc, "done")
                should_complete = g.and_(is_started, calc_done)
                g.returns(should_complete)
            with rule.body() as b:
                b.call(started_reg, "write", b.const(0, 1))
                b.call(done_reg, "write", b.const(1, 1))

    # Generate MLIR
    mlir_str = circuit.emit_mlir()
    print("=== Generated MLIR ===")
    print(mlir_str[:4000])

    # Save MLIR to file for analysis
    with open("/tmp/nested_proc_debug.mlir", "w") as f:
        f.write(mlir_str)
    print("\nMLIR saved to /tmp/nested_proc_debug.mlir")

    # Generate simulation workspace
    print("\n=== Generating Simulation Workspace ===")
    ws_dir = os.path.join(os.path.dirname(__file__), "nested_proc_debug_sim")
    ws = SimulationWorkspace(circuit, ws_dir)
    ws.generate_placeholder()
    print(f"Simulation workspace generated at: {ws_dir}")

    # Run lowering passes to see if any errors
    print("\n=== Running Lowering Passes ===")
    try:
        result = subprocess.run(
            [os.path.join(build_dir, "build/bin/circt-opt"), "/tmp/nested_proc_debug.mlir",
             "--cmt2-inline-private-funcs",
             "--cmt2-static-inference",
             "--cmt2-compile-static",
             "--cmt2-tdcc",
             "--cmt2-proc-stmt-to-action",
             "-o", "/tmp/nested_proc_debug_lowered.mlir"],
            capture_output=True, text=True, cwd=os.path.dirname(__file__)
        )
        if result.returncode != 0:
            print(f"Lowering error: {result.stderr}")
        else:
            print("Lowering succeeded")
    except Exception as e:
        print(f"Error during lowering: {e}")

if __name__ == "__main__":
    main()
