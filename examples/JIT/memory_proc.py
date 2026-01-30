#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Memory STL Example with Procedural Control using Testbench DSL.

This example demonstrates:
1. Using Memory STL components (async memory)
2. Procedural control (proc.step, proc.seq) for multi-cycle operations
3. A memory sum accumulator that reads memory and sums values
4. Simulation verification with Testbench DSL

The design implements a simple memory accumulator:
- Stores values at addresses 0-3
- Has a procedural "sum" operation that reads and accumulates values
- Uses async memory for combinational read access

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/memory_proc.py
"""

import cmt2.jit as jit

import sys
import os
import shutil
from pathlib import Path

# Add circt Python packages to path
build_dir = os.path.dirname(os.path.abspath(__file__))
while build_dir and not os.path.exists(os.path.join(build_dir, "build")):
    build_dir = os.path.dirname(build_dir)
if build_dir:
    sys.path.insert(0, os.path.join(build_dir, "build/tools/circt/python_packages/circt_core"))

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, Memory, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_memory_accumulator_circuit():
    """Create a circuit that uses Memory STL with procedural control."""
    clear_stl_registry()

    circuit = Circuit("MemoryAccumulator")

    # Create memory module: 4 entries, 32-bit data, 2-bit address
    mem_mod = Memory.create_1r1w_async(circuit, data_width=32, addr_width=2, depth=4)

    # Create register modules for state
    reg32 = Reg.create(circuit, 32)
    reg2 = Reg.create(circuit, 2)  # For address counter
    reg1 = Reg.create(circuit, 1)  # For control flags

    with jit.module(circuit, "MemAccum") as m:
        clk = m.clock()
        rst = m.reset()

        # Instantiate memory and registers
        mem = m.instance(mem_mod, clk=clk, rst=rst)
        accum_reg = m.instance(reg32, clk=clk, rst=rst)
        addr_reg = m.instance(reg2, clk=clk, rst=rst)
        busy_reg = m.instance(reg1, clk=clk, rst=rst)

        # =====================================================================
        # Method: write(addr, data) - Write to memory
        # =====================================================================
        @jit.method(m)
        def write(write_meth, addr: UInt[2], data: UInt[32]) -> None:
            with write_meth.guard:
                write_meth.returns(write_meth.not_(busy_reg.read))
            with write_meth.body:
                mem.write(data, addr)

        # =====================================================================
        # Method: read(addr) -> data - Read from memory (combinational)
        # =====================================================================
        @jit.method(m)
        def read(read_meth, addr: UInt[2]) -> UInt[32]:
            with read_meth.guard:
                read_meth.always()
            with read_meth.body:
                read_meth.returns(mem.read(addr))

        # =====================================================================
        # Value: get_sum() -> sum - Get accumulated sum
        # =====================================================================
        @jit.value(m)
        def get_sum(get_sum_val) -> UInt[32]:
            with get_sum_val.guard:
                get_sum_val.always()
            with get_sum_val.body:
                get_sum_val.returns(accum_reg.read)

        # =====================================================================
        # Value: is_busy() -> bool - Check if sum operation is in progress
        # =====================================================================
        @jit.value(m)
        def is_busy(is_busy_val) -> UInt[1]:
            with is_busy_val.guard:
                is_busy_val.always()
            with is_busy_val.body:
                is_busy_val.returns(busy_reg.read)

        # =====================================================================
        # Procedural Steps for sum operation
        # =====================================================================

        # Step: init_sum
        with m.step() as init_sum:
            accum_reg.next = init_sum.const(0, 32)
            addr_reg.next = init_sum.const(0, 2)
            busy_reg.next = init_sum.const(1, 1)
            init_sum.done(init_sum.const(1, 1))

        # Step: read_and_add
        with m.step() as read_and_add:
            addr = addr_reg.read
            data = mem.read(addr)
            current_sum = accum_reg.read
            new_sum = read_and_add.add(current_sum, data)
            accum_reg.next = new_sum
            new_addr = read_and_add.add(addr, read_and_add.const(1, 2))
            addr_reg.next = read_and_add.bits(new_addr, 1, 0)
            read_and_add.done(read_and_add.const(1, 1))

        # Step: finish_sum
        with m.step() as finish_sum:
            busy_reg.next = finish_sum.const(0, 1)
            finish_sum.done(finish_sum.const(1, 1))

        # =====================================================================
        # Method: start_sum() - Start the sum operation
        # =====================================================================
        @jit.method(m)
        def start_sum(start_sum_meth) -> None:
            with start_sum_meth.guard:
                start_sum_meth.returns(start_sum_meth.not_(busy_reg.read))
            with start_sum_meth.body:
                accum_reg.write(start_sum_meth.const(0, 32))
                addr_reg.write(start_sum_meth.const(0, 2))
                busy_reg.write(start_sum_meth.const(1, 1))

        # =====================================================================
        # Proc Rule: sum_loop
        # =====================================================================
        with m.proc_rule() as sum_loop:
            with sum_loop.guard as g:
                busy = busy_reg.read
                g.returns(busy)
            with sum_loop.control() as ctrl:
                with ctrl.seq():
                    ctrl.enable(read_and_add.ref())
                    ctrl.enable(read_and_add.ref())
                    ctrl.enable(read_and_add.ref())
                    ctrl.enable(read_and_add.ref())
                    ctrl.enable(finish_sum.ref())

    return circuit


def create_memory_testbench(circuit):
    """Create testbench using DSL for memory accumulator."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # Test values: mem[0]=10, mem[1]=20, mem[2]=30, mem[3]=40
    test_values = [10, 20, 30, 40]
    expected_sum = sum(test_values)  # = 100

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("is_busy_res0", 0, "Should not be busy after reset")
        seq.expect("get_sum_res0", 0, "Sum should be 0 after reset")
        seq.print("Reset test passed")

    # =========================================================================
    # Test Sequence: Memory Write
    # =========================================================================
    with tb.sequence("test_memory_write") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Write values to memory")
        seq.comment("=" * 60)
        seq.reset(5)

        # Write test values to memory
        for addr, value in enumerate(test_values):
            seq.comment(f"Write mem[{addr}] = {value}")
            seq.wait_condition("dut->write_ready", timeout=10)
            seq.drive("write_addr", addr)
            seq.drive("write_data", value)
            seq.drive("write_enable", 1)
            seq.wait(1)
            seq.drive("write_enable", 0)
            seq.wait(1)

        seq.print("Memory write completed")

    # =========================================================================
    # Test Sequence: Memory Read Verification
    # =========================================================================
    with tb.sequence("test_memory_read") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Read and verify memory contents")
        seq.comment("=" * 60)
        seq.reset(5)

        # First write values
        for addr, value in enumerate(test_values):
            seq.wait_condition("dut->write_ready", timeout=10)
            seq.drive("write_addr", addr)
            seq.drive("write_data", value)
            seq.drive("write_enable", 1)
            seq.wait(1)
            seq.drive("write_enable", 0)
            seq.wait(1)

        # Then read and verify
        seq.comment("Reading back values...")
        for addr, expected in enumerate(test_values):
            seq.drive("read_addr", addr)
            seq.drive("read_enable", 1)
            seq.wait(1)
            seq.drive("read_enable", 0)
            seq.expect("read_res0", expected, f"mem[{addr}] should be {expected}")
            seq.print(f"mem[{addr}] = ", "read_res0")

        seq.print("Memory read verification passed")

    # =========================================================================
    # Test Sequence: Sum Operation
    # =========================================================================
    with tb.sequence("test_sum_operation") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Sum operation - accumulates all memory values")
        seq.comment("=" * 60)
        seq.reset(5)

        # Write test values
        for addr, value in enumerate(test_values):
            seq.wait_condition("dut->write_ready", timeout=10)
            seq.drive("write_addr", addr)
            seq.drive("write_data", value)
            seq.drive("write_enable", 1)
            seq.wait(1)
            seq.drive("write_enable", 0)
            seq.wait(1)

        # Start sum operation
        seq.comment("Starting sum operation...")
        seq.expect("is_busy_res0", 0, "Should not be busy before start")
        seq.wait_condition("dut->start_sum_ready", timeout=10)
        seq.drive("start_sum_enable", 1)
        seq.wait(1)
        seq.drive("start_sum_enable", 0)

        # Wait for sum to complete
        seq.record_cycle("sum_start")
        seq.wait_condition("!dut->is_busy_res0", timeout=50)
        seq.record_cycle("sum_end")

        # Verify result
        seq.expect("get_sum_res0", expected_sum, f"Sum should be {expected_sum}")
        seq.print_cycle_diff("sum_start", "sum_end", "Sum operation latency")
        seq.print("Sum = ", "get_sum_res0")
        seq.print("Sum operation test PASSED")

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification")
        seq.comment("=" * 60)
        seq.reset(5)

        # Write values
        for addr, value in enumerate(test_values):
            seq.wait_condition("dut->write_ready", timeout=10)
            seq.drive("write_addr", addr)
            seq.drive("write_data", value)
            seq.drive("write_enable", 1)
            seq.wait(1)
            seq.drive("write_enable", 0)
            seq.wait(1)

        # sum_loop should not fire when not busy
        seq.comment("Before start_sum, sum_loop should not fire")
        seq.expect_rule_fired("sum_loop_state0", False)

        # Start sum
        seq.wait_condition("dut->start_sum_ready", timeout=10)
        seq.drive("start_sum_enable", 1)
        seq.wait(1)
        seq.drive("start_sum_enable", 0)

        # Monitor sum_loop during operation
        seq.comment("Monitoring sum_loop during sum operation...")
        for i in range(8):
            seq.wait(1)
            seq.print_rule_status("sum_loop_state0")

        # Wait for completion
        seq.wait_condition("!dut->is_busy_res0", timeout=50)
        seq.wait(2)

        # After completion, sum_loop should not fire
        seq.comment("After completion, sum_loop should not fire")
        seq.expect_rule_fired("sum_loop_state0", False)
        seq.print("Debug port verification completed")

    return tb


def main():
    print("=" * 70)
    print("Memory STL Example with Procedural Control (Testbench DSL)")
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

    # Create testbench using DSL
    print("\n3. Creating testbench using Testbench DSL...")
    tb = create_memory_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    print("\n4. Setting up simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    # Generate workspace with testbench
    ws.generate_with_testbench(tb)
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
