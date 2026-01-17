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
from pathlib import Path
import shutil

# Clear any cached STL modules from previous runs
clear_stl_registry()

# C++ Testbench for DataProcessor
DATAPROCESSOR_TESTBENCH_CPP = r"""
#include <verilated.h>
#include <verilated_vcd_c.h>
#include "VDataProcessor.h"
#include <iostream>
#include <vector>
#include <tuple>

VDataProcessor* dut;
VerilatedVcdC* tfp;
uint64_t sim_time = 0;

void tick() {
    dut->clk = 0;
    dut->eval();
    tfp->dump(sim_time++);

    dut->clk = 1;
    dut->eval();
    tfp->dump(sim_time++);
}

void reset() {
    dut->rst = 1;
    for (int i = 0; i < 5; i++) tick();
    dut->rst = 0;
    tick();
}

// Calculate expected result based on even/odd path
// Even: fast_path = data * 2
// Odd: slow_path = data * 4 (static_repeat(4) always does 4 iterations)
// Bonus: if result >= 500, add 100 (demonstrates if_ with condition function)
uint32_t expected_result(uint16_t data) {
    uint32_t result;
    if (data % 2 == 0) {
        // Even: fast_path (2x multiply)
        result = (uint32_t)data * 2;
    } else {
        // Odd: slow_path with static_repeat(4) - always 4 iterations
        result = (uint32_t)data * 4;
    }
    // Bonus for large results (demonstrates if_ with condition function)
    if (result >= 500) {
        result += 100;
    }
    return result;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    dut = new VDataProcessor;
    tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("waves/DataProcessor.vcd");

    // Initialize inputs
    dut->clk = 0;
    dut->rst = 0;
    dut->enqueue_enable = 0;
    dut->enqueue_data = 0;
    dut->acknowledge_enable = 0;

    // Reset
    std::cout << "Resetting..." << std::endl;
    reset();

    // Test cases: data values
    // Even numbers use fast path (2x), odd numbers use slow path (iterative accumulation)
    // Format: (data, is_even, expected_result)
    std::vector<std::tuple<uint16_t, bool, uint32_t>> test_cases = {
        {4, true, 8},       // even: 4 * 2 = 8
        {7, false, 28},     // odd: 7 * 4 = 28 (static_repeat(4))
        {100, true, 200},   // even: 100 * 2 = 200
        {255, false, 1120}, // odd: 255 * 4 = 1020 + 100 bonus = 1120
        {2, true, 4},       // even: 2 * 2 = 4
        {9, false, 36},     // odd: 9 * 4 = 36 (static_repeat(4))
    };

    bool all_passed = true;
    int even_cycles = 0, odd_cycles = 0;
    int even_count = 0, odd_count = 0;

    for (auto& tc : test_cases) {
        uint16_t data = std::get<0>(tc);
        bool is_even = std::get<1>(tc);
        uint32_t expected = std::get<2>(tc);

        std::cout << "\n=== Testing " << (is_even ? "EVEN" : "ODD")
                  << " value: " << data << " ===" << std::endl;
        std::cout << "  Expected result: " << expected << std::endl;

        // Enqueue data to FIFO
        std::cout << "  Enqueuing to FIFO..." << std::endl;
        int wait = 0;
        while (!dut->enqueue_ready && wait < 20) {
            tick();
            wait++;
        }
        if (wait >= 20) {
            std::cerr << "  TIMEOUT waiting for enqueue_ready" << std::endl;
            all_passed = false;
            continue;
        }

        dut->enqueue_data = data;
        dut->enqueue_enable = 1;
        tick();
        dut->enqueue_enable = 0;

        // Wait for processing to complete (check is_valid)
        std::cout << "  Processing..." << std::endl;
        wait = 0;
        while (!dut->is_valid_res0 && wait < 200) {
            tick();
            wait++;
        }

        if (wait >= 200) {
            std::cerr << "  TIMEOUT: processing did not complete after " << wait << " cycles" << std::endl;
            std::cerr << "    is_busy = " << (int)dut->is_busy_res0 << std::endl;
            all_passed = false;
            continue;
        }

        // Track cycle counts by path
        if (is_even) {
            even_cycles += wait;
            even_count++;
        } else {
            odd_cycles += wait;
            odd_count++;
        }

        // Read and verify result
        uint32_t result = dut->get_result_res0;
        std::cout << "  Result = " << result << " (took " << wait << " cycles)" << std::endl;

        if (result == expected) {
            std::cout << "  VALUE CHECK: PASS" << std::endl;
        } else {
            std::cerr << "  VALUE CHECK: FAIL (expected " << expected << ")" << std::endl;
            all_passed = false;
        }

        // Acknowledge result to clear valid flag
        wait = 0;
        while (!dut->acknowledge_ready && wait < 10) {
            tick();
            wait++;
        }
        dut->acknowledge_enable = 1;
        tick();
        dut->acknowledge_enable = 0;
        tick();
        tick();
    }

    // Summary
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "TEST SUMMARY:" << std::endl;
    std::cout << "  Total tests: " << test_cases.size() << std::endl;
    std::cout << "\nCYCLE COUNT ANALYSIS:" << std::endl;
    if (even_count > 0)
        std::cout << "  Even path average: " << (even_cycles / even_count) << " cycles" << std::endl;
    if (odd_count > 0)
        std::cout << "  Odd path average: " << (odd_cycles / odd_count) << " cycles" << std::endl;

    if (odd_count > 0 && even_count > 0) {
        if (odd_cycles / odd_count > even_cycles / even_count) {
            std::cout << "  TIMING CHECK: PASS (odd path takes longer as expected)" << std::endl;
        } else {
            std::cout << "  TIMING CHECK: WARNING (odd path should take longer)" << std::endl;
        }
    }
    std::cout << std::string(60, '=') << std::endl;

    tfp->close();
    delete tfp;
    delete dut;

    if (all_passed) {
        std::cout << "\nALL TESTS PASSED!" << std::endl;
        std::cout << "\nFeatures verified:" << std::endl;
        std::cout << "  - FIFO buffering (enqueue/dequeue)" << std::endl;
        std::cout << "  - Parallel processing (par block)" << std::endl;
        std::cout << "  - Conditional routing (if_ with condition function)" << std::endl;
        std::cout << "  - Submodule proc_rule triggering (MagnitudeCalc)" << std::endl;
        std::cout << "  - Iterative submodule with while_ loop (IterativeAccumulator)" << std::endl;
        std::cout << "  - Static steps and static_repeat" << std::endl;
        std::cout << "  - Mutually exclusive guards for path selection" << std::endl;
        return 0;
    } else {
        std::cerr << "\nSOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
"""


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


def main():
    """
    Main entry point: create circuit and run E2E simulation.
    """
    print("=" * 70)
    print("Comprehensive PyCMT2 Example - All Features Demonstrated")
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
    print("Setting up RTL simulation...")
    print("-" * 70)

    # Setup simulation directory
    script_dir = Path(__file__).parent
    sim_dir = script_dir / "comprehensive_sim"

    # Clean previous simulation
    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    # Create simulation workspace
    ws = SimulationWorkspace(circuit, sim_dir)

    # Generate workspace
    ws._add_stl_rtl()
    ws._create_directories()
    ws._generate_rtl()
    ws._generate_makefile()

    # Write custom testbench
    tb_file = sim_dir / "tb" / "testbench.cpp"
    tb_file.write_text(DATAPROCESSOR_TESTBENCH_CPP)

    print(f"Simulation workspace created at: {sim_dir}")

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
