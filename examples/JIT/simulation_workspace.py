#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Simulation Workspace Generation Example (JIT stacked on PyCMT2).

This example demonstrates how to use SimulationWorkspace with Testbench DSL
to generate a complete Verilator simulation environment with functional tests.

The generated workspace includes:
- rtl/          : Generated Verilog RTL files (including STL modules)
- tb/           : C++ testbench generated from Testbench DSL
- build/        : Build output directory
- waves/        : VCD waveform output directory
- Makefile      : Build and run automation

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/simulation_workspace.py
"""

import cmt2.jit as jit

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_demo_circuit():
    """Create a simple counter circuit for demonstration."""
    clear_stl_registry()

    circuit = Circuit("CounterDemo")

    # Use STL Reg module
    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)

    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers
        count_reg = m.instance(reg32, "count", clk=clk, rst=rst)
        running_reg = m.instance(reg1, "running", clk=clk, rst=rst)

        @jit.method(m)
        def start(meth) -> None:
            with meth.guard:
                meth.returns(meth.not_(running_reg.read))
            with meth.body:
                running_reg.write(meth.const(1, 1))

        @jit.method(m)
        def stop(meth) -> None:
            with meth.guard:
                meth.returns(running_reg.read)
            with meth.body:
                running_reg.write(meth.const(0, 1))

        @jit.method(m)
        def reset_count(meth) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                count_reg.write(meth.const(0, 32))

        # Rule: increment - Increment counter when running
        with jit.rule(m, "increment") as rule:
            with rule.guard as g:
                g.returns(running_reg.read)
            with rule.body as body:
                count_reg.next = count_reg.read + 1

        @jit.value(m)
        def get_count(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(count_reg.read)

        @jit.value(m)
        def is_running(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(running_reg.read)

    return circuit


def create_counter_testbench(circuit):
    """Create testbench using DSL for counter demonstration."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("get_count_res0", 0, "Count should be 0 after reset")
        seq.expect("is_running_res0", 0, "Should not be running after reset")
        seq.print("Reset test passed - count=0, is_running=0")

    # =========================================================================
    # Test Sequence: Start/Stop Counting
    # =========================================================================
    with tb.sequence("test_start_stop") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Start counting, wait, then stop")
        seq.comment("=" * 60)
        seq.reset(5)

        # Start the counter
        seq.comment("Starting counter...")
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        seq.expect("is_running_res0", 1, "Should be running after start")

        # Wait for some cycles and observe counting
        seq.comment("Counting for 10 cycles...")
        seq.record_cycle("count_start")
        for i in range(10):
            seq.wait(1)
            seq.print("count=", "get_count_res0")

        seq.record_cycle("count_end")

        # Stop the counter
        seq.comment("Stopping counter...")
        seq.drive("stop_enable", 1)
        seq.wait(1)
        seq.drive("stop_enable", 0)

        seq.expect("is_running_res0", 0, "Should not be running after stop")
        seq.print("Final count = ", "get_count_res0")
        seq.print_cycle_diff("count_start", "count_end", "Counting duration")
        seq.print("Start/Stop test PASSED")

    # =========================================================================
    # Test Sequence: Reset Count Method
    # =========================================================================
    with tb.sequence("test_reset_count") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Reset count method")
        seq.comment("=" * 60)
        seq.reset(5)

        # Start counting
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Count for a few cycles
        seq.wait(5)
        seq.print("Count before reset: ", "get_count_res0")

        # Reset the count
        seq.comment("Resetting count...")
        seq.drive("reset_count_enable", 1)
        seq.wait(1)
        seq.drive("reset_count_enable", 0)

        seq.expect("get_count_res0", 0, "Count should be 0 after reset_count")
        seq.print("Count after reset: ", "get_count_res0")

        # Should still be running
        seq.expect("is_running_res0", 1, "Should still be running after reset_count")
        seq.print("Reset count test PASSED")

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification")
        seq.comment("=" * 60)
        seq.reset(5)

        # Increment rule should not fire when not running
        seq.wait(1)
        seq.comment("Before start, increment should not fire")
        seq.expect_rule_fired("increment", False)
        seq.print_rule_status("increment")

        # Start counting
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Increment rule should fire when running
        seq.wait(1)
        seq.comment("After start, increment should fire")
        seq.print_rule_status("increment")

        # Monitor for a few cycles
        for i in range(5):
            seq.wait(1)
            seq.print_rule_status("increment")

        seq.print("Debug port verification completed")

    return tb


def main():
    print("=" * 70)
    print("Simulation Workspace Generation - Using Testbench DSL")
    print("=" * 70)

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "sim_workspace_demo"

    # Clean previous workspace
    if workspace_dir.exists():
        print(f"\nRemoving existing workspace: {workspace_dir}")
        shutil.rmtree(workspace_dir)

    # Create circuit
    print("\n1. Creating Counter circuit...")
    circuit = create_demo_circuit()

    # Create testbench using DSL
    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_counter_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports
    print("\n3. Setting up simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, workspace_dir, debug_ports=True)

    # Generate the workspace with testbench
    print("\n4. Generating workspace with Testbench DSL...")
    ws.generate_with_testbench(tb)
    print(f"   Workspace generated at: {workspace_dir}")

    # Show the generated structure
    print("\n5. Generated workspace structure:")
    print("-" * 70)

    def show_tree(path, prefix=""):
        items = sorted(path.iterdir(), key=lambda x: (x.is_file(), x.name))
        for i, item in enumerate(items):
            is_last = i == len(items) - 1
            connector = "└── " if is_last else "├── "
            print(f"{prefix}{connector}{item.name}")
            if item.is_dir() and item.name not in ["build", "waves"]:
                extension = "    " if is_last else "│   "
                show_tree(item, prefix + extension)

    show_tree(workspace_dir)
    print("-" * 70)

    # Build simulation
    print("\n6. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    # Run simulation
    print("\n7. Running simulation...")
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    # Summary
    print("\n" + "=" * 70)
    print("Workspace generated and tests passed!")
    print("=" * 70)
    print(f"""
Workspace location: {workspace_dir}
Waveforms: {workspace_dir / 'waves' / 'Counter.vcd'}

To rebuild and run manually:
   cd {workspace_dir}
   make        # Build the simulation
   make run    # Run the simulation
   make waves  # View waveforms (requires GTKWave)
""")

    return 0


if __name__ == "__main__":
    sys.exit(main())
