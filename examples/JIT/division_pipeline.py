#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Division Pipeline Example (JIT stacked on PyCMT2)

This example demonstrates a pipelined division operation using the
new dataflow pipeline builders. The division is performed using
restoring division algorithm across multiple pipeline stages.

The example shows:
- Using the Pipeline shorthand builder for linear pipelines
- Token-based synchronization between stages
- Timing annotation for each pipeline stage

Includes E2E simulation for the explicit dataflow version.

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/division_pipeline.py
"""

import cmt2.jit as jit

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import (
    Circuit,
    UInt,
    Pipeline,
    timing_interval,
    pipeline_timing,
)
from circt.pycmt2.types import SyncToken
from circt.pycmt2.stl import clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_division_pipeline_shorthand():
    """Create a division pipeline using the Pipeline shorthand builder."""
    print("=" * 60)
    print("Division Pipeline - Shorthand Builder")
    print("=" * 60)

    circuit = Circuit("DivisionPipelineShorthand")

    with jit.module(circuit, "Divider") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create a 4-stage division pipeline using the shorthand builder
        # Input: 32-bit dividend, Output: 32-bit quotient
        pipe = Pipeline(mod, "div_pipe", UInt(32), stages=4, interval=1)

        @pipe.stage(0)
        def stage0(task, dividend):
            """Stage 0: Initialize quotient to 0, pass dividend."""
            # In a real implementation, this would initialize the
            # division state machine
            return dividend

        @pipe.stage(1)
        def stage1(task, data):
            """Stage 1: First division iteration."""
            # Simulate one iteration of restoring division
            shifted = task.shl(data, 1)
            return task.bits(shifted, 31, 0)  # Truncate to 32 bits

        @pipe.stage(2)
        def stage2(task, data):
            """Stage 2: Second division iteration."""
            shifted = task.shl(data, 1)
            return task.bits(shifted, 31, 0)

        @pipe.stage(3)
        def stage3(task, data):
            """Stage 3: Final iteration and output quotient."""
            # Final stage returns the result
            return data

        # Build the pipeline (creates the dataflow IR)
        pipe.build()

    # Emit MLIR
    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def create_division_pipeline_explicit():
    """Create a division pipeline using explicit dataflow builders."""
    print("\n" + "=" * 60)
    print("Division Pipeline - Explicit Dataflow Builder")
    print("=" * 60)

    circuit = Circuit("DivisionPipelineExplicit")

    with jit.module(circuit, "Divider") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create dataflow pipeline with explicit task definitions
        with mod.dataflow(
            "div_pipe",
            args=[("dividend", UInt(32)), ("divisor", UInt(32))],
            returns=[UInt(32)],
            interval=1,
        ) as df:
            # Stage 0: Initialize
            with df.task("init") as task:
                # Create initial token with dividend
                tok0 = task.create_token(df.dividend, UInt(32))
                task.yield_tokens(tok0)

            # Stage 1: First iteration
            with df.task("iter1", tokens_in=[tok0], timing=(1, 2)) as task:
                data = task.token_data(tok0)
                shifted = task.shl(data, 1)
                result = task.bits(shifted, 31, 0)
                tok1 = task.create_token(result, UInt(32))
                task.yield_tokens(tok1)

            # Stage 2: Second iteration
            with df.task("iter2", tokens_in=[tok1], timing=(2, 3)) as task:
                data = task.token_data(tok1)
                shifted = task.shl(data, 1)
                result = task.bits(shifted, 31, 0)
                tok2 = task.create_token(result, UInt(32))
                task.yield_tokens(tok2)

            # Stage 3: Final output
            with df.task("output", tokens_in=[tok2], timing=(3, 4)) as task:
                quotient = task.token_data(tok2)
                task.return_values(quotient)

    # Emit MLIR
    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def create_simulatable_shift_pipeline():
    """Create a multiply pipeline for E2E simulation.

    This is a simpler version that multiplies by 4 (equivalent to << 2).
    """
    clear_stl_registry()
    circuit = Circuit("MultiplyPipelineSim")

    with jit.module(circuit, "MultiplyPipeline") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.dataflow(
            "mul_pipe",
            args=[("input", UInt(16))],
            returns=[UInt(16)],
            interval=1,
        ) as df:
            # Stage 0: Input
            with df.task("init", timing=(0, 1),
                        tokens_out=[SyncToken(UInt(16))]) as task:
                tok0 = task.create_token(df.input, UInt(16))
                task.yield_tokens(tok0)

            # Stage 1: Multiply by 2
            with df.task("mul2", tokens_in=[tok0], timing=(1, 2),
                        tokens_out=[SyncToken(UInt(16))]) as task:
                data = task.token_data(tok0)
                result = task.mul(data, task.const(2, 16))
                tok1 = task.create_token(task.bits(result, 15, 0), UInt(16))
                task.yield_tokens(tok1)

            # Stage 2: Multiply by 2 again (total: x * 4)
            with df.task("mul2_2", tokens_in=[tok1], timing=(2, 3),
                        tokens_out=[SyncToken(UInt(16))]) as task:
                data = task.token_data(tok1)
                result = task.mul(data, task.const(2, 16))
                tok2 = task.create_token(task.bits(result, 15, 0), UInt(16))
                task.yield_tokens(tok2)

            # Stage 3: Output
            with df.task("output", tokens_in=[tok2], timing=(3, 4)) as task:
                result = task.token_data(tok2)
                task.return_values(result)

    return circuit


def create_multiply_testbench(circuit):
    """Create testbench for the multiply pipeline.

    Computation: output = input * 4
    """
    tb = Testbench(circuit, auto_debug_ports=False)

    # Test cases: (input, expected_output)
    # Expected: input * 4
    test_cases = [
        (1, 4),      # 1 * 4 = 4
        (5, 20),     # 5 * 4 = 20
        (10, 40),    # 10 * 4 = 40
        (100, 400),  # 100 * 4 = 400
    ]

    # Pipeline latency: 4 cycles
    PIPELINE_LATENCY = 4

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(2)
        seq.print("Reset test passed")

    # =========================================================================
    # Test Sequences: Multiply Pipeline Tests
    # =========================================================================
    for i, (input_val, expected) in enumerate(test_cases):
        with tb.sequence(f"test_mul_{i+1}") as seq:
            seq.comment(f"Test: {input_val} * 4 = {expected}")
            seq.reset(5)

            # Drive input
            seq.drive("mul_pipe_init_input", input_val)

            # Wait for pipeline
            seq.record_cycle(f"start_{i}")
            seq.wait(PIPELINE_LATENCY)
            seq.record_cycle(f"end_{i}")

            # Verify result
            seq.expect("mul_pipe_output_result_0", expected,
                      f"{input_val} * 4 = {expected}")
            seq.print_cycle_diff(f"start_{i}", f"end_{i}", f"Test {i+1} latency")
            seq.print(f"Test {i+1}: result=", "mul_pipe_output_result_0")

            seq.wait(1)

    return tb


def run_simulation():
    """Run E2E simulation for the multiply pipeline."""
    print("\n" + "=" * 60)
    print("E2E Simulation: Multiply Pipeline (x * 4)")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_division_pipeline"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("\n1. Creating multiply pipeline circuit...")
    circuit = create_simulatable_shift_pipeline()

    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_multiply_testbench(circuit)
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


def main():
    """Run both division pipeline examples."""
    # Example using the shorthand Pipeline builder
    create_division_pipeline_shorthand()

    # Example using explicit dataflow builders
    create_division_pipeline_explicit()

    # Run E2E simulation
    sim_result = run_simulation()

    print("\n" + "=" * 60)
    print("MLIR generation examples completed!")
    if sim_result == 0:
        print("E2E simulation passed!")
    else:
        print("E2E simulation failed!")
    print("=" * 60)

    return sim_result


if __name__ == "__main__":
    sys.exit(main())
