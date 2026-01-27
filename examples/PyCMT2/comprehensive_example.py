#!/usr/bin/env python3
"""
Comprehensive PyCMT2 Example
============================

Prerequisites:
    Build CIRCT with Python bindings enabled:
        cmake -DCIRCT_BINDINGS_PYTHON_ENABLED=ON ...
        ninja CIRCTPythonModules

    Run from build directory:
        cd circt-cmt2/build
        PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/comprehensive_example.py

This example demonstrates ALL major PyCMT2 features:

PROCEDURAL CONTROL FLOW:
  1. proc_rule - Multi-cycle procedural rules with FSM generation
  2. seq - Sequential composition (execute steps one after another)
  3. par - Parallel composition (execute branches concurrently)
  4. while_ - Dynamic loops with runtime condition (condition function)
  5. if_ with condition function - Conditional with runtime-dependent condition
  6. static_repeat - Fixed iteration loops with compile-time known count
  7. static_step - Fixed-latency operations
  8. step (dynamic) - Variable-latency operations with done signal

DATA STRUCTURES:
  9. FIFO1Push - Single-entry FIFO for buffering
  10. Reg - Register for state storage

ATOMIC OPERATIONS:
  11. rule - Single-cycle rules with guards
  12. method - Action methods that can modify state
  13. value - Read-only value methods

HIERARCHICAL DESIGN:
  14. Submodules with proc_rules - Parent triggers submodule, waits for completion
  15. Module instantiation and method calls across hierarchy

FULL PIPELINE:
  16. CMT2 MLIR -> FIRRTL -> SystemVerilog -> RTL Simulation with Verilator

Architecture:
                    +------------------+
    input  ------->| Input Buffer     |
                    | (FIFO)           |
                    +--------+---------+
                             |
                    +--------v---------+
                    | Parallel Analyze | (par block)
                    | +------+ +-----+ |
                    | |Even/ | |Mag  | | <- MagnitudeCalc submodule
                    | |Odd   | |Calc | |    with proc_rule
                    | +------+ +-----+ |
                    +--------+---------+
                             |
                    +--------v---------+
                    | Conditional      | (if_ with condition function)
                    | Router           |
                    +--------+---------+
                        /         \\
               +-------v-+     +---v-----------+
               |Fast Path|     |Slow Path      |
               |(simple) |     |(Accumulator   | <- IterativeAccumulator
               +---------+     | submodule)    |    with proc_rule + while_
                        \\      +--------------+
                    +----v-------v----+
                    | Output Collect  |
                    +-----------------+

The example runs full end-to-end RTL simulation with Verilator, verifying
that the generated hardware correctly processes test data.
"""

import sys
import os

# Add the build directory to path for imports
build_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
python_pkg_dir = os.path.join(build_dir, "build", "tools", "circt", "python_packages", "circt_core")
if os.path.exists(python_pkg_dir):
    sys.path.insert(0, python_pkg_dir)

from circt.pycmt2 import Circuit, UInt, SInt
from circt.pycmt2.stl import Reg, Wire, FIFO1Push, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from pathlib import Path
import shutil

# Clear any cached STL modules from previous runs
clear_stl_registry()


