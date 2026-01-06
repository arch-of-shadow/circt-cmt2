#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Static Procedural Control End-to-End Example

This example demonstrates all static proc features with full Verilator simulation:
1. Static steps with fixed cycle counts
2. Sequential composition (proc.seq) of static steps
3. Parallel composition (proc.par) of static steps
4. Hybrid static-dynamic control
5. Pipelined operations with known latencies

Design: A pipelined accumulator that performs:
  - Load two values (dynamic)
  - Multiply them (static 3-cycle)
  - Add to accumulator (static 2-cycle)
  - Store result (static 1-cycle)

The timing passes generate FSM control logic for predictable execution.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/static_proc.py
"""

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry


def create_static_proc_circuit():
    """Create a circuit demonstrating static procedural control features."""
    clear_stl_registry()

    circuit = Circuit("StaticProcDemo")

    # Create register modules
    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)

    with circuit.module("PipelinedAccumulator") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers
        reg_a = m.instance(reg32, "reg_a", clk=clk, rst=rst)
        reg_b = m.instance(reg32, "reg_b", clk=clk, rst=rst)
        reg_product = m.instance(reg32, "reg_product", clk=clk, rst=rst)
        reg_accum = m.instance(reg32, "reg_accum", clk=clk, rst=rst)
        reg_busy = m.instance(reg1, "reg_busy", clk=clk, rst=rst)

        # =====================================================================
        # Static Steps - Fixed cycle count operations
        # =====================================================================

        # Static step: multiply (3 cycles - simulates pipelined multiplier)
        # Cycle 0: Read operands
        # Cycle 1: Compute product (pipelined)
        # Cycle 2: Write result
        with m.static_step(3, "multiply") as step_mul:
            a_val = step_mul.call(reg_a, "read")
            b_val = step_mul.call(reg_b, "read")
            # Simulate multiply with repeated addition for simplicity
            # In real hardware, this would be a pipelined multiplier
            product = step_mul.mul(a_val, b_val)
            step_mul.call(reg_product, "write", product)

        # Static step: accumulate (2 cycles)
        # Cycle 0: Read product and accumulator
        # Cycle 1: Write sum
        with m.static_step(2, "accumulate") as step_acc:
            prod = step_acc.call(reg_product, "read")
            acc = step_acc.call(reg_accum, "read")
            new_acc = step_acc.add(prod, acc)
            step_acc.call(reg_accum, "write", new_acc)

        # Static step: clear busy flag (1 cycle)
        with m.static_step(1, "finish") as step_finish:
            step_finish.call(reg_busy, "write", step_finish.const(0, 1))

        # Static step: set busy flag (1 cycle)
        with m.static_step(1, "set_busy") as step_set_busy:
            step_set_busy.call(reg_busy, "write", step_set_busy.const(1, 1))

        # Static step: nop delay (1 cycle) - useful for timing alignment
        with m.static_step(1, "delay") as step_delay:
            pass  # Just a cycle delay

        # =====================================================================
        # Dynamic Step - Variable latency operation
        # =====================================================================

        # Dynamic step: load values (completes when inputs are valid)
        with m.step("load_values") as step_load:
            # This step has explicit done signal
            step_load.done(step_load.const(1, 1))

        # =====================================================================
        # Proc Rules - Multi-cycle control sequences
        # =====================================================================

        # Proc Rule 1: Sequential pipeline
        # Executes: set_busy -> multiply -> accumulate -> finish
        # Total: 1 + 3 + 2 + 1 = 7 cycles (statically known)
        with m.proc_rule("compute_seq") as rule:
            with rule.guard() as g:
                # Can start when not busy
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with rule.control() as ctrl:
                with ctrl.seq():
                    ctrl.enable(step_set_busy.ref())
                    ctrl.enable(step_mul.ref())
                    ctrl.enable(step_acc.ref())
                    ctrl.enable(step_finish.ref())

        # Proc Rule 2: Parallel operations (where possible)
        # Demonstrates parallel composition of non-conflicting steps
        with m.proc_rule("parallel_demo") as rule2:
            with rule2.guard() as g:
                g.returns(g.const(0, 1))  # Disabled - just for demonstration
            with rule2.control() as ctrl:
                with ctrl.seq():
                    # First: multiply (3 cycles)
                    ctrl.enable(step_mul.ref())
                    # Then: parallel delay and accumulate
                    # (only works if they don't conflict)
                    with ctrl.par():
                        ctrl.enable(step_delay.ref())
                        ctrl.enable(step_acc.ref())

        # =====================================================================
        # Interface Methods
        # =====================================================================

        # Method: start - Load operands and begin computation
        with m.method("start", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
            with meth.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with meth.body() as body:
                a_in = body.arg("a")
                b_in = body.arg("b")
                body.call(reg_a, "write", a_in)
                body.call(reg_b, "write", b_in)

        # Value: get_result - Read accumulator
        with m.value("get_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with val.body() as body:
                result = body.call(reg_accum, "read")
                body.returns(result)

        # Value: is_busy - Check if computation in progress
        with m.value("is_busy", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                busy = body.call(reg_busy, "read")
                body.returns(busy)

        # Method: reset_accum - Clear accumulator
        with m.method("reset_accum") as meth:
            with meth.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with meth.body() as body:
                body.call(reg_accum, "write", body.const(0, 32))

    return circuit


# Verilator C++ testbench
STATIC_PROC_TESTBENCH_CPP = """\
// Static Procedural Control Testbench
// Tests pipelined accumulator with static scheduling

