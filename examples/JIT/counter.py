#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Counter example using Cmt2 JIT (stacked on PyCMT2)

This example builds the design using **JIT** (`cmt2.jit`) and uses **PyCMT2**
(`circt.pycmt2`) for lowering, codegen, simulation workspace generation, and the
Testbench DSL.
It shows:
- Module creation with clock/reset ports
- Rule definitions with guards and bodies
- Value methods for reading state
- End-to-end compilation to MLIR, FIRRTL, and Verilog
- E2E simulation with Verilator

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/counter.py
"""

import cmt2.jit as jit

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_basic_counter():
    """Create a basic counter circuit for MLIR emission demonstration."""
    circuit = Circuit("Counter")

    with jit.module(circuit, "Counter") as m:
        # Ports
        clk = m.clock()
        rst = m.reset()
        enable = m.input("enable", UInt(1))

        # Increment rule - always fires (demonstrates rule structure)
        # Note: Using g.always() because CMT2 rules are IsolatedFromAbove
        # and can't directly access module ports. A real counter would
        # need to use a register interface method call in the guard.
        with jit.rule(m, "increment") as rule:
            with rule.guard as g:
                g.always()
            with rule.body as b:
                # Would increment register here
                pass

        # Read value method - always ready
        with jit.value(m, "read", returns=[UInt(32)]) as val:
            with val.guard as g:
                g.always()
            with val.body as b:
                b.returns(b.const(0, 32))

    return circuit


@jit.elaborate
def create_simulatable_counter():
    """Create a counter circuit with Reg for E2E simulation.

    This counter:
    - Has a 32-bit count register
    - Increments on every cycle
    - Provides read access to the count value via a value method
    """
    clear_stl_registry()
    circuit = Circuit("CounterSim")

    with jit.module(circuit, "SimCounter") as m:
        clk = m.clock()
        rst = m.reset()

        # Create a 32-bit register for the count value
        count = m.instance(Reg.create(circuit, 32), "count", clk=clk, rst=rst)

        # Increment rule - always enabled, increments count
        with jit.rule(m, "increment") as rule:
            with rule.guard as g:
                g.always()
            with rule.body as b:
                val = b.call(count, "read")
                next_val = b.add(val, b.const(1, 32))
                b.call(count, "write", next_val)

        # Value method to expose the count value as a port
        with jit.value(m, "get_count", returns=[UInt(32)]) as val:
            with val.guard as g:
                g.always()
            with val.body as b:
                cnt = b.call(count, "read")
                b.returns(cnt)

    return circuit


def create_counter_testbench(circuit):
    """Create testbench for the counter simulation.

    Tests:
    1. Reset behavior - counter should start at 0
    2. Increment behavior - counter should increment each cycle

    The counter exposes a value method `get_count` which becomes port `get_count_res0`.
    """
    tb = Testbench(circuit, auto_debug_ports=False)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify counter starts at 0 after reset")
        seq.reset(5)
        seq.expect("get_count_res0", 0, "Counter should be 0 after reset")
        seq.print("Reset test passed")

    # =========================================================================
    # Test Sequence: Increment Test
    # =========================================================================
    with tb.sequence("test_increment") as seq:
        seq.comment("Test: Verify counter increments each cycle")
        seq.reset(5)

        # Verify initial value
        seq.expect("get_count_res0", 0, "Counter should be 0 after reset")

        # Wait a few cycles and check increments
        for i in range(1, 6):
            seq.wait(1)
            seq.expect("get_count_res0", i, f"Counter should be {i} after {i} cycles")

        seq.print("Increment test passed: count=", "get_count_res0")

    # =========================================================================
    # Test Sequence: Longer Run Test
    # =========================================================================
    with tb.sequence("test_long_run") as seq:
        seq.comment("Test: Verify counter works over longer period")
        seq.reset(5)

        # Run for 20 cycles
        seq.record_cycle("start")
        seq.wait(20)
        seq.record_cycle("end")

        seq.expect("get_count_res0", 20, "Counter should be 20 after 20 cycles")
        seq.print_cycle_diff("start", "end", "Long run duration")
        seq.print("Long run test passed: count=", "get_count_res0")

    return tb


def run_simulation():
    """Run E2E simulation for the counter."""
    print("\n" + "=" * 60)
    print("E2E Simulation: Counter")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_counter"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("\n1. Creating counter circuit...")
    circuit = create_simulatable_counter()

    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_counter_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    print("\n3. Setting up simulation workspace...")
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    print("\n4. Generating workspace with testbench...")
    ws.generate_with_testbench(tb)
    print(f"   Workspace: {sim_dir}")

    print("\n5. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    print("\n6. Running simulation...")
    success, output = ws.run()
    print(output)

    print("\n" + "=" * 60)
    if success:
        print("E2E Simulation PASSED!")
    else:
        print("E2E Simulation FAILED!")
    print("=" * 60)

    return 0 if success else 1


def main():
    """Run counter example with MLIR emission and E2E simulation."""
    print("=" * 60)
    print("JIT Counter Example (stacked on PyCMT2)")
    print("=" * 60)

    # Create and show basic counter MLIR
    circuit = create_basic_counter()

    # Emit CMT2 MLIR
    print("\n=== CMT2 MLIR ===\n")
    print(circuit.emit_mlir())

    # Try to emit FIRRTL (may fail if passes not available)
    print("\n=== FIRRTL (if available) ===\n")
    try:
        print(circuit.emit_firrtl())
    except Exception as e:
        print(f"FIRRTL emission not available: {e}")

    # Try to emit Verilog
    print("\n=== Verilog (if available) ===\n")
    try:
        verilog = circuit.emit_verilog()
        if "Verilog generation failed" in verilog:
            print("Verilog emission failed.")
            print(verilog)
        else:
            print(verilog)
    except Exception as e:
        print(f"Verilog emission not available: {e}")

    # Run E2E simulation
    sim_result = run_simulation()

    print("\n" + "=" * 60)
    print("Counter example completed!")
    if sim_result == 0:
        print("E2E simulation passed!")
    else:
        print("E2E simulation failed!")
    print("=" * 60)

    return sim_result


if __name__ == "__main__":
    sys.exit(main())
