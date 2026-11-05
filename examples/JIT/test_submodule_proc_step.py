#!/usr/bin/env python3
"""
Test case for Issue 7: Submodule proc_rule triggered from static step.
(Using Testbench DSL)

This test verifies whether calling a submodule method from within a static step
correctly triggers the submodule's proc_rule.

Expected behavior:
1. Parent's proc_rule fires when triggered
2. Parent's static step calls submodule.start()
3. This sets submodule's busy_reg = 1
4. Submodule's proc_rule guard becomes true (busy_reg == 1)
5. Submodule's proc_rule fires and executes its control flow
6. Submodule computes result and clears busy_reg
7. Parent waits for submodule to complete, then collects result

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/test_submodule_proc_step.py
"""

import cmt2.jit as jit

import shutil
import sys
import os
from pathlib import Path

# Setup path
build_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
python_pkg_dir = os.path.join(build_dir, "build", "tools", "circt", "python_packages", "circt_core")
if os.path.exists(python_pkg_dir):
    sys.path.insert(0, python_pkg_dir)

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def create_submodule_testbench(circuit):
    """Create testbench using DSL for submodule proc step test.

    Test: call start(21), expect result = 42 (21 * 2)
    """
    tb = Testbench(circuit, auto_debug_ports=True)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("done_res0", 0, "Should not be done after reset")
        seq.print("Reset test passed")

    # =========================================================================
    # Test Sequence: Submodule Trigger Test
    # =========================================================================
    with tb.sequence("test_submodule_trigger") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Submodule proc_rule triggered from static step")
        seq.comment("=" * 60)
        seq.comment("Start(21) -> submodule computes 21 * 2 = 42")
        seq.reset(5)

        # Wait for start_ready
        seq.wait_condition("dut->start_ready", timeout=20)

        # Call start method with data=21
        seq.comment("Calling start(21)...")
        seq.record_cycle("start")
        seq.drive("start_data", 21)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Wait for completion
        seq.wait_condition("dut->done_res0", timeout=100)
        seq.record_cycle("end")

        # Verify result
        seq.expect("result_res0", 42, "21 * 2 = 42")

        seq.print_cycle_diff("start", "end", "Processing latency")
        seq.print("Result = ", "result_res0")
        seq.print("Submodule proc_rule trigger test PASSED")

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification")
        seq.comment("=" * 60)
        seq.reset(5)

        # Wait for start_ready
        seq.wait_condition("dut->start_ready", timeout=20)

        # Start processing
        seq.drive("start_data", 10)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Monitor rule firing
        seq.comment("Monitoring rules during processing...")
        for i in range(15):
            seq.wait(1)
            seq.print_rule_status("process_state0")

        # Wait for completion
        seq.wait_condition("dut->done_res0", timeout=100)

        seq.print("Result = ", "result_res0")
        seq.print("Debug port verification completed")

    return tb


