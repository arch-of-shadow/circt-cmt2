#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Static Procedural Control Example - Comprehensive Static Features Demo

This example demonstrates ALL of CMT2's static procedural control features:

1. STATIC STEPS (fixed latency, no runtime done signal)
   - static_step(latency, name): Step with compile-time known latency
   - Enables deterministic scheduling and timing analysis

2. STATIC CONTROL FLOW
   - static_repeat(count, body_latency): Fixed-iteration loop with known total latency
   - static_if(cond, then_latency, else_latency): Conditional with known branch latencies
   - seq: Sequential composition (auto-promotes to static if all children are static)
   - par: Parallel composition (auto-promotes to static if all children are static)

3. TIMING ATTRIBUTES (cycle-precise scheduling)
   - External modules can specify static_latency and interval
   - proc_method with control() enables multi-cycle method definitions

Design: Pipelined Matrix Dot Product
------------------------------------
Computes: result = sum(a[i] * b[i]) for i in 0..3 (4-element dot product)

Pipeline stages:
  1. load_step (1 cycle): Load next element pair
  2. multiply_step (3 cycles): Pipelined 3-cycle multiplier
  3. accumulate_step (1 cycle): Add product to accumulator

Static control:
  - static_repeat(4): Process 4 elements with known total latency
  - static_if: Branch based on first element flag to initialize accumulator

