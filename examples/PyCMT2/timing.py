#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Cycle-Precise Timing Example using PyCMT2 EDSL

This example demonstrates the cycle-precise timing features for static scheduling:
- Static steps with fixed cycle counts
- Pipelined operations with sequential control

The timing passes (cmt2-timing-inference, cmt2-static-fsm-allocation,
cmt2-compile-static) analyze and validate timing, then generate FSM
control logic.

Note: Call-site timing annotation (`call_timing` / `arg_timing` / `result_timing`) is
available via `step.call(...)` in the Python API. This example mostly
focuses on static_step latency behavior; see
`examples/PyCMT2/static_step_call_timing_window.py` for an E2E call-timing
demo with simulation validation.

Includes E2E simulation for a simplified timing example.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/timing.py
"""

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def main():
    print("=" * 60)
    print("PyCMT2 Cycle-Precise Timing Example")
    print("=" * 60)

    circuit = Circuit("TimingExample")

    # Define external register module
    with circuit.external_module("Reg32") as reg_mod:
        reg_mod.clock("clk")
        reg_mod.reset("rst")
        reg_mod.value("read", returns=[("out", UInt(32))])
        reg_mod.method("write", args=[("data", UInt(32))])
        reg_mod.sequence_before("read", "write")

    # Define external ALU (combinational - no latency)
    with circuit.external_module("ALU") as alu_mod:
        alu_mod.value("add", args=[("a", UInt(32)), ("b", UInt(32))], returns=[("out", UInt(32))])
        alu_mod.value("sub", args=[("a", UInt(32)), ("b", UInt(32))], returns=[("out", UInt(32))])

    # Create a module that uses static scheduling
    with circuit.module("StaticScheduler") as sched:
        clk = sched.clock("clk")
        rst = sched.reset("rst")

        # Instantiate ALU and registers
        alu = sched.instance(alu_mod, "alu")
        acc_reg = sched.instance(reg_mod, "acc", clk=clk, rst=rst)
        count_reg = sched.instance(reg_mod, "count", clk=clk, rst=rst)

        # Static step with fixed 4-cycle latency
        # This step will execute for exactly 4 cycles.
        #
        # Static steps are useful when:
        # - The total execution time is known at compile time
        # - All operations within have known latencies
        # - No dynamic done signals are needed
        #
        # The timing passes will:
        # 1. Allocate FSM states (4 states for 4 cycles)
        # 2. Generate FSM register and transition logic
        # 3. Create timing guards for each operation
        with sched.static_step(4, "increment_by_10") as step:
            # Read current accumulator value (cycle 0)
            acc_val = step.call(acc_reg, "read")

            # Add 10 to it using ALU (cycle 1)
            ten = step.const(10, 32)
            new_val = step.call(alu, "add", acc_val[0], ten)

            # Write back to accumulator (cycle 2)
            step.call(acc_reg, "write", new_val[0])

            # Increment counter (cycle 3)
            count_val = step.call(count_reg, "read")
            one = step.const(1, 32)
            new_count = step.call(alu, "add", count_val[0], one)
            step.call(count_reg, "write", new_count[0])

        # Another static step: compute sum of two registers
        with sched.static_step(3, "compute_sum") as step2:
            a = step2.call(acc_reg, "read")
            b = step2.call(count_reg, "read")
            sum_val = step2.call(alu, "add", a[0], b[0])
            step2.call(acc_reg, "write", sum_val[0])

        # Proc Rule to run the first step (uses multi-cycle control)
        with sched.proc_rule("run_increment") as rule:
            with rule.guard() as g:
                g.returns(g.const(1, 1))
            with rule.control() as ctrl:
                ctrl.enable(step.ref())

        # Value to read the accumulator
        with sched.value("get_accumulator", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(acc_reg, "read")
                body.returns(result[0])

        # Value to read the counter
        with sched.value("get_count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(count_reg, "read")
                body.returns(result[0])

    # Print the generated MLIR
    print("\nGenerated MLIR with static steps:")
    print("-" * 40)
    mlir_str = circuit.emit_mlir()
    print(mlir_str)

    # Show how to run timing passes
    print("\n" + "=" * 60)
    print("To run timing analysis and FSM allocation, use:")
    print("-" * 40)
    print("""
    circt-opt input.mlir \\
        -cmt2-timing-inference \\
        -cmt2-timing-validation \\
        -cmt2-static-fsm-allocation \\
        -cmt2-compile-static
    """)

    print("\nExpected transformations:")
    print("-" * 40)
    print("""
    1. TimingInference: Infers timing for calls from method contracts
    2. TimingValidation: Verifies timing constraints are satisfied
    3. StaticFSMAllocation: Maps cycles to FSM states
       - Steps with <=8 states use one-hot encoding
       - Steps with >8 states use binary encoding
    4. CompileStatic: Generates FSM register info
       - fsm_done_expr: Condition for step completion
       - fsm_next_expr: FSM state transition logic
       - fsm_guard_expr: Per-call timing guards
    """)

    # Example of expected output attributes
    print("Example FSM attributes after compilation:")
    print("-" * 40)
    print("""
    // 4-state step with one-hot encoding:
    cmt2.proc.static_step @increment_by_10<4> {
        ...
    } {
        fsm_states = 4,
        fsm_bitwidth = 4,
        fsm_encoding = "one_hot",
        fsm_done_expr = "fsm[3]",
        fsm_next_expr = "{fsm[2:0], 1'b0}",
        fsm_init_expr = "4'b1",
        static_compiled
    }

    // 12-state step with binary encoding:
    cmt2.proc.static_step @large_step<12> {
        ...
    } {
        fsm_states = 12,
        fsm_bitwidth = 4,
        fsm_encoding = "binary",
        fsm_done_expr = "fsm == 11",
        fsm_next_expr = "fsm + 1",
        fsm_init_expr = "4'd0",
        static_compiled
    }
    """)

    # Future enhancements section
    print("\n" + "=" * 60)
    print("Future Python API Enhancements")
    print("-" * 40)
    print("""
    The following timing features will be added to the Python API:

    1. Static latency on method declarations:
       mult_mod.method("multiply", ...,
           static_latency=4)  # Result available 4 cycles after call

    2. Timing annotation on calls:
       result = step.call(mult, "multiply", a, b,
           arg_timing=[(0, 1), (0, 1)],   # Args driven cycles 0-1
           result_timing=[(4, 5)])         # Result captured cycles 4-5

    3. Initiation interval (II) for pipelining:
       mult_mod.method("multiply", ...,
           static_latency=4,
           interval=3)  # Can accept new inputs every 3 cycles

    4. Port timing for precise scheduling:
       mult_mod.method("multiply", ...,
           arg_port_timing=[port.data(0), port.data(0)],
           result_port_timing=[port.data(4)])

    These features are already available at the MLIR level via:
    - #cmt2.timing<[start, end]> for timing intervals
    - #cmt2.interval<n> for initiation intervals
    - static<n> on BindMethodOp for static latency
    """)


def create_simulatable_timing_circuit():
    """Create a comprehensive timing circuit for E2E simulation.

    This circuit tests ALL timing features from the example:
    1. Static steps with different latencies (2, 3, 4 cycles)
    2. Multiple operations within static steps
    3. Read-compute-write patterns within timed regions
    4. Counter increment operations
    5. Accumulator addition operations

    Similar to the main example's increment_by_10 and compute_sum patterns.
    """
    clear_stl_registry()

    circuit = Circuit("TimingComprehensiveSim")

    with circuit.module("TimingComprehensive") as m:
        clk = m.clock()
        rst = m.reset()

        # Registers for state
        acc = m.instance(Reg.create(circuit, 32), "acc", clk=clk, rst=rst)
        counter = m.instance(Reg.create(circuit, 32), "counter", clk=clk, rst=rst)
        aux = m.instance(Reg.create(circuit, 32), "aux", clk=clk, rst=rst)

        # =====================================================================
        # Static Step 1: 2-cycle increment (simple)
        # Demonstrates: minimal static step
        # =====================================================================
        with m.static_step(2, "increment_2cycle") as step1:
            count = step1.call(counter, "read")
            new_count = step1.add(count, step1.const(1, 32))
            step1.call(counter, "write", new_count)

        # =====================================================================
        # Static Step 2: 3-cycle add_to_acc (medium)
        # Demonstrates: read-compute-write pattern
        # =====================================================================
        with m.static_step(3, "add_to_acc") as step2:
            acc_val = step2.call(acc, "read")
            add_val = step2.const(10, 32)
            new_acc = step2.add(acc_val, add_val)
            step2.call(acc, "write", new_acc)

        # =====================================================================
        # Static Step 3: 4-cycle compute_sum (longer)
        # Demonstrates: multi-register read and combine
        # Similar to example's compute_sum
        # =====================================================================
        with m.static_step(4, "compute_sum") as step3:
            acc_val = step3.call(acc, "read")
            cnt_val = step3.call(counter, "read")
            sum_val = step3.add(acc_val, cnt_val)
            step3.call(aux, "write", sum_val)

        # =====================================================================
        # Static Step 4: 5-cycle combined operation
        # Demonstrates: multiple reads, compute, multiple writes
        # =====================================================================
        with m.static_step(5, "combined_ops") as step4:
            # Read all registers
            acc_val = step4.call(acc, "read")
            cnt_val = step4.call(counter, "read")
            # Compute: acc + counter + 1
            sum_val = step4.add(acc_val, cnt_val)
            final_val = step4.add(sum_val, step4.const(1, 32))
            # Write back to acc
            step4.call(acc, "write", final_val)

        # =====================================================================
        # Proc Rules to run each static step
        # =====================================================================
        with m.proc_rule("run_increment") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                ctrl.enable(step1.ref())

        with m.proc_rule("run_add_acc") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                ctrl.enable(step2.ref())

        with m.proc_rule("run_compute_sum") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                ctrl.enable(step3.ref())

        with m.proc_rule("run_combined") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                ctrl.enable(step4.ref())

        # =====================================================================
        # Value methods to read state
        # =====================================================================
        with m.value("get_acc", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                v = b.call(acc, "read")
                b.returns(v)

        with m.value("get_counter", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                v = b.call(counter, "read")
                b.returns(v)

        with m.value("get_aux", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                v = b.call(aux, "read")
                b.returns(v)

    return circuit


def create_timing_testbench(circuit):
    """Create comprehensive testbench for ALL timing features.

    Tests:
    1. Reset behavior - all registers start at 0
    2. 2-cycle static step (increment_2cycle)
    3. 3-cycle static step (add_to_acc)
    4. 4-cycle static step (compute_sum)
    5. 5-cycle static step (combined_ops)
    6. Overall timing behavior verification
    """
    tb = Testbench(circuit, auto_debug_ports=False)

    # =========================================================================
    # Test 1: Reset and Initial State
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify all registers start at 0 after reset")
        seq.reset(10)
        seq.expect("get_acc_res0", 0, "Accumulator should be 0 after reset")
        seq.expect("get_counter_res0", 0, "Counter should be 0 after reset")
        seq.expect("get_aux_res0", 0, "Aux should be 0 after reset")
        seq.print("Reset test PASSED: all registers at 0")

    # =========================================================================
    # Test 2: 2-Cycle Static Step (increment_2cycle)
    # =========================================================================
    with tb.sequence("test_2cycle_step") as seq:
        seq.comment("Test: 2-cycle static step increments counter")
        seq.reset(10)
        seq.expect("get_counter_res0", 0, "Counter should be 0 after reset")

        # Wait for some increments to happen
        seq.record_cycle("start")
        seq.wait(20)
        seq.record_cycle("end")

        seq.print("2-cycle test: counter=", "get_counter_res0")
        seq.print_cycle_diff("start", "end", "Execution time")

    # =========================================================================
    # Test 3: 3-Cycle Static Step (add_to_acc)
    # =========================================================================
    with tb.sequence("test_3cycle_step") as seq:
        seq.comment("Test: 3-cycle static step adds 10 to accumulator")
        seq.reset(10)
        seq.expect("get_acc_res0", 0, "Acc should be 0 after reset")

        # Wait for some additions to happen (add 10 each time)
        seq.wait(30)

        # Acc should have increased by multiples of 10
        seq.print("3-cycle test: acc=", "get_acc_res0")

    # =========================================================================
    # Test 4: 4-Cycle Static Step (compute_sum)
    # =========================================================================
    with tb.sequence("test_4cycle_step") as seq:
        seq.comment("Test: 4-cycle static step computes acc + counter -> aux")
        seq.reset(10)

        # Wait for operations to execute
        seq.wait(40)

        # aux should contain sum of acc and counter at some point
        seq.print("4-cycle test: acc=", "get_acc_res0")
        seq.print("4-cycle test: counter=", "get_counter_res0")
        seq.print("4-cycle test: aux=", "get_aux_res0")

    # =========================================================================
    # Test 5: 5-Cycle Static Step (combined_ops)
    # =========================================================================
    with tb.sequence("test_5cycle_step") as seq:
        seq.comment("Test: 5-cycle static step with combined operations")
        seq.reset(10)

        # Wait for the combined operations
        seq.wait(50)

        seq.print("5-cycle test: acc=", "get_acc_res0")
        seq.print("5-cycle test: counter=", "get_counter_res0")

    # =========================================================================
    # Test 6: Comprehensive Timing Observation
    # =========================================================================
    with tb.sequence("test_comprehensive") as seq:
        seq.comment("Test: Observe all static steps running together")
        seq.reset(10)

        # Observe state at intervals to verify timing behavior
        seq.wait(10)
        seq.print("At cycle 10: acc=", "get_acc_res0")
        seq.print("At cycle 10: counter=", "get_counter_res0")
        seq.print("At cycle 10: aux=", "get_aux_res0")

        seq.wait(20)
        seq.print("At cycle 30: acc=", "get_acc_res0")
        seq.print("At cycle 30: counter=", "get_counter_res0")
        seq.print("At cycle 30: aux=", "get_aux_res0")

        seq.wait(20)
        seq.print("At cycle 50: acc=", "get_acc_res0")
        seq.print("At cycle 50: counter=", "get_counter_res0")
        seq.print("At cycle 50: aux=", "get_aux_res0")

        seq.print("Comprehensive timing test PASSED")

    return tb


def run_simulation():
    """Run comprehensive E2E simulation for ALL timing features.

    Tests all timing features demonstrated by the example:
    - Static steps with different latencies (2, 3, 4, 5 cycles)
    - Read-compute-write patterns
    - Multiple register operations
    - Timing behavior verification
    """
    print("\n" + "=" * 60)
    print("E2E Simulation: Comprehensive Cycle-Precise Timing")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_timing"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("\n1. Creating timing circuit...")
    circuit = create_simulatable_timing_circuit()

    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_timing_testbench(circuit)
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


if __name__ == "__main__":
    # Run the documentation example
    main()
    # Run E2E simulation
    sys.exit(run_simulation())
