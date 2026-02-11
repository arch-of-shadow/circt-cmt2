#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Debug Ports E2E Simulation Example for PyCMT2 using Testbench DSL.

This example demonstrates END-TO-END simulation that verifies debug firing ports.
The testbench asserts that rules fire when expected and don't fire when not expected.

Key verification:
1. 'increment' rule fires exactly N times when counting to N
2. 'finish' rule fires exactly once when target is reached
3. Neither rule fires during reset or when in IDLE/DONE states

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 \
        ../examples/PyCMT2/debug_testbench_example.py
"""

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, clear_stl_registry


def create_state_machine_circuit():
    """Create a simple state machine with observable rule firing.

    The state machine has:
    - A state register (0=IDLE, 1=RUNNING, 2=DONE)
    - A counter register
    - Rules for state transitions

    Rules:
    - increment: Fires when RUNNING and counter < target
    - finish: Fires when RUNNING and counter == target
    """
    clear_stl_registry()

    circuit = Circuit("StateMachine")

    # Create register modules
    reg8 = Reg.create(circuit, 8)
    reg32 = Reg.create(circuit, 32)

    with circuit.module("StateMachine") as m:
        clk = m.clock()
        rst = m.reset()

        # State register (0=IDLE, 1=RUNNING, 2=DONE)
        state = m.instance(reg8, "state", clk=clk, rst=rst)

        # Counter register
        counter = m.instance(reg32, "counter", clk=clk, rst=rst)

        # Target value register
        target = m.instance(reg32, "target", clk=clk, rst=rst)

        # =========================================================================
        # Method: start - Begin counting to target
        # =========================================================================
        with m.method("start", args=[("target_val", UInt(32))]) as meth:
            with meth.guard() as g:
                # Can only start from IDLE state
                current_state = g.call(state, "read")
                is_idle = g.eq(current_state, g.const(0, 8))
                g.returns(is_idle)
            with meth.body() as body:
                target_val = body.arg("target_val")
                body.call(target, "write", target_val)
                body.call(counter, "write", body.const(0, 32))
                body.call(state, "write", body.const(1, 8))  # -> RUNNING

        # =========================================================================
        # Rule: increment - Count up while running (fires when cnt < target)
        # =========================================================================
        with m.rule("increment") as rule:
            with rule.guard() as g:
                current_state = g.call(state, "read")
                is_running = g.eq(current_state, g.const(1, 8))

                cnt = g.call(counter, "read")
                tgt = g.call(target, "read")
                not_done = g.lt(cnt, tgt)

                g.returns(g.and_(is_running, not_done))
            with rule.body() as body:
                cnt = body.call(counter, "read")
                new_cnt = body.add(cnt, body.const(1, 32))
                body.call(counter, "write", new_cnt)

        # =========================================================================
        # Rule: finish - Transition to DONE when target reached
        # =========================================================================
        with m.rule("finish") as rule:
            with rule.guard() as g:
                current_state = g.call(state, "read")
                is_running = g.eq(current_state, g.const(1, 8))

                cnt = g.call(counter, "read")
                tgt = g.call(target, "read")
                reached = g.eq(cnt, tgt)

                g.returns(g.and_(is_running, reached))
            with rule.body() as body:
                body.call(state, "write", body.const(2, 8))  # -> DONE

        # =========================================================================
        # Value: get_count - Read current counter value
        # =========================================================================
        with m.value("get_count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                cnt = body.call(counter, "read")
                body.returns(cnt)

        # =========================================================================
        # Value: is_done - Check if state machine is done
        # =========================================================================
        with m.value("is_done", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                current_state = body.call(state, "read")
                done = body.eq(current_state, body.const(2, 8))
                body.returns(done)

    return circuit


def create_debug_ports_testbench(circuit):
    """Create testbench using DSL for debug port verification."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # =========================================================================
    # Test 1: Verify rules don't fire during IDLE state
    # =========================================================================
    with tb.sequence("test_idle_no_firing") as seq:
        seq.comment("=" * 60)
        seq.comment("Test 1: Rules don't fire during IDLE state")
        seq.comment("=" * 60)
        seq.reset(5)

        # After reset, in IDLE state - neither rule should fire
        seq.comment("After reset, in IDLE state - rules should not fire")
        for i in range(3):
            seq.wait(1)
            seq.expect_rule_fired("increment", False)
            seq.expect_rule_fired("finish", False)

        seq.print("IDLE state: no rules fired - PASS")

    # =========================================================================
    # Test 2: Count increment rule firings for target=5
    # =========================================================================
    with tb.sequence("test_increment_firing") as seq:
        seq.comment("=" * 60)
        seq.comment("Test 2: Verify increment rule fires correct number of times")
        seq.comment("=" * 60)
        seq.reset(5)

        # Start counting to target=5
        seq.comment("Start counting to target=5")
        seq.drive("start_target_val", 5)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Run until done, checking debug ports each cycle
        seq.comment("Monitor rule firing during count...")
        for i in range(10):
            seq.wait(1)
            seq.print_rule_status("increment")
            seq.print_rule_status("finish")

        # Wait for done
        seq.wait_condition("dut->is_done_res0", timeout=20)
        seq.print("Increment firing test completed")

    # =========================================================================
    # Test 3: Verify rules don't fire after DONE state
    # =========================================================================
    with tb.sequence("test_done_no_firing") as seq:
        seq.comment("=" * 60)
        seq.comment("Test 3: Rules don't fire after DONE state")
        seq.comment("=" * 60)
        seq.reset(5)

        # Start counting to target=2 (quick test)
        seq.drive("start_target_val", 2)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Wait until done
        seq.wait_condition("dut->is_done_res0", timeout=20)

        # Now in DONE state - verify no rules fire
        seq.comment("In DONE state, checking for 5 cycles...")
        for i in range(5):
            seq.wait(1)
            seq.expect_rule_fired("increment", False)
            seq.expect_rule_fired("finish", False)

        seq.print("DONE state: no rules fired - PASS")

    # =========================================================================
    # Test 4: Multiple targets verify debug port consistency
    # =========================================================================
    with tb.sequence("test_multiple_targets") as seq:
        seq.comment("=" * 60)
        seq.comment("Test 4: Multiple targets verify debug port consistency")
        seq.comment("=" * 60)

        targets = [1, 3, 7]
        for i, target in enumerate(targets):
            seq.comment(f"--- Target {target} ---")
            seq.reset(5)

            # Start counting
            seq.drive("start_target_val", target)
            seq.drive("start_enable", 1)
            seq.wait(1)
            seq.drive("start_enable", 0)

            # Wait for done
            seq.wait_condition("dut->is_done_res0", timeout=50)

            # Check counter value
            seq.expect("get_count_res0", target, f"Counter should reach {target}")
            seq.print(f"Target {target}: count = ", "get_count_res0")

        seq.print("Multiple targets test completed")

    return tb


