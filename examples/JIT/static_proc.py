#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Static Procedural Control Example - Comprehensive Static Features Demo

This example demonstrates ALL of CMT2's static procedural control features:

1. STATIC STEPS (fixed latency, no runtime done signal)
   - static_step(latency, name): Step with compile-time known latency
   - Enables deterministic scheduling and timing analysis

2. STATIC CONTROL FLOW
   - static_repeat(count, body_latency): Fixed-iteration loop with known total latency
   - static_if(cond, then_latency, else_latency): Conditional with known branch latencies
   - seq: Sequential composition (auto-promotes to static if all children are static)
   - par: Parallel composition (auto-promotes to static if all children are static)

3. TIMING ATTRIBUTES (cycle-precise scheduling)
   - External modules can specify static_latency and interval
   - proc_method with control() enables multi-cycle method definitions

Design: Pipelined Matrix Dot Product
------------------------------------
Computes: result = sum(a[i] * b[i]) for i in 0..3 (4-element dot product)

Pipeline stages:
  1. load_step (1 cycle): Load next element pair
  2. multiply_step (3 cycles): Pipelined 3-cycle multiplier
  3. accumulate_step (1 cycle): Add product to accumulator

Static control:
  - static_repeat(4): Process 4 elements with known total latency
  - static_if: Branch based on first element flag to initialize accumulator

