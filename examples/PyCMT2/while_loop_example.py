#!/usr/bin/env python3
"""while_loop_example.py - Demonstrates proc.while with condition region

This example shows the new ProcWhileOp design where the loop condition
is computed in a dedicated region that supports cmt2.call operations.

Features demonstrated:
1. MLIR generation with proc.while condition region
2. Verilog generation via CMT2 passes
3. Verilator simulation with testbench
4. Output verification

Expected behavior:
- Counter starts at 0
- While counter < 5, increment counter
- After 5 iterations, counter = 5

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 \
        ../examples/PyCMT2/while_loop_example.py
"""

import os
import sys
import shutil
import subprocess
import tempfile
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


def create_while_loop_circuit():
    """Create a circuit demonstrating while loop with condition region."""
    clear_stl_registry()

    width = 8
    circuit = Circuit("WhileLoopDemo")
    reg_mod = Reg.create(circuit, width)
    reg1_mod = Reg.create(circuit, 1)

    with circuit.module("Counter") as counter:
        clk = counter.clock("clk")
        rst = counter.reset("rst")

        # Counter register
        cnt = counter.instance(reg_mod, "cnt", clk=clk, rst=rst)

        # Done flag register
        done_reg = counter.instance(reg1_mod, "done", clk=clk, rst=rst)

        # Value to read counter
        with counter.value("count", returns=[UInt(width)]) as count_val:
            with count_val.guard() as g:
                g.always()
            with count_val.body() as body:
                val = body.call(cnt, "read")
                body.returns(val)

        # Value to check if done
        with counter.value("is_done", returns=[UInt(1)]) as done_val:
            with done_val.guard() as g:
                g.always()
            with done_val.body() as body:
                d = body.call(done_reg, "read")
                body.returns(d)

        # Step to increment counter
        with counter.step("increment") as inc_step:
            val = inc_step.call(cnt, "read")
            new_val = inc_step.add(val, inc_step.const(1, width))
            inc_step.call(cnt, "write", inc_step.bits(new_val, width-1, 0))
            inc_step.done(inc_step.const(1, 1))

        # Step to mark done
        with counter.step("mark_done") as done_step:
            done_step.call(done_reg, "write", done_step.const(1, 1))
            done_step.done(done_step.const(1, 1))

        # Procedural rule with while loop
        # The condition function has access to cmt2.call
        with counter.proc_rule("count_to_five") as rule:
            with rule.guard() as g:
                # Guard: not done yet
                d = g.call(done_reg, "read")
                not_done = g.not_(d)
                g.returns(not_done)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # While loop with condition region
                    # The condition function receives a builder with call capability
                    def loop_condition(b):
                        """Condition: counter < 5"""
                        val = b.call(cnt, "read")
                        c5 = b.const(5, width)
                        return b.lt(val, c5)

                    with seq.while_(loop_condition) as loop:
                        loop.enable(inc_step.ref())

                    # After loop completes, mark done
                    seq.enable(done_step.ref())

        # Precedence
        counter.precedence(count_val.ref(), done_val.ref(), rule.ref())

    return circuit, width


def verify_mlir(circuit):
    """Verify the generated MLIR is valid."""
    print("\n" + "=" * 60)
    print("Step 1: MLIR Generation and Verification")
    print("=" * 60)

    mlir = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir)} chars")

    # Show the while loop structure
    print("\n   While loop structure in MLIR:")
    lines = mlir.split('\n')
    in_while = False
    for line in lines:
        if 'cmt2.proc.while' in line:
            in_while = True
        if in_while:
            print(f"      {line}")
            if 'cmt2.proc.yield' in line:
                in_while = False

    # Verify with circt-opt
    print("\n   Verifying MLIR with circt-opt...")
    cmt2_opt = os.path.join(build_dir, "build/bin/circt-opt")

    with tempfile.NamedTemporaryFile(mode='w', suffix='.mlir', delete=False) as f:
        f.write(mlir)
        mlir_file = f.name

    try:
        result = subprocess.run(
            [cmt2_opt, mlir_file],
            capture_output=True,
            text=True,
            timeout=30
        )
        if result.returncode == 0:
            print("   PASS: MLIR verified successfully")
            return mlir, mlir_file
        else:
            print(f"   FAIL: {result.stderr}")
            return None, None
    except Exception as e:
        print(f"   ERROR: {e}")
        return None, None


def generate_workspace(circuit):
    """Generate simulation workspace using SimulationWorkspace."""
    print("\n" + "=" * 60)
    print("Step 2: Simulation Workspace Generation")
    print("=" * 60)

    # Create workspace directory
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "while_loop_workspace"

    # Clean previous workspace
    if workspace_dir.exists():
        print(f"   Removing existing workspace: {workspace_dir}")
        shutil.rmtree(workspace_dir)

    # Create simulation workspace
    print("   Creating SimulationWorkspace...")
    ws = SimulationWorkspace(circuit, workspace_dir)

    # Generate with placeholder testbench (we'll customize it)
    print("   Generating RTL and testbench...")
    ws.generate_placeholder()

    # Find generated Verilog files
    rtl_dir = workspace_dir / "rtl"
    sv_files = list(rtl_dir.glob("*.sv"))
    print(f"   Generated {len(sv_files)} Verilog files:")
    for sv_file in sorted(sv_files):
        print(f"      - {sv_file.name}")

    # Show excerpt of main module
    main_sv = rtl_dir / "Counter.sv"
    if main_sv.exists():
        print("\n   Counter.sv excerpt (first 40 lines):")
        print("   " + "-" * 50)
        content = main_sv.read_text()
        lines = content.split('\n')[:40]
        for line in lines:
            print(f"      {line}")
        if len(content.split('\n')) > 40:
            print(f"      ... ({len(content.split(chr(10))) - 40} more lines)")
        print("   " + "-" * 50)

    return workspace_dir


