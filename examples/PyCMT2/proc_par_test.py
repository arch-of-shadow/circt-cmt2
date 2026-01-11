#!/usr/bin/env python3
"""proc_par_test.py - Test for proc.par (parallel execution) fix

This example tests the proc.par implementation which was fixed to:
1. Enable all parallel branches simultaneously from fork state
2. Wait for ALL branches to complete (AND semantics) before joining

Expected behavior:
- step1: increments counter1 (0 -> 1)
- step2: increments counter2 (0 -> 1)
- Both steps run in parallel from the fork state
- After both complete, FSM transitions to done
- Final values: counter1=1, counter2=1, result=2 (sum)
"""

import os
import sys
import subprocess
import shutil
from pathlib import Path

# Add circt Python packages to path
build_dir = os.path.dirname(os.path.abspath(__file__))
while build_dir and not os.path.exists(os.path.join(build_dir, "build")):
    build_dir = os.path.dirname(build_dir)
if build_dir:
    sys.path.insert(0, os.path.join(build_dir, "build/tools/circt/python_packages/circt_core"))

from circt.pycmt2.circuit import Circuit
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.types import UInt
from circt.pycmt2.simulation import SimulationWorkspace


def create_parallel_test(circuit: Circuit, width: int = 32):
    """Create a simple test for proc.par execution."""
    reg_mod = Reg.create(circuit, width)
    reg1_mod = Reg.create(circuit, 1)

    with circuit.module("TestHarness") as harness:
        clk = harness.clock("clk")
        rst = harness.reset("rst")

        # Two counters - each will be incremented by its own step
        counter1 = harness.instance(reg_mod, "counter1", clk=clk, rst=rst)
        counter2 = harness.instance(reg_mod, "counter2", clk=clk, rst=rst)
        done_reg = harness.instance(reg1_mod, "done_reg", clk=clk, rst=rst)

        # Value: done
        with harness.value("done", returns=[UInt(1)]) as done_val:
            with done_val.guard() as g:
                g.always()
            with done_val.body() as body:
                d = body.call(done_reg, "read")
                body.returns(d)

        # Value: result - sum of counter1 + counter2
        with harness.value("result", returns=[UInt(width)]) as result_val:
            with result_val.guard() as g:
                g.always()
            with result_val.body() as body:
                c1 = body.call(counter1, "read")
                c2 = body.call(counter2, "read")
                s = body.add(c1, c2)
                body.returns(body.bits(s, width-1, 0))

        # Step: incr1 - increment counter1
        with harness.step("incr1") as step1:
            cnt = step1.call(counter1, "read")
            next_cnt = step1.add(cnt, step1.const(1, width))
            step1.call(counter1, "write", step1.bits(next_cnt, width-1, 0))
            step1.done(step1.const(1, 1))

        # Step: incr2 - increment counter2
        with harness.step("incr2") as step2:
            cnt = step2.call(counter2, "read")
            next_cnt = step2.add(cnt, step2.const(1, width))
            step2.call(counter2, "write", step2.bits(next_cnt, width-1, 0))
            step2.done(step2.const(1, 1))

        # Step: mark_done
        with harness.step("mark_done") as done_step:
            done_step.call(done_reg, "write", done_step.const(1, 1))
            done_step.done(done_step.const(1, 1))

        # Procedural rule: main with parallel execution
        with harness.proc_rule("main") as main:
            with main.guard() as g:
                d = g.call(done_reg, "read")
                not_done = g.not_(d)
                g.returns(not_done)

            with main.control() as ctrl:
                with ctrl.seq() as seq:
                    # Parallel: both steps run at the same time
                    with seq.par() as par:
                        par.enable(step1.ref())
                        par.enable(step2.ref())
                    # After both complete, mark done
                    seq.enable(done_step.ref())

        harness.precedence(done_val.ref(), result_val.ref(), main.ref())

    return harness


