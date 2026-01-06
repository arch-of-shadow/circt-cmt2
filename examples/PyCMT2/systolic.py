#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Systolic Array for Matrix Multiplication

This example demonstrates a 2x2 systolic array for matrix multiplication,
inspired by Calyx's systolic-lang
(https://github.com/calyxir/calyx/tree/main/frontends/systolic-lang).

A systolic array is a grid of processing elements (PEs) that perform
multiply-accumulate (MAC) operations. Data flows through the array in a
pipelined, wavefront manner:
- Matrix A elements flow left-to-right through rows
- Matrix B elements flow top-to-bottom through columns
- Each PE computes: acc += a * b

For a 2x2 systolic array computing C = A * B:
- A is fed row-by-row from the left with staggered timing
- B is fed column-by-column from the top with staggered timing
- Results accumulate in each PE

Architecture (2x2):
    ┌─────┐  ┌─────┐
    │PE00 │──│PE01 │  ← A[0,:]
    └──┬──┘  └──┬──┘
       │        │
    ┌──┴──┐  ┌──┴──┐
    │PE10 │──│PE11 │  ← A[1,:]
    └─────┘  └─────┘
       ↑        ↑
     B[:,0]   B[:,1]

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/systolic.py
"""

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry


def create_systolic_circuit():
    """Create a flat 2x2 systolic array for matrix multiplication.

    Uses a flat design where all PE state is in the top-level module.
    Each PE has:
    - Accumulator register (acc)
    - A pass-through register (for right neighbor)
    - B pass-through register (for bottom neighbor)
    """
    clear_stl_registry()

    circuit = Circuit("SystolicMM")

    # Create register modules
    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)

    with circuit.module("SystolicArray2x2") as m:
        clk = m.clock()
        rst = m.reset()

        # =====================================================================
        # PE state: Each PE has accumulator, A pass-through, B pass-through
        # =====================================================================
        # PE[0,0]
        pe00_acc = m.instance(reg32, "pe00_acc", clk=clk, rst=rst)
        pe00_a = m.instance(reg32, "pe00_a", clk=clk, rst=rst)
        pe00_b = m.instance(reg32, "pe00_b", clk=clk, rst=rst)

        # PE[0,1]
        pe01_acc = m.instance(reg32, "pe01_acc", clk=clk, rst=rst)
        pe01_a = m.instance(reg32, "pe01_a", clk=clk, rst=rst)
        pe01_b = m.instance(reg32, "pe01_b", clk=clk, rst=rst)

        # PE[1,0]
        pe10_acc = m.instance(reg32, "pe10_acc", clk=clk, rst=rst)
        pe10_a = m.instance(reg32, "pe10_a", clk=clk, rst=rst)
        pe10_b = m.instance(reg32, "pe10_b", clk=clk, rst=rst)

        # PE[1,1]
        pe11_acc = m.instance(reg32, "pe11_acc", clk=clk, rst=rst)
        pe11_a = m.instance(reg32, "pe11_a", clk=clk, rst=rst)
        pe11_b = m.instance(reg32, "pe11_b", clk=clk, rst=rst)

        # Input registers
        a0_in = m.instance(reg32, "a0_in", clk=clk, rst=rst)
        a1_in = m.instance(reg32, "a1_in", clk=clk, rst=rst)
        b0_in = m.instance(reg32, "b0_in", clk=clk, rst=rst)
        b1_in = m.instance(reg32, "b1_in", clk=clk, rst=rst)

        # Control
        busy = m.instance(reg1, "busy", clk=clk, rst=rst)
        cycle_count = m.instance(reg32, "cycle_count", clk=clk, rst=rst)

        # =====================================================================
        # Method: load_a - Load A row values
        # =====================================================================
        with m.method("load_a", args=[("a0", UInt(32)), ("a1", UInt(32))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                not_busy = g.not_(is_busy)
                g.returns(not_busy)
            with meth.body() as body:
                body.call(a0_in, "write", body.arg("a0"))
                body.call(a1_in, "write", body.arg("a1"))

        # =====================================================================
        # Method: load_b - Load B column values
        # =====================================================================
        with m.method("load_b", args=[("b0", UInt(32)), ("b1", UInt(32))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                not_busy = g.not_(is_busy)
                g.returns(not_busy)
            with meth.body() as body:
                body.call(b0_in, "write", body.arg("b0"))
                body.call(b1_in, "write", body.arg("b1"))

        # =====================================================================
        # Method: start - Begin computation
        # =====================================================================
        with m.method("start") as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                not_busy = g.not_(is_busy)
                g.returns(not_busy)
            with meth.body() as body:
                body.call(busy, "write", body.const(1, 1))
                body.call(cycle_count, "write", body.const(0, 32))

        # =====================================================================
        # Rule: compute - One systolic step
        # Each PE: reads A from left (or input), B from top (or input)
        #          computes MAC, passes A right and B down
        # =====================================================================
        with m.rule("compute") as rule:
            with rule.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(is_busy)
            with rule.body() as body:
                # Read inputs
                a0_val = body.call(a0_in, "read")
                a1_val = body.call(a1_in, "read")
                b0_val = body.call(b0_in, "read")
                b1_val = body.call(b1_in, "read")

                # Read PE pass-through values (from previous cycle)
                pe00_a_val = body.call(pe00_a, "read")
                pe00_b_val = body.call(pe00_b, "read")
                pe10_a_val = body.call(pe10_a, "read")
                pe01_b_val = body.call(pe01_b, "read")

                # =============== PE[0,0]: gets A from input, B from input ===============
                # MAC
                pe00_acc_val = body.call(pe00_acc, "read")
                pe00_product = body.mul(a0_val, b0_val)
                pe00_new_acc = body.add(pe00_acc_val, pe00_product)
                body.call(pe00_acc, "write", pe00_new_acc)
                # Pass-through
                body.call(pe00_a, "write", a0_val)
                body.call(pe00_b, "write", b0_val)

                # =============== PE[0,1]: gets A from PE[0,0], B from input ===============
                pe01_acc_val = body.call(pe01_acc, "read")
                pe01_product = body.mul(pe00_a_val, b1_val)
                pe01_new_acc = body.add(pe01_acc_val, pe01_product)
                body.call(pe01_acc, "write", pe01_new_acc)
                body.call(pe01_a, "write", pe00_a_val)
                body.call(pe01_b, "write", b1_val)

                # =============== PE[1,0]: gets A from input, B from PE[0,0] ===============
                pe10_acc_val = body.call(pe10_acc, "read")
                pe10_product = body.mul(a1_val, pe00_b_val)
                pe10_new_acc = body.add(pe10_acc_val, pe10_product)
                body.call(pe10_acc, "write", pe10_new_acc)
                body.call(pe10_a, "write", a1_val)
                body.call(pe10_b, "write", pe00_b_val)

                # =============== PE[1,1]: gets A from PE[1,0], B from PE[0,1] ===============
                pe11_acc_val = body.call(pe11_acc, "read")
                pe11_product = body.mul(pe10_a_val, pe01_b_val)
                pe11_new_acc = body.add(pe11_acc_val, pe11_product)
                body.call(pe11_acc, "write", pe11_new_acc)
                body.call(pe11_a, "write", pe10_a_val)
                body.call(pe11_b, "write", pe01_b_val)

                # Update cycle counter
                cyc = body.call(cycle_count, "read")
                new_cyc = body.add(cyc, body.const(1, 32))
                body.call(cycle_count, "write", new_cyc)

                # Check if done (4 cycles for 2x2 * 2 depth)
                # geq not available, use: done = (cyc >= 4) = not (cyc < 4)
                not_done = body.lt(new_cyc, body.const(4, 32))
                done_check = body.not_(not_done)
                new_busy = body.mux(done_check, body.const(0, 1), body.const(1, 1))
                body.call(busy, "write", new_busy)

        # =====================================================================
        # Value methods to read results
        # =====================================================================
        with m.value("get_c00", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(pe00_acc, "read"))

        with m.value("get_c01", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(pe01_acc, "read"))

        with m.value("get_c10", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(pe10_acc, "read"))

        with m.value("get_c11", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(pe11_acc, "read"))

        with m.value("is_done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                is_busy = body.call(busy, "read")
                body.returns(body.not_(is_busy))

        # =====================================================================
        # Method: clear - Reset all PE accumulators
        # =====================================================================
        with m.method("clear") as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with meth.body() as body:
                zero = body.const(0, 32)
                body.call(pe00_acc, "write", zero)
                body.call(pe01_acc, "write", zero)
                body.call(pe10_acc, "write", zero)
                body.call(pe11_acc, "write", zero)
                body.call(pe00_a, "write", zero)
                body.call(pe00_b, "write", zero)
                body.call(pe01_a, "write", zero)
                body.call(pe01_b, "write", zero)
                body.call(pe10_a, "write", zero)
                body.call(pe10_b, "write", zero)
                body.call(pe11_a, "write", zero)
                body.call(pe11_b, "write", zero)

    return circuit


# Verilator C++ testbench
SYSTOLIC_TESTBENCH_CPP = """\
// Systolic Array 2x2 Testbench
// Tests matrix multiplication with staggered input timing

#include "VSystolicArray2x2.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <iostream>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto dut = new VSystolicArray2x2();
    auto tfp = new VerilatedVcdC();
    dut->trace(tfp, 99);
    tfp->open("waves/SystolicArray2x2.vcd");

    dut->clk = 0;
    dut->rst = 1;
    dut->start_enable = 0;
    dut->load_a_enable = 0;
    dut->load_b_enable = 0;
    dut->clear_enable = 0;

    int cycle = 0;

    auto tick = [&]() {
        dut->clk = 0;
        dut->eval();
        tfp->dump(cycle * 10);
        dut->clk = 1;
        dut->eval();
        tfp->dump(cycle * 10 + 5);
        cycle++;
    };

    std::cout << "=== Systolic Array 2x2 Matrix Multiply ===" << std::endl;
    std::cout << std::endl;

    // Test: C = A * B where:
    // A = [[1, 2], [3, 4]]
    // B = [[5, 6], [7, 8]]
    // C = [[1*5+2*7, 1*6+2*8], [3*5+4*7, 3*6+4*8]] = [[19, 22], [43, 50]]

    std::cout << "Matrix A = [[1,2],[3,4]]" << std::endl;
    std::cout << "Matrix B = [[5,6],[7,8]]" << std::endl;
    std::cout << "Expected C = [[19,22],[43,50]]" << std::endl;
    std::cout << std::endl;

    // Reset
    std::cout << "Resetting..." << std::endl;
    for (int i = 0; i < 5; i++) tick();
    dut->rst = 0;
    tick();

    // Clear accumulators
    std::cout << "Clearing accumulators..." << std::endl;
    dut->clear_enable = 1;
    tick();
    dut->clear_enable = 0;
    tick();

    // Systolic feeding with proper staggering:
    // For C = A * B where A[i,:] feeds row i, B[:,j] feeds column j
    //
    // Cycle 0: PE[0,0] gets A[0,0]=1, B[0,0]=5 -> acc += 1*5 = 5
    // Cycle 1: PE[0,0] gets A[0,1]=2, B[1,0]=7 -> acc += 2*7 = 19
    //          PE[0,1] gets A[0,0]=1, B[0,1]=6 -> acc += 1*6 = 6
    //          PE[1,0] gets A[1,0]=3, B[0,0]=5 -> acc += 3*5 = 15
    // Cycle 2: PE[0,1] gets A[0,1]=2, B[1,1]=8 -> acc += 2*8 = 22
    //          PE[1,0] gets A[1,1]=4, B[1,0]=7 -> acc += 4*7 = 43
    //          PE[1,1] gets A[1,0]=3, B[0,1]=6 -> acc += 3*6 = 18
    // Cycle 3: PE[1,1] gets A[1,1]=4, B[1,1]=8 -> acc += 4*8 = 50

    std::cout << "Starting systolic computation with staggered feeding..." << std::endl;

    // Load initial values and start
    dut->load_a_a0 = 1; dut->load_a_a1 = 0;
    dut->load_b_b0 = 5; dut->load_b_b1 = 0;
    dut->load_a_enable = 1;
    dut->load_b_enable = 1;
    tick();
    dut->load_a_enable = 0;
    dut->load_b_enable = 0;

    dut->start_enable = 1;
    tick();
    dut->start_enable = 0;

    // Feed subsequent values with staggering
    // Cycle 1: A[0,1]=2, A[1,0]=3, B[1,0]=7, B[0,1]=6
    dut->load_a_a0 = 2; dut->load_a_a1 = 3;
    dut->load_b_b0 = 7; dut->load_b_b1 = 6;
    dut->load_a_enable = 1;
    dut->load_b_enable = 1;
    tick();
    dut->load_a_enable = 0;
    dut->load_b_enable = 0;

    // Cycle 2: A[1,1]=4, B[1,1]=8
    dut->load_a_a0 = 0; dut->load_a_a1 = 4;
    dut->load_b_b0 = 0; dut->load_b_b1 = 8;
    dut->load_a_enable = 1;
    dut->load_b_enable = 1;
    tick();
    dut->load_a_enable = 0;
    dut->load_b_enable = 0;

    // Cycle 3: zeros
    dut->load_a_a0 = 0; dut->load_a_a1 = 0;
    dut->load_b_b0 = 0; dut->load_b_b1 = 0;
    dut->load_a_enable = 1;
    dut->load_b_enable = 1;
    tick();
    dut->load_a_enable = 0;
    dut->load_b_enable = 0;

    // Wait for completion
    int wait = 0;
    while (!dut->is_done_res0 && wait < 10) {
        tick();
        wait++;
    }

    std::cout << "Completed after " << (4 + wait) << " total cycles" << std::endl;
    tick();

    // Read and verify results
    std::cout << std::endl << "Results:" << std::endl;
    uint32_t c00 = dut->get_c00_res0;
    uint32_t c01 = dut->get_c01_res0;
    uint32_t c10 = dut->get_c10_res0;
    uint32_t c11 = dut->get_c11_res0;

    bool pass = true;
    std::cout << "  C[0,0] = " << c00 << " (expected 19) " << (c00 == 19 ? "PASS" : "FAIL") << std::endl;
    std::cout << "  C[0,1] = " << c01 << " (expected 22) " << (c01 == 22 ? "PASS" : "FAIL") << std::endl;
    std::cout << "  C[1,0] = " << c10 << " (expected 43) " << (c10 == 43 ? "PASS" : "FAIL") << std::endl;
    std::cout << "  C[1,1] = " << c11 << " (expected 50) " << (c11 == 50 ? "PASS" : "FAIL") << std::endl;

    if (c00 != 19 || c01 != 22 || c10 != 43 || c11 != 50) pass = false;

    tfp->close();
    delete tfp;
    delete dut;

    std::cout << std::endl;
    if (pass) {
        std::cout << "=== ALL TESTS PASSED ===" << std::endl;
        return 0;
    } else {
        std::cout << "=== SOME TESTS FAILED ===" << std::endl;
        std::cout << "Note: Results depend on correct staggered input timing." << std::endl;
        return 1;
    }
}
"""


def main():
    print("=" * 70)
    print("Systolic Array for Matrix Multiplication")
    print("=" * 70)
    print()
    print("Inspired by Calyx's systolic-lang:")
    print("https://github.com/calyxir/calyx/tree/main/frontends/systolic-lang")
    print()

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "systolic_workspace"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("1. Generating 2x2 systolic array...")
    circuit = create_systolic_circuit()

    print("\n2. Architecture (flat design with PE state as registers):")
    print("   Each PE has: accumulator, A pass-through, B pass-through")
    print()
    print("   ┌────┐  ┌────┐")
    print("   │PE00│──│PE01│  ← A[0,:]")
    print("   └──┬─┘  └──┬─┘")
    print("      │       │")
    print("   ┌──┴─┐  ┌──┴─┐")
    print("   │PE10│──│PE11│  ← A[1,:]")
    print("   └────┘  └────┘")
    print("      ↑       ↑")
    print("    B[:,0]  B[:,1]")

    print("\n3. Setting up simulation workspace...")
    ws = SimulationWorkspace(circuit, sim_dir)
    ws._add_stl_rtl()
    ws._create_directories()
    ws._generate_rtl()
    ws._generate_makefile()

    tb_file = sim_dir / "tb" / "testbench.cpp"
    tb_file.write_text(SYSTOLIC_TESTBENCH_CPP)
    print(f"   Workspace: {sim_dir}")

    print("\n4. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    print("\n5. Running simulation...")
    success, output = ws.run()
    print(output)

    print("\n" + "=" * 70)
    print("Systolic Array Example Complete!")
    print(f"Waveforms: {sim_dir / 'waves' / 'SystolicArray2x2.vcd'}")
    print("=" * 70)

    return 0 if success else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())
