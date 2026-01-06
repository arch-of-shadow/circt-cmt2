#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Memory STL Example with Procedural Control for PyCMT2.

This example demonstrates:
1. Using Memory STL components (async and sync memory)
2. Procedural control (proc.step, proc.seq) for multi-cycle operations
3. A memory sum accumulator that reads memory and sums values
4. Simulation verification with Verilator

The design implements a simple memory accumulator:
- Stores values at addresses 0-3
- Has a procedural "sum" operation that reads and accumulates values
- Uses async memory for combinational read access

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core:../lib/Bindings/Python python ../examples/PyCMT2/memory_proc.py
"""

import sys
import os
import shutil
from pathlib import Path

# Add paths for both installed and source pycmt2
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../lib/Bindings/Python'))

from pycmt2 import Circuit, UInt
from pycmt2.stl import Reg, Memory
from pycmt2.simulation import SimulationWorkspace


# C++ testbench for memory accumulator
MEMORY_TESTBENCH_CPP = """
#include <verilated.h>
#include <verilated_vcd_c.h>
#include "VMemAccum.h"
#include <iostream>
#include <cstdint>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto dut = new VMemAccum();
    auto tfp = new VerilatedVcdC();
    dut->trace(tfp, 99);
    tfp->open("waves/MemAccum.vcd");

    // Initialize
    dut->clk = 0;
    dut->rst = 1;
    dut->write_enable = 0;
    dut->read_enable = 0;
    dut->start_sum_enable = 0;
    dut->write_addr = 0;
    dut->write_data = 0;
    dut->read_addr = 0;

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

    // Reset
    std::cout << "Resetting..." << std::endl;
    for (int i = 0; i < 5; i++) {
        tick();
    }
    dut->rst = 0;
    tick();

    std::cout << "\\n=== Memory Write Test ===" << std::endl;

    // Write values to memory: mem[0]=10, mem[1]=20, mem[2]=30, mem[3]=40
    uint32_t test_values[4] = {10, 20, 30, 40};
    uint32_t expected_sum = 10 + 20 + 30 + 40;  // = 100

    for (int addr = 0; addr < 4; addr++) {
        // Check write ready
        if (dut->write_ready) {
            dut->write_enable = 1;
            dut->write_addr = addr;
            dut->write_data = test_values[addr];
            std::cout << "Writing mem[" << addr << "] = " << test_values[addr] << std::endl;
            tick();
            dut->write_enable = 0;
        } else {
            std::cout << "Write not ready at addr " << addr << std::endl;
            all_passed = false;
        }
        tick();
    }

    std::cout << "\\n=== Memory Read Test ===" << std::endl;

    // Read back values to verify
    for (int addr = 0; addr < 4; addr++) {
        dut->read_addr = addr;
        dut->read_enable = 1;
        tick();
        dut->read_enable = 0;

        std::cout << "Read mem[" << addr << "] = " << dut->read_res0 << std::endl;

        if (dut->read_res0 != test_values[addr]) {
            std::cout << "FAIL: Expected " << test_values[addr] << std::endl;
            all_passed = false;
        }
    }

    if (all_passed) {
        std::cout << "Memory read verification PASSED!" << std::endl;
    }

    std::cout << "\\n=== Sum Operation Test ===" << std::endl;

    // Check is_busy before starting
    std::cout << "Before start: is_busy = " << (int)dut->is_busy_res0 << std::endl;

    // Start sum operation
    if (dut->start_sum_ready) {
        std::cout << "Starting sum operation..." << std::endl;
        dut->start_sum_enable = 1;
        tick();
        dut->start_sum_enable = 0;
    } else {
        std::cout << "FAIL: start_sum not ready" << std::endl;
        all_passed = false;
    }

    // Wait for sum operation to complete
    int max_cycles = 100;

    for (int i = 0; i < max_cycles; i++) {
        tick();

        // Check if done (not busy anymore)
        if (!dut->is_busy_res0) {
            std::cout << "Sum operation completed in " << (i + 1) << " cycles" << std::endl;
            break;
        }

        if (i == max_cycles - 1) {
            std::cout << "FAIL: Sum operation timed out" << std::endl;
            all_passed = false;
        }
    }

    // Read the accumulated sum
    uint32_t actual_sum = dut->get_sum_res0;
    std::cout << "Accumulated sum = " << actual_sum << " (expected " << expected_sum << ")" << std::endl;

    if (actual_sum == expected_sum) {
        std::cout << "Sum verification PASSED!" << std::endl;
    } else {
        std::cout << "FAIL: Sum mismatch!" << std::endl;
        all_passed = false;
    }

    tfp->close();
    delete tfp;
    delete dut;

    if (all_passed) {
        std::cout << "\\n=== All Tests PASSED! ===" << std::endl;
        return 0;
    } else {
        std::cerr << "\\nSOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
"""


def create_memory_accumulator_circuit():
    """Create a circuit that uses Memory STL with procedural control.

    The MemoryAccumulator module has:
    - A 4-entry memory (32-bit data, 2-bit address)
    - A write method to store values
    - A sum method that procedurally reads all entries and returns their sum
    """
    circuit = Circuit("MemoryAccumulator")

    # Create memory module: 4 entries, 32-bit data, 2-bit address
    # Using async memory (0-cycle read latency) for simpler procedural control
    mem_mod = Memory.create_1r1w_async(circuit, data_width=32, addr_width=2, depth=4)

    # Create register modules for state
    reg32 = Reg.create(circuit, 32)
    reg2 = Reg.create(circuit, 2)  # For address counter
    reg1 = Reg.create(circuit, 1)  # For control flags

    with circuit.module("MemAccum") as m:
        clk = m.clock()
        rst = m.reset()

        # Instantiate memory and registers
        mem = m.instance(mem_mod, "mem", clk=clk, rst=rst)
        accum_reg = m.instance(reg32, "accum", clk=clk, rst=rst)
        addr_reg = m.instance(reg2, "addr", clk=clk, rst=rst)
        busy_reg = m.instance(reg1, "busy", clk=clk, rst=rst)

        # =====================================================================
        # Method: write(addr, data) - Write to memory
        # =====================================================================
        with m.method("write", args=[("addr", UInt(2)), ("data", UInt(32))]) as write_meth:
            with write_meth.guard() as g:
                # Can write when not busy
                busy = g.call(busy_reg, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with write_meth.body() as body:
                addr = body.arg("addr")
                data = body.arg("data")
                body.call(mem, "write", data, addr)

        # =====================================================================
        # Method: read(addr) -> data - Read from memory (combinational)
        # =====================================================================
        with m.method("read", args=[("addr", UInt(2))], returns=[UInt(32)]) as read_meth:
            with read_meth.guard() as g:
                g.always()
            with read_meth.body() as body:
                addr = body.arg("addr")
                data = body.call(mem, "read", addr)
                body.returns(data)

        # =====================================================================
        # Value: get_sum() -> sum - Get accumulated sum
        # =====================================================================
        with m.value("get_sum", returns=[UInt(32)]) as get_sum_val:
            with get_sum_val.guard() as g:
                g.always()
            with get_sum_val.body() as body:
                result = body.call(accum_reg, "read")
                body.returns(result)

        # =====================================================================
        # Value: is_busy() -> bool - Check if sum operation is in progress
        # =====================================================================
        with m.value("is_busy", returns=[UInt(1)]) as is_busy_val:
            with is_busy_val.guard() as g:
                g.always()
            with is_busy_val.body() as body:
                result = body.call(busy_reg, "read")
                body.returns(result)

        # =====================================================================
        # Procedural Steps for sum operation
        # =====================================================================

        # Step: init_sum - Initialize accumulator and address counter
        with m.step("init_sum") as step:
            step.call(accum_reg, "write", step.const(0, 32))
            step.call(addr_reg, "write", step.const(0, 2))
            step.call(busy_reg, "write", step.const(1, 1))
            step.done(step.const(1, 1))

        # Step: read_and_add - Read memory at current address and add to accumulator
        with m.step("read_and_add") as step:
            addr = step.call(addr_reg, "read")
            data = step.call(mem, "read", addr)
            current_sum = step.call(accum_reg, "read")
            new_sum = step.add(current_sum, data)
            step.call(accum_reg, "write", new_sum)
            # Increment address
            new_addr = step.add(addr, step.const(1, 2))
            step.call(addr_reg, "write", new_addr)
            step.done(step.const(1, 1))

        # Step: finish_sum - Clear busy flag
        with m.step("finish_sum") as step:
            step.call(busy_reg, "write", step.const(0, 1))
            step.done(step.const(1, 1))

        # =====================================================================
        # Method: start_sum() - Start the sum operation (procedural)
        # Uses proc rule internally to sequence the steps
        # =====================================================================
        with m.method("start_sum") as start_sum_meth:
            with start_sum_meth.guard() as g:
                # Can start when not busy
                busy = g.call(busy_reg, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with start_sum_meth.body() as body:
                # Just initialize - the proc rule will handle the rest
                body.call(accum_reg, "write", body.const(0, 32))
                body.call(addr_reg, "write", body.const(0, 2))
                body.call(busy_reg, "write", body.const(1, 1))

        # =====================================================================
        # Proc Rule: sum_loop - Procedural control for summing memory
        # This rule fires when busy and sequences through the read operations
        # =====================================================================
        with m.proc_rule("sum_loop") as rule:
            with rule.guard() as g:
                # Fire when busy
                busy = g.call(busy_reg, "read")
                g.returns(busy)
            with rule.control() as ctrl:
                # Sequential execution: read 4 addresses, then finish
                with ctrl.seq():
                    ctrl.enable(m._steps["read_and_add"].ref())
                    ctrl.enable(m._steps["read_and_add"].ref())
                    ctrl.enable(m._steps["read_and_add"].ref())
                    ctrl.enable(m._steps["read_and_add"].ref())
                    ctrl.enable(m._steps["finish_sum"].ref())

    return circuit


def main():
    print("=" * 70)
    print("Memory STL Example with Procedural Control")
    print("=" * 70)

    # Setup paths
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_memory_proc"

    # Clean previous simulation
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("\n1. Creating circuit...")
    circuit = create_memory_accumulator_circuit()

    print("\n2. Emitting CMT2 MLIR...")
    mlir = circuit.emit_mlir()
    print("-" * 70)
    print(mlir[:2000])
    if len(mlir) > 2000:
        print(f"... ({len(mlir) - 2000} more characters)")
    print("-" * 70)

    # Verify key constructs
    constructs = [
        ("Memory module", "Mem1r1w0c"),
        ("proc.step", "cmt2.proc.step"),
        ("proc.rule", "cmt2.proc.rule"),
        ("proc.seq", "cmt2.proc.seq"),
        ("proc.enable", "cmt2.proc.enable"),
    ]

    print("\n3. Verifying constructs in MLIR:")
    for name, pattern in constructs:
        found = pattern in mlir
        status = "FOUND" if found else "MISSING"
        print(f"   {name}: {status}")

    print("\n4. Setting up simulation workspace...")
    ws = SimulationWorkspace(circuit, sim_dir)

    # Generate workspace - STL RTL files are auto-added
    ws._add_stl_rtl()
    ws._create_directories()
    ws._generate_rtl()
    ws._generate_makefile()

    # Write custom testbench
    tb_file = sim_dir / "tb" / "testbench.cpp"
    tb_file.write_text(MEMORY_TESTBENCH_CPP)

    print("   Workspace generated at:", sim_dir)

    print("\n5. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    print("\n6. Running simulation...")
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    print("\n" + "=" * 70)
    print("Memory STL example completed!")
    print(f"Waveforms available at: {sim_dir / 'waves' / 'MemAccum.vcd'}")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