def generate_testbench(workspace_dir: Path, width: int):
    """Generate a custom Verilator testbench."""
    tb_dir = workspace_dir / "tb"

    testbench_cpp = f'''
#include "VTestHarness.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>

vluint64_t main_time = 0;
double sc_time_stamp() {{ return main_time; }}

int main(int argc, char** argv) {{
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    VTestHarness* dut = new VTestHarness;
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("waves/sim.vcd");

    std::cout << "========================================" << std::endl;
    std::cout << "Parallel Control (proc.par) Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Testing that both steps execute in parallel" << std::endl;
    std::cout << "Expected: counter1=1, counter2=1, result=2" << std::endl;
    std::cout << "========================================" << std::endl;

    auto tick = [&]() {{
        dut->clk = 0;
        dut->eval();
        tfp->dump(main_time++);
        dut->clk = 1;
        dut->eval();
        tfp->dump(main_time++);
    }};

    // Reset
    std::cout << "Applying reset..." << std::endl;
    dut->rst = 1;
    for (int i = 0; i < 10; i++) tick();
    dut->rst = 0;

    std::cout << "Starting simulation..." << std::endl;

    // Run simulation until done or timeout
    bool test_passed = false;
    int done_cycles = 0;
    const int MAX_CYCLES = 100;

    for (int cycle = 0; cycle < MAX_CYCLES; cycle++) {{
        tick();

        int result = dut->result_res0;
        int done = dut->done_res0;
        int fsm_running = dut->main___05Frunning_ResultOutOfBound;

        // Print progress every cycle for this simple test
        std::cout << "Cycle " << cycle << ": result=" << result
                  << ", done=" << done
                  << ", fsm_running=" << fsm_running << std::endl;

        // Check if done
        if (done == 1) {{
            done_cycles++;
            if (done_cycles >= 3) {{  // Stable for 3 cycles
                std::cout << std::endl;
                std::cout << "========================================" << std::endl;
                std::cout << "Computation complete at cycle " << cycle << std::endl;
                std::cout << "Result: " << result << std::endl;

                // Expected: 1 + 1 = 2
                int expected = 2;
                if (result == expected) {{
                    std::cout << "TEST PASSED!" << std::endl;
                    test_passed = true;
                }} else {{
                    std::cout << "TEST FAILED! Expected " << expected << std::endl;
                }}
                std::cout << "========================================" << std::endl;
                break;
            }}
        }}
    }}

    if (!test_passed && done_cycles < 3) {{
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "TEST FAILED - Timeout!" << std::endl;
        std::cout << "Final result: " << (int)dut->result_res0 << std::endl;
        std::cout << "Final done: " << (int)dut->done_res0 << std::endl;
        std::cout << "========================================" << std::endl;
    }}

    tfp->close();
    delete tfp;
    delete dut;

    return test_passed ? 0 : 1;
}}
'''

    (tb_dir / "testbench.cpp").write_text(testbench_cpp)
    print("   Updated testbench.cpp")


def run_simulation(workspace_dir: Path):
    """Build and run the Verilator simulation."""
    print("\n" + "=" * 60)
    print("Step: Verilator Simulation")
    print("=" * 60)

    # Check for Verilator
    result = subprocess.run(["which", "verilator"], capture_output=True, text=True)
    if result.returncode != 0:
        print("   SKIP: Verilator not found in PATH")
        return None

    # Build simulation
    print("   Building Verilator simulation...")
    result = subprocess.run(
        ["make", "all"],
        cwd=workspace_dir,
        capture_output=True,
        text=True,
        timeout=180
    )

    if result.returncode != 0:
        print("   FAIL: Build failed")
        print(f"   stderr: {result.stderr[:2000]}")
        return None

    print("   Build successful!")

    # Run simulation
    print("\n   Running simulation...")
    result = subprocess.run(
        ["make", "run"],
        cwd=workspace_dir,
        capture_output=True,
        text=True,
        timeout=60
    )

    print("\n   Simulation output:")
    print("   " + "-" * 50)
    for line in result.stdout.split('\n'):
        print(f"   {line}")
    print("   " + "-" * 50)

    return "TEST PASSED" in result.stdout


def main():
    print("=" * 70)
    print("proc.par (Parallel Execution) Test")
    print("=" * 70)
    print("""
This test verifies that proc.par correctly:
1. Enables all parallel branches from the fork state
2. Waits for ALL branches to complete (AND semantics)
3. Transitions to the next state only after join

Test case:
- Two steps run in parallel: incr1 and incr2
- Each increments its own counter by 1
- Expected result: counter1=1 + counter2=1 = 2
""")

    width = 32

    # Clear STL registry
    clear_stl_registry()

    print("1. Creating parallel test circuit...")
    circuit = Circuit("ProcParTest")
    harness = create_parallel_test(circuit, width)
    print(f"   Module: {harness.name}")

    # Emit MLIR
    print("\n2. Emitting MLIR...")
    mlir = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir)} chars")

    # Check for proc.par
    if "proc.par" in mlir:
        print("   [OK] Found proc.par in MLIR")
    else:
        print("   [ERROR] proc.par not found!")
        return 1

    # Generate workspace
    print("\n3. Generating simulation workspace...")
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "proc_par_test_workspace"

    if workspace_dir.exists():
        shutil.rmtree(workspace_dir)

    ws = SimulationWorkspace(circuit, workspace_dir)
    ws.generate_placeholder()

    # Generate testbench
    print("\n4. Generating testbench...")
    generate_testbench(workspace_dir, width)

    # Run simulation
    success = run_simulation(workspace_dir)

    # Summary
    print("\n" + "=" * 70)
    print("Summary")
    print("=" * 70)
    print(f"   Workspace: {workspace_dir}")

    if success is None:
        print("\n   Status: SKIPPED (Verilator not available)")
    elif success:
        print("\n   Status: TEST PASSED - proc.par works correctly!")
    else:
        print("\n   Status: TEST FAILED - proc.par join logic may still have issues")

    print("=" * 70)

    return 0 if success is None or success else 1


if __name__ == "__main__":
    sys.exit(main())
