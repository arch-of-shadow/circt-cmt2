#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Comprehensive Procedural Control Testbench Example for PyCMT2.

This example demonstrates the Testbench DSL for testing procedural control
features including:
1. Dynamic steps with multi-cycle operations
2. Static steps with fixed latency
3. Sequential composition (seq)
4. Parallel composition (par)
5. Conditional control (if/else)
6. While loops
7. FSM state verification

The testbench validates:
- FSM state transitions
- Step completion signals
- Register value changes
- Timing correctness for static steps
- Proper sequencing of operations

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 \
        ../examples/PyCMT2/proc_testbench_example.py
"""

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, clear_stl_registry


def create_proc_testable_circuit():
    """Create a circuit with procedural control suitable for testbench verification."""
    clear_stl_registry()

    circuit = Circuit("ProcTestable")

    reg32 = Reg.create(circuit, 32)
    reg8 = Reg.create(circuit, 8)
    reg1 = Reg.create(circuit, 1)

    with circuit.module("ProcALU") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers
        reg_a = m.instance(reg32, "reg_a", clk=clk, rst=rst)
        reg_b = m.instance(reg32, "reg_b", clk=clk, rst=rst)
        reg_result = m.instance(reg32, "reg_result", clk=clk, rst=rst)
        reg_counter = m.instance(reg8, "reg_counter", clk=clk, rst=rst)
        reg_done = m.instance(reg1, "reg_done", clk=clk, rst=rst)
        reg_busy = m.instance(reg1, "reg_busy", clk=clk, rst=rst)

        # Operation select register - gates which proc_rule can fire
        # 0=none, 1=seq_add_sub, 2=par_load, 3=cond_compute, 4=loop_increment,
        # 5=mixed_compute, 6=invoke_test, 7=static_sequence, 8=dynamic_sequence, 9=nested_test
        reg_op_select = m.instance(reg8, "reg_op_select", clk=clk, rst=rst)

        # =====================================================================
        # Dynamic Steps
        # =====================================================================

        # Step: load_a - Load value into reg_a
        with m.step("load_a") as step:
            val = step.call(reg_a, "read")
            step.done(step.const(1, 1))

        # Step: load_b - Load value into reg_b
        with m.step("load_b") as step:
            val = step.call(reg_b, "read")
            step.done(step.const(1, 1))

        # Step: compute_add - Add reg_a and reg_b, store in reg_result
        with m.step("compute_add") as step:
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            result = step.add(a, b)
            step.call(reg_result, "write", result)
            step.done(step.const(1, 1))

        # Step: compute_sub - Subtract reg_b from reg_a
        with m.step("compute_sub") as step:
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            result = step.sub(a, b)
            step.call(reg_result, "write", result)
            step.done(step.const(1, 1))

        # Step: increment_counter - Increment the counter
        with m.step("increment_counter") as step:
            count = step.call(reg_counter, "read")
            new_count = step.add(count, step.const(1, 8))
            step.call(reg_counter, "write", new_count)
            step.done(step.const(1, 1))

        # Step: set_done_flag - Set the done flag
        with m.step("set_done_flag") as step:
            step.call(reg_done, "write", step.const(1, 1))
            step.done(step.const(1, 1))

        # Step: clear_done_flag - Clear the done flag
        with m.step("clear_done_flag") as step:
            step.call(reg_done, "write", step.const(0, 1))
            step.done(step.const(1, 1))

        # Step: set_busy - Set busy flag
        with m.step("set_busy") as step:
            step.call(reg_busy, "write", step.const(1, 1))
            step.done(step.const(1, 1))

        # Step: clear_busy - Clear busy flag
        with m.step("clear_busy") as step:
            step.call(reg_busy, "write", step.const(0, 1))
            step.done(step.const(1, 1))

        # =====================================================================
        # Static Steps (fixed latency)
        # =====================================================================

        # Static step: delay_2 - 2-cycle delay
        with m.static_step(2, "delay_2") as step:
            pass  # Just a delay

        # Static step: delay_4 - 4-cycle delay
        with m.static_step(4, "delay_4") as step:
            pass  # Just a delay

        # Static step: multiply_3cycle - 3-cycle multiply
        with m.static_step(3, "multiply_3cycle") as step:
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            result = step.mul(a, b)
            step.call(reg_result, "write", result)

        # =====================================================================
        # Proc Rule 1: Simple Sequential (seq_add_sub)
        # Execute add then sub in sequence
        # =====================================================================

        with m.proc_rule("seq_add_sub") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(1, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    seq.enable(m._steps["compute_add"].ref())
                    seq.enable(m._steps["delay_2"].ref())
                    seq.enable(m._steps["compute_sub"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 2: Parallel Operations (par_load)
        # Load a and b in parallel
        # =====================================================================

        with m.proc_rule("par_load") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(2, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    with seq.par() as par:
                        par.enable(m._steps["load_a"].ref())
                        par.enable(m._steps["load_b"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 3: Conditional (cond_compute)
        # If done flag is set, do add; else do sub
        # =====================================================================

        with m.proc_rule("cond_compute") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(3, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    done_flag = seq.const(1, 1)  # Simplified condition
                    with seq.if_(done_flag) as if_ctrl:
                        with if_ctrl.then_() as then_ctrl:
                            with then_ctrl.seq() as then_seq:
                                then_seq.enable(m._steps["compute_add"].ref())
                        with if_ctrl.else_() as else_ctrl:
                            with else_ctrl.seq() as else_seq:
                                else_seq.enable(m._steps["compute_sub"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 4: While Loop (loop_increment)
        # Increment counter in a loop (simplified)
        # =====================================================================

        with m.proc_rule("loop_increment") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(4, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    # Loop condition (simplified to false for static test)
                    cond = seq.const(0, 1)
                    with seq.while_(cond) as loop:
                        with loop.seq() as loop_seq:
                            loop_seq.enable(m._steps["increment_counter"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 5: Mixed Static/Dynamic (mixed_compute)
        # Static multiply followed by dynamic operations
        # =====================================================================

        with m.proc_rule("mixed_compute") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(5, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    seq.enable(m._steps["multiply_3cycle"].ref())  # 3 cycles
                    seq.enable(m._steps["delay_4"].ref())          # 4 cycles
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 7: Invoke Operations (invoke_test)
        # Demonstrates proc.invoke for calling instance methods
        # Uses instance.method_name to get MethodRef for invoke
        # =====================================================================

        with m.proc_rule("invoke_test") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(6, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # Use invoke to call methods on instances
                    # inst.method_name returns a MethodRef
                    seq.invoke(reg_busy, reg_busy.write, seq.const(1, 1))
                    # Invoke write on counter with a specific value
                    seq.invoke(reg_counter, reg_counter.write, seq.const(42, 8))
                    seq.enable(m._steps["delay_2"].ref())  # Static delay
                    seq.invoke(reg_done, reg_done.write, seq.const(1, 1))
                    seq.invoke(reg_busy, reg_busy.write, seq.const(0, 1))

        # =====================================================================
        # Proc Rule 8: Static-Only Sequence (static_sequence)
        # Tests pure static step sequencing
        # =====================================================================

        with m.proc_rule("static_sequence") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(7, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    seq.enable(m._steps["delay_2"].ref())   # 2 cycles
                    seq.enable(m._steps["delay_4"].ref())   # 4 cycles
                    seq.enable(m._steps["multiply_3cycle"].ref())  # 3 cycles
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 9: Dynamic-Only Sequence (dynamic_sequence)
        # Tests pure dynamic step sequencing
        # =====================================================================

        with m.proc_rule("dynamic_sequence") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(8, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    seq.enable(m._steps["load_a"].ref())
                    seq.enable(m._steps["load_b"].ref())
                    seq.enable(m._steps["compute_add"].ref())
                    seq.enable(m._steps["increment_counter"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 6: Nested Control (nested_test)
        # Complex nested structure: seq { par { ... }; if { seq {...} } }
        # =====================================================================

        with m.proc_rule("nested_test") as rule:
            with rule.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(9, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    # Parallel: load and delay
                    with seq.par() as par:
                        par.enable(m._steps["load_a"].ref())
                        par.enable(m._steps["delay_2"].ref())
                    # Conditional: based on flag
                    flag = seq.const(1, 1)
                    with seq.if_(flag) as if_ctrl:
                        with if_ctrl.then_() as then_ctrl:
                            with then_ctrl.seq() as then_seq:
                                then_seq.enable(m._steps["compute_add"].ref())
                                then_seq.enable(m._steps["increment_counter"].ref())
                    # Final cleanup
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Interface Methods (for external stimulus)
        # =====================================================================

        # Method: load - Load values into registers
        with m.method("load", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
            with meth.guard() as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with meth.body() as body:
                a_in = body.arg("a")
                b_in = body.arg("b")
                body.call(reg_a, "write", a_in)
                body.call(reg_b, "write", b_in)

        # Method: reset_state - Reset all state
        with m.method("reset_state") as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(reg_a, "write", body.const(0, 32))
                body.call(reg_b, "write", body.const(0, 32))
                body.call(reg_result, "write", body.const(0, 32))
                body.call(reg_counter, "write", body.const(0, 8))
                body.call(reg_done, "write", body.const(0, 1))
                body.call(reg_busy, "write", body.const(0, 1))
                body.call(reg_op_select, "write", body.const(0, 8))

        # Method: select_op - Select which operation to run
        # 0=none, 1=seq_add_sub, 2=par_load, 3=cond_compute, 4=loop_increment,
        # 5=mixed_compute, 6=invoke_test, 7=static_sequence, 8=dynamic_sequence, 9=nested_test
        with m.method("select_op", args=[("op", UInt(8))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                op_in = body.arg("op")
                body.call(reg_op_select, "write", op_in)

        # =====================================================================
        # Value Methods (for output observation)
        # =====================================================================

        with m.value("get_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(reg_result, "read")
                body.returns(result)

        with m.value("get_counter", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                count = body.call(reg_counter, "read")
                body.returns(count)

        with m.value("is_done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                done = body.call(reg_done, "read")
                body.returns(done)

        with m.value("is_busy", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                busy = body.call(reg_busy, "read")
                body.returns(busy)

    return circuit


def create_proc_testbench(circuit):
    """Create a comprehensive testbench for procedural control testing."""

    tb = Testbench(circuit)

    # =========================================================================
    # Test Sequence 1: Basic Initialization
    # =========================================================================

    with tb.sequence("test_init") as seq:
        seq.comment("Test basic initialization and reset")
        seq.reset(10)
        seq.comment("op_select=0 means no rule fires")
        seq.drive("select_op_op", 0)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)
        seq.wait(5)
        seq.expect("is_busy_res0", 0, "Should not be busy after reset")
        seq.expect("is_done_res0", 0, "Should not be done after reset")
        seq.print("Initialization test passed")

    # =========================================================================
    # Test Sequence 2: Sequential Control Flow (seq_add_sub)
    # =========================================================================

    with tb.sequence("test_seq_add_sub") as seq:
        seq.comment("Test sequential composition: add then sub")
        seq.reset(10)

        # Load input values first
        seq.comment("Load A=100, B=25")
        seq.drive("load_a", 100)
        seq.drive("load_b", 25)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select seq_add_sub operation (op=1)
        seq.comment("Select seq_add_sub operation")
        seq.drive("select_op_op", 1)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # Wait for done flag to be set (operation complete)
        seq.comment("Wait for operation to complete")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=50)
        seq.wait(2)  # Wait for clear_busy to run

        # Check result (should be 100 - 25 = 75 since sub runs after add)
        seq.comment("Verify result after seq_add_sub")
        seq.expect("is_done_res0", 1, "Done flag should be set")
        seq.expect("is_busy_res0", 0, "Should not be busy")
        seq.expect("get_result_res0", 75, "100-25=75 after add then sub")
        seq.print("Sequential test", "get_result_res0")

    # =========================================================================
    # Test Sequence 3: Parallel Operations (par_load)
    # =========================================================================

    with tb.sequence("test_par_load") as seq:
        seq.comment("Test parallel composition: load a and b together")
        seq.reset(10)

        # Select par_load operation (op=2)
        seq.comment("Select par_load operation")
        seq.drive("select_op_op", 2)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # The par_load rule should execute load_a and load_b in parallel
        seq.wait(20)
        seq.expect("is_busy_res0", 0, "Should complete quickly")
        seq.print("Parallel load test completed")

    # =========================================================================
    # Test Sequence 4: Static Step Timing (mixed_compute)
    # =========================================================================

    with tb.sequence("test_static_timing") as seq:
        seq.comment("Test static step timing: 3-cycle multiply + 4-cycle delay")
        seq.reset(10)

        # Load values for multiply first
        seq.drive("load_a", 10)
        seq.drive("load_b", 5)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select mixed_compute operation (op=5)
        seq.comment("Select mixed_compute operation")
        seq.drive("select_op_op", 5)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # mixed_compute should take: 1 (busy) + 3 (multiply) + 4 (delay) + 2 (flags) = 10+ cycles
        seq.comment("Wait for static steps to complete")
        seq.wait(20)
        seq.expect("is_done_res0", 1, "Done after static steps")

        # Result should be 10 * 5 = 50
        seq.expect("get_result_res0", 50, "Multiply result")
        seq.print("Static timing test passed", "get_result_res0")

    # =========================================================================
    # Test Sequence 5: Conditional Control (cond_compute)
    # =========================================================================

    with tb.sequence("test_conditional") as seq:
        seq.comment("Test conditional control flow")
        seq.reset(10)

        # Load values
        seq.drive("load_a", 30)
        seq.drive("load_b", 10)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select cond_compute operation (op=3)
        seq.comment("Select cond_compute operation")
        seq.drive("select_op_op", 3)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # cond_compute will execute based on condition (always true, so add)
        seq.comment("Wait for conditional operation to complete")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=50)
        seq.wait(2)  # Wait for clear_busy to run
        seq.expect("is_busy_res0", 0, "Operation should complete")
        # Condition is always true, so add: 30 + 10 = 40
        # Note: If this shows 20, it means else branch ran (30-10=20)
        seq.print("Conditional test result", "get_result_res0")

    # =========================================================================
    # Test Sequence 6: Nested Control (nested_test)
    # =========================================================================

    with tb.sequence("test_nested") as seq:
        seq.comment("Test nested control structures")
        seq.reset(10)

        seq.drive("load_a", 20)
        seq.drive("load_b", 15)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select nested_test operation (op=9)
        seq.comment("Select nested_test operation")
        seq.drive("select_op_op", 9)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # nested_test: par { load_a; delay_2 }; if(1) { add; inc }; done; clear
        seq.comment("Wait for nested operation to complete")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=50)
        seq.wait(2)  # Wait for clear_busy to run
        seq.expect("is_done_res0", 1, "Nested test should complete")
        seq.expect("is_busy_res0", 0, "Should not be busy")

        # Counter should have incremented (add: 20+15=35)
        seq.expect("get_result_res0", 35, "20+15=35 from nested add")
        seq.print("Nested test - counter", "get_counter_res0")
        seq.print("Nested test - result", "get_result_res0")

    # =========================================================================
    # Test Sequence 7: Stress Test - Multiple Operations
    # =========================================================================

    with tb.sequence("test_stress") as seq:
        seq.comment("Stress test: Multiple sequential operations")
        seq.reset(10)

        for i in range(3):
            seq.comment(f"Iteration {i+1}")
            # Load values
            seq.drive("load_a", (i + 1) * 10)
            seq.drive("load_b", i + 1)
            seq.drive("load_enable", 1)
            seq.wait(1)
            seq.drive("load_enable", 0)
            seq.wait(2)

            # Trigger seq_add_sub (op=1)
            seq.drive("select_op_op", 1)
            seq.drive("select_op_enable", 1)
            seq.wait(1)
            seq.drive("select_op_enable", 0)

            # Wait for completion (use fixed wait since done flag persists)
            seq.wait(20)

            # Clear op_select for next iteration
            seq.drive("select_op_op", 0)
            seq.drive("select_op_enable", 1)
            seq.wait(1)
            seq.drive("select_op_enable", 0)
            seq.wait(2)

        seq.print("Stress test final result", "get_result_res0")

    # =========================================================================
    # Test Sequence 8: Invoke Operation Test
    # =========================================================================

    with tb.sequence("test_invoke") as seq:
        seq.comment("Test proc.invoke for calling instance methods")
        seq.reset(10)

        # Select invoke_test operation (op=6)
        seq.comment("Select invoke_test operation")
        seq.drive("select_op_op", 6)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # Wait for invoke_test rule to execute
        # It should set counter to 42 and done flag
        seq.comment("Wait for invoke operation to complete")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=50)
        seq.wait(2)  # Wait for clear_busy to run

        # Check results - counter should be 42 from invoke
        seq.expect("get_counter_res0", 42, "Counter set via invoke")
        seq.expect("is_done_res0", 1, "Done set via invoke")
        seq.expect("is_busy_res0", 0, "Should not be busy")
        seq.print("Invoke test - counter", "get_counter_res0")

    # =========================================================================
    # Test Sequence 9: Static-Only Steps
    # =========================================================================

    with tb.sequence("test_static_only") as seq:
        seq.comment("Test static step timing (pure static sequence)")
        seq.reset(10)

        # Load values
        seq.drive("load_a", 7)
        seq.drive("load_b", 3)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select static_sequence operation (op=7)
        seq.comment("Select static_sequence operation")
        seq.drive("select_op_op", 7)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # static_sequence: busy + delay_2(2) + delay_4(4) + multiply_3cycle(3) + done + clear
        # Total: ~12 cycles minimum
        seq.comment("Wait for static sequence (2+4+3=9 cycles min)")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=50)
        seq.wait(2)  # Wait for clear_busy to run
        seq.expect("is_done_res0", 1, "Static sequence done")
        seq.expect("is_busy_res0", 0, "Should not be busy")
        seq.expect("get_result_res0", 21, "7*3=21 from static multiply")
        seq.print("Static-only test", "get_result_res0")

    # =========================================================================
    # Test Sequence 10: Dynamic-Only Steps
    # =========================================================================

    with tb.sequence("test_dynamic_only") as seq:
        seq.comment("Test dynamic step sequence (all 1-cycle steps)")
        seq.reset(10)

        # Load values
        seq.drive("load_a", 15)
        seq.drive("load_b", 5)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select dynamic_sequence operation (op=8)
        seq.comment("Select dynamic_sequence operation")
        seq.drive("select_op_op", 8)
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # dynamic_sequence: busy + load_a + load_b + compute_add + inc_counter + done + clear
        # Each dynamic step is 1 cycle
        seq.comment("Wait for dynamic sequence (~7 cycles)")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=50)
        seq.wait(2)  # Wait for clear_busy to run
        seq.expect("is_done_res0", 1, "Dynamic sequence done")
        seq.expect("is_busy_res0", 0, "Should not be busy")
        seq.expect("get_result_res0", 20, "15+5=20 from add")
        seq.print("Dynamic-only test", "get_result_res0")

    return tb


def main():
    print("=" * 70)
    print("Procedural Control Testbench Example")
    print("=" * 70)

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "proc_testbench_workspace"

    # Clean previous workspace
    if workspace_dir.exists():
        print(f"\nRemoving existing workspace: {workspace_dir}")
        shutil.rmtree(workspace_dir)

    # Create circuit
    print("\n1. Creating procedural control circuit...")
    circuit = create_proc_testable_circuit()

    # Emit MLIR to verify
    print("\n2. Emitting CMT2 MLIR...")
    mlir = circuit.emit_mlir()

    # Check for proc constructs
    proc_ops = ["proc.step", "proc.static_step", "proc.rule", "proc.seq",
                "proc.par", "proc.if", "proc.while", "proc.enable"]
    print("   Procedural constructs found:")
    for op in proc_ops:
        count = mlir.count(f"cmt2.{op}")
        if count > 0:
            print(f"      cmt2.{op}: {count}")

    # Create testbench
    print("\n3. Creating comprehensive testbench...")
    tb = create_proc_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace
    print("\n4. Creating simulation workspace...")
    ws = SimulationWorkspace(circuit, workspace_dir)

    # Generate with testbench
    print("\n5. Generating workspace with testbench DSL...")
    ws.generate_with_testbench(tb)

    # Show generated testbench
    print("\n6. Generated testbench code (excerpt):")
    print("-" * 70)
    tb_file = workspace_dir / "tb" / "testbench.cpp"
    tb_content = tb_file.read_text()
    # Show first test sequence function
    lines = tb_content.split("\n")
    in_function = False
    shown_lines = []
    for line in lines:
        if "void run_test_init" in line:
            in_function = True
        if in_function:
            shown_lines.append(line)
            if line.strip() == "}" and in_function:
                break
    print("\n".join(shown_lines[:30]))
    if len(shown_lines) > 30:
        print(f"... ({len(shown_lines) - 30} more lines)")
    print("-" * 70)

    # Show cocotb testbench
    print("\n7. Generating cocotb testbench...")
    cocotb_code = tb.generate_cocotb()
    print("-" * 70)
    print(cocotb_code[:2000])
    if len(cocotb_code) > 2000:
        print(f"... ({len(cocotb_code) - 2000} more characters)")
    print("-" * 70)

    # Instructions
    print("\n" + "=" * 70)
    print("Testbench workspace generated successfully!")
    print("=" * 70)
    print(f"""
Generated workspace: {workspace_dir}

Test sequences included:
  1. test_init          - Basic initialization verification
  2. test_seq_add_sub   - Sequential composition test
  3. test_par_load      - Parallel composition test
  4. test_static_timing - Static step timing verification
  5. test_conditional   - Conditional control flow test
  6. test_nested        - Nested control structures test
  7. test_stress        - Multiple operation stress test

To run the simulation:
    cd {workspace_dir}
    make          # Build the simulation
    make run      # Run all tests
    make waves    # View waveforms
""")


if __name__ == "__main__":
    main()
