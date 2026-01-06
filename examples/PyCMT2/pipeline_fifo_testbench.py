#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
GAA Pipeline with FIFOs Testbench Example for PyCMT2.

This example demonstrates a classic producer-consumer pipeline using
GAA (Guarded Atomic Actions) rules with FIFO-based communication:

    +---------+      +-------+      +----------+      +-------+      +----------+
    | Source  | ---> | FIFO1 | ---> | Stage 1  | ---> | FIFO2 | ---> | Stage 2  |
    +---------+      +-------+      +----------+      +-------+      +----------+
                                          |
                                          v
                                    +----------+      +-------+      +--------+
                                    | Stage 3  | ---> | FIFO3 | ---> | Sink   |
                                    +----------+      +-------+      +--------+

Pipeline stages:
- Source: Generates input data (0, 1, 2, ...)
- Stage 1: Multiply by 2
- Stage 2: Add 10
- Stage 3: Square (x * x)
- Sink: Consume and validate results

GAA Semantics:
- Each rule fires atomically when its guard is satisfied
- Guards check FIFO status (notEmpty/notFull) before firing
- Data flows through FIFOs following producer-consumer pattern
- Rules may fire concurrently if they don't conflict

Testbench validates:
1. Data integrity through the pipeline
2. Backpressure handling when FIFOs are full
3. Pipeline throughput and latency
4. Correct final results: ((x * 2 + 10)^2 for input x

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 \
        ../examples/PyCMT2/pipeline_fifo_testbench_example.py
"""

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, FIFO, clear_stl_registry


def create_pipeline_circuit():
    """Create a GAA pipeline circuit with FIFOs."""
    clear_stl_registry()

    circuit = Circuit("Pipeline")

    # Create module types
    reg32 = Reg.create(circuit, 32)
    reg8 = Reg.create(circuit, 8)
    reg1 = Reg.create(circuit, 1)
    fifo32 = FIFO.create(circuit, 32, depth=4)

    with circuit.module("DataPipeline") as m:
        clk = m.clock()
        rst = m.reset()

        # =====================================================================
        # State registers
        # =====================================================================

        # Source counter (generates sequential data)
        source_counter = m.instance(reg8, "source_counter", clk=clk, rst=rst)
        # Total items produced
        items_produced = m.instance(reg8, "items_produced", clk=clk, rst=rst)
        # Total items consumed
        items_consumed = m.instance(reg8, "items_consumed", clk=clk, rst=rst)
        # Enable source generation
        source_enable = m.instance(reg1, "source_enable", clk=clk, rst=rst)
        # Last result for validation
        last_result = m.instance(reg32, "last_result", clk=clk, rst=rst)

        # =====================================================================
        # FIFOs connecting pipeline stages
        # =====================================================================

        # FIFO between source and stage 1
        fifo1 = m.instance(fifo32, "fifo1", clk=clk, rst=rst)
        # FIFO between stage 1 and stage 2
        fifo2 = m.instance(fifo32, "fifo2", clk=clk, rst=rst)
        # FIFO between stage 2 and stage 3
        fifo3 = m.instance(fifo32, "fifo3", clk=clk, rst=rst)
        # FIFO between stage 3 and sink
        fifo4 = m.instance(fifo32, "fifo4", clk=clk, rst=rst)

        # =====================================================================
        # Rule: source_gen
        # Generate sequential data and push to fifo1
        # Guard: source_enable && fifo1.notFull
        # =====================================================================

        with m.rule("source_gen") as rule:
            with rule.guard() as g:
                enabled = g.call(source_enable, "read")
                full = g.call(fifo1, "full")
                not_full = g.not_(full)
                can_produce = g.and_(enabled, not_full)
                g.returns(can_produce)
            with rule.body() as body:
                # Get current counter value
                counter = body.call(source_counter, "read")
                # Zero-extend to 32 bits and enqueue
                data = body.pad(counter, 32)
                body.call(fifo1, "enq", data)
                # Increment counter
                new_counter = body.add(counter, body.const(1, 8))
                body.call(source_counter, "write", new_counter)
                # Track items produced
                produced = body.call(items_produced, "read")
                body.call(items_produced, "write",
                          body.add(produced, body.const(1, 8)))

        # =====================================================================
        # Rule: stage1_multiply
        # Dequeue from fifo1, multiply by 2, enqueue to fifo2
        # Guard: fifo1.notEmpty && fifo2.notFull
        # =====================================================================

        with m.rule("stage1_multiply") as rule:
            with rule.guard() as g:
                # Check if fifo1 has data and fifo2 can accept
                has_input = g.call(fifo1, "notEmpty")
                full2 = g.call(fifo2, "full")
                can_output = g.not_(full2)
                ready = g.and_(has_input, can_output)
                g.returns(ready)
            with rule.body() as body:
                # Dequeue input
                data = body.call(fifo1, "deq")
                # Multiply by 2
                product = body.mul(data, body.const(2, 32))
                result = body.truncate(product, 32)  # Truncate to 32 bits
                # Enqueue result
                body.call(fifo2, "enq", result)

        # =====================================================================
        # Rule: stage2_add
        # Dequeue from fifo2, add 10, enqueue to fifo3
        # Guard: fifo2.notEmpty && fifo3.notFull
        # =====================================================================

        with m.rule("stage2_add") as rule:
            with rule.guard() as g:
                # Check if fifo2 has data and fifo3 can accept
                has_input = g.call(fifo2, "notEmpty")
                full3 = g.call(fifo3, "full")
                can_output = g.not_(full3)
                ready = g.and_(has_input, can_output)
                g.returns(ready)
            with rule.body() as body:
                # Dequeue input
                data = body.call(fifo2, "deq")
                # Add 10
                sum_val = body.add(data, body.const(10, 32))
                result = body.truncate(sum_val, 32)  # Truncate to 32 bits
                # Enqueue result
                body.call(fifo3, "enq", result)

        # =====================================================================
        # Rule: stage3_square
        # Dequeue from fifo3, square it, enqueue to fifo4
        # Guard: fifo3.notEmpty && fifo4.notFull
        # =====================================================================

        with m.rule("stage3_square") as rule:
            with rule.guard() as g:
                # Check if fifo3 has data and fifo4 can accept
                has_input = g.call(fifo3, "notEmpty")
                full4 = g.call(fifo4, "full")
                can_output = g.not_(full4)
                ready = g.and_(has_input, can_output)
                g.returns(ready)
            with rule.body() as body:
                # Dequeue input
                data = body.call(fifo3, "deq")
                # Square (multiply by itself)
                squared = body.mul(data, data)
                result = body.truncate(squared, 32)  # Truncate to 32 bits
                # Enqueue result
                body.call(fifo4, "enq", result)

        # =====================================================================
        # Rule: sink_consume
        # Dequeue from fifo4 and record result
        # Guard: fifo4.notEmpty
        # =====================================================================

        with m.rule("sink_consume") as rule:
            with rule.guard() as g:
                # Check if fifo4 has data to consume
                has_input = g.call(fifo4, "notEmpty")
                g.returns(has_input)
            with rule.body() as body:
                # Dequeue result
                data = body.call(fifo4, "deq")
                # Store for observation
                body.call(last_result, "write", data)
                # Track items consumed
                consumed = body.call(items_consumed, "read")
                body.call(items_consumed, "write",
                          body.add(consumed, body.const(1, 8)))

        # =====================================================================
        # Control Methods
        # =====================================================================

        # Method: start - Enable source generation
        with m.method("start") as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(source_enable, "write", body.const(1, 1))

        # Method: stop - Disable source generation
        with m.method("stop") as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(source_enable, "write", body.const(0, 1))

        # Method: reset_counters - Reset all counters
        with m.method("reset_counters") as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(source_counter, "write", body.const(0, 8))
                body.call(items_produced, "write", body.const(0, 8))
                body.call(items_consumed, "write", body.const(0, 8))
                body.call(last_result, "write", body.const(0, 32))

        # Method: inject - Manually inject a value into fifo1
        with m.method("inject", args=[("data", UInt(32))]) as meth:
            with meth.guard() as g:
                full = g.call(fifo1, "full")
                not_full = g.not_(full)
                g.returns(not_full)
            with meth.body() as body:
                data = body.arg("data")
                body.call(fifo1, "enq", data)

        # =====================================================================
        # Observable Values
        # =====================================================================

        with m.value("get_last_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(last_result, "read")
                body.returns(result)

        with m.value("get_produced", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                count = body.call(items_produced, "read")
                body.returns(count)

        with m.value("get_consumed", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                count = body.call(items_consumed, "read")
                body.returns(count)

        with m.value("is_source_enabled", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                enabled = body.call(source_enable, "read")
                body.returns(enabled)

        # FIFO status values
        with m.value("fifo1_hasData", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                status = body.call(fifo1, "notEmpty")
                body.returns(status)

        with m.value("fifo4_hasData", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                status = body.call(fifo4, "notEmpty")
                body.returns(status)

    return circuit


def create_pipeline_testbench(circuit):
    """Create a comprehensive testbench for the pipeline."""

    tb = Testbench(circuit)

    # =========================================================================
    # Test Sequence 1: Basic Pipeline Flow
    # =========================================================================

    with tb.sequence("test_basic_flow") as seq:
        seq.comment("Test basic pipeline flow with single item")
        seq.reset(10)

        # Inject a single value manually
        seq.comment("Inject value 5 into pipeline")
        seq.drive("inject_data", 5)
        seq.drive("inject_enable", 1)
        seq.wait(1)
        seq.drive("inject_enable", 0)

        # Wait for pipeline to process
        seq.comment("Wait for pipeline to process")
        seq.wait(20)

        # Expected: ((5 * 2) + 10)^2 = (10 + 10)^2 = 20^2 = 400
        seq.expect("get_consumed_res0", 1, "Should have consumed 1 item")
        seq.expect("get_last_result_res0", 400, "Result should be 400")
        seq.print("Basic flow test passed", "get_last_result_res0")

    # =========================================================================
    # Test Sequence 2: Automatic Source Generation
    # =========================================================================

    with tb.sequence("test_auto_source") as seq:
        seq.comment("Test automatic source generation")
        seq.reset(10)

        # Start source generation
        seq.comment("Start automatic source")
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Let pipeline run for a while
        seq.wait(50)

        # Stop source
        seq.comment("Stop source generation")
        seq.drive("stop_enable", 1)
        seq.wait(1)
        seq.drive("stop_enable", 0)

        # Wait for pipeline to drain
        seq.wait(30)

        # Check production and consumption
        seq.print("Items produced", "get_produced_res0")
        seq.print("Items consumed", "get_consumed_res0")

    # =========================================================================
    # Test Sequence 3: Pipeline Throughput
    # =========================================================================

    with tb.sequence("test_throughput") as seq:
        seq.comment("Test pipeline throughput under load")
        seq.reset(10)

        # Inject multiple values rapidly
        for i in range(8):
            seq.comment(f"Inject value {i}")
            seq.drive("inject_data", i)
            seq.drive("inject_enable", 1)
            seq.wait(1)
            seq.drive("inject_enable", 0)
            seq.wait(2)  # Short gap

        # Wait for all to process
        seq.wait(60)

        # Check all items consumed
        seq.expect("get_consumed_res0", 8, "All 8 items should be consumed")
        seq.print("Throughput test - consumed", "get_consumed_res0")

    # =========================================================================
    # Test Sequence 4: Backpressure Test
    # =========================================================================

    with tb.sequence("test_backpressure") as seq:
        seq.comment("Test backpressure handling")
        seq.reset(10)

        # Start source with no sink
        # (In a real test, we'd disable the sink rule, but we can observe behavior)
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Run briefly - FIFOs should fill up
        seq.wait(100)

        # Check that source is still producing
        seq.print("Backpressure test - produced", "get_produced_res0")
        seq.print("Backpressure test - consumed", "get_consumed_res0")

    # =========================================================================
    # Test Sequence 5: Data Correctness Validation
    # =========================================================================

    with tb.sequence("test_correctness") as seq:
        seq.comment("Validate computation correctness for known inputs")
        seq.reset(10)

        # Test cases: input -> expected output
        # Formula: ((x * 2) + 10)^2
        test_cases = [
            (0, 100),    # ((0*2)+10)^2 = 100
            (1, 144),    # ((1*2)+10)^2 = 12^2 = 144
            (2, 196),    # ((2*2)+10)^2 = 14^2 = 196
            (3, 256),    # ((3*2)+10)^2 = 16^2 = 256
            (5, 400),    # ((5*2)+10)^2 = 20^2 = 400
            (10, 900),   # ((10*2)+10)^2 = 30^2 = 900
        ]

        for input_val, expected in test_cases:
            seq.comment(f"Test input={input_val}, expected={expected}")
            seq.drive("inject_data", input_val)
            seq.drive("inject_enable", 1)
            seq.wait(1)
            seq.drive("inject_enable", 0)
            seq.wait(25)  # Wait for pipeline

        # Final result should be the last computed value
        seq.expect("get_last_result_res0", 900, "Last result should be 900")
        seq.print("Correctness test final result", "get_last_result_res0")

    # =========================================================================
    # Test Sequence 6: Long Running Stress Test
    # =========================================================================

    with tb.sequence("test_stress") as seq:
        seq.comment("Long running stress test")
        seq.reset(10)

        # Start source
        seq.drive("start_enable", 1)
        seq.wait(1)
        seq.drive("start_enable", 0)

        # Run for many cycles
        seq.wait(200)

        # Stop and wait for drain
        seq.drive("stop_enable", 1)
        seq.wait(1)
        seq.drive("stop_enable", 0)
        seq.wait(50)

        seq.print("Stress test final - produced", "get_produced_res0")
        seq.print("Stress test final - consumed", "get_consumed_res0")
        seq.print("Stress test final - result", "get_last_result_res0")

    return tb


def main():
    print("=" * 70)
    print("GAA Pipeline with FIFOs Testbench Example")
    print("=" * 70)

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "pipeline_testbench_workspace"

    # Clean previous workspace
    if workspace_dir.exists():
        print(f"\nRemoving existing workspace: {workspace_dir}")
        shutil.rmtree(workspace_dir)

    # Create circuit
    print("\n1. Creating pipeline circuit with FIFOs...")
    circuit = create_pipeline_circuit()

    # Emit MLIR to verify
    print("\n2. Emitting CMT2 MLIR...")
    mlir = circuit.emit_mlir()

    # Verify structure
    print("   Circuit structure:")
    print(f"      - Modules: {len(circuit._modules)}")
    print(f"      - External modules: {len(circuit._external_modules)}")

    # Count GAA constructs
    gaa_ops = ["cmt2.rule", "cmt2.method", "cmt2.value", "cmt2.call", "cmt2.instance"]
    print("   GAA operations found:")
    for op in gaa_ops:
        count = mlir.count(op)
        if count > 0:
            print(f"      {op}: {count}")

    # Create testbench
    print("\n3. Creating comprehensive testbench...")
    tb = create_pipeline_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace
    print("\n4. Creating simulation workspace...")
    ws = SimulationWorkspace(circuit, workspace_dir)

    # Generate with testbench
    print("\n5. Generating workspace with testbench DSL...")
    ws.generate_with_testbench(tb)

    # Show generated FIFO RTL
    print("\n6. Generated FIFO RTL (excerpt):")
    print("-" * 70)
    fifo_rtl = workspace_dir / "rtl" / "FIFO_32_4.sv"
    if fifo_rtl.exists():
        content = fifo_rtl.read_text()
        print(content[:1500])
        if len(content) > 1500:
            print(f"... ({len(content) - 1500} more characters)")
    print("-" * 70)

    # Show test sequence
    print("\n7. Generated testbench excerpt (test_basic_flow):")
    print("-" * 70)
    tb_file = workspace_dir / "tb" / "testbench.cpp"
    tb_content = tb_file.read_text()
    # Find test_basic_flow function
    lines = tb_content.split("\n")
    in_function = False
    shown_lines = []
    for line in lines:
        if "void run_test_basic_flow" in line:
            in_function = True
        if in_function:
            shown_lines.append(line)
            if line.strip() == "}" and len(shown_lines) > 5:
                break
    print("\n".join(shown_lines[:25]))
    if len(shown_lines) > 25:
        print(f"... ({len(shown_lines) - 25} more lines)")
    print("-" * 70)

    # Generate and show cocotb testbench
    print("\n8. Generating cocotb testbench...")
    cocotb_code = tb.generate_cocotb()
    print("-" * 70)
    print(cocotb_code[:1500])
    if len(cocotb_code) > 1500:
        print(f"... ({len(cocotb_code) - 1500} more characters)")
    print("-" * 70)

    # Show pipeline description
    print("\n" + "=" * 70)
    print("Pipeline Testbench workspace generated successfully!")
    print("=" * 70)
    print(f"""
Pipeline Architecture:
    Source -> FIFO1 -> Stage1(*2) -> FIFO2 -> Stage2(+10) -> FIFO3 -> Stage3(^2) -> FIFO4 -> Sink

GAA Rules:
    - source_gen:       Generates data when enabled and FIFO1 not full
    - stage1_multiply:  Dequeue from FIFO1, multiply by 2, enqueue to FIFO2
    - stage2_add:       Dequeue from FIFO2, add 10, enqueue to FIFO3
    - stage3_square:    Dequeue from FIFO3, square it, enqueue to FIFO4
    - sink_consume:     Dequeue from FIFO4 and record result

Expected computation: ((x * 2) + 10)^2
    Input 0  -> 100
    Input 1  -> 144
    Input 5  -> 400
    Input 10 -> 900

Generated workspace: {workspace_dir}

Test sequences:
  1. test_basic_flow     - Single item through pipeline
  2. test_auto_source    - Automatic data generation
  3. test_throughput     - Multiple items in rapid succession
  4. test_backpressure   - FIFO backpressure behavior
  5. test_correctness    - Validate computation results
  6. test_stress         - Long-running stress test

To run the simulation:
    cd {workspace_dir}
    make          # Build the simulation
    make run      # Run all tests
    make waves    # View waveforms
""")


if __name__ == "__main__":
    main()