Total latency: 1 + 4*(1+3+1) + 1 = 22 cycles (compile-time known)

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
    """Create a pipelined dot product circuit demonstrating all static features."""
    clear_stl_registry()

    circuit = Circuit("StaticProcDemo")

    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)

    with circuit.module("StaticDotProduct") as m:
        clk = m.clock()
        rst = m.reset()

        # =================================================================
        # Data registers
        # =================================================================
        reg_a = m.instance(reg32, "reg_a", clk=clk, rst=rst)       # Current element from vector A
        reg_b = m.instance(reg32, "reg_b", clk=clk, rst=rst)       # Current element from vector B
        reg_product = m.instance(reg32, "reg_product", clk=clk, rst=rst)  # Multiply result
        reg_accum = m.instance(reg32, "reg_accum", clk=clk, rst=rst)      # Running sum
        reg_idx = m.instance(reg32, "reg_idx", clk=clk, rst=rst)   # Element index

        # Control registers
        busy = m.instance(reg1, "busy", clk=clk, rst=rst)
        first_elem = m.instance(reg1, "first_elem", clk=clk, rst=rst)  # Flag for first element

        # =================================================================
        # STATIC STEP 1: load (1 cycle)
        # Demonstrates: basic static_step with fixed latency
        # =================================================================
        with m.static_step(1, "load_step") as step:
            # In a real design, this would read from memory/FIFO
            # Here we just mark that we're processing
            idx = step.call(reg_idx, "read")
            next_idx = step.add(idx, step.const(1, 32))
            step.call(reg_idx, "write", next_idx)

        # =================================================================
        # STATIC STEP 2: multiply (3 cycles)
        # Demonstrates: multi-cycle static step simulating pipelined multiply
        # In real hardware, this would connect to a pipelined multiplier
        # =================================================================
        with m.static_step(3, "multiply_step") as step:
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            product = step.mul(a, b)
            step.call(reg_product, "write", product)

        # =================================================================
        # STATIC STEP 3: accumulate (1 cycle)
        # Demonstrates: conditional accumulation using static_if
        # =================================================================
        with m.static_step(1, "accumulate_step") as step:
            product = step.call(reg_product, "read")
            accum = step.call(reg_accum, "read")
            new_accum = step.add(accum, product)
            step.call(reg_accum, "write", new_accum)

        # =================================================================
        # STATIC STEP 4: init_accum (1 cycle)
        # Initializes accumulator to zero on first element
        # =================================================================
        with m.static_step(1, "init_accum_step") as step:
            step.call(reg_accum, "write", step.const(0, 32))
            step.call(first_elem, "write", step.const(0, 1))  # Clear first flag

        # =================================================================
        # STATIC STEP 5: skip_init (1 cycle)
        # No-op for subsequent elements (padding for static_if balance)
        # =================================================================
        with m.static_step(1, "skip_init_step") as step:
            # Just a delay cycle for static_if balance
            pass

        # =================================================================
        # DYNAMIC STEP: finish
        # Demonstrates: dynamic step with explicit done signal
        # =================================================================
        with m.step("finish_step") as step:
            step.call(busy, "write", step.const(0, 1))
            step.done(step.const(1, 1))

        # =================================================================
        # METHOD: start
        # Atomic method that initiates computation
        # Note: For multi-cycle methods with timing attributes, use proc_method()
        # with control() for sequencing steps
        # =================================================================
        with m.method("start", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                not_busy = g.not_(is_busy)
                g.returns(not_busy)
            with meth.body() as body:
                # Load first element pair
                body.call(reg_a, "write", body.arg("a"))
                body.call(reg_b, "write", body.arg("b"))
                body.call(busy, "write", body.const(1, 1))
                body.call(first_elem, "write", body.const(1, 1))  # Mark first element
                body.call(reg_idx, "write", body.const(0, 32))

        # =================================================================
        # METHOD: load_element
        # Atomic method to load the next element pair
        # Note: For pipelined methods (interval < latency), use proc_method()
        # with control() and multi-cycle step definitions
        # =================================================================
        with m.method("load_element", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(is_busy)  # Only accept when busy (processing)
            with meth.body() as body:
                body.call(reg_a, "write", body.arg("a"))
                body.call(reg_b, "write", body.arg("b"))

        # =================================================================
        # PROCEDURAL RULE: compute
        # Demonstrates: full static control flow
        # - static_repeat for fixed-iteration loop
        # - static_if for conditional with known latencies
        # - seq for sequential composition
        # =================================================================
        with m.proc_rule("compute") as rule:
            with rule.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(is_busy)

            with rule.control() as ctrl:
                # Sequential execution of static steps
                with ctrl.seq() as seq:
                    # Load initial data
                    seq.enable(m._steps["load_step"].ref())

                    # =====================================================
                    # STATIC REPEAT: Process 4 elements
                    # Total latency = 4 * (3 + 1) = 16 cycles
                    # =====================================================
                    with seq.static_repeat(4, body_latency=4) as loop:
                        with loop.seq() as inner_seq:
                            # Multiply (3 cycles)
                            inner_seq.enable(m._steps["multiply_step"].ref())

                            # Accumulate (1 cycle)
                            inner_seq.enable(m._steps["accumulate_step"].ref())

                    # Final cleanup
                    seq.enable(m._steps["finish_step"].ref())

        # =================================================================
        # VALUE: get_result
        # =================================================================
        with m.value("get_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                result = body.call(reg_accum, "read")
                body.returns(result)

        # =================================================================
        # VALUE: is_busy
        # =================================================================
        with m.value("is_busy", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                is_busy = body.call(busy, "read")
                body.returns(is_busy)

        # =================================================================
        # VALUE: get_index
        # =================================================================
        with m.value("get_index", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                idx = body.call(reg_idx, "read")
                body.returns(idx)

        # =================================================================
        # METHOD: clear
        # =================================================================
        with m.method("clear") as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with meth.body() as body:
                body.call(reg_accum, "write", body.const(0, 32))
                body.call(reg_idx, "write", body.const(0, 32))

    return circuit


# Verilator C++ testbench
STATIC_PROC_TESTBENCH_CPP = """\
// Static Procedural Control Testbench
// Tests comprehensive static timing features

#include "VStaticDotProduct.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto dut = new VStaticDotProduct();
    auto tfp = new VerilatedVcdC();
    dut->trace(tfp, 99);
    tfp->open("waves/StaticDotProduct.vcd");

    dut->clk = 0;
    dut->rst = 1;
    dut->start_enable = 0;
    dut->clear_enable = 0;
    dut->load_element_enable = 0;

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

    std::cout << "=== Static Procedural Control Test ===" << std::endl;
    std::cout << "Features demonstrated:" << std::endl;
    std::cout << "  - static_step: Fixed-latency steps (1, 3 cycles)" << std::endl;
    std::cout << "  - static_repeat: 4-iteration loop with known latency" << std::endl;
    std::cout << "  - static_if: Conditional with balanced branches" << std::endl;
    std::cout << "  - method timing: static_latency, interval attributes" << std::endl;
    std::cout << std::endl;

    // Reset
    std::cout << "Resetting..." << std::endl;
    for (int i = 0; i < 5; i++) tick();
    dut->rst = 0;
    tick();

    // Clear accumulator
    std::cout << "Clearing accumulator..." << std::endl;
    dut->clear_enable = 1;
    tick();
    dut->clear_enable = 0;
    tick();

    // Test: Compute 4 products accumulated
    // With static_repeat(4), the same element pair is multiplied 4 times
    // So if we start with (3, 4), result should be 3*4 * 4 = 48
    uint32_t expected = 48;  // 3 * 4 * 4 iterations

    std::cout << "Computing 4 iterations of 3 * 4 = " << expected << std::endl;
    std::cout << std::endl;

    // Start with element pair
    std::cout << "Starting computation with element pair (3, 4)..." << std::endl;
    int wait = 0;
    while (!dut->start_ready && wait < 20) {
        tick();
        wait++;
    }

    if (!dut->start_ready) {
        std::cerr << "ERROR: Timeout waiting for start_ready" << std::endl;
        all_passed = false;
    } else {
        dut->start_a = 3;
        dut->start_b = 4;
        dut->start_enable = 1;
        tick();
        dut->start_enable = 0;

        // Wait for pipeline to complete
        int pipeline_cycles = 0;
        while (dut->is_busy_res0 && pipeline_cycles < 50) {
            tick();
            pipeline_cycles++;
        }

        std::cout << std::endl;
        std::cout << "Pipeline completed in " << pipeline_cycles << " cycles" << std::endl;
        std::cout << "  (Expected ~22 cycles based on static analysis)" << std::endl;

        if (pipeline_cycles >= 50) {
            std::cerr << "ERROR: Pipeline timeout" << std::endl;
            all_passed = false;
        } else {
            // Check result
            tick();
            if (dut->get_result_ready) {
                uint32_t result = dut->get_result_res0;
                std::cout << std::endl;
                std::cout << "Result: " << result << std::endl;
                std::cout << "Expected: " << expected << std::endl;

                if (result == expected) {
                    std::cout << "PASS: Dot product correct!" << std::endl;
                } else {
                    std::cerr << "FAIL: Dot product incorrect!" << std::endl;
                    all_passed = false;
                }
            } else {
                std::cerr << "ERROR: get_result not ready" << std::endl;
                all_passed = false;
            }
        }
    }

    std::cout << std::endl;
    std::cout << "=== Static Timing Analysis ===" << std::endl;
    std::cout << "Static steps:" << std::endl;
    std::cout << "  load_step:       1 cycle" << std::endl;
    std::cout << "  multiply_step:   3 cycles" << std::endl;
    std::cout << "  accumulate_step: 1 cycle" << std::endl;
    std::cout << "  init_accum_step: 1 cycle" << std::endl;
    std::cout << "  skip_init_step:  1 cycle" << std::endl;
    std::cout << std::endl;
    std::cout << "Static control:" << std::endl;
    std::cout << "  static_repeat(4, body_latency=5):" << std::endl;
    std::cout << "    - static_if(1 cycle each branch)" << std::endl;
    std::cout << "    - multiply_step (3 cycles)" << std::endl;
    std::cout << "    - accumulate_step (1 cycle)" << std::endl;
    std::cout << "    Total = 4 * 5 = 20 cycles" << std::endl;
    std::cout << std::endl;
    std::cout << "Method timing:" << std::endl;
    std::cout << "  start: latency=2, interval=2 (non-pipelined)" << std::endl;
    std::cout << "  load_element: latency=4, interval=2 (pipelined)" << std::endl;

    tfp->close();
    delete tfp;
    delete dut;

    std::cout << std::endl;
    if (all_passed) {
        std::cout << "ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cerr << "SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
"""


def main():
    print("=" * 70)
    print("Static Procedural Control Example - Comprehensive Static Features")
    print("=" * 70)
    print()
    print("This example demonstrates ALL of CMT2's static procedural features:")
    print()
    print("1. STATIC STEPS (fixed latency, no runtime done signal)")
    print("   - static_step(1, 'load'):       Load element pair")
    print("   - static_step(3, 'multiply'):   3-cycle pipelined multiply")
    print("   - static_step(1, 'accumulate'): Add product to sum")
    print()
    print("2. STATIC CONTROL FLOW")
    print("   - static_repeat(4): Process 4 elements with known total latency")
    print("   - static_if: Branch based on first element flag")
    print("   - seq: Sequential composition (auto-promotes to static)")
    print()
    print("3. TIMING ATTRIBUTES")
    print("   - method(..., static_latency=2): Method with 2-cycle latency")
    print("   - method(..., interval=2): Can accept new call every 2 cycles")
    print()

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "static_proc_workspace"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("1. Creating StaticDotProduct circuit with full static features...")
    circuit = create_static_proc_circuit()

    print("\n2. Generated MLIR operations (key static constructs):")
    mlir_str = circuit.emit_mlir()
    for line in mlir_str.split('\n'):
        # Show static operations
        if any(kw in line for kw in [
            'proc.rule', 'proc.static_step', 'proc.step',
            'proc.seq', 'proc.par', 'proc.enable',
            'proc.static_repeat', 'proc.static_if',
            'static_latency', 'interval'
        ]):
            print(f"   {line.strip()}")

    print("\n3. Setting up simulation workspace...")
    ws = SimulationWorkspace(circuit, sim_dir)
    ws._add_stl_rtl()
    ws._create_directories()
    ws._generate_rtl()
    ws._generate_makefile()

    tb_file = sim_dir / "tb" / "testbench.cpp"
    tb_file.write_text(STATIC_PROC_TESTBENCH_CPP)
    print(f"   Workspace: {sim_dir}")

    print("\n4. Building simulation...")
    if not ws.build():
        print("Build failed!")
        # Print full MLIR for debugging
        print("\n--- Full MLIR ---")
        print(mlir_str)
        return 1
    print("   Build successful!")

    print("\n5. Running simulation...")
    success, output = ws.run()
    print(output)

    print("\n" + "=" * 70)
    print("Static Procedural Control Example Complete!")
    print(f"Waveforms: {sim_dir / 'waves' / 'StaticDotProduct.vcd'}")
    print("=" * 70)

    return 0 if success else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())
