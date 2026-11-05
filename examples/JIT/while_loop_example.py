#!/usr/bin/env python3
"""while_loop_example.py - Demonstrates proc.while with condition region using Testbench DSL

This example shows the new ProcWhileOp design where the loop condition
is computed in a dedicated region that supports cmt2.call operations.

Features demonstrated:
1. MLIR generation with proc.while condition region
2. Verilog generation via CMT2 passes
3. Verilator simulation with Testbench DSL
4. Output verification

Expected behavior:
- Counter starts at 0
- While counter < 5, increment counter
- After 5 iterations, counter = 5

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/while_loop_example.py
"""

import cmt2.jit as jit

import os
import sys
import shutil
from pathlib import Path

# Add circt Python packages to path
build_dir = os.path.dirname(os.path.abspath(__file__))
while build_dir and not os.path.exists(os.path.join(build_dir, "build")):
    build_dir = os.path.dirname(build_dir)
if build_dir:
    sys.path.insert(0, os.path.join(build_dir, "build/tools/circt/python_packages/circt_core"))

from circt.pycmt2.circuit import Circuit
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.types import UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_while_loop_circuit():
    """Create a circuit demonstrating while loop with condition region."""
    clear_stl_registry()

    width = 8
    circuit = Circuit("WhileLoopDemo")
    reg_mod = Reg.create(circuit, width)
    reg1_mod = Reg.create(circuit, 1)

    with jit.module(circuit, "Counter") as counter:
        clk = counter.clock("clk")
        rst = counter.reset("rst")

        # Counter register
        cnt = counter.instance(reg_mod, clk=clk, rst=rst)

        # Done flag register
        done_reg = counter.instance(reg1_mod, clk=clk, rst=rst)

        @jit.value(counter)
        def count(count_val) -> UInt[width]:
            with count_val.guard:
                count_val.always()
            with count_val.body:
                count_val.returns(cnt.read)

        @jit.value(counter)
        def is_done(done_val) -> UInt[1]:
            with done_val.guard:
                done_val.always()
            with done_val.body:
                done_val.returns(done_reg.read)

        # Step to increment counter
        with counter.step() as increment:
            val = cnt.read
            new_val = increment.add(val, increment.const(1, width))
            cnt.next = increment.bits(new_val, width - 1, 0)

        # Step to mark done
        with counter.step() as mark_done:
            done_reg.next = mark_done.const(1, 1)

        # Procedural rule with while loop
        # The condition function has access to cmt2.call
        with counter.proc_rule() as count_to_five:
            with count_to_five.guard as g:
                # Guard: not done yet
                d = done_reg.read
                not_done = g.not_(d)
                g.returns(not_done)

            with count_to_five.control() as ctrl:
                with ctrl.seq() as seq:
                    # While loop with condition region
                    # The condition function receives a builder with call capability
                    def loop_condition(b):
                        """Condition: counter < 5"""
                        val = b.call(cnt.instance, cnt.instance.read)
                        c5 = b.const(5, width)
                        return b.lt(val, c5)

                    with seq.while_(loop_condition) as loop:
                        loop.enable(increment.ref())

                    # After loop completes, mark done
                    seq.enable(mark_done.ref())

        # Precedence
        counter.precedence(count.ref(), is_done.ref(), count_to_five.ref())

    return circuit