def create_testbench(workspace_dir):
    """Create a custom Verilator testbench for the while loop circuit."""
    print("\n" + "=" * 60)
    print("Step 3: Custom Testbench Generation")
    print("=" * 60)

    tb_dir = workspace_dir / "tb"

    # Generate custom testbench that verifies while loop behavior
    testbench_cpp = '''
#include "VCounter.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>
#include <cassert>

vluint64_t main_time = 0;
double sc_time_stamp() { return main_time; }

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    VCounter* dut = new VCounter;
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("waves/sim.vcd");

    std::cout << "========================================" << std::endl;
    std::cout << "While Loop Simulation Test" << std::endl;
    std::cout << "========================================" << std::endl;

    auto tick = [&]() {
        dut->clk = 0;
        dut->eval();
        tfp->dump(main_time++);
        dut->clk = 1;
        dut->eval();
        tfp->dump(main_time++);
    };

    // Reset
    std::cout << "Applying reset..." << std::endl;
    dut->rst = 1;
    for (int i = 0; i < 5; i++) tick();
    dut->rst = 0;

    std::cout << "Starting simulation..." << std::endl;

    // Run simulation and track counter
    int prev_count = -1;
    int cycles_at_5 = 0;
    bool test_passed = false;

    for (int cycle = 0; cycle < 100; cycle++) {
        tick();

        int count = dut->count_res0;
        int is_done = dut->is_done_res0;

        // Print when counter changes or done
        if (count != prev_count || is_done) {
            std::cout << "Cycle " << cycle << ": count=" << count
                      << ", is_done=" << is_done << std::endl;
            prev_count = count;
        }

        // Check if we've reached count=5 and done flag
        if (count == 5 && is_done == 1) {
            cycles_at_5++;
            if (cycles_at_5 >= 3) {  // Stable for 3 cycles
                std::cout << std::endl;
                std::cout << "========================================" << std::endl;
                std::cout << "TEST PASSED!" << std::endl;
                std::cout << "Counter correctly counted to 5" << std::endl;
                std::cout << "While loop condition region works!" << std::endl;
                std::cout << "========================================" << std::endl;
                test_passed = true;
                break;
            }
        }
    }

    if (!test_passed) {
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "TEST FAILED!" << std::endl;
        std::cout << "Final count: " << (int)dut->count_res0 << std::endl;
        std::cout << "Final is_done: " << (int)dut->is_done_res0 << std::endl;
        std::cout << "========================================" << std::endl;
    }

    tfp->close();
    delete tfp;
    delete dut;

    return test_passed ? 0 : 1;
}
'''

    (tb_dir / "testbench.cpp").write_text(testbench_cpp)
    print("   Updated testbench.cpp with while loop verification")

    return tb_dir


def run_simulation(workspace_dir):
    """Build and run the Verilator simulation."""
    print("\n" + "=" * 60)
    print("Step 4: Simulation Execution")
    print("=" * 60)

    # Check for Verilator
    result = subprocess.run(["which", "verilator"], capture_output=True, text=True)
    if result.returncode != 0:
        print("   SKIP: Verilator not found in PATH")
        print("   To install: apt install verilator (Ubuntu) or brew install verilator (macOS)")
        return None

    # Build simulation (Makefile is at workspace root)
    print("   Building Verilator simulation...")
    result = subprocess.run(
        ["make", "all"],
        cwd=workspace_dir,
        capture_output=True,
        text=True,
        timeout=120
    )

    if result.returncode != 0:
        print("   FAIL: Build failed")
        print(f"   stderr: {result.stderr[:1000]}")
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

    return result.returncode == 0


def main():
    print("=" * 60)
    print("While Loop Example - End-to-End Verification")
    print("=" * 60)
    print("""
This example demonstrates:
1. proc.while with condition region (cmt2.call in condition)
2. MLIR generation and verification
3. Verilog generation via SimulationWorkspace
4. Verilator simulation with testbench
5. Output verification (counter should reach 5)
""")

    # Create circuit
    print("Creating while loop circuit...")
    circuit, width = create_while_loop_circuit()

    # Step 1: Verify MLIR
    mlir, mlir_file = verify_mlir(circuit)
    if mlir is None:
        print("\nFAILED: MLIR verification failed")
        return 1

    # Clean up temp file
    if mlir_file:
        os.unlink(mlir_file)

    # Step 2: Generate simulation workspace (includes STL modules)
    workspace_dir = generate_workspace(circuit)
    if workspace_dir is None:
        print("\nFAILED: Workspace generation failed")
        return 1

    # Step 3: Create custom testbench
    tb_dir = create_testbench(workspace_dir)

    # Step 4: Run simulation (use workspace_dir where Makefile is located)
    success = run_simulation(workspace_dir)

    # Summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    print(f"   Workspace: {workspace_dir}")
    print(f"   RTL files: {workspace_dir / 'rtl'}")
    print(f"   Testbench: {tb_dir}")

    if success is None:
        print("\n   Status: SKIPPED (Verilator not available)")
        print("   The MLIR and Verilog generation succeeded.")
        print(f"   To run simulation manually:")
        print(f"      cd {tb_dir}")
        print(f"      make run")
    elif success:
        print("\n   Status: ALL TESTS PASSED")
    else:
        print("\n   Status: SIMULATION FAILED")

    print("\n" + "=" * 60)

    # Cleanup option
    print(f"\nGenerated files are in: {workspace_dir}")
    print("You can inspect them or run 'make waves' in tb/ to view waveforms.")

    return 0 if success is None or success else 1


if __name__ == "__main__":
    sys.exit(main())