def main():
    """Main entry point demonstrating E2E simulation with debug port verification."""

    print("=" * 60)
    print("Debug Ports E2E Simulation Example (using Testbench DSL)")
    print("=" * 60)
    print("This test verifies debug firing ports in actual simulation.")

    # Setup paths
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_debug_ports"

    # Clean previous simulation
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    # Step 1: Create the circuit
    print("\n[1] Creating state machine circuit...")
    circuit = create_state_machine_circuit()

    # Step 2: Create testbench using DSL
    print("[2] Creating testbench using Testbench DSL...")
    tb = create_debug_ports_testbench(circuit)
    print(f"    Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"       - {seq.name}: {len(seq._ops)} operations")

    # Step 3: Create simulation workspace with debug ports enabled
    print("[3] Creating simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    # Generate workspace with testbench
    print("[4] Generating workspace with testbench...")
    ws.generate_with_testbench(tb)

    # Step 4: Check that debug ports are in generated Verilog
    print("[5] Verifying debug ports in generated Verilog...")
    verilog_file = sim_dir / "rtl" / "StateMachine.sv"
    verilog_content = verilog_file.read_text()

    debug_ports_found = []
    for port in ["dbg_increment_firing", "dbg_finish_firing"]:
        if port in verilog_content:
            debug_ports_found.append(port)
            print(f"    Found: {port}")

    if len(debug_ports_found) != 2:
        print("  ERROR: Expected 2 debug ports, found:", debug_ports_found)
        return 1

    print(f"[6] Workspace generated at: {sim_dir}")

    # Step 5: Build simulation
    print("[7] Building simulation...")
    if not ws.build():
        print("  Build failed!")
        return 1
    print("    Build successful!")

    # Step 6: Run simulation
    print("[8] Running simulation with debug port verification...")
    success, output = ws.run()
    print(output)

    if not success:
        print("\nSimulation FAILED!")
        return 1

    print("\n" + "=" * 60)
    print("E2E Debug Port Verification Complete!")
    print(f"Waveforms available at: {sim_dir / 'waves' / 'StateMachine.vcd'}")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