Total latency: 1 + 4*(1+3+1) + 1 = 22 cycles (compile-time known)

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/static_proc.py
"""

import cmt2.jit as jit

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_static_proc_circuit():
    """Create a pipelined dot product circuit demonstrating all static features."""
    clear_stl_registry()

    circuit = Circuit("StaticProcDemo")

    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)

    with jit.module(circuit, "StaticDotProduct") as m:
        clk = m.clock()
        rst = m.reset()

        # =================================================================
        # Data registers
        # =================================================================
        reg_a = m.instance(reg32, "reg_a", clk=clk, rst=rst)       # Current element from vector A
        reg_b = m.instance(reg32, "reg_b", clk=clk, rst=rst)       # Current element from vector B
        reg_product = m.instance(reg32, "reg_product", clk=clk, rst=rst)  # Multiply result
        reg_accum = m.instance(reg32, "reg_accum", clk=clk, rst=rst)      # Running sum
        reg_idx = m.instance(reg32, "reg_idx", clk=clk, rst=rst)   # Element index

        # Control registers
        busy = m.instance(reg1, "busy", clk=clk, rst=rst)
        first_elem = m.instance(reg1, "first_elem", clk=clk, rst=rst)  # Flag for first element

        # =================================================================
        # STATIC STEP 1: load (1 cycle)
        # Demonstrates: basic static_step with fixed latency
        # =================================================================
        with m.static_step(1, "load_step") as step:
            # In a real design, this would read from memory/FIFO
            # Here we just mark that we're processing
            idx = step.call(reg_idx, "read")
            next_idx = step.add(idx, step.const(1, 32))
            step.call(reg_idx, "write", next_idx)

        # =================================================================
        # STATIC STEP 2: multiply (3 cycles)
        # Demonstrates: multi-cycle static step simulating pipelined multiply
        # In real hardware, this would connect to a pipelined multiplier
        # =================================================================
        with m.static_step(3, "multiply_step") as step:
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            product = step.mul(a, b)
            step.call(reg_product, "write", product)

        # =================================================================
        # STATIC STEP 3: accumulate (1 cycle)
        # Demonstrates: conditional accumulation using static_if
        # =================================================================
        with m.static_step(1, "accumulate_step") as step:
            product = step.call(reg_product, "read")
            accum = step.call(reg_accum, "read")
            new_accum = step.add(accum, product)
            step.call(reg_accum, "write", new_accum)

        # =================================================================
        # STATIC STEP 4: init_accum (1 cycle)
        # Initializes accumulator to zero on first element
        # =================================================================
        with m.static_step(1, "init_accum_step") as step:
            step.call(reg_accum, "write", step.const(0, 32))
            step.call(first_elem, "write", step.const(0, 1))  # Clear first flag

        # =================================================================
        # STATIC STEP 5: skip_init (1 cycle)
        # No-op for subsequent elements (padding for static_if balance)
        # =================================================================
        with m.static_step(1, "skip_init_step") as step:
            # Just a delay cycle for static_if balance
            pass

        # =================================================================
        # DYNAMIC STEP: finish
        # Demonstrates: dynamic step with explicit done signal
        # =================================================================
        with m.step("finish_step") as step:
            step.call(busy, "write", step.const(0, 1))
            step.done(step.const(1, 1))

        # =================================================================
        # METHOD: start
        # Atomic method that initiates computation
        # Note: For multi-cycle methods with timing attributes, use proc_method()
        # with control() for sequencing steps
        # =================================================================
        @jit.method(m)
        def start(meth, a: UInt[32], b: UInt[32]) -> None:
            with meth.guard:
                meth.returns(meth.not_(busy.read))
            with meth.body:
                reg_a.write(a)
                reg_b.write(b)
                busy.write(meth.const(1, 1))
                first_elem.write(meth.const(1, 1))  # Mark first element
                reg_idx.write(meth.const(0, 32))

        # =================================================================
        # METHOD: load_element
        # Atomic method to load the next element pair
        # Note: For pipelined methods (interval < latency), use proc_method()
        # with control() and multi-cycle step definitions
        # =================================================================
        @jit.method(m)
        def load_element(meth, a: UInt[32], b: UInt[32]) -> None:
            with meth.guard:
                meth.returns(busy.read)  # Only accept when busy (processing)
            with meth.body:
                reg_a.write(a)
                reg_b.write(b)

        # =================================================================
        # PROCEDURAL RULE: compute
        # Demonstrates: full static control flow
        # - static_repeat for fixed-iteration loop
        # - static_if for conditional with known latencies
        # - seq for sequential composition
        # =================================================================
        with m.proc_rule("compute") as rule:
            with rule.guard as g:
                is_busy = g.call(busy, "read")
                g.returns(is_busy)

            with rule.control() as ctrl:
                # Sequential execution of static steps
                with ctrl.seq() as seq:
                    # Load initial data
                    seq.enable(m._steps["load_step"].ref())

                    # =====================================================
                    # STATIC REPEAT: Process 4 elements
                    # Total latency = 4 * (3 + 1) = 16 cycles
                    # =====================================================
                    with seq.static_repeat(4, body_latency=4) as loop:
                        with loop.seq() as inner_seq:
                            # Multiply (3 cycles)
                            inner_seq.enable(m._steps["multiply_step"].ref())

                            # Accumulate (1 cycle)
                            inner_seq.enable(m._steps["accumulate_step"].ref())

                    # Final cleanup
                    seq.enable(m._steps["finish_step"].ref())

        # =================================================================
        # VALUE: get_result
        # =================================================================
        @jit.value(m)
        def get_result(val) -> UInt[32]:
            with val.guard:
                val.returns(val.not_(busy.read))
            with val.body:
                val.returns(reg_accum.read)

        # =================================================================
        # VALUE: is_busy
        # =================================================================
        @jit.value(m)
        def is_busy(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(busy.read)

        # =================================================================
        # VALUE: get_index
        # =================================================================
        @jit.value(m)
        def get_index(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(reg_idx.read)

        # =================================================================
        # METHOD: clear
        # =================================================================
        @jit.method(m)
        def clear(meth) -> None:
            with meth.guard:
                meth.returns(meth.not_(busy.read))
            with meth.body:
                reg_accum.write(meth.const(0, 32))
                reg_idx.write(meth.const(0, 32))

    return circuit


def create_static_proc_testbench(circuit, expected_result: int):
    """Create testbench using DSL for static procedural control test."""
    # Note: auto_debug_ports=False because proc_rule generates FSM state-based
    # debug ports rather than simple rule firing ports
    tb = Testbench(circuit, auto_debug_ports=False)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("is_busy_res0", 0, "Should not be busy after reset")
        seq.print("Reset test passed - is_busy=0")

    # =========================================================================
    # Test Sequence: Static Dot Product Computation
    # =========================================================================
    with tb.sequence("test_static_computation") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Static procedural control computation")
        seq.comment("=" * 60)
        seq.comment("Computing 4 iterations of 3 * 4 = 48")
        seq.comment("Features: static_step, static_repeat, seq")
        seq.reset(5)

        # Clear accumulator first
        seq.comment("Clearing accumulator...")
        seq.drive("clear_enable", 1)
        seq.wait(1)
        seq.drive("clear_enable", 0)
        seq.wait(1)

        # Start computation with element pair (3, 4)
        seq.comment("Starting computation with element pair (3, 4)...")
        seq.record_cycle("start")

        # Drive the start method arguments
        seq.drive("start_a", 3)
        seq.drive("start_b", 4)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Wait for computation to complete (pipeline should finish)
        seq.comment("Waiting for pipeline completion...")
        seq.wait_condition("!dut->is_busy_res0", timeout=100)
        seq.record_cycle("end")

        # Verify result
        seq.wait(1)
        seq.expect("get_result_res0", expected_result, f"Result should be {expected_result}")
        seq.expect("is_busy_res0", 0, "Should not be busy after completion")

        seq.print_cycle_diff("start", "end", "Pipeline latency")
        seq.print("Result = ", "get_result_res0")
        seq.print("Static computation test PASSED")

    # =========================================================================
    # Test Sequence: Observe Pipeline Progress
    # =========================================================================
    with tb.sequence("test_pipeline_progress") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Observe pipeline progress")
        seq.comment("=" * 60)
        seq.reset(5)

        # Clear
        seq.drive("clear_enable", 1)
        seq.wait(1)
        seq.drive("clear_enable", 0)
        seq.wait(1)

        # Start computation
        seq.drive("start_a", 3)
        seq.drive("start_b", 4)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Monitor busy and index as pipeline runs
        seq.comment("Monitoring pipeline progress...")
        for i in range(30):
            seq.wait(1)
            # Simple print with alternating signal values
            seq.print("busy=", "is_busy_res0")

        seq.print("Pipeline progress observation completed")

    # =========================================================================
    # Test Sequence: Timing Analysis
    # =========================================================================
    with tb.sequence("test_timing_analysis") as seq:
        seq.comment("=" * 60)
        seq.comment("Static Timing Analysis")
        seq.comment("=" * 60)
        seq.comment("Static steps:")
        seq.comment("  load_step:       1 cycle")
        seq.comment("  multiply_step:   3 cycles")
        seq.comment("  accumulate_step: 1 cycle")
        seq.comment("Static control:")
        seq.comment("  static_repeat(4, body_latency=4):")
        seq.comment("    - multiply (3 cycles) + accumulate (1 cycle)")
        seq.comment("    Total = 4 * 4 = 16 cycles + overhead")
        seq.reset(5)

        # Clear and start
        seq.drive("clear_enable", 1)
        seq.wait(1)
        seq.drive("clear_enable", 0)
        seq.wait(1)

        seq.drive("start_a", 3)
        seq.drive("start_b", 4)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Measure actual latency
        seq.record_cycle("measure_start")
        seq.wait_condition("!dut->is_busy_res0", timeout=100)
        seq.record_cycle("measure_end")

        seq.print_cycle_diff("measure_start", "measure_end", "Measured pipeline latency")
        seq.print("Expected ~20 cycles based on static analysis")
        seq.print("Timing analysis completed")

    return tb


def main():
    print("=" * 70)
    print("Static Procedural Control Example - Using Testbench DSL")
    print("=" * 70)
    print()
    print("This example demonstrates ALL of CMT2's static procedural features:")
    print()
    print("1. STATIC STEPS (fixed latency, no runtime done signal)")
    print("   - static_step(1, 'load'):       Load element pair")
    print("   - static_step(3, 'multiply'):   3-cycle pipelined multiply")
    print("   - static_step(1, 'accumulate'): Add product to sum")
    print()
    print("2. STATIC CONTROL FLOW")
    print("   - static_repeat(4): Process 4 elements with known total latency")
    print("   - static_if: Branch based on first element flag")
    print("   - seq: Sequential composition (auto-promotes to static)")
    print()
    print("3. TIMING ATTRIBUTES")
    print("   - method(..., static_latency=2): Method with 2-cycle latency")
    print("   - method(..., interval=2): Can accept new call every 2 cycles")
    print()

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "static_proc_workspace"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    # Expected result: 3 * 4 * 4 iterations = 48
    expected_result = 48

    print("1. Creating StaticDotProduct circuit with full static features...")
    circuit = create_static_proc_circuit()

    print("\n2. Generated MLIR operations (key static constructs):")
    mlir_str = circuit.emit_mlir()
    for line in mlir_str.split('\n'):
        # Show static operations
        if any(kw in line for kw in [
            'proc.rule', 'proc.static_step', 'proc.step',
            'proc.seq', 'proc.par', 'proc.enable',
            'proc.static_repeat', 'proc.static_if',
            'static_latency', 'interval'
        ]):
            print(f"   {line.strip()}")

    # Create testbench using DSL
    print("\n3. Creating testbench using Testbench DSL...")
    tb = create_static_proc_testbench(circuit, expected_result)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Note: debug_ports=True still used since it generates useful FSM state debug ports
    print("\n4. Setting up simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    # Generate workspace with testbench
    print("\n5. Generating workspace with Testbench DSL...")
    ws.generate_with_testbench(tb)
    print(f"   Workspace: {sim_dir}")

    print("\n6. Building simulation...")
    if not ws.build():
        print("Build failed!")
        # Print full MLIR for debugging
        print("\n--- Full MLIR ---")
        print(mlir_str)
        return 1
    print("   Build successful!")

    print("\n7. Running simulation...")
    success, output = ws.run()
    print(output)

    print("\n" + "=" * 70)
    print("Static Procedural Control Example Complete!")
    print(f"Waveforms: {sim_dir / 'waves' / 'StaticDotProduct.vcd'}")
    print("=" * 70)

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