def main():
    print("=" * 70)
    print("Submodule Proc Step Test - Using Testbench DSL")
    print("=" * 70)
    print("""
This test verifies that calling a submodule method from within a static step
correctly triggers the submodule's proc_rule.

Test: start(21) -> submodule computes 21 * 2 = 42
""")

    # Setup paths
    script_dir = Path(__file__).parent
    ws_dir = script_dir / "submodule_proc_step_sim"

    # Clean previous workspace
    if ws_dir.exists():
        shutil.rmtree(ws_dir)

    clear_stl_registry()

    print("1. Creating circuit with submodule...")
    circuit = create_circuit()

    # Generate MLIR
    mlir_str = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir_str)} chars")

    # Create testbench using DSL
    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_submodule_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports
    print("\n3. Setting up simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, ws_dir, debug_ports=True)

    # Generate workspace with testbench
    ws.generate_with_testbench(tb)
    print(f"   Workspace generated at: {ws_dir}")

    # Build simulation
    print("\n4. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    # Run simulation
    print("\n5. Running simulation...")
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    # Summary
    print("\n" + "=" * 70)
    print("Submodule Proc Step Test Complete!")
    print(f"Waveforms: {ws_dir / 'waves' / 'ParentModule.vcd'}")
    print("=" * 70)

    return 0


@jit.elaborate
def create_circuit():
    """Create the circuit with submodule proc_rule triggering."""
    circuit = Circuit("SubmoduleProcStepTest")
    reg1_mod = Reg.create(circuit, 1, init=0)
    reg8_mod = Reg.create(circuit, 8, init=0)

    # ========================================
    # Submodule with proc_rule
    # ========================================
    with jit.module(circuit, "Calculator") as calc_mod:
        clk = calc_mod.clock()
        rst = calc_mod.reset()

        input_reg = calc_mod.instance(reg8_mod, clk=clk, rst=rst)
        result_reg = calc_mod.instance(reg8_mod, clk=clk, rst=rst)
        busy_reg = calc_mod.instance(reg1_mod, clk=clk, rst=rst)

        @jit.method(calc_mod)
        def start(meth, data: UInt[8]) -> None:
            with meth.guard:
                meth.returns(meth.eq(busy_reg.read, meth.const(0, 1)))
            with meth.body:
                input_reg.write(data)
                busy_reg.write(meth.const(1, 1))

        @jit.value(calc_mod)
        def result(val) -> UInt[8]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(result_reg.read)

        @jit.value(calc_mod)
        def done(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(val.eq(busy_reg.read, val.const(0, 1)))

        # Static step: compute result (doubles input)
        with calc_mod.static_step(2) as compute:
            val = input_reg.read
            result = compute.add(val, val)  # input * 2
            result_reg.next = result

        # Static step: clear busy
        with calc_mod.static_step(1) as clear_busy:
            busy_reg.next = clear_busy.const(0, 1)

        # Proc rule: trigger computation when busy
        with calc_mod.proc_rule() as run_calc:
            with run_calc.guard as g:
                is_busy = busy_reg.read
                g.returns(is_busy)

            with run_calc.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(compute.ref())
                    seq.enable(clear_busy.ref())

    # ========================================
    # Parent module that calls submodule from static step
    # ========================================
    with jit.module(circuit, "ParentModule") as parent_mod:
        clk = parent_mod.clock()
        rst = parent_mod.reset()

        # State
        data_reg = parent_mod.instance(reg8_mod, clk=clk, rst=rst)
        started_reg = parent_mod.instance(reg1_mod, clk=clk, rst=rst)
        done_reg = parent_mod.instance(reg1_mod, clk=clk, rst=rst)
        result_reg = parent_mod.instance(reg8_mod, clk=clk, rst=rst)

        # Submodule instance
        calc = parent_mod.instance(calc_mod, clk=clk, rst=rst)

        # Static step: call submodule.start() - THIS IS THE KEY TEST
        # Issue 7 claims this pattern may not work correctly
        with parent_mod.static_step(1) as trigger_submodule:
            """
            This step calls calc.start() which sets calc.busy_reg = 1.
            This should trigger calc.run_calc proc_rule to fire.
            """
            data = data_reg.read
            calc.start(data)

        # Static step: collect result
        with parent_mod.static_step(1) as collect_result:
            result = calc.result
            result_reg.next = result
            done_reg.next = collect_result.const(1, 1)

        @jit.method(parent_mod)
        def start(meth, data: UInt[8]) -> None:
            with meth.guard:
                meth.returns(meth.eq(started_reg.read, meth.const(0, 1)))
            with meth.body:
                data_reg.write(data)
                started_reg.write(meth.const(1, 1))
                done_reg.write(meth.const(0, 1))

        @jit.value(parent_mod)
        def result(val) -> UInt[8]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(result_reg.read)

        @jit.value(parent_mod)
        def done(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(done_reg.read)

        # Static step: no-op wait step
        with parent_mod.static_step(1) as wait_step:
            """
            No-op step used for waiting. Just passes time.
            """
            pass  # Nothing to do, just wait one cycle

        # Proc rule: processing pipeline
        with parent_mod.proc_rule() as process:
            """
            When started:
            1. Trigger submodule (calls calc.start)
            2. Wait for calc to complete (using while_ with condition function)
            3. Collect result
            """
            with process.guard as g:
                is_started = started_reg.read
                g.returns(is_started)

            with process.control() as ctrl:
                with ctrl.seq() as seq:
                    # Step 1: Trigger submodule
                    seq.enable(trigger_submodule.ref())

                    # Step 2: Wait for submodule to complete
                    # Using while_ with condition function (since submodule completion
                    # depends on its proc_rule firing)
                    def wait_for_calc(b):
                        calc_done = b.call(calc.instance, calc.instance.done)
                        not_done = b.eq(calc_done, b.const(0, 1))
                        return not_done

                    with seq.while_(wait_for_calc) as loop:
                        loop.enable(wait_step.ref())

                    # Step 3: Collect result and mark done
                    seq.enable(collect_result.ref())

        # Rule: clear started after done
        with jit.rule(parent_mod) as clear_started:
            with clear_started.guard as g:
                is_started = started_reg.read
                is_done = done_reg.read
                should_clear = g.and_(is_started, is_done)
                g.returns(should_clear)
            with clear_started.body as b:
                started_reg.next = b.const(0, 1)

    return circuit


if __name__ == "__main__":
    sys.exit(main())
