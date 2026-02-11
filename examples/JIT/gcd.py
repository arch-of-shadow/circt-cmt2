#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
GCD Example with Verilator Simulation using the PyCMT2 Testbench DSL.

This is a **Cmt2 JIT** example (stacked on PyCMT2): the design is written with
`cmt2.jit`, while simulation/testbench is provided by `circt.pycmt2`.

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/gcd.py
"""

import cmt2.jit as jit

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, clear_stl_registry


@jit.elaborate
def create_gcd_circuit():
    """Create the GCD circuit using STL Reg modules."""
    # Clear any previous STL registry entries
    clear_stl_registry()

    circuit = Circuit("GCD")

    # Create 32-bit register module using STL
    reg_mod = Reg.create(circuit, 32)

    with jit.module(circuit, "GCD") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers using STL Reg module
        reg_a = m.instance(reg_mod, clk=clk, rst=rst)
        reg_b = m.instance(reg_mod, clk=clk, rst=rst)

        @jit.method(m)
        def load(meth, a: UInt[32], b: UInt[32]) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                reg_a.write(a)
                reg_b.write(b)

        @jit.rule(m)
        def compute(rule):
            with rule.guard:
                rule.returns(reg_b.read != 0)

            with rule.body:
                a_val = reg_a.read
                b_val = reg_b.read
                a_gt_b = rule.gt(a_val, b_val)
                new_a = rule.sub(a_val, b_val)
                new_b = rule.sub(b_val, a_val)
                final_a = rule.mux(a_gt_b, new_a, a_val)
                final_b = rule.mux(a_gt_b, b_val, new_b)
                reg_a.write(final_a)
                reg_b.write(final_b)

        @jit.value(m)
        def result(val) -> UInt[32]:
            with val.guard:
                val.returns(reg_b.read == 0)
            with val.body:
                val.returns(reg_a.read)

    return circuit


def create_gcd_testbench(circuit):
    """Create a testbench using the Testbench DSL for GCD verification."""

    # Enable auto_debug_ports for rule firing observation
    tb = Testbench(circuit, auto_debug_ports=True)

    # Test cases: (a, b, expected_gcd)
    test_cases = [
        (48, 18, 6),
        (100, 80, 20),
        (17, 13, 1),
        (270, 192, 6),
        (12, 12, 12),
        (54, 24, 6),
        (35, 14, 7),
        (21, 14, 7),
    ]

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.comment("During reset, compute rule should not fire")
        seq.wait(1)
        seq.expect_rule_fired("compute", False)
        seq.print("Reset test passed - compute rule did not fire during reset")

    # =========================================================================
    # Test Sequences: GCD Computations
    # =========================================================================
    for i, (a, b, expected) in enumerate(test_cases):
        with tb.sequence(f"test_gcd_{i+1}") as seq:
            seq.comment(f"Test GCD({a}, {b}) = {expected}")
            seq.reset(5)

            # Load input values
            seq.comment(f"Load A={a}, B={b}")
            seq.drive("load_a", a)
            seq.drive("load_b", b)
            seq.drive("load_enable", 1)
            seq.wait(1)
            seq.drive("load_enable", 0)

            # Wait for result (result_ready becomes 1 when b == 0)
            seq.comment("Wait for GCD computation to complete")
            seq.wait_condition("dut->result_ready", timeout=100)
            seq.wait(1)

            # Check result
            seq.expect("result_res0", expected, f"GCD({a},{b}) should be {expected}")
            seq.print(f"GCD({a}, {b}) = ", "result_res0")

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification Test")
        seq.comment("Verify dbg_compute_firing shows rule execution")
        seq.comment("=" * 60)
        seq.reset(5)

        # Load values that will require multiple iterations
        seq.comment("Load GCD(48, 18) - requires multiple compute iterations")
        seq.drive("load_a", 48)
        seq.drive("load_b", 18)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)

        # Wait a few cycles and check compute is firing
        seq.wait(1)
        seq.comment("Check that compute rule is firing (b != 0)")
        seq.print_rule_status("compute")

        # Wait more and check again
        seq.wait(3)
        seq.print_rule_status("compute")

        # Wait for completion
        seq.wait_condition("dut->result_ready", timeout=50)
        seq.wait(1)

        # After result is ready (b == 0), compute should NOT fire
        seq.comment("After result ready, compute should NOT fire (b == 0)")
        seq.expect_rule_fired("compute", False)
        seq.print_rule_status("compute")

        seq.expect("result_res0", 6, "GCD(48,18)=6")
        seq.print("Debug port verification PASSED")

    # =========================================================================
    # Test Sequence: Stress Test with Debug Port Counting
    # =========================================================================
    with tb.sequence("test_stress") as seq:
        seq.comment("=" * 60)
        seq.comment("Stress Test: Multiple GCD computations")
        seq.comment("=" * 60)

        for i, (a, b, expected) in enumerate(test_cases[:3]):
            seq.comment(f"--- Iteration {i+1}: GCD({a}, {b}) ---")
            seq.reset(5)

            seq.drive("load_a", a)
            seq.drive("load_b", b)
            seq.drive("load_enable", 1)
            seq.wait(1)
            seq.drive("load_enable", 0)

            # Use unique labels per iteration to avoid redeclaration
            seq.record_cycle(f"start_{i}")
            seq.wait_condition("dut->result_ready", timeout=100)
            seq.record_cycle(f"end_{i}")

            seq.print_cycle_diff(f"start_{i}", f"end_{i}", f"GCD({a},{b}) cycles")
            seq.expect("result_res0", expected)
            seq.print(f"Result: ", "result_res0")

        seq.print("Stress test completed")

    return tb


def main():
    print("=" * 60)
    print("GCD Simulation Test (using PyCMT2 Testbench DSL)")
    print("=" * 60)

    # Setup paths
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_gcd"

    # Clean previous simulation
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    # Create circuit
    print("\n1. Creating GCD circuit with STL Reg modules...")
    circuit = create_gcd_circuit()

    # Create testbench using DSL
    print("2. Creating testbench using Testbench DSL...")
    tb = create_gcd_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports enabled
    print("3. Setting up simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    # Generate workspace with testbench
    print("4. Generating workspace with testbench...")
    ws.generate_with_testbench(tb)

    print(f"   Workspace generated at: {sim_dir}")

    # Build simulation
    print("5. Building simulation...")
    if not ws.build():
        return 1
    print("   Build successful!")

    # Run simulation
    print("6. Running simulation...")
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    print("\n" + "=" * 60)
    print("Simulation completed!")
    print(f"Waveforms available at: {sim_dir / 'waves' / 'GCD.vcd'}")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
