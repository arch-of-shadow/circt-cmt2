#!/usr/bin/env python3
"""
Test case for Issue 7: Submodule proc_rule triggered from static step.

This test verifies whether calling a submodule method from within a static step
correctly triggers the submodule's proc_rule.

Expected behavior:
1. Parent's proc_rule fires when triggered
2. Parent's static step calls submodule.start()
3. This sets submodule's busy_reg = 1
4. Submodule's proc_rule guard becomes true (busy_reg == 1)
5. Submodule's proc_rule fires and executes its control flow
6. Submodule computes result and clears busy_reg
7. Parent waits for submodule to complete, then collects result
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


# C++ testbench for this specific test
TESTBENCH_CPP = r"""
#include <verilated.h>
#include <verilated_vcd_c.h>
#include "VParentModule.h"
#include <iostream>

VParentModule* dut;
VerilatedVcdC* tfp;
uint64_t sim_time = 0;

void tick() {
    dut->clk = 0;
    dut->eval();
    tfp->dump(sim_time++);

    dut->clk = 1;
    dut->eval();
    tfp->dump(sim_time++);
}

void reset() {
    dut->rst = 1;
    for (int i = 0; i < 5; i++) tick();
    dut->rst = 0;
    tick();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    dut = new VParentModule;
    tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("waves/ParentModule.vcd");

    // Initialize
    dut->clk = 0;
    dut->rst = 0;
    dut->start_enable = 0;
    dut->start_data = 0;

    reset();
    std::cout << "Reset complete" << std::endl;

    // Test: call start method with data=21
    // Expected: submodule computes 21 * 2 = 42
    std::cout << "\n=== Test: Calling start(21) ===" << std::endl;
    std::cout << "Expected result: 42 (21 * 2)" << std::endl;

    // Wait for start_ready
    int wait = 0;
    while (!dut->start_ready && wait < 20) {
        tick();
        wait++;
    }

    if (!dut->start_ready) {
        std::cerr << "FAIL: start method not ready after " << wait << " cycles" << std::endl;
        tfp->close();
        return 1;
    }

    // Call start method
    dut->start_data = 21;
    dut->start_enable = 1;
    tick();
    dut->start_enable = 0;

    std::cout << "Started processing..." << std::endl;

    // Wait for completion (done_res0 = 1)
    wait = 0;
    while (!dut->done_res0 && wait < 100) {
        tick();
        wait++;

        // Debug output every 10 cycles
        if (wait % 10 == 0) {
            std::cout << "  Cycle " << wait << ": done=" << (int)dut->done_res0
                      << " result=" << (int)dut->result_res0 << std::endl;
        }
    }

    if (wait >= 100) {
        std::cerr << "TIMEOUT: processing did not complete after " << wait << " cycles" << std::endl;
        std::cerr << "  done=" << (int)dut->done_res0 << " result=" << (int)dut->result_res0 << std::endl;
        tfp->close();
        return 1;
    }

    // Check result
    uint32_t result = dut->result_res0;
    std::cout << "Completed in " << wait << " cycles" << std::endl;
    std::cout << "Result = " << result << std::endl;

    if (result == 42) {
        std::cout << "\n=== TEST PASSED ===" << std::endl;
        std::cout << "Submodule proc_rule fired correctly from static step trigger" << std::endl;
    } else {
        std::cerr << "\n=== TEST FAILED ===" << std::endl;
        std::cerr << "Expected 42, got " << result << std::endl;
        tfp->close();
        return 1;
    }

    tfp->close();
    delete tfp;
    delete dut;
    return 0;
}
"""


def main():
    clear_stl_registry()

    circuit = Circuit("SubmoduleProcStepTest")
    reg1_mod = Reg.create(circuit, 1, init=0)
    reg8_mod = Reg.create(circuit, 8, init=0)

    # ========================================
    # Submodule with proc_rule
    # ========================================
    with circuit.module("Calculator") as calc_mod:
        clk = calc_mod.clock()
        rst = calc_mod.reset()

        input_reg = calc_mod.instance(reg8_mod, "input_reg", clk=clk, rst=rst)
        result_reg = calc_mod.instance(reg8_mod, "result_reg", clk=clk, rst=rst)
        busy_reg = calc_mod.instance(reg1_mod, "busy_reg", clk=clk, rst=rst)

        # Method: start calculation (sets busy_reg)
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
            val = step.call(input_reg, "read")
            result = step.add(val, val)  # input * 2
            step.call(result_reg, "write", result)

        # Static step: clear busy
        with calc_mod.static_step(1, "clear_busy") as step:
            step.call(busy_reg, "write", step.const(0, 1))

        # Proc rule: trigger computation when busy
        with calc_mod.proc_rule("run_calc") as rule:
            with rule.guard() as g:
                is_busy = g.call(busy_reg, "read")
                g.returns(is_busy)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(calc_mod._steps["compute"].ref())
                    seq.enable(calc_mod._steps["clear_busy"].ref())

    # ========================================
    # Parent module that calls submodule from static step
    # ========================================
    with circuit.module("ParentModule") as parent_mod:
        clk = parent_mod.clock()
        rst = parent_mod.reset()

        # State
        data_reg = parent_mod.instance(reg8_mod, "data_reg", clk=clk, rst=rst)
        started_reg = parent_mod.instance(reg1_mod, "started_reg", clk=clk, rst=rst)
        done_reg = parent_mod.instance(reg1_mod, "done_reg", clk=clk, rst=rst)
        result_reg = parent_mod.instance(reg8_mod, "result_reg", clk=clk, rst=rst)

        # Submodule instance
        calc = parent_mod.instance(calc_mod, "calc", clk=clk, rst=rst)

        # Static step: call submodule.start() - THIS IS THE KEY TEST
        # Issue 7 claims this pattern may not work correctly
        with parent_mod.static_step(1, "trigger_submodule") as step:
            """
            This step calls calc.start() which sets calc.busy_reg = 1.
            This should trigger calc.run_calc proc_rule to fire.
            """
            data = step.call(data_reg, "read")
            step.call(calc, "start", data)

        # Static step: collect result
        with parent_mod.static_step(1, "collect_result") as step:
            result = step.call(calc, "result")
            step.call(result_reg, "write", result)
            step.call(done_reg, "write", step.const(1, 1))

        # Method: start processing (external trigger)
        with parent_mod.method("start", args=[("data", UInt(8))]) as meth:
            with meth.guard() as g:
                is_started = g.call(started_reg, "read")
                not_started = g.eq(is_started, g.const(0, 1))
                g.returns(not_started)
            with meth.body() as b:
                data = b.arg("data")
                b.call(data_reg, "write", data)
                b.call(started_reg, "write", b.const(1, 1))
                b.call(done_reg, "write", b.const(0, 1))

        # Value: get result
        with parent_mod.value("result", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(result_reg, "read")
                b.returns(result)

        # Value: is done
        with parent_mod.value("done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                done = b.call(done_reg, "read")
                b.returns(done)

        # Static step: no-op wait step
        with parent_mod.static_step(1, "wait_step") as step:
            """
            No-op step used for waiting. Just passes time.
            """
            pass  # Nothing to do, just wait one cycle

        # Proc rule: processing pipeline
        with parent_mod.proc_rule("process") as rule:
            """
            When started:
            1. Trigger submodule (calls calc.start)
            2. Wait for calc to complete (using while_ with condition function)
            3. Collect result
            """
            with rule.guard() as g:
                is_started = g.call(started_reg, "read")
                g.returns(is_started)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # Step 1: Trigger submodule
                    seq.enable(parent_mod._steps["trigger_submodule"].ref())

                    # Step 2: Wait for submodule to complete
                    # Using while_ with condition function (since submodule completion
                    # depends on its proc_rule firing)
                    def wait_for_calc(b):
                        calc_done = b.call(calc, "done")
                        not_done = b.eq(calc_done, b.const(0, 1))
                        return not_done

                    with seq.while_(wait_for_calc) as loop:
                        loop.enable(parent_mod._steps["wait_step"].ref())

                    # Step 3: Collect result and mark done
                    seq.enable(parent_mod._steps["collect_result"].ref())

        # Rule: clear started after done
        with parent_mod.rule("clear_started") as rule:
            with rule.guard() as g:
                is_started = g.call(started_reg, "read")
                is_done = g.call(done_reg, "read")
                should_clear = g.and_(is_started, is_done)
                g.returns(should_clear)
            with rule.body() as b:
                b.call(started_reg, "write", b.const(0, 1))

    # Generate MLIR
    mlir_str = circuit.emit_mlir()
    print("=== Generated MLIR (first 3000 chars) ===")
    print(mlir_str[:3000])

    # Save MLIR
    with open("/tmp/submodule_proc_step_test.mlir", "w") as f:
        f.write(mlir_str)
    print("\nMLIR saved to /tmp/submodule_proc_step_test.mlir")

    # Run lowering passes
    print("\n=== Running Lowering Passes ===")
    result = subprocess.run(
        [os.path.join(build_dir, "build/bin/circt-opt"), "/tmp/submodule_proc_step_test.mlir",
         "--cmt2-inline-private-funcs",
         "--cmt2-static-inference",
         "--cmt2-compile-static",
         "--cmt2-tdcc",
         "--cmt2-proc-stmt-to-action",
         "-o", "/tmp/submodule_proc_step_test_lowered.mlir"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Lowering error:\n{result.stderr}")
        return 1
    print("Lowering succeeded")

    # Generate simulation workspace
    print("\n=== Generating Simulation Workspace ===")
    ws_dir = os.path.join(os.path.dirname(__file__), "submodule_proc_step_sim")
    ws = SimulationWorkspace(circuit, ws_dir)
    ws.generate_placeholder()

    # Write custom testbench
    tb_path = os.path.join(ws_dir, "tb", "testbench.cpp")
    with open(tb_path, "w") as f:
        f.write(TESTBENCH_CPP)
    print(f"Custom testbench written to {tb_path}")

    # Build and run
    print("\n=== Building Simulation ===")
    result = subprocess.run(
        ["make", "-C", ws_dir],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"Build error:\n{result.stdout}\n{result.stderr}")
        return 1
    print("Build succeeded")

    print("\n=== Running Simulation ===")
    result = subprocess.run(
        ["make", "-C", ws_dir, "run"],
        capture_output=True, text=True
    )
    print(result.stdout)
    if result.returncode != 0:
        print(f"Simulation error:\n{result.stderr}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