def create_while_loop_testbench(circuit):
    """Create testbench using DSL for while loop example."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("count_res0", 0, "Counter should be 0 after reset")
        seq.expect("is_done_res0", 0, "Should not be done after reset")
        seq.print("Reset test passed - counter=0, is_done=0")

    # =========================================================================
    # Test Sequence: While Loop Counting
    # =========================================================================
    with tb.sequence("test_while_loop") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: While loop counts from 0 to 5")
        seq.comment("=" * 60)
        seq.reset(5)

        # Monitor counter progression
        seq.comment("Monitoring counter progression...")
        seq.record_cycle("loop_start")

        # The while loop should count: 0->1->2->3->4->5 then done
        for i in range(20):  # Max 20 cycles
            seq.wait(1)
            seq.print("count=", "count_res0")

        # Wait for done flag
        seq.wait_condition("dut->is_done_res0", timeout=30)
        seq.record_cycle("loop_end")

        # Verify final state
        seq.expect("count_res0", 5, "Counter should reach 5")
        seq.expect("is_done_res0", 1, "Should be done after counting")

        seq.print_cycle_diff("loop_start", "loop_end", "While loop total cycles")
        seq.print("While loop test PASSED - counter reached 5 and is_done=1")

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification")
        seq.comment("=" * 60)
        seq.reset(5)

        # Before rule fires, count_to_five should not fire when already done
        seq.comment("Observing rule firing during count...")
        for i in range(8):
            seq.wait(1)
            seq.print_rule_status("count_to_five_state0")

        # Wait for completion
        seq.wait_condition("dut->is_done_res0", timeout=30)
        seq.wait(2)

        # After done, rule should not fire
        seq.comment("After done, rule should not fire")
        seq.expect_rule_fired("count_to_five_state0", False)
        seq.print_rule_status("count_to_five_state0")

        seq.print("Debug port verification completed")

    # =========================================================================
    # Test Sequence: Stability Check
    # =========================================================================
    with tb.sequence("test_stability") as seq:
        seq.comment("=" * 60)
        seq.comment("Stability Check: Counter stays at 5 after done")
        seq.comment("=" * 60)
        seq.reset(5)

        # Wait for done
        seq.wait_condition("dut->is_done_res0", timeout=30)

        # Verify counter stays stable at 5
        seq.comment("Checking counter stability...")
        for i in range(5):
            seq.wait(1)
            seq.expect("count_res0", 5, "Counter should stay at 5")
            seq.expect("is_done_res0", 1, "Should stay done")

        seq.print("Stability test PASSED - counter stable at 5")

    return tb


def main():
    print("=" * 60)
    print("While Loop Example - Using Testbench DSL")
    print("=" * 60)
    print("""
This example demonstrates:
1. proc.while with condition region (cmt2.call in condition)
2. MLIR generation and verification
3. Verilog generation via SimulationWorkspace
4. Verilator simulation with Testbench DSL
5. Output verification (counter should reach 5)
""")

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "while_loop_workspace"

    # Clean previous workspace
    if workspace_dir.exists():
        shutil.rmtree(workspace_dir)

    # Create circuit
    print("Creating while loop circuit...")
    circuit = create_while_loop_circuit()

    # Show MLIR excerpt
    print("\n" + "=" * 60)
    print("Step 1: MLIR Generation")
    print("=" * 60)
    mlir = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir)} chars")

    # Show the while loop structure
    print("\n   While loop structure in MLIR:")
    lines = mlir.split('\n')
    in_while = False
    for line in lines:
        if 'cmt2.proc.while' in line:
            in_while = True
        if in_while:
            print(f"      {line}")
            if 'cmt2.proc.yield' in line:
                in_while = False

    # Create testbench using DSL
    print("\n" + "=" * 60)
    print("Step 2: Creating Testbench DSL")
    print("=" * 60)
    tb = create_while_loop_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports
    print("\n" + "=" * 60)
    print("Step 3: Simulation Workspace Generation")
    print("=" * 60)
    ws = SimulationWorkspace(circuit, workspace_dir, debug_ports=True)

    # Generate workspace with testbench
    print("   Generating workspace with testbench...")
    ws.generate_with_testbench(tb)
    print(f"   Workspace generated at: {workspace_dir}")

    # Build simulation
    print("\n" + "=" * 60)
    print("Step 4: Building Simulation")
    print("=" * 60)
    if not ws.build():
        print("   FAIL: Build failed")
        return 1
    print("   Build successful!")

    # Run simulation
    print("\n" + "=" * 60)
    print("Step 5: Running Simulation")
    print("=" * 60)
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation FAILED!")
        return 1

    # Summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    print(f"   Workspace: {workspace_dir}")
    print(f"   RTL files: {workspace_dir / 'rtl'}")
    print(f"   Waveforms: {workspace_dir / 'waves' / 'Counter.vcd'}")
    print("\n   Status: ALL TESTS PASSED")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
