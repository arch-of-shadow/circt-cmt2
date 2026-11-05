#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Test MultiCycleALU with Verilator Simulation using the PyCMT2 Testbench DSL.

This is a **Cmt2 JIT** example (stacked on PyCMT2):
- JIT (`cmt2.jit`) provides the rule/method/value syntax used to build the design.
- PyCMT2 (`circt.pycmt2`) provides MLIR lowering, Verilog generation, simulation
  workspace generation, and the Testbench DSL.

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/alu.py
"""

import cmt2.jit as jit

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, clear_stl_registry


@jit.elaborate
def create_alu_circuit():
    """Create the MultiCycleALU circuit using STL Reg modules."""
    # Clear any previous STL registry entries
    clear_stl_registry()

    circuit = Circuit("ProcCircuit")

    # Create register modules using STL
    reg32_mod = Reg.create(circuit, 32)
    reg1_mod = Reg.create(circuit, 1)

    with jit.module(circuit, "MultiCycleALU") as alu:
        clk = alu.clock("clk")
        rst = alu.reset("rst")

        # Internal state registers using STL Reg modules
        reg_a = alu.instance(reg32_mod, clk=clk, rst=rst)
        reg_b = alu.instance(reg32_mod, clk=clk, rst=rst)
        reg_result = alu.instance(reg32_mod, clk=clk, rst=rst)
        busy_reg = alu.instance(reg1_mod, clk=clk, rst=rst)

        @jit.method(alu)
        def start(meth, a: UInt[32], b: UInt[32]) -> None:
            with meth.guard:
                meth.returns(meth.not_(busy_reg.read))
            with meth.body:
                reg_a.write(a)
                reg_b.write(b)
                busy_reg.write(meth.const(1, 1))

        @jit.rule(alu)
        def compute(rule):
            with rule.guard:
                rule.returns(busy_reg.read)
            with rule.body:
                result = reg_a.read + reg_b.read
                reg_result.write(result)
                busy_reg.write(rule.const(0, 1))

        @jit.value(alu)
        def get_result(val) -> UInt[32]:
            with val.guard:
                val.returns(val.not_(busy_reg.read))
            with val.body:
                val.returns(reg_result.read)

        @jit.value(alu)
        def is_ready(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(val.not_(busy_reg.read))

    return circuit


def create_alu_testbench(circuit):
    """Create testbench using DSL for MultiCycleALU."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # Test cases: (a, b, expected_sum)
    test_cases = [
        (10, 20, 30),
        (100, 200, 300),
        (0, 0, 0),
        (0xFFFFFFFF, 1, 0),  # Overflow (wraps to 0)
        (1234, 5678, 6912),
        (42, 0, 42),
    ]

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("is_ready_res0", 1, "ALU should be ready after reset")
        seq.print("Reset test passed - ALU is ready")

    # =========================================================================
    # Test Sequences: ALU Operations
    # =========================================================================
    for i, (a, b, expected) in enumerate(test_cases):
        with tb.sequence(f"test_alu_{i+1}") as seq:
            seq.comment(f"Test: {a} + {b} = {expected}")
            seq.reset(5)

            # Wait for ready
            seq.wait_condition("dut->start_ready", timeout=10)

            # Start computation
            seq.comment(f"Start computation: {a} + {b}")
            seq.drive("start_a", a)
            seq.drive("start_b", b)
            seq.drive("start_enable", 1)
            seq.wait(1)
            seq.drive("start_enable", 0)

            # Wait for result to be ready
            seq.record_cycle(f"start_{i}")
            seq.wait_condition("dut->get_result_ready", timeout=10)
            seq.record_cycle(f"end_{i}")

            # Verify result
            seq.expect("get_result_res0", expected, f"{a} + {b} = {expected}")
            seq.print_cycle_diff(f"start_{i}", f"end_{i}", f"Test {i+1} latency")
            seq.print(f"Test {i+1}: {a} + {b} = ", "get_result_res0")

            seq.wait(1)

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification")
        seq.comment("=" * 60)
        seq.reset(5)

        # Compute rule should NOT fire when idle
        seq.wait(1)
        seq.comment("When idle, compute rule should not fire")
        seq.expect_rule_fired("compute", False)
        seq.print_rule_status("compute")

        # Start a computation
        seq.drive("start_a", 5)
        seq.drive("start_b", 3)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Compute rule should fire when busy
        seq.wait(1)
        seq.comment("When busy, compute rule should fire")
        seq.print_rule_status("compute")

        # Wait for completion
        seq.wait_condition("dut->get_result_ready", timeout=10)
        seq.expect("get_result_res0", 8, "5 + 3 = 8")

        # After completion, compute should not fire
        seq.wait(1)
        seq.comment("After completion, compute should not fire")
        seq.expect_rule_fired("compute", False)
        seq.print_rule_status("compute")

        seq.print("Debug port verification PASSED")

    return tb


def main():
    print("=" * 60)
    print("MultiCycleALU Simulation Test (using Testbench DSL)")
    print("=" * 60)

    # Setup paths
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_alu"

    # Clean previous simulation
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    # Create circuit
    print("\n1. Creating MultiCycleALU circuit with STL Reg modules...")
    circuit = create_alu_circuit()

    # Create testbench using DSL
    print("2. Creating testbench using Testbench DSL...")
    tb = create_alu_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports
    print("3. Setting up simulation workspace...")
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    # Generate workspace with testbench
    ws.generate_with_testbench(tb)

    print("4. Workspace generated at:", sim_dir)

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
    print(f"Waveforms available at: {sim_dir / 'waves' / 'MultiCycleALU.vcd'}")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