def create_comprehensive_example():
    """
    Create a comprehensive CMT2 design demonstrating all major features.
    """
    circuit = Circuit("ComprehensiveExample")

    # =========================================================================
    # Part 1: Create STL modules we'll use
    # =========================================================================

    # 16-bit register for state storage
    reg16_mod = Reg.create(circuit, 16, init=0)

    # 32-bit register for accumulator
    reg32_mod = Reg.create(circuit, 32, init=0)

    # 1-bit register for flags
    reg1_mod = Reg.create(circuit, 1, init=0)

    # FIFO for input buffering (16-bit data)
    fifo_mod = FIFO1Push.create(circuit, 16)

    # =========================================================================
    # Part 2: Utility Submodule - Magnitude Calculator
    # =========================================================================

    with circuit.module("MagnitudeCalc") as mag_mod:
        """
        Submodule that calculates the "magnitude" (iteration count)
        of a 16-bit value using a multi-cycle static step.

        This submodule has a proc_rule that fires when start() is called,
        demonstrating submodule proc_rule interaction with parent.
        """
        clk = mag_mod.clock()
        rst = mag_mod.reset()

        # Internal state
        input_reg = mag_mod.instance(reg16_mod, "input_reg", clk=clk, rst=rst)
        result_reg = mag_mod.instance(reg16_mod, "result_reg", clk=clk, rst=rst)
        busy_flag = mag_mod.instance(reg1_mod, "busy", clk=clk, rst=rst)

        # Static step: 2-cycle magnitude calculation
        with mag_mod.static_step(2, "calc_magnitude") as step:
            """
            Static step with 2-cycle latency.
            Calculates iteration count for slow path based on data value.
            """
            val = step.call(input_reg, "read")

            # Use lower 2 bits + 1 to get iteration count (1-4 iterations)
            low_bits = step.bits(val, 1, 0)
            magnitude = step.add(step.pad(low_bits, 16), step.const(1, 16))

            step.call(result_reg, "write", magnitude)

        # Static step: clear busy flag
        with mag_mod.static_step(1, "clear_busy_step") as step:
            step.call(busy_flag, "write", step.const(0, 1))

        # Method: start calculation
        with mag_mod.method("start", args=[("data", UInt(16))]) as meth:
            with meth.guard() as g:
                is_busy = g.call(busy_flag, "read")
                not_busy = g.eq(is_busy, g.const(0, 1))
                g.returns(not_busy)
            with meth.body() as b:
                data = b.arg("data")
                b.call(input_reg, "write", data)
                b.call(busy_flag, "write", b.const(1, 1))

        # Value: get result
        with mag_mod.value("result", returns=[UInt(16)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(result_reg, "read")
                b.returns(result)

        # Value: check if done (not busy)
        with mag_mod.value("done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                is_busy = b.call(busy_flag, "read")
                is_done = b.eq(is_busy, b.const(0, 1))
                b.returns(is_done)

        # Proc rule: trigger calculation when busy
        with mag_mod.proc_rule("run_calc") as rule:
            """
            This proc_rule fires when busy_flag is set by start().
            Executes the magnitude calculation and clears busy.
            """
            with rule.guard() as g:
                is_busy = g.call(busy_flag, "read")
                g.returns(is_busy)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(mag_mod._steps["calc_magnitude"].ref())
                    seq.enable(mag_mod._steps["clear_busy_step"].ref())

    # =========================================================================
    # Part 3: Accumulator Submodule with while_ loop
    # =========================================================================

    with circuit.module("IterativeAccumulator") as acc_mod:
        """
        Submodule that accumulates values using a while loop.
        Demonstrates procedural control with while_.

        When start() is called, it sets up iteration parameters and
        the proc_rule fires to execute the iterative accumulation.
        """
        clk = acc_mod.clock()
        rst = acc_mod.reset()

        # State
        accumulator = acc_mod.instance(reg32_mod, "acc", clk=clk, rst=rst)
        counter = acc_mod.instance(reg16_mod, "counter", clk=clk, rst=rst)
        target = acc_mod.instance(reg16_mod, "target", clk=clk, rst=rst)
        increment = acc_mod.instance(reg16_mod, "increment", clk=clk, rst=rst)
        running = acc_mod.instance(reg1_mod, "running", clk=clk, rst=rst)

        # Static step: single accumulation iteration
        with acc_mod.static_step(1, "accumulate_step") as step:
            """
            One iteration: acc += increment, counter++
            """
            acc_val = step.call(accumulator, "read")
            inc_val = step.call(increment, "read")
            cnt_val = step.call(counter, "read")

            # Accumulate
            inc_extended = step.pad(inc_val, 32)
            new_acc = step.add(acc_val, inc_extended)
            step.call(accumulator, "write", new_acc)

            # Increment counter
            new_cnt = step.add(cnt_val, step.const(1, 16))
            step.call(counter, "write", new_cnt)

        # Static step: clear running flag
        with acc_mod.static_step(1, "clear_running") as step:
            step.call(running, "write", step.const(0, 1))

        # Proc rule: iterative accumulation with while loop
        with acc_mod.proc_rule("run_accumulation") as rule:
            """
            Procedural rule using while loop for iterative computation.
            Demonstrates: while_ with condition function, enable, seq
            """
            with rule.guard() as g:
                is_running = g.call(running, "read")
                g.returns(is_running)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # While counter < target (using condition function)
                    def loop_condition(b):
                        cnt = b.call(counter, "read")
                        tgt = b.call(target, "read")
                        return b.lt(cnt, tgt)

                    with seq.while_(loop_condition) as loop:
                        loop.enable(acc_mod._steps["accumulate_step"].ref())

                    # Clear running after loop completes
                    seq.enable(acc_mod._steps["clear_running"].ref())

        # Method: start accumulation
        with acc_mod.method("start", args=[("iterations", UInt(16)), ("inc_value", UInt(16))]) as meth:
            with meth.guard() as g:
                is_running = g.call(running, "read")
                not_running = g.eq(is_running, g.const(0, 1))
                g.returns(not_running)
            with meth.body() as b:
                iters = b.arg("iterations")
                inc_val = b.arg("inc_value")

                # Initialize state
                b.call(accumulator, "write", b.const(0, 32))
                b.call(counter, "write", b.const(0, 16))
                b.call(target, "write", iters)
                b.call(increment, "write", inc_val)
                b.call(running, "write", b.const(1, 1))

        # Value: get result
        with acc_mod.value("result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(accumulator, "read")
                b.returns(result)

        # Value: check if done
        with acc_mod.value("done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                is_running = b.call(running, "read")
                is_done = b.eq(is_running, b.const(0, 1))
                b.returns(is_done)

    # =========================================================================
    # Part 4: Main Processing Module with All Features
    # =========================================================================

    with circuit.module("DataProcessor") as main_mod:
        """
        Main processing module demonstrating ALL features:
        - FIFO buffering
        - Parallel processing (par)
        - if_ with condition function (ProcCondIfOp)
        - Submodule instantiation and proc_rule triggering
        - Static repeat
        - while_ loops
        """
        clk = main_mod.clock()
        rst = main_mod.reset()

        # ---------------------------------------------------------------------
        # Instantiate components
        # ---------------------------------------------------------------------

        # Input FIFO for buffering
        input_fifo = main_mod.instance(fifo_mod, "input_fifo", clk=clk, rst=rst)

        # Processing registers
        even_flag = main_mod.instance(reg1_mod, "even_flag", clk=clk, rst=rst)
        magnitude_reg = main_mod.instance(reg16_mod, "magnitude", clk=clk, rst=rst)

        # Result storage
        result_reg = main_mod.instance(reg32_mod, "result", clk=clk, rst=rst)
        valid_reg = main_mod.instance(reg1_mod, "valid", clk=clk, rst=rst)

        # Submodule instances - THESE WILL ACTUALLY BE USED
        mag_calc = main_mod.instance(mag_mod, "mag_calc", clk=clk, rst=rst)
        accumulator = main_mod.instance(acc_mod, "accumulator", clk=clk, rst=rst)

        # State for pipeline control
        pipeline_stage = main_mod.instance(reg16_mod, "stage", clk=clk, rst=rst)
        current_data = main_mod.instance(reg16_mod, "current_data", clk=clk, rst=rst)

        # ---------------------------------------------------------------------
        # Method: Enqueue data to FIFO
        # ---------------------------------------------------------------------

        with main_mod.method("enqueue", args=[("data", UInt(16))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as b:
                b.call(input_fifo, "enq", b.arg("data"))

        # ---------------------------------------------------------------------
        # Step definitions
        # ---------------------------------------------------------------------

        # Dequeue from FIFO
        with main_mod.static_step(1, "dequeue_and_start") as step:
            data = step.call(input_fifo, "deq")
            step.call(current_data, "write", data)
            step.call(valid_reg, "write", step.const(0, 1))
            step.call(pipeline_stage, "write", step.const(1, 16))

        # Check even/odd
        with main_mod.static_step(1, "check_even_odd") as step:
            data = step.call(current_data, "read")
            is_even = step.eq(step.bit(data, 0), step.const(0, 1))
            step.call(even_flag, "write", is_even)

        # Start magnitude calculation submodule
        with main_mod.static_step(1, "start_mag_calc") as step:
            data = step.call(current_data, "read")
            step.call(mag_calc, "start", data)

        # Wait step (no-op, for while loops)
        with main_mod.static_step(1, "wait_step") as step:
            pass

        # Inline magnitude calculation: magnitude = (data & 3) + 1
        with main_mod.static_step(1, "calc_magnitude_inline") as step:
            data = step.call(current_data, "read")
            low_bits = step.bits(data, 1, 0)  # data & 3
            low_bits_ext = step.pad(low_bits, 16)
            magnitude = step.add(low_bits_ext, step.const(1, 16))  # + 1
            step.call(magnitude_reg, "write", magnitude)

        # Move to stage 2
        with main_mod.static_step(1, "move_to_stage2") as step:
            step.call(pipeline_stage, "write", step.const(2, 16))

        # Fast path: result = data * 2
        with main_mod.static_step(1, "fast_path_compute") as step:
            data = step.call(current_data, "read")
            data_ext = step.pad(data, 32)
            doubled = step.add(data_ext, data_ext)
            step.call(result_reg, "write", doubled)
            step.call(pipeline_stage, "write", step.const(5, 16))

        # Slow path: initialize result to 0
        with main_mod.static_step(1, "slow_path_init") as step:
            step.call(result_reg, "write", step.const(0, 32))

        # Slow path: single accumulation iteration (result += data)
        with main_mod.static_step(1, "slow_path_accumulate") as step:
            result = step.call(result_reg, "read")
            data = step.call(current_data, "read")
            data_ext = step.pad(data, 32)
            new_result = step.add(result, data_ext)
            step.call(result_reg, "write", new_result)

        # Slow path: move to done stage after accumulation
        with main_mod.static_step(1, "slow_path_done") as step:
            step.call(pipeline_stage, "write", step.const(5, 16))

        # Conditional bonus: add 100 if result >= 500 (demonstrates if_ with condition function)
        with main_mod.static_step(1, "apply_bonus") as step:
            result = step.call(result_reg, "read")
            bonus_amount = step.const(100, 32)
            new_result = step.add(result, bonus_amount)
            step.call(result_reg, "write", new_result)

        # Finalize result
        with main_mod.static_step(1, "finalize_result") as step:
            step.call(valid_reg, "write", step.const(1, 1))
            step.call(pipeline_stage, "write", step.const(0, 16))

        # ---------------------------------------------------------------------
        # Pipeline Rules - Using ALL control flow features
        # ---------------------------------------------------------------------

        # Rule 1: Dequeue from FIFO (stage 0 -> 1)
        with main_mod.proc_rule("stage0_dequeue") as rule:
            with rule.guard() as g:
                stage = g.call(pipeline_stage, "read")
                is_idle = g.eq(stage, g.const(0, 16))
                g.returns(is_idle)

            with rule.control() as ctrl:
                ctrl.enable(main_mod._steps["dequeue_and_start"].ref())

        # Rule 2: Analysis (stage 1)
        # Demonstrates: par block for parallel execution
        with main_mod.proc_rule("stage1_analysis") as rule:
            """
            Analysis of data using parallel execution:
            1. Check even/odd flag  |  (parallel)
               Calculate magnitude  |
            2. Move to stage 2 (after both complete)

            Demonstrates: par block - check_even_odd and calc_magnitude_inline
            run in parallel since they both read current_data but write to
            different registers.
            """
            with rule.guard() as g:
                stage = g.call(pipeline_stage, "read")
                is_stage1 = g.eq(stage, g.const(1, 16))
                g.returns(is_stage1)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # Par block: both steps read current_data, write to different regs
                    with seq.par() as p:
                        p.enable(main_mod._steps["check_even_odd"].ref())
                        p.enable(main_mod._steps["calc_magnitude_inline"].ref())
                    # After parallel steps complete, move to next stage
                    seq.enable(main_mod._steps["move_to_stage2"].ref())

        # Rule 3a: Fast path for even numbers (stage 2)
        # Uses mutually exclusive guard with stage2_odd_slow
        with main_mod.proc_rule("stage2_even_fast") as rule:
            """
            Fast path for even numbers using mutually exclusive guards.
            """
            with rule.guard() as g:
                stage = g.call(pipeline_stage, "read")
                is_stage2 = g.eq(stage, g.const(2, 16))
                is_even = g.call(even_flag, "read")
                cond = g.and_(is_stage2, is_even)
                g.returns(cond)

            with rule.control() as ctrl:
                ctrl.enable(main_mod._steps["fast_path_compute"].ref())

        # Rule 3b: Slow path for odd numbers (stage 2)
        # Demonstrates: static_repeat
        with main_mod.proc_rule("stage2_odd_slow") as rule:
            """
            Slow path for odd numbers using static_repeat.
            Demonstrates: nested seq and static_repeat(4)
            """
            with rule.guard() as g:
                stage = g.call(pipeline_stage, "read")
                is_stage2 = g.eq(stage, g.const(2, 16))
                is_even = g.call(even_flag, "read")
                is_odd = g.eq(is_even, g.const(0, 1))
                cond = g.and_(is_stage2, is_odd)
                g.returns(cond)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    seq.enable(main_mod._steps["slow_path_init"].ref())
                    with seq.static_repeat(4) as loop:
                        loop.enable(main_mod._steps["slow_path_accumulate"].ref())
                    seq.enable(main_mod._steps["slow_path_done"].ref())

        # Rule 5: Finalize (stage 5)
        # Demonstrates: if_ with condition function (ProcCondIfOp)
        with main_mod.proc_rule("stage5_finalize") as rule:
            """
            Finalization with conditional bonus using if_ with condition function.
            Demonstrates: ProcCondIfOp - dynamic condition evaluated each cycle.
            If result >= 500, add bonus of 100 before finalizing.
            """
            with rule.guard() as g:
                stage = g.call(pipeline_stage, "read")
                is_done = g.eq(stage, g.const(5, 16))
                g.returns(is_done)

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # Condition function: evaluate result >= 500 dynamically
                    def check_large_result(b):
                        result = b.call(result_reg, "read")
                        threshold = b.const(500, 32)
                        return b.ge(result, threshold)  # greater or equal

                    # if_ with condition function (creates ProcCondIfOp)
                    with seq.if_(check_large_result) as if_:
                        with if_.then_() as then_builder:
                            then_builder.enable(main_mod._steps["apply_bonus"].ref())

                    # Always finalize
                    seq.enable(main_mod._steps["finalize_result"].ref())

        # ---------------------------------------------------------------------
        # Value methods for external access
        # ---------------------------------------------------------------------

        with main_mod.value("get_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(result_reg, "read")
                b.returns(result)

        with main_mod.value("is_valid", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                valid = b.call(valid_reg, "read")
                b.returns(valid)

        with main_mod.value("is_busy", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                stage = b.call(pipeline_stage, "read")
                is_busy = b.neq(stage, b.const(0, 16))
                b.returns(is_busy)

        # Method for acknowledging result
        with main_mod.method("acknowledge") as meth:
            with meth.guard() as g:
                valid = g.call(valid_reg, "read")
                g.returns(valid)
            with meth.body() as b:
                b.call(valid_reg, "write", b.const(0, 1))

    return circuit


def create_comprehensive_testbench(circuit):
    """Create testbench using DSL for comprehensive example."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # Test cases: (data, is_even, expected_result)
    # Even: fast_path = data * 2
    # Odd: slow_path = data * 4 (static_repeat(4))
    # Bonus: if result >= 500, add 100
    test_cases = [
        (4, True, 8),       # even: 4 * 2 = 8
        (7, False, 28),     # odd: 7 * 4 = 28 (static_repeat(4))
        (100, True, 200),   # even: 100 * 2 = 200
        (255, False, 1120), # odd: 255 * 4 = 1020 + 100 bonus = 1120
        (2, True, 4),       # even: 2 * 2 = 4
        (9, False, 36),     # odd: 9 * 4 = 36 (static_repeat(4))
    ]

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(2)
        seq.expect("is_busy_res0", 0, "Should not be busy after reset")
        seq.expect("is_valid_res0", 0, "Should not be valid after reset")
        seq.print("Reset test passed")

    # =========================================================================
    # Test Sequences: Data Processing Tests
    # =========================================================================
    for i, (data, is_even, expected) in enumerate(test_cases):
        path_name = "EVEN" if is_even else "ODD"
        with tb.sequence(f"test_data_{i+1}") as seq:
            seq.comment(f"Test {path_name} value: {data}, expected result: {expected}")
            seq.reset(5)

            # Wait for enqueue to be ready
            seq.comment("Enqueue data to FIFO")
            seq.wait_condition("dut->enqueue_ready", timeout=20)
            seq.drive("enqueue_data", data)
            seq.drive("enqueue_enable", 1)
            seq.wait(1)
            seq.drive("enqueue_enable", 0)

            # Wait for processing to complete
            seq.comment("Wait for processing to complete")
            seq.record_cycle(f"proc_start_{i}")
            seq.wait_condition("dut->is_valid_res0", timeout=200)
            seq.record_cycle(f"proc_end_{i}")

            # Verify result
            seq.expect("get_result_res0", expected, f"Data {data} should produce {expected}")
            seq.print_cycle_diff(f"proc_start_{i}", f"proc_end_{i}", f"{path_name} path processing time")
            seq.print(f"Test {i+1} result: ", "get_result_res0")

            # Acknowledge result
            seq.wait_condition("dut->acknowledge_ready", timeout=10)
            seq.drive("acknowledge_enable", 1)
            seq.wait(1)
            seq.drive("acknowledge_enable", 0)
            seq.wait(2)

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification Test")
        seq.comment("=" * 60)
        seq.reset(5)

        # Process an even number (fast path)
        seq.comment("Process even number to verify stage debug ports")
        seq.wait_condition("dut->enqueue_ready", timeout=20)
        seq.drive("enqueue_data", 10)  # even: 10 * 2 = 20
        seq.drive("enqueue_enable", 1)
        seq.wait(1)
        seq.drive("enqueue_enable", 0)

        # Monitor debug ports during processing
        seq.wait(2)
        seq.print_rule_status("stage0_dequeue_state0")
        seq.wait(3)
        seq.print_rule_status("stage1_analysis_state0")
        seq.wait(3)
        seq.print_rule_status("stage2_even_fast_state0")

        # Wait for completion
        seq.wait_condition("dut->is_valid_res0", timeout=100)
        seq.expect("get_result_res0", 20, "10*2=20 for even path")

        # Acknowledge
        seq.wait_condition("dut->acknowledge_ready", timeout=10)
        seq.drive("acknowledge_enable", 1)
        seq.wait(1)
        seq.drive("acknowledge_enable", 0)
        seq.wait(2)

        # Process an odd number (slow path)
        seq.comment("Process odd number to verify slow path debug ports")
        seq.wait_condition("dut->enqueue_ready", timeout=20)
        seq.drive("enqueue_data", 5)  # odd: 5 * 4 = 20
        seq.drive("enqueue_enable", 1)
        seq.wait(1)
        seq.drive("enqueue_enable", 0)

        seq.wait(5)
        seq.print_rule_status("stage2_odd_slow_state0")

        seq.wait_condition("dut->is_valid_res0", timeout=100)
        seq.expect("get_result_res0", 20, "5*4=20 for odd path")

        seq.print("Debug port verification PASSED")

    # =========================================================================
    # Test Sequence: Timing Comparison (Even vs Odd path)
    # =========================================================================
    with tb.sequence("test_timing_comparison") as seq:
        seq.comment("=" * 60)
        seq.comment("Timing Comparison: Even (fast) vs Odd (slow) paths")
        seq.comment("=" * 60)

        # Test even path timing
        seq.reset(5)
        seq.comment("--- Even path timing ---")
        seq.wait_condition("dut->enqueue_ready", timeout=20)
        seq.drive("enqueue_data", 6)  # even
        seq.drive("enqueue_enable", 1)
        seq.wait(1)
        seq.drive("enqueue_enable", 0)

        seq.record_cycle("even_start")
        seq.wait_condition("dut->is_valid_res0", timeout=100)
        seq.record_cycle("even_end")
        seq.print_cycle_diff("even_start", "even_end", "Even path cycles")

        # Acknowledge
        seq.wait_condition("dut->acknowledge_ready", timeout=10)
        seq.drive("acknowledge_enable", 1)
        seq.wait(1)
        seq.drive("acknowledge_enable", 0)
        seq.wait(5)

        # Test odd path timing
        seq.comment("--- Odd path timing ---")
        seq.wait_condition("dut->enqueue_ready", timeout=20)
        seq.drive("enqueue_data", 7)  # odd
        seq.drive("enqueue_enable", 1)
        seq.wait(1)
        seq.drive("enqueue_enable", 0)

        seq.record_cycle("odd_start")
        seq.wait_condition("dut->is_valid_res0", timeout=100)
        seq.record_cycle("odd_end")
        seq.print_cycle_diff("odd_start", "odd_end", "Odd path cycles")

        seq.print("Timing comparison completed - odd path should take longer")

    return tb


def main():
    """
    Main entry point: create circuit and run E2E simulation.
    """
    print("=" * 70)
    print("Comprehensive PyCMT2 Example - Using Testbench DSL")
    print("=" * 70)
    print()

    # Create the comprehensive example
    circuit = create_comprehensive_example()

    # Emit CMT2 MLIR
    print("Generating CMT2 MLIR...")
    mlir = circuit.emit_mlir()

    print()
    print("-" * 70)
    print("CMT2 MLIR Output (first 3000 chars):")
    print("-" * 70)
    print(mlir[:3000])
    if len(mlir) > 3000:
        print(f"... ({len(mlir) - 3000} more characters)")

    # =========================================================================
    # RTL Simulation
    # =========================================================================

    print()
    print("-" * 70)
    print("Setting up RTL simulation with Testbench DSL...")
    print("-" * 70)

    # Setup simulation directory
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "comprehensive_sim"

    # Clean previous simulation
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    # Create testbench using DSL
    print("Creating testbench using Testbench DSL...")
    tb = create_comprehensive_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports enabled
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    # Generate workspace with testbench
    print(f"Generating workspace at: {sim_dir}")
    ws.generate_with_testbench(tb)

    # Build simulation
    print()
    print("-" * 70)
    print("Building simulation...")
    print("-" * 70)

    if not ws.build():
        print("Build failed!")
        return 1

    print("Build successful!")

    # Run simulation
    print()
    print("-" * 70)
    print("Running simulation...")
    print("-" * 70)

    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    print()
    print("=" * 70)
    print("Example completed successfully!")
    print(f"Waveforms available at: {sim_dir / 'waves' / 'DataProcessor.vcd'}")
    print("=" * 70)

    return 0


if __name__ == "__main__":
    sys.exit(main())