#include "VPipelinedAccumulator.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto dut = new VPipelinedAccumulator();
    auto tfp = new VerilatedVcdC();
    dut->trace(tfp, 99);
    tfp->open("waves/PipelinedAccumulator.vcd");

    // Initialize
    dut->clk = 0;
    dut->rst = 1;
    dut->start_enable = 0;
    dut->start_a = 0;
    dut->start_b = 0;
    dut->reset_accum_enable = 0;

    int cycle = 0;
    bool all_passed = true;

    auto tick = [&]() {
        dut->clk = 0;
        dut->eval();
        tfp->dump(cycle * 10);
        dut->clk = 1;
        dut->eval();
        tfp->dump(cycle * 10 + 5);
        cycle++;
    };

    // Reset sequence
    std::cout << "=== Static Proc Demo: Pipelined Accumulator ===" << std::endl;
    std::cout << "Resetting..." << std::endl;
    for (int i = 0; i < 5; i++) {
        tick();
    }
    dut->rst = 0;
    tick();

    // Clear accumulator
    std::cout << "Clearing accumulator..." << std::endl;
    dut->reset_accum_enable = 1;
    tick();
    dut->reset_accum_enable = 0;
    tick();

    // Test cases: (a, b) pairs to multiply and accumulate
    // Expected: sum of all products
    std::vector<std::pair<uint32_t, uint32_t>> test_cases = {
        {3, 4},    // 12
        {5, 2},    // 10, total = 22
        {7, 3},    // 21, total = 43
        {2, 8},    // 16, total = 59
    };

    uint32_t expected_accum = 0;
    int test_num = 1;

    for (const auto& [a, b] : test_cases) {
        uint32_t product = a * b;
        expected_accum += product;

        std::cout << "\\nTest " << test_num++ << ": " << a << " * " << b
                  << " = " << product << " (expected accum = " << expected_accum << ")" << std::endl;

        // Wait for ready
        int wait = 0;
        while (!dut->start_ready && wait < 20) {
            tick();
            wait++;
        }

        if (!dut->start_ready) {
            std::cerr << "  ERROR: Timeout waiting for start_ready" << std::endl;
            all_passed = false;
            continue;
        }

        // Start computation
        dut->start_a = a;
        dut->start_b = b;
        dut->start_enable = 1;
        tick();
        dut->start_enable = 0;

        // Wait for computation to complete (7 cycles for static pipeline)
        std::cout << "  Waiting for pipeline (expecting ~7 cycles)..." << std::endl;
        int compute_cycles = 0;
        while (dut->is_busy_res0 && compute_cycles < 20) {
            tick();
            compute_cycles++;
        }

        if (compute_cycles >= 20) {
            std::cerr << "  ERROR: Computation timeout" << std::endl;
            all_passed = false;
            continue;
        }

        std::cout << "  Completed in " << compute_cycles << " cycles" << std::endl;

        // Check result
        tick();  // Allow result to settle
        if (dut->get_result_ready) {
            uint32_t result = dut->get_result_res0;
            if (result == expected_accum) {
                std::cout << "  PASS: accumulator = " << result << std::endl;
            } else {
                std::cerr << "  FAIL: accumulator = " << result
                          << ", expected " << expected_accum << std::endl;
                all_passed = false;
            }
        } else {
            std::cerr << "  ERROR: get_result not ready" << std::endl;
            all_passed = false;
        }
    }

    // Final summary
    std::cout << "\\n=== Summary ===" << std::endl;
    std::cout << "Final accumulator value: " << dut->get_result_res0 << std::endl;
    std::cout << "Expected: " << expected_accum << std::endl;

    tfp->close();
    delete tfp;
    delete dut;

    if (all_passed) {
        std::cout << "\\nALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cerr << "\\nSOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
"""


def main():
    print("=" * 70)
    print("Static Procedural Control End-to-End Example")
    print("=" * 70)

    # Setup paths
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "static_proc_workspace"

    # Clean previous simulation
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    # Create circuit
    print("\n1. Creating PipelinedAccumulator circuit...")
    print("   - Static steps: multiply(3), accumulate(2), finish(1), delay(1)")
    print("   - Proc rule: sequential pipeline (7 cycles total)")
    circuit = create_static_proc_circuit()

    # Show MLIR
    print("\n2. Generated MLIR (showing proc constructs):")
    print("-" * 70)
    mlir_str = circuit.emit_mlir()
    # Show relevant parts
    for line in mlir_str.split('\n'):
        if any(kw in line for kw in ['static_step', 'proc.rule', 'proc.seq',
                                      'proc.par', 'proc.enable', 'step']):
            print(line)
    print("-" * 70)

    # Create simulation workspace
    print("\n3. Setting up simulation workspace...")
    ws = SimulationWorkspace(circuit, sim_dir)

    # Generate workspace
    ws._add_stl_rtl()
    ws._create_directories()
    ws._generate_rtl()
    ws._generate_makefile()

    # Write custom testbench
    tb_file = sim_dir / "tb" / "testbench.cpp"
    tb_file.write_text(STATIC_PROC_TESTBENCH_CPP)

    print(f"   Workspace generated at: {sim_dir}")

    # Build simulation
    print("\n4. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    # Run simulation
    print("\n5. Running simulation...")
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    print("\n" + "=" * 70)
    print("Static Proc Example Completed!")
    print(f"Waveforms: {sim_dir / 'waves' / 'PipelinedAccumulator.vcd'}")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
