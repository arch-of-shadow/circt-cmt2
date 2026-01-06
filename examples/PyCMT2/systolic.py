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
    The matrices are pre-loaded, then computation proceeds with the systolic
    data flow handled internally based on the cycle counter.

    For 2x2 matrix multiply C = A * B:
    - A = [[a00, a01], [a10, a11]]
    - B = [[b00, b01], [b10, b11]]

    Systolic schedule (4 cycles):
    Cycle 0: PE[0,0] uses a00, b00
    Cycle 1: PE[0,0] uses a01, b10; PE[0,1] uses a00, b01; PE[1,0] uses a10, b00
    Cycle 2: PE[0,1] uses a01, b11; PE[1,0] uses a11, b10; PE[1,1] uses a10, b01
    Cycle 3: PE[1,1] uses a11, b11
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
        # Matrix storage registers (pre-loaded before computation)
        # =====================================================================
        # Matrix A: 2x2
        a00_reg = m.instance(reg32, "a00_reg", clk=clk, rst=rst)
        a01_reg = m.instance(reg32, "a01_reg", clk=clk, rst=rst)
        a10_reg = m.instance(reg32, "a10_reg", clk=clk, rst=rst)
        a11_reg = m.instance(reg32, "a11_reg", clk=clk, rst=rst)

        # Matrix B: 2x2
        b00_reg = m.instance(reg32, "b00_reg", clk=clk, rst=rst)
        b01_reg = m.instance(reg32, "b01_reg", clk=clk, rst=rst)
        b10_reg = m.instance(reg32, "b10_reg", clk=clk, rst=rst)
        b11_reg = m.instance(reg32, "b11_reg", clk=clk, rst=rst)

        # =====================================================================
        # PE accumulators (result matrix C)
        # =====================================================================
        c00_acc = m.instance(reg32, "c00_acc", clk=clk, rst=rst)
        c01_acc = m.instance(reg32, "c01_acc", clk=clk, rst=rst)
        c10_acc = m.instance(reg32, "c10_acc", clk=clk, rst=rst)
        c11_acc = m.instance(reg32, "c11_acc", clk=clk, rst=rst)

        # Control
        busy = m.instance(reg1, "busy", clk=clk, rst=rst)
        cycle_count = m.instance(reg32, "cycle_count", clk=clk, rst=rst)

        # =====================================================================
        # Method: load_a - Load matrix A (row-major)
        # =====================================================================
        with m.method("load_a", args=[("a00", UInt(32)), ("a01", UInt(32)),
                                       ("a10", UInt(32)), ("a11", UInt(32))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                not_busy = g.not_(is_busy)
                g.returns(not_busy)
            with meth.body() as body:
                body.call(a00_reg, "write", body.arg("a00"))
                body.call(a01_reg, "write", body.arg("a01"))
                body.call(a10_reg, "write", body.arg("a10"))
                body.call(a11_reg, "write", body.arg("a11"))

        # =====================================================================
        # Method: load_b - Load matrix B (row-major)
        # =====================================================================
        with m.method("load_b", args=[("b00", UInt(32)), ("b01", UInt(32)),
                                       ("b10", UInt(32)), ("b11", UInt(32))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy, "read")
                not_busy = g.not_(is_busy)
                g.returns(not_busy)
            with meth.body() as body:
                body.call(b00_reg, "write", body.arg("b00"))
                body.call(b01_reg, "write", body.arg("b01"))
                body.call(b10_reg, "write", body.arg("b10"))
                body.call(b11_reg, "write", body.arg("b11"))

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
                # Clear accumulators
                body.call(c00_acc, "write", body.const(0, 32))
                body.call(c01_acc, "write", body.const(0, 32))
                body.call(c10_acc, "write", body.const(0, 32))
                body.call(c11_acc, "write", body.const(0, 32))

        # =====================================================================
        # Rule: cycle0 - Cycle 0: Only PE[0,0] active
        # PE[0,0] += a00 * b00
        # =====================================================================
        with m.rule("cycle0") as rule:
            with rule.guard() as g:
                is_busy = g.call(busy, "read")
                cyc = g.call(cycle_count, "read")
                is_cyc0 = g.eq(cyc, g.const(0, 32))
                g.returns(g.and_(is_busy, is_cyc0))
            with rule.body() as body:
                a00 = body.call(a00_reg, "read")
                b00 = body.call(b00_reg, "read")
                acc = body.call(c00_acc, "read")
                product = body.mul(a00, b00)
                new_acc = body.add(acc, product)
                body.call(c00_acc, "write", new_acc)
                body.call(cycle_count, "write", body.const(1, 32))

        # =====================================================================
        # Rule: cycle1 - Cycle 1: PE[0,0], PE[0,1], PE[1,0] active
        # PE[0,0] += a01 * b10
        # PE[0,1] += a00 * b01
        # PE[1,0] += a10 * b00
        # =====================================================================
        with m.rule("cycle1") as rule:
            with rule.guard() as g:
                is_busy = g.call(busy, "read")
                cyc = g.call(cycle_count, "read")
                is_cyc1 = g.eq(cyc, g.const(1, 32))
                g.returns(g.and_(is_busy, is_cyc1))
            with rule.body() as body:
                # PE[0,0] += a01 * b10
                a01 = body.call(a01_reg, "read")
                b10 = body.call(b10_reg, "read")
                c00_val = body.call(c00_acc, "read")
                c00_new = body.add(c00_val, body.mul(a01, b10))
                body.call(c00_acc, "write", c00_new)

                # PE[0,1] += a00 * b01
                a00 = body.call(a00_reg, "read")
                b01 = body.call(b01_reg, "read")
                c01_val = body.call(c01_acc, "read")
                c01_new = body.add(c01_val, body.mul(a00, b01))
                body.call(c01_acc, "write", c01_new)

                # PE[1,0] += a10 * b00
                a10 = body.call(a10_reg, "read")
                b00 = body.call(b00_reg, "read")
                c10_val = body.call(c10_acc, "read")
                c10_new = body.add(c10_val, body.mul(a10, b00))
                body.call(c10_acc, "write", c10_new)

                body.call(cycle_count, "write", body.const(2, 32))

        # =====================================================================
        # Rule: cycle2 - Cycle 2: PE[0,1], PE[1,0], PE[1,1] active
        # PE[0,1] += a01 * b11
        # PE[1,0] += a11 * b10
        # PE[1,1] += a10 * b01
        # =====================================================================
        with m.rule("cycle2") as rule:
            with rule.guard() as g:
                is_busy = g.call(busy, "read")
                cyc = g.call(cycle_count, "read")
                is_cyc2 = g.eq(cyc, g.const(2, 32))
                g.returns(g.and_(is_busy, is_cyc2))
            with rule.body() as body:
                # PE[0,1] += a01 * b11
                a01 = body.call(a01_reg, "read")
                b11 = body.call(b11_reg, "read")
                c01_val = body.call(c01_acc, "read")
                c01_new = body.add(c01_val, body.mul(a01, b11))
                body.call(c01_acc, "write", c01_new)

                # PE[1,0] += a11 * b10
                a11 = body.call(a11_reg, "read")
                b10 = body.call(b10_reg, "read")
                c10_val = body.call(c10_acc, "read")
                c10_new = body.add(c10_val, body.mul(a11, b10))
                body.call(c10_acc, "write", c10_new)

                # PE[1,1] += a10 * b01
                a10 = body.call(a10_reg, "read")
                b01 = body.call(b01_reg, "read")
                c11_val = body.call(c11_acc, "read")
                c11_new = body.add(c11_val, body.mul(a10, b01))
                body.call(c11_acc, "write", c11_new)

                body.call(cycle_count, "write", body.const(3, 32))

        # =====================================================================
        # Rule: cycle3 - Cycle 3: Only PE[1,1] active, then done
        # PE[1,1] += a11 * b11
        # =====================================================================
        with m.rule("cycle3") as rule:
            with rule.guard() as g:
                is_busy = g.call(busy, "read")
                cyc = g.call(cycle_count, "read")
                is_cyc3 = g.eq(cyc, g.const(3, 32))
                g.returns(g.and_(is_busy, is_cyc3))
            with rule.body() as body:
                # PE[1,1] += a11 * b11
                a11 = body.call(a11_reg, "read")
                b11 = body.call(b11_reg, "read")
                c11_val = body.call(c11_acc, "read")
                c11_new = body.add(c11_val, body.mul(a11, b11))
                body.call(c11_acc, "write", c11_new)

                # Done
                body.call(busy, "write", body.const(0, 1))
                body.call(cycle_count, "write", body.const(4, 32))

        # =====================================================================
        # Value methods to read results
        # =====================================================================
        with m.value("get_c00", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(c00_acc, "read"))

        with m.value("get_c01", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(c01_acc, "read"))

        with m.value("get_c10", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(c10_acc, "read"))

        with m.value("get_c11", returns=[UInt(32)]) as val:
            with val.guard() as g:
                is_busy = g.call(busy, "read")
                g.returns(g.not_(is_busy))
            with val.body() as body:
                body.returns(body.call(c11_acc, "read"))

        with m.value("is_done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                is_busy = body.call(busy, "read")
                body.returns(body.not_(is_busy))

    return circuit


# Verilator C++ testbench
SYSTOLIC_TESTBENCH_CPP = """\
// Systolic Array 2x2 Testbench
// Tests matrix multiplication with pre-loaded matrices

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

    // Load matrix A: [[1, 2], [3, 4]]
    std::cout << "Loading matrix A..." << std::endl;
    dut->load_a_a00 = 1;
    dut->load_a_a01 = 2;
    dut->load_a_a10 = 3;
    dut->load_a_a11 = 4;
    dut->load_a_enable = 1;
    tick();
    dut->load_a_enable = 0;

    // Load matrix B: [[5, 6], [7, 8]]
    std::cout << "Loading matrix B..." << std::endl;
    dut->load_b_b00 = 5;
    dut->load_b_b01 = 6;
    dut->load_b_b10 = 7;
    dut->load_b_b11 = 8;
    dut->load_b_enable = 1;
    tick();
    dut->load_b_enable = 0;

    // Start computation
    std::cout << "Starting systolic computation..." << std::endl;
    dut->start_enable = 1;
    tick();
    dut->start_enable = 0;

    // Wait for completion (4 cycles for 2x2 systolic)
    std::cout << "Running systolic schedule:" << std::endl;
    std::cout << "  Cycle 0: PE[0,0] += a00*b00" << std::endl;
    std::cout << "  Cycle 1: PE[0,0] += a01*b10, PE[0,1] += a00*b01, PE[1,0] += a10*b00" << std::endl;
    std::cout << "  Cycle 2: PE[0,1] += a01*b11, PE[1,0] += a11*b10, PE[1,1] += a10*b01" << std::endl;
    std::cout << "  Cycle 3: PE[1,1] += a11*b11" << std::endl;

    int wait = 0;
    while (!dut->is_done_res0 && wait < 20) {
        tick();
        wait++;
    }

    std::cout << "Completed after " << wait << " cycles" << std::endl;
    tick();  // One more cycle to ensure results are ready

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

    print("\n2. Architecture:")
    print("   - Matrices A and B are pre-loaded into registers")
    print("   - Computation follows systolic schedule (4 cycles for 2x2)")
    print("   - Each PE computes: C[i,j] = sum(A[i,k] * B[k,j])")
    print()
    print("   ┌────┐  ┌────┐")
    print("   │C00 │  │C01 │")
    print("   └────┘  └────┘")
    print("   ┌────┐  ┌────┐")
    print("   │C10 │  │C11 │")
    print("   └────┘  └────┘")
    print()
    print("   Systolic schedule:")
    print("     Cycle 0: C00 += A00*B00")
    print("     Cycle 1: C00 += A01*B10, C01 += A00*B01, C10 += A10*B00")
    print("     Cycle 2: C01 += A01*B11, C10 += A11*B10, C11 += A10*B01")
    print("     Cycle 3: C11 += A11*B11")

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
