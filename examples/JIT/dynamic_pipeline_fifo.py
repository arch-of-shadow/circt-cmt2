#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Dynamic Pipeline with FIFO1 Modules - End-to-End Test (Testbench DSL)

This example creates a 3-stage dynamic pipeline connected by FIFO1 modules:

    +--------+    +---------+    +--------+    +---------+    +--------+
    | Source | -> | FIFO1_a | -> | Stage1 | -> | FIFO1_b | -> |  Sink  |
    +--------+    +---------+    | (*2)   |    +---------+    +--------+
                                 +--------+

Pipeline stages:
- Source: Generates values 0, 1, 2, 3, 4 (one per cycle when not blocked)
- FIFO1_a: Depth-1 FIFO buffer between source and stage1
- Stage1: Multiplies input by 2 (output = input * 2)
- FIFO1_b: Depth-1 FIFO buffer between stage1 and sink
- Sink: Consumes values and accumulates them into a result register

Expected behavior:
- Source produces: 0, 1, 2, 3, 4
- After Stage1 (*2): 0, 2, 4, 6, 8
- Sink accumulates: 0 + 2 + 4 + 6 + 8 = 20

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/dynamic_pipeline_fifo.py
"""

import cmt2.jit as jit

import os
import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, FIFO1Push, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def create_pipeline_testbench(circuit, expected_result: int):
    """Create testbench using DSL for dynamic pipeline test."""
    tb = Testbench(circuit, auto_debug_ports=True)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("is_done_res0", 0, "Should not be done after reset")
        seq.expect("get_result_res0", 0, "Result should be 0 after reset")
        seq.print("Reset test passed - is_done=0, get_result=0")

    # =========================================================================
    # Test Sequence: Pipeline Computation
    # =========================================================================
    with tb.sequence("test_pipeline") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Dynamic pipeline computation")
        seq.comment("=" * 60)
        seq.comment(f"Expected result: {expected_result}")
        seq.comment("Source: 0,1,2,3,4 -> Stage1(*2): 0,2,4,6,8 -> Sum: 20")
        seq.reset(5)

        seq.record_cycle("start")

        # Wait for done
        seq.wait_condition("dut->is_done_res0", timeout=100)
        seq.record_cycle("end")

        # Verify result
        seq.expect("get_result_res0", expected_result, f"Result should be {expected_result}")
        seq.expect("is_done_res0", 1, "Should be done")

        seq.print_cycle_diff("start", "end", "Pipeline latency")
        seq.print("Result = ", "get_result_res0")
        seq.print("Pipeline test PASSED")

    # =========================================================================
    # Test Sequence: Result Stability Check
    # =========================================================================
    with tb.sequence("test_stability") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Result stability after completion")
        seq.comment("=" * 60)
        seq.reset(5)

        # Wait for done
        seq.wait_condition("dut->is_done_res0", timeout=100)

        # Verify result stays stable
        seq.comment("Checking result stability...")
        for i in range(5):
            seq.wait(1)
            seq.expect("get_result_res0", expected_result, f"Result should stay at {expected_result}")
            seq.expect("is_done_res0", 1, "Should stay done")

        seq.print(f"Stability test PASSED - result stable at {expected_result}")

    # =========================================================================
    # Test Sequence: Debug Port Verification
    # =========================================================================
    with tb.sequence("test_debug_ports") as seq:
        seq.comment("=" * 60)
        seq.comment("Debug Port Verification")
        seq.comment("=" * 60)
        seq.reset(5)

        # Monitor rule firing during pipeline operation
        seq.comment("Monitoring rules during pipeline operation...")
        for i in range(10):
            seq.wait(1)
            seq.print_rule_status("source_push")
            seq.print_rule_status("stage1_process")
            seq.print_rule_status("sink_consume")

        # Wait for completion
        seq.wait_condition("dut->is_done_res0", timeout=100)
        seq.wait(2)

        # After completion, source should not fire (counter exhausted)
        seq.comment("After completion, checking rule status...")
        seq.print_rule_status("mark_done")

        seq.print("Debug port verification completed")

    return tb


@jit.elaborate
def create_pipeline_circuit():
    """Create the dynamic pipeline circuit with FIFO1 modules."""
    clear_stl_registry()

    circuit = Circuit("DynamicPipeline")

    # Create STL modules
    # Note: With FIFO1 (depth-1) and ORAAT semantics, the pipeline achieves
    # 3 cycles per value throughput (source->stage1->sink serialized).
    # This is correct behavior - guards see state at cycle start.
    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)
    fifo32 = FIFO1Push.create(circuit, 32)

    with jit.module(circuit, "Pipeline") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers
        source_counter = m.instance(reg32, "source_counter", clk=clk, rst=rst)
        sink_result = m.instance(reg32, "sink_result", clk=clk, rst=rst)
        done_flag = m.instance(reg1, "done_flag", clk=clk, rst=rst)

        # Pipeline FIFOs
        fifo_a = m.instance(fifo32, "fifo_a", clk=clk, rst=rst)
        fifo_b = m.instance(fifo32, "fifo_b", clk=clk, rst=rst)

        # =================================================================
        # Rule: source_push
        # Push values 0,1,2,3,4 into fifo_a (one per cycle when not blocked)
        # =================================================================
        with jit.rule(m, "source_push") as source_rule:
            with source_rule.guard as g:
                counter = g.call(source_counter, "read")
                can_produce = g.lt(counter, g.const(5, 32))
                g.returns(can_produce)
            with source_rule.body as b:
                counter = b.call(source_counter, "read")
                b.call(fifo_a, "enq", counter)
                new_counter = b.add(counter, b.const(1, 32))
                b.call(source_counter, "write", b.bits(new_counter, 31, 0))

        # =================================================================
        # Rule: stage1_process
        # Read from fifo_a, multiply by 2, push to fifo_b
        # =================================================================
        with jit.rule(m, "stage1_process") as stage1_rule:
            with stage1_rule.guard as g:
                g.always()
            with stage1_rule.body as b:
                val = fifo_a.deq()
                doubled = val * 2
                fifo_b.enq(b.bits(doubled, 31, 0))

        # =================================================================
        # Rule: sink_consume
        # Read from fifo_b and accumulate into sink_result
        # =================================================================
        with jit.rule(m, "sink_consume") as sink_rule:
            with sink_rule.guard as g:
                g.always()
            with sink_rule.body as b:
                val = fifo_b.deq()
                sink_result.write(b.bits(sink_result.read + val, 31, 0))

        # =================================================================
        # Rule: mark_done
        # Set done flag when source has produced all values and FIFOs are empty
        # =================================================================
        with jit.rule(m, "mark_done") as done_rule:
            with done_rule.guard as g:
                counter = g.call(source_counter, "read")
                all_produced = g.eq(counter, g.const(5, 32))
                # For FIFO1, not full == empty (depth 1)
                fifo_a_full = g.call(fifo_a, "full")
                fifo_b_full = g.call(fifo_b, "full")
                fifo_a_empty = g.not_(fifo_a_full)
                fifo_b_empty = g.not_(fifo_b_full)
                fifos_empty = g.and_(fifo_a_empty, fifo_b_empty)
                done = g.call(done_flag, "read")
                not_done = g.not_(done)
                g.returns(g.and_(g.and_(all_produced, fifos_empty), not_done))
            with done_rule.body as b:
                b.call(done_flag, "write", b.const(1, 1))

        @jit.value(m)
        def get_result(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(sink_result.read)

        @jit.value(m)
        def is_done(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(done_flag.read)

        # Precedence: downstream drains before upstream fills
        # sink > stage1 > source > mark_done
        m.precedence(sink_rule.ref(), stage1_rule.ref())
        m.precedence(stage1_rule.ref(), source_rule.ref())
        m.precedence(source_rule.ref(), done_rule.ref())

    return circuit


def main():
    print("=" * 70)
    print("Dynamic Pipeline with FIFO1 - Using Testbench DSL")
    print("=" * 70)
    print("""
Pipeline structure:
  Source -> FIFO1_a -> Stage1(*2) -> FIFO1_b -> Sink

Expected:
  Source: 0,1,2,3,4 -> Stage1(*2): 0,2,4,6,8 -> Sum: 20
""")

    expected_result = 20  # 0*2 + 1*2 + 2*2 + 3*2 + 4*2

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "sim_dynamic_pipeline"

    # Clean previous workspace
    if workspace_dir.exists():
        shutil.rmtree(workspace_dir)

    # Create circuit
    print("1. Creating dynamic pipeline circuit...")
    circuit = create_pipeline_circuit()

    # Emit MLIR
    print("\n2. Emitting MLIR...")
    mlir = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir)} chars")

    # Create testbench using DSL
    print("\n3. Creating testbench using Testbench DSL...")
    tb = create_pipeline_testbench(circuit, expected_result)
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
    print(f"   Expected result: {expected_result}")
    print(f"   Waveforms: {workspace_dir / 'waves' / 'Pipeline.vcd'}")
    print("\n   Status: ALL TESTS PASSED")
    print("=" * 70)

    return 0


if __name__ == "__main__":
    sys.exit(main())
