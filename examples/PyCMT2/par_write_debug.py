#!/usr/bin/env python3
"""
Minimal test case to debug parallel block register write visibility issue.

This test checks whether a register written in a parallel branch is visible
to subsequent stages after the parallel block completes.
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

def main():
    clear_stl_registry()

    circuit = Circuit("ParWriteDebug")
    reg1_mod = Reg.create(circuit, 1, init=0)
    reg8_mod = Reg.create(circuit, 8, init=0)

    with circuit.module("ParWriteTest") as m:
        clk = m.clock()
        rst = m.reset()

        # Flag register - written in parallel branch
        flag_reg = m.instance(reg1_mod, "flag_reg", clk=clk, rst=rst)

        # Result register - written after parallel block based on flag
        result_reg = m.instance(reg8_mod, "result_reg", clk=clk, rst=rst)

        # Processing state
        busy_reg = m.instance(reg1_mod, "busy_reg", clk=clk, rst=rst)
        done_reg = m.instance(reg1_mod, "done_reg", clk=clk, rst=rst)

        # Value: get result
        with m.value("get_result", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(result_reg, "read")
                b.returns(result)

        # Value: is done
        with m.value("is_done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                done = b.call(done_reg, "read")
                b.returns(done)

        # Method: start
        with m.method("start", args=[("data", UInt(8))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy_reg, "read")
                not_busy = g.eq(is_busy, g.const(0, 1))
                g.returns(not_busy)
            with meth.body() as b:
                b.call(busy_reg, "write", b.const(1, 1))
                b.call(done_reg, "write", b.const(0, 1))

        # Static steps for parallel execution
        with m.static_step(1, "set_flag") as step:
            """Branch 0: set flag to 1."""
            step.call(flag_reg, "write", step.const(1, 1))

        with m.static_step(1, "dummy") as step:
            """Branch 1: do nothing (just to have two branches)."""
            pass

        with m.static_step(1, "check_flag") as step:
            """Read flag and write result based on it."""
            flag_val = step.call(flag_reg, "read")
            # If flag is 1, result = 42; if flag is 0, result = 99
            result = step.mux(flag_val, step.const(42, 8), step.const(99, 8))
            step.call(result_reg, "write", result)

        with m.static_step(1, "finish") as step:
            """Mark done."""
            step.call(busy_reg, "write", step.const(0, 1))
            step.call(done_reg, "write", step.const(1, 1))

        # Main processing rule with COMPLEX parallel block
        # Using nested seq blocks to create per-branch FSMs
        with m.proc_rule("process") as proc:
            with proc.guard() as g:
                is_busy = g.call(busy_reg, "read")
                g.returns(is_busy)

            with proc.control() as ctrl:
                with ctrl.seq() as seq:
                    # Stage 1: COMPLEX Parallel block - branches have nested seq blocks
                    # This should create per-branch FSMs (unlike simple par with enables)
                    with seq.par() as par:
                        # Branch 0: nested seq with two steps
                        with par.seq() as branch0:
                            branch0.enable(m._steps["set_flag"].ref())
                            branch0.enable(m._steps["dummy"].ref())  # Add second step to make it multi-cycle
                        # Branch 1: nested seq with one step
                        with par.seq() as branch1:
                            branch1.enable(m._steps["dummy"].ref())

                    # Stage 2: Read flag and write result
                    # THIS IS THE KEY TEST - can we see the flag written in Stage 1?
                    seq.enable(m._steps["check_flag"].ref())

                    # Final: Mark done
                    seq.enable(m._steps["finish"].ref())

    # Generate MLIR
    mlir_str = circuit.emit_mlir()
    print("=== Generated MLIR ===")
    print(mlir_str)

    # Save MLIR to file for analysis
    with open("/tmp/par_write_debug.mlir", "w") as f:
        f.write(mlir_str)
    print("\nMLIR saved to /tmp/par_write_debug.mlir")

    # Run TDCC to see state assignments
    print("\n=== Running TDCC ===")
    try:
        result = subprocess.run(
            [os.path.join(build_dir, "build/bin/circt-opt"), "/tmp/par_write_debug.mlir",
             "--cmt2-inline-private-funcs",
             "--cmt2-static-inference",
             "--cmt2-compile-static",
             "--cmt2-tdcc",
             "-o", "/tmp/par_write_debug_tdcc.mlir"],
            capture_output=True, text=True, cwd=os.path.dirname(__file__)
        )
        if result.returncode != 0:
            print(f"TDCC error: {result.stderr}")
        else:
            print("TDCC succeeded")
            with open("/tmp/par_write_debug_tdcc.mlir", "r") as f:
                tdcc_mlir = f.read()
            print("\n=== After TDCC ===")
            print(tdcc_mlir)
    except Exception as e:
        print(f"Error during TDCC: {e}")

    # Run full ProcToGAA to see generated rules
    print("\n=== Running ProcToGAA ===")
    try:
        result = subprocess.run(
            [os.path.join(build_dir, "build/bin/circt-opt"), "/tmp/par_write_debug_tdcc.mlir",
             "--cmt2-proc-to-gaa",
             "-o", "/tmp/par_write_debug_gaa.mlir"],
            capture_output=True, text=True, cwd=os.path.dirname(__file__)
        )
        if result.returncode != 0:
            print(f"ProcToGAA error: {result.stderr}")
        else:
            print("ProcToGAA succeeded")
            with open("/tmp/par_write_debug_gaa.mlir", "r") as f:
                gaa_mlir = f.read()
            print("\n=== After ProcToGAA (GAA rules) ===")
            print(gaa_mlir[:3000])  # Only print first 3000 chars
    except Exception as e:
        print(f"Error during ProcToGAA: {e}")

    # Generate simulation workspace for E2E testing
    print("\n=== Generating Simulation Workspace ===")
    from circt.pycmt2.simulation import SimulationWorkspace
    ws_dir = os.path.join(os.path.dirname(__file__), "par_write_debug_sim")
    ws = SimulationWorkspace(circuit, ws_dir)
    ws.generate_placeholder()
    print(f"Simulation workspace generated at: {ws_dir}")

if __name__ == "__main__":
    main()
