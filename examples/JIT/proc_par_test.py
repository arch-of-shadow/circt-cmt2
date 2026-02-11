#!/usr/bin/env python3
"""proc_par_test.py - Test for proc.par (parallel execution) fix using Testbench DSL

This example tests the proc.par implementation which was fixed to:
1. Enable all parallel branches simultaneously from fork state
2. Wait for ALL branches to complete (AND semantics) before joining

Expected behavior:
- step1: increments counter1 (0 -> 1)
- step2: increments counter2 (0 -> 1)
- Both steps run in parallel from the fork state
- After both complete, FSM transitions to done
- Final values: counter1=1, counter2=1, result=2 (sum)

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \\
      python3 ../examples/JIT/proc_par_test.py
"""

import cmt2.jit as jit

import shutil
from pathlib import Path

from circt.pycmt2.circuit import Circuit
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.types import UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_parallel_test(width: int = 32) -> Circuit:
    """Create a simple test for proc.par execution."""
    clear_stl_registry()

    circuit = Circuit("ProcParTest")
    reg_mod = Reg.create(circuit, width)
    reg1_mod = Reg.create(circuit, 1)

    with jit.module(circuit, "TestHarness") as harness:
        clk = harness.clock("clk")
        rst = harness.reset("rst")

        # Two counters - each will be incremented by its own step
        counter1 = harness.instance(reg_mod, clk=clk, rst=rst)
        counter2 = harness.instance(reg_mod, clk=clk, rst=rst)
        done_reg = harness.instance(reg1_mod, clk=clk, rst=rst)

        @jit.value(harness)
        def done(done_val) -> UInt[1]:
            with done_val.guard:
                done_val.always()
            with done_val.body:
                done_val.returns(done_reg.read)

        @jit.value(harness)
        def result(result_val) -> UInt[width]:
            with result_val.guard:
                result_val.always()
            with result_val.body:
                s = counter1.read + counter2.read
                result_val.returns(result_val.bits(s, width - 1, 0))

        # Step: incr1 - increment counter1
        with harness.step() as incr1:
            counter1.next = counter1.read + 1

        # Step: incr2 - increment counter2
        with harness.step() as incr2:
            counter2.next = counter2.read + 1

        # Step: mark_done
        with harness.step() as mark_done:
            done_reg.next = mark_done.const(1, 1)

        # Procedural rule: main with parallel execution
        with harness.proc_rule() as main:
            with main.guard as g:
                g.returns(~done_reg.read)

            with main.control() as ctrl:
                with ctrl.seq() as seq:
                    # Parallel: both steps run at the same time
                    with seq.par() as par:
                        par.enable(incr1.ref())
                        par.enable(incr2.ref())
                    # After both complete, mark done
                    seq.enable(mark_done.ref())

        harness.precedence(done._cmt2_ref, result._cmt2_ref, main.ref())

    return circuit


def create_parallel_testbench(circuit):
    """Create testbench using DSL for proc.par test."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("done_res0", 0, "Should not be done after reset")
        seq.expect("result_res0", 0, "Result should be 0 after reset")
        seq.print("Reset test passed - done=0, result=0")

    # =========================================================================
    # Test Sequence: Parallel Execution Test
    # =========================================================================
    with tb.sequence("test_parallel_execution") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Parallel execution (proc.par)")
        seq.comment("=" * 60)
        seq.comment("Both incr1 and incr2 should run in parallel")
        seq.comment("Expected: counter1=1, counter2=1, result=2")
        seq.reset(5)

        seq.record_cycle("start")

        # Monitor the parallel execution
        seq.comment("Monitoring parallel execution...")
        for i in range(10):
            seq.wait(1)
            seq.print("result=", "result_res0")

        # Wait for done
        seq.wait_condition("dut->done_res0", timeout=50)
        seq.record_cycle("end")

        # Verify result: counter1=1 + counter2=1 = 2
        seq.expect("result_res0", 2, "1 + 1 = 2 (parallel execution)")
        seq.expect("done_res0", 1, "Should be done")

        seq.print_cycle_diff("start", "end", "Parallel execution latency")
        seq.print("Final result = ", "result_res0")
        seq.print("Parallel execution test PASSED")

    # =========================================================================
    # Test Sequence: Result Stability Check
    # =========================================================================
    with tb.sequence("test_stability") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Result stability after completion")
        seq.comment("=" * 60)
        seq.reset(5)

        # Wait for done
        seq.wait_condition("dut->done_res0", timeout=50)

        # Verify result stays stable
        seq.comment("Checking result stability...")
        for i in range(5):
            seq.wait(1)
            seq.expect("result_res0", 2, "Result should stay at 2")
            seq.expect("done_res0", 1, "Should stay done")

        seq.print("Stability test PASSED - result stable at 2")

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification")
        seq.comment("=" * 60)
        seq.reset(5)

        # Monitor main rule firing during execution
        seq.comment("Monitoring main rule during execution...")
        for i in range(8):
            seq.wait(1)
            seq.print_rule_status("main_state0")

        # Wait for completion
        seq.wait_condition("dut->done_res0", timeout=50)
        seq.wait(2)

        # After completion, main should not fire
        seq.comment("After completion, main rule should not fire")
        seq.expect_rule_fired("main_state0", False)
        seq.print_rule_status("main_state0")

        seq.print("Debug port verification completed")

    return tb


def main():
    print("=" * 70)
    print("proc.par (Parallel Execution) Test - Using Testbench DSL")
    print("=" * 70)
    print("""
This test verifies that proc.par correctly:
1. Enables all parallel branches from the fork state
2. Waits for ALL branches to complete (AND semantics)
3. Transitions to the next state only after join

Test case:
- Two steps run in parallel: incr1 and incr2
- Each increments its own counter by 1
- Expected result: counter1=1 + counter2=1 = 2
""")

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "proc_par_test_workspace"

    # Clean previous workspace
    if workspace_dir.exists():
        shutil.rmtree(workspace_dir)

    print("1. Creating parallel test circuit...")
    circuit = create_parallel_test()

    # Emit MLIR
    print("\n2. Emitting MLIR...")
    mlir = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir)} chars")

    # Check for proc.par
    if "proc.par" in mlir:
        print("   [OK] Found proc.par in MLIR")
    else:
        print("   [ERROR] proc.par not found!")
        return 1

    # Create testbench using DSL
    print("\n3. Creating testbench using Testbench DSL...")
    tb = create_parallel_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports
    print("\n4. Setting up simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, workspace_dir, debug_ports=True)

    # Generate workspace with testbench
    ws.generate_with_testbench(tb)
    print(f"   Workspace generated at: {workspace_dir}")

    # Build simulation
    print("\n5. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    # Run simulation
    print("\n6. Running simulation...")
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    # Summary
    print("\n" + "=" * 70)
    print("Summary")
    print("=" * 70)
    print(f"   Workspace: {workspace_dir}")
    print(f"   Waveforms: {workspace_dir / 'waves' / 'TestHarness.vcd'}")
    print("\nE2E Simulation PASSED!")
    print("=" * 70)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
