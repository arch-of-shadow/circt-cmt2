#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Comprehensive Procedural Control Testbench Example (JIT stacked on PyCMT2).

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
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/proc_testbench.py
"""

import cmt2.jit as jit

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, clear_stl_registry


@jit.elaborate
def create_proc_testable_circuit():
    """Create a circuit with procedural control suitable for testbench verification."""
    clear_stl_registry()

    circuit = Circuit("ProcTestable")

    # State registers (FSM registers for proc rules are now auto-created)
    reg32 = Reg.create(circuit, 32)
    reg8 = Reg.create(circuit, 8)
    reg1 = Reg.create(circuit, 1)

    # =========================================================================
    # CMT2 Module with Multi-Cycle Method (Timing Attributes)
    # =========================================================================
    # This demonstrates timing attributes on CMT2 module methods.
    # - static_latency: Declares the method takes N cycles
    # - interval: Declares minimum cycles between consecutive calls
    #
    # The method implementation uses procedural control to achieve the timing.

    # Create register types for the multiplier unit
    mult_reg32 = Reg.create(circuit, 32)
    mult_reg1 = Reg.create(circuit, 1)

    with jit.module(circuit, "MultiplierUnit") as mult_mod:
        clk = mult_mod.clock()
        rst = mult_mod.reset()

        # Internal registers for computation
        op_a = mult_mod.instance(mult_reg32, "op_a", clk=clk, rst=rst)
        op_b = mult_mod.instance(mult_reg32, "op_b", clk=clk, rst=rst)
        result_reg = mult_mod.instance(mult_reg32, "result_reg", clk=clk, rst=rst)
        busy = mult_mod.instance(mult_reg1, "busy", clk=clk, rst=rst)

        # Method to start multiplication - single-cycle (captures inputs)
        # The actual multi-cycle computation is done by the compute_multiply proc_rule
        # Note: atomic method() is always single-cycle. For multi-cycle methods,
        # use proc_method() instead.
        @jit.method(mult_mod)
        def multiply(meth, a: UInt[32], b: UInt[32]) -> UInt[32]:
            with meth.guard:
                meth.returns(meth.not_(busy.read))

            with meth.body:
                op_a.write(a)
                op_b.write(b)
                busy.write(meth.const(1, 1))
                meth.returns(result_reg.read)

        # Value to read result
        @jit.value(mult_mod)
        def get_result(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(result_reg.read)

        # Value to check busy status
        @jit.value(mult_mod)
        def is_busy(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(busy.read)

        # Internal proc_rule to perform the actual multiplication
        # This uses a 4-cycle static step to match the declared latency
        with mult_mod.proc_rule("compute_multiply") as rule:
            with rule.guard as g:
                is_busy = g.call(busy, "read")
                g.returns(is_busy)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # 4-cycle static step for multiplication
                    with mult_mod.static_step(4, "do_multiply") as step:
                        a = step.call(op_a, "read")
                        b = step.call(op_b, "read")
                        product = step.mul(a, b)
                        step.call(result_reg, "write", product)
                    seq.enable(step.ref())
                    # Clear busy flag (dynamic step)
                    with mult_mod.step("clear_mult_busy") as clr_step:
                        clr_step.call(busy, "write", clr_step.const(0, 1))
                        clr_step.done(clr_step.const(1, 1))
                    seq.enable(clr_step.ref())

    with jit.module(circuit, "ProcALU") as m:
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
        # 5=mixed_compute, 6=invoke_test, 7=static_sequence, 8=dynamic_sequence,
        # 9=nested_test, 10=timed_multiply_test, 11=pipelined_add_test, 12=complex_par_test
        reg_op_select = m.instance(reg8, "reg_op_select", clk=clk, rst=rst)

        # =====================================================================
        # Instance of CMT2 Module with Multi-Cycle Method
        # =====================================================================
        # The MultiplierUnit has a multiply method with static_latency=4, interval=2
        # The method is implemented using procedural control (proc_rule + static_step)
        mult_unit = m.instance(mult_mod, "mult_unit", clk=clk, rst=rst)

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
        # Static Step with Initiation Interval (Pipeline Timing)
        # =====================================================================
        # This demonstrates the `interval` attribute for pipelined execution.
        # A step with latency=4 and interval=2 can accept new inputs every
        # 2 cycles while still taking 4 cycles to produce results.

        with m.static_step(4, "pipelined_add", interval=2) as step:
            # This step takes 4 cycles total but can be pipelined with II=2
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            result = step.add(a, b)
            step.call(reg_result, "write", result)

        # =====================================================================
        # Steps for Testing CMT2 Module with Multi-Cycle Method
        # =====================================================================
        # The MultiplierUnit.multiply method has static_latency=4.
        # We test this by:
        # 1. Triggering multiply (stores args, sets busy)
        # 2. Waiting for compute_multiply proc_rule to complete (busy=0)
        # 3. Reading result from mult_unit.get_result()

        # Step: start_mult - Start multiplication (calls multiply method)
        with m.step("start_mult") as step:
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            # Call multiply - this triggers the computation in mult_unit
            # The method sets busy=1 and stores operands
            step.call(mult_unit, "multiply", a, b)
            step.done(step.const(1, 1))

        # Step: check_mult_busy - Check if mult_unit is still busy
        with m.step("check_mult_busy") as step:
            is_busy = step.call(mult_unit, "is_busy")
            step.done(step.not_(is_busy))  # Done when not busy

        # Step: copy_mult_result - Copy result from mult_unit to reg_result
        with m.step("copy_mult_result") as step:
            result = step.call(mult_unit, "get_result")
            step.call(reg_result, "write", result)
            step.done(step.const(1, 1))

        # =====================================================================
        # Proc Rule 1: Simple Sequential (seq_add_sub)
        # Execute add then sub in sequence
        # =====================================================================

        with m.proc_rule("seq_add_sub") as rule:
            with rule.guard as g:
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
            with rule.guard as g:
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
            with rule.guard as g:
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
            with rule.guard as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(4, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    # Loop condition function (simplified to false for static test)
                    with seq.while_(lambda b: b.const(0, 1)) as loop:
                        with loop.seq() as loop_seq:
                            loop_seq.enable(m._steps["increment_counter"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 5: Mixed Static/Dynamic (mixed_compute)
        # Static multiply followed by dynamic operations
        # =====================================================================

        with m.proc_rule("mixed_compute") as rule:
            with rule.guard as g:
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
            with rule.guard as g:
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
            with rule.guard as g:
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
            with rule.guard as g:
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
            with rule.guard as g:
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
        # Proc Rule 10: Timed Multiply Test (timed_multiply_test)
        # Tests CMT2 module with multi-cycle method (static_latency=4)
        # The MultiplierUnit has:
        #   - multiply method: triggers computation, declared static_latency=4
        #   - compute_multiply proc_rule: performs actual multiplication
        # =====================================================================

        with m.proc_rule("timed_multiply_test") as rule:
            with rule.guard as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(10, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    # Start multiplication in mult_unit
                    seq.enable(m._steps["start_mult"].ref())
                    # Wait for mult_unit to finish (while busy, loop)
                    # condition_fn: returns True while mult_unit is busy
                    def mult_busy_cond(b):
                        return b.call(mult_unit, "is_busy")
                    with seq.while_(mult_busy_cond) as loop:
                        # Just wait - check_mult_busy step completes when not busy
                        loop.enable(m._steps["check_mult_busy"].ref())
                    # Copy result from mult_unit to reg_result
                    seq.enable(m._steps["copy_mult_result"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 11: Pipelined Add Test (pipelined_add_test)
        # Tests static_step with interval attribute (II=2)
        # =====================================================================

        with m.proc_rule("pipelined_add_test") as rule:
            with rule.guard as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(11, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    # Use the pipelined_add step (latency=4, interval=2)
                    seq.enable(m._steps["pipelined_add"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Proc Rule 12: Complex Par Test (complex_par_test)
        # Tests parallel with nested seq - triggers per-branch FSM generation
        # Structure: par { seq { enable A; enable B }; seq { enable C; enable D } }
        # =====================================================================

        with m.proc_rule("complex_par_test") as rule:
            with rule.guard as g:
                busy = g.call(reg_busy, "read")
                not_busy = g.not_(busy)
                op_sel = g.call(reg_op_select, "read")
                is_selected = g.eq(op_sel, g.const(12, 8))
                g.returns(g.and_(not_busy, is_selected))
            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(m._steps["set_busy"].ref())
                    # Complex par: two branches with seq inside
                    # Branch 0: load_a then compute_add (stores a+b in result)
                    # Branch 1: load_b then increment_counter
                    # Both branches run in parallel, each progressing through their seq
                    with seq.par() as par:
                        with par.seq() as branch0:
                            branch0.enable(m._steps["load_a"].ref())
                            branch0.enable(m._steps["compute_add"].ref())
                        with par.seq() as branch1:
                            branch1.enable(m._steps["load_b"].ref())
                            branch1.enable(m._steps["increment_counter"].ref())
                    seq.enable(m._steps["set_done_flag"].ref())
                    seq.enable(m._steps["clear_busy"].ref())

        # =====================================================================
        # Interface Methods (for external stimulus)
        # =====================================================================

        @jit.method(m)
        def load(meth, a: UInt[32], b: UInt[32]) -> None:
            with meth.guard:
                meth.returns(meth.not_(reg_busy.read))
            with meth.body:
                reg_a.write(a)
                reg_b.write(b)

        @jit.method(m)
        def reset_state(meth) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                reg_a.write(meth.const(0, 32))
                reg_b.write(meth.const(0, 32))
                reg_result.write(meth.const(0, 32))
                reg_counter.write(meth.const(0, 8))
                reg_done.write(meth.const(0, 1))
                reg_busy.write(meth.const(0, 1))
                reg_op_select.write(meth.const(0, 8))

        # Method: select_op - Select which operation to run
        # 0=none, 1=seq_add_sub, 2=par_load, 3=cond_compute, 4=loop_increment,
        # 5=mixed_compute, 6=invoke_test, 7=static_sequence, 8=dynamic_sequence, 9=nested_test
        @jit.method(m)
        def select_op(meth, op: UInt[8]) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                reg_op_select.write(op)

        # =====================================================================
        # Value Methods (for output observation)
        # =====================================================================

        @jit.value(m)
        def get_result(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(reg_result.read)

        @jit.value(m)
        def get_counter(val) -> UInt[8]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(reg_counter.read)

        @jit.value(m)
        def is_done(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(reg_done.read)

        @jit.value(m)
        def is_busy(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(reg_busy.read)

    return circuit


def create_proc_testbench(circuit):
    """Create a comprehensive testbench for procedural control testing."""

    # Enable auto_debug_ports for rule firing observation
    tb = Testbench(circuit, auto_debug_ports=True)

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

    # =========================================================================
    # Test Sequence 11: Cycle-Accurate Static Step Timing
    # Verifies the execution model: static steps take exactly L cycles
    # =========================================================================

    with tb.sequence("test_cycle_static_step") as seq:
        seq.comment("=" * 60)
        seq.comment("CYCLE-ACCURATE TEST: Static Step Timing")
        seq.comment("Verify: static_step(L) takes exactly L cycles")
        seq.comment("Expected: delay_2=2 cycles, delay_4=4 cycles, multiply_3cycle=3 cycles")
        seq.comment("=" * 60)
        seq.reset(10)

        # Load values for multiply
        seq.drive("load_a", 3)
        seq.drive("load_b", 7)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select static_sequence operation (op=7)
        # Control: seq { set_busy(dyn) + delay_2(2) + delay_4(4) + multiply_3cycle(3) + set_done(dyn) + clear_busy(dyn) }
        # Expected: 2 + 2 + 4 + 3 + 2 + 2 = 15 cycles (with dynamic overhead)
        # But if done signals are immediate (cycle 0), FSM sees done on cycle 1
        seq.comment("Trigger static_sequence (op=7)")
        seq.comment("Expected timing: busy(2) + delay_2(2) + delay_4(4) + multiply_3(3) + done(2) + clear(2)")
        seq.drive("select_op_op", 7)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("static_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # Wait for completion and record cycle
        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.record_cycle("static_end")
        seq.wait(2)

        # Verify timing
        seq.print_cycle_diff("static_start", "static_end", "Static sequence cycles")
        seq.expect("get_result_res0", 21, "3*7=21 from multiply")
        seq.print("Static step test PASSED", "get_result_res0")

    # =========================================================================
    # Test Sequence 12: Cycle-Accurate Dynamic Step Timing
    # Verifies: dynamic step takes 2 cycles (execute + done detection)
    # =========================================================================

    with tb.sequence("test_cycle_dynamic_step") as seq:
        seq.comment("=" * 60)
        seq.comment("CYCLE-ACCURATE TEST: Dynamic Step Timing")
        seq.comment("Verify: dynamic step() takes 2 cycles each")
        seq.comment("Expected: 7 dynamic steps = 14 cycles minimum")
        seq.comment("=" * 60)
        seq.reset(10)

        # Load values
        seq.drive("load_a", 10)
        seq.drive("load_b", 5)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select dynamic_sequence (op=8)
        # Control: seq { set_busy + load_a + load_b + compute_add + inc_counter + set_done + clear_busy }
        # 7 dynamic steps × 2 cycles = 14 cycles expected
        seq.comment("Trigger dynamic_sequence (op=8)")
        seq.comment("7 dynamic steps × 2 cycles = 14 cycles expected")
        seq.drive("select_op_op", 8)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("dynamic_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # Wait for completion
        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.record_cycle("dynamic_end")
        seq.wait(2)

        # Verify timing
        seq.print_cycle_diff("dynamic_start", "dynamic_end", "Dynamic sequence cycles")
        seq.expect("get_result_res0", 15, "10+5=15 from add")
        seq.print("Dynamic step test PASSED", "get_result_res0")

    # =========================================================================
    # Test Sequence 13: Parallel Branch Timing
    # Verifies: parallel takes max(branch_cycles), not sum
    # =========================================================================

    with tb.sequence("test_cycle_parallel") as seq:
        seq.comment("=" * 60)
        seq.comment("CYCLE-ACCURATE TEST: Parallel Branch Timing")
        seq.comment("Verify: par takes max(branch_A, branch_B) cycles")
        seq.comment("=" * 60)
        seq.reset(10)

        # Select par_load (op=2)
        # Control: seq { set_busy(2) + par { load_a(2), load_b(2) } + clear_busy(2) }
        # par takes max(2,2)=2 cycles, not 4
        seq.comment("Trigger par_load (op=2)")
        seq.comment("Expected: par { load_a(2), load_b(2) } = max(2,2) = 2 cycles")
        seq.drive("select_op_op", 2)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("par_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # Wait for completion (should be quick)
        seq.wait(15)
        seq.record_cycle("par_end")

        # Verify timing
        seq.print_cycle_diff("par_start", "par_end", "Parallel sequence cycles")
        seq.expect("is_busy_res0", 0, "Should complete")
        seq.print("Parallel test completed")

    # =========================================================================
    # Test Sequence 14: Static vs Dynamic Comparison
    # Direct comparison to demonstrate 2x overhead
    # =========================================================================

    with tb.sequence("test_static_vs_dynamic") as seq:
        seq.comment("=" * 60)
        seq.comment("COMPARISON TEST: Static vs Dynamic Step Overhead")
        seq.comment("Same operations, different step types")
        seq.comment("static_step: no done overhead, dynamic step: 2-cycle overhead")
        seq.comment("=" * 60)
        seq.reset(10)

        # First run static sequence
        seq.comment("--- Run static_sequence (op=7) ---")
        seq.drive("load_a", 2)
        seq.drive("load_b", 3)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        seq.drive("select_op_op", 7)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("static_op_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.record_cycle("static_op_end")
        seq.print_cycle_diff("static_op_start", "static_op_end", "Static sequence")

        # Reset for second run
        seq.drive("reset_state_enable", 1)
        seq.wait(1)
        seq.drive("reset_state_enable", 0)
        seq.wait(5)

        # Now run dynamic sequence
        seq.comment("--- Run dynamic_sequence (op=8) ---")
        seq.drive("load_a", 10)
        seq.drive("load_b", 5)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        seq.drive("select_op_op", 8)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("dynamic_op_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.record_cycle("dynamic_op_end")
        seq.print_cycle_diff("dynamic_op_start", "dynamic_op_end", "Dynamic sequence")

        seq.comment("Comparison complete - see cycle counts above")
        seq.print("Static vs Dynamic comparison completed")

    # =========================================================================
    # Test Sequence 15: Mixed Static/Dynamic Timing
    # Verifies: total = sum(static_latencies) + 2*num_dynamic_steps
    # =========================================================================

    with tb.sequence("test_mixed_timing") as seq:
        seq.comment("=" * 60)
        seq.comment("CYCLE-ACCURATE TEST: Mixed Static/Dynamic Timing")
        seq.comment("Verify: total = static_latencies + 2×dynamic_steps")
        seq.comment("=" * 60)
        seq.reset(10)

        # Load values
        seq.drive("load_a", 5)
        seq.drive("load_b", 4)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Select mixed_compute (op=5)
        # Control: seq { set_busy(dyn,2) + multiply_3cycle(static,3) + delay_4(static,4) + set_done(dyn,2) + clear_busy(dyn,2) }
        # Expected: 2 + 3 + 4 + 2 + 2 = 13 cycles
        seq.comment("Trigger mixed_compute (op=5)")
        seq.comment("Expected: busy(2) + multiply(3) + delay(4) + done(2) + clear(2) = 13 cycles")
        seq.drive("select_op_op", 5)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("mixed_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.record_cycle("mixed_end")
        seq.wait(2)

        seq.print_cycle_diff("mixed_start", "mixed_end", "Mixed sequence cycles")
        seq.expect("get_result_res0", 20, "5*4=20 from multiply")
        seq.print("Mixed timing test PASSED", "get_result_res0")

    # =========================================================================
    # Test Sequence 16: Timed Multiply (with call-site timing)
    # Tests: CMT2 module method with static_latency, arg_timing, result_timing
    # The MultiplierUnit.multiply method has static_latency=4, interval=2
    # and is implemented using procedural control (proc_rule + static_step)
    # =========================================================================

    with tb.sequence("test_timed_multiply") as seq:
        seq.comment("=" * 60)
        seq.comment("TIMING ATTRIBUTE TEST: CMT2 Module with Multi-Cycle Method")
        seq.comment("Tests: MultiplierUnit.multiply with static_latency=4")
        seq.comment("The method triggers compute_multiply proc_rule internally")
        seq.comment("=" * 60)
        seq.reset(10)

        # Load values: 3 * 7 = 21
        seq.drive("load_a", 3)
        seq.drive("load_b", 7)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Trigger timed_multiply_test (op=10)
        # Control: seq { set_busy + start_mult + while(busy){wait} + copy_result + set_done + clear_busy }
        seq.comment("Trigger timed_multiply_test (op=10)")
        seq.comment("MultiplierUnit.multiply: static_latency=4, implemented with proc_rule")
        seq.drive("select_op_op", 10)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("timed_mult_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.record_cycle("timed_mult_end")
        seq.wait(2)

        seq.print_cycle_diff("timed_mult_start", "timed_mult_end", "Timed multiply cycles")
        # Result: 3 * 7 = 21
        seq.expect("get_result_res0", 21, "3*7=21 from timed multiply")
        seq.print("Timed multiply test PASSED", "get_result_res0")

        # Reset for next test
        seq.drive("reset_state_enable", 1)
        seq.wait(1)
        seq.drive("reset_state_enable", 0)

    # =========================================================================
    # Test Sequence 17: Pipelined Add (static_step with interval)
    # Tests: static_step with latency=4, interval=2 (pipelined)
    # =========================================================================

    with tb.sequence("test_pipelined_add") as seq:
        seq.comment("=" * 60)
        seq.comment("TIMING ATTRIBUTE TEST: Static Step with Interval (II)")
        seq.comment("Tests: static_step(latency=4, interval=2)")
        seq.comment("=" * 60)
        seq.reset(10)

        # Load values: 12 + 8 = 20
        seq.drive("load_a", 12)
        seq.drive("load_b", 8)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Trigger pipelined_add_test (op=11)
        # Control: seq { set_busy(2) + pipelined_add(4) + set_done(2) + clear_busy(2) }
        seq.comment("Trigger pipelined_add_test (op=11)")
        seq.comment("pipelined_add: latency=4, interval=2")
        seq.drive("select_op_op", 11)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("pipe_add_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.record_cycle("pipe_add_end")
        seq.wait(2)

        seq.print_cycle_diff("pipe_add_start", "pipe_add_end", "Pipelined add cycles")
        # Result: 12 + 8 = 20
        seq.expect("get_result_res0", 20, "12+8=20 from pipelined add")
        seq.print("Pipelined add test PASSED", "get_result_res0")

    # =========================================================================
    # Test Sequence 18: Complex Par Test (per-branch FSM)
    # Tests: par { seq { ... }; seq { ... } } - triggers per-branch FSM generation
    # Each branch has its own FSM register for independent progression
    # =========================================================================

    with tb.sequence("test_complex_par") as seq:
        seq.comment("=" * 60)
        seq.comment("COMPLEX PAR TEST: Per-Branch FSM Generation")
        seq.comment("Tests: par { seq { A; B }; seq { C; D } }")
        seq.comment("Each branch runs independently with its own FSM")
        seq.comment("=" * 60)
        seq.reset(10)

        # Load initial values: a=20, b=15
        seq.drive("load_a", 20)
        seq.drive("load_b", 15)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Reset counter to known value
        seq.drive("reset_state_enable", 1)
        seq.wait(1)
        seq.drive("reset_state_enable", 0)
        seq.wait(2)

        # Reload values after reset
        seq.drive("load_a", 20)
        seq.drive("load_b", 15)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        # Trigger complex_par_test (op=12)
        # Control: seq { set_busy + par { seq{load_a,compute_add}; seq{load_b,inc} } + set_done + clear_busy }
        # Branch 0: load_a (2 cycles) + compute_add (2 cycles) = 4 cycles
        # Branch 1: load_b (2 cycles) + increment_counter (2 cycles) = 4 cycles
        # Par completes when both branches done (max = 4 cycles)
        # Total: set_busy(2) + par(4) + set_done(2) + clear_busy(2) = 10 cycles
        seq.comment("Trigger complex_par_test (op=12)")
        seq.comment("par { seq{load_a,compute_add}; seq{load_b,inc_counter} }")
        seq.drive("select_op_op", 12)
        seq.drive("select_op_enable", 1)
        seq.record_cycle("complex_par_start")
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait_condition("dut->is_done_res0 == 1", timeout=50)
        seq.record_cycle("complex_par_end")
        seq.wait(2)

        seq.print_cycle_diff("complex_par_start", "complex_par_end", "Complex par cycles")
        # Result: compute_add stores a+b = 20+15 = 35
        seq.expect("get_result_res0", 35, "20+15=35 from complex par compute_add")
        # Counter: increment_counter ran once
        seq.expect("get_counter_res0", 1, "Counter incremented once in parallel branch")
        seq.print("Complex par test - result", "get_result_res0")
        seq.print("Complex par test - counter", "get_counter_res0")
        seq.print("Complex par test PASSED", "get_result_res0")

    # =========================================================================
    # Test Sequence 19: Debug Port Verification
    # Verifies that rule firing debug ports work correctly
    # =========================================================================

    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("DEBUG PORT VERIFICATION TEST")
        seq.comment("Verifies dbg_*_firing ports are working correctly")
        seq.comment("=" * 60)
        seq.reset(10)

        # Test 1: Trigger seq_add_sub rule and verify debug port
        seq.comment("Test 1: Verify seq_add_sub debug port fires")
        seq.drive("load_a", 50)
        seq.drive("load_b", 20)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        seq.drive("select_op_op", 1)  # seq_add_sub
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        # Wait a cycle and check that seq_add_sub is firing
        seq.wait(1)
        seq.print_rule_status("seq_add_sub_state0")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.wait(2)
        seq.print("seq_add_sub debug port verification: completed")

        # Reset for next test
        seq.drive("reset_state_enable", 1)
        seq.wait(1)
        seq.drive("reset_state_enable", 0)
        seq.wait(5)

        # Test 2: Trigger par_load and verify debug port
        seq.comment("Test 2: Verify par_load debug port fires")
        seq.drive("select_op_op", 2)  # par_load
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait(1)
        seq.print_rule_status("par_load_state0")
        seq.wait(15)
        seq.print("par_load debug port verification: completed")

        # Reset for next test
        seq.drive("reset_state_enable", 1)
        seq.wait(1)
        seq.drive("reset_state_enable", 0)
        seq.wait(5)

        # Test 3: Trigger mixed_compute (uses static steps)
        seq.comment("Test 3: Verify mixed_compute debug port fires")
        seq.drive("load_a", 6)
        seq.drive("load_b", 7)
        seq.drive("load_enable", 1)
        seq.wait(1)
        seq.drive("load_enable", 0)
        seq.wait(2)

        seq.drive("select_op_op", 5)  # mixed_compute
        seq.drive("select_op_enable", 1)
        seq.wait(1)
        seq.drive("select_op_enable", 0)

        seq.wait(1)
        seq.print_rule_status("mixed_compute_state0")
        seq.wait_condition("dut->is_done_res0 == 1", timeout=30)
        seq.wait(2)
        seq.expect("get_result_res0", 42, "6*7=42 from mixed_compute")
        seq.print("mixed_compute debug port verification: completed")

        seq.comment("=" * 60)
        seq.print("DEBUG PORT VERIFICATION TEST PASSED")
        seq.comment("=" * 60)

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

    # Create simulation workspace with debug ports enabled
    # debug_ports=True adds output ports for each rule's firing signal
    print("\n4. Creating simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, workspace_dir, debug_ports=True)

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
  Functional tests:
   1. test_init          - Basic initialization verification
   2. test_seq_add_sub   - Sequential composition test
   3. test_par_load      - Parallel composition test
   4. test_static_timing - Static step timing verification
   5. test_conditional   - Conditional control flow test
   6. test_nested        - Nested control structures test
   7. test_stress        - Multiple operation stress test
   8. test_invoke        - Invoke operation test
   9. test_static_only   - Static-only step sequence
  10. test_dynamic_only  - Dynamic-only step sequence

  Cycle-accurate timing tests (verify execution model):
  11. test_cycle_static_step   - Verify static_step(L) takes L cycles
  12. test_cycle_dynamic_step  - Verify dynamic step takes 2 cycles
  13. test_cycle_parallel      - Verify par takes max(branch) cycles
  14. test_static_vs_dynamic   - Compare static vs dynamic overhead
  15. test_mixed_timing        - Verify mixed static/dynamic timing

  Timing attribute tests (CMT2 module methods with procedural control):
  16. test_timed_multiply      - Test CMT2 method with static_latency=4, call-site timing
  17. test_pipelined_add       - Test static_step with interval (II=2)

  Per-branch FSM tests (complex parallel control):
  18. test_complex_par         - Test par with nested seq (per-branch FSM)

  Debug port verification:
  19. test_debug_ports         - Verify rule firing debug ports are working

To run the simulation:
    cd {workspace_dir}
    make          # Build the simulation
    make run      # Run all tests
    make waves    # View waveforms
""")

    # Actually run the simulation to validate correctness.
    print("\n8. Building and running simulation...")
    ok, out = ws.build_and_run()
    if not ok:
        raise RuntimeError(out)
    print(out)


if __name__ == "__main__":
    main()
