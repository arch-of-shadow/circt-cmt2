#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Fork-Join Dataflow Example using Cmt2 JIT (stacked on PyCMT2)

This example demonstrates fork-join dataflow patterns where a single
source task produces tokens consumed by multiple parallel branches,
which then rejoin at a sink task.

The example shows:
- Fork pattern: one token consumed by multiple tasks
- Join pattern: one task waiting for multiple input tokens
- Using the ForkJoinPipeline shorthand builder
- Using explicit dataflow builders for complex patterns

This file includes E2E simulation for the DiamondPipeline example.

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/dataflow_forkjoin.py
"""

import cmt2.jit as jit

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import (
    Circuit,
    UInt,
    ForkJoinPipeline,
)
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_forkjoin_shorthand():
    """Create a fork-join pipeline using the ForkJoinPipeline shorthand."""
    print("=" * 60)
    print("Fork-Join Pipeline - Shorthand Builder")
    print("=" * 60)

    circuit = Circuit("ForkJoinShorthand")

    with jit.module(circuit, "ParallelAdd") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create a fork-join pipeline with 2 parallel branches
        fjp = ForkJoinPipeline(mod, "parallel_compute", UInt(32), output_type=UInt(33))

        @fjp.source()
        def source(task, input_data):
            """Source: pass through input data."""
            return input_data

        @fjp.branch("add_one")
        def add_one(task, data):
            """Branch 1: Add 1 to data."""
            return task.add(data, task.const(1, 32))

        @fjp.branch("add_two")
        def add_two(task, data):
            """Branch 2: Add 2 to data."""
            return task.add(data, task.const(2, 32))

        @fjp.sink()
        def sink(task, results):
            """Sink: Add results from both branches."""
            a, b = results
            # a and b are both UInt(33) due to add widening
            return task.add(a, b)

        fjp.build()

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def create_forkjoin_explicit():
    """Create a complex fork-join pattern using explicit dataflow builders."""
    print("\n" + "=" * 60)
    print("Fork-Join Pipeline - Explicit Dataflow Builder")
    print("=" * 60)

    circuit = Circuit("ForkJoinExplicit")

    with jit.module(circuit, "DiamondPipeline") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create a "diamond" dataflow pattern:
        #
        #           source
        #          /      \
        #     branch_a   branch_b
        #          \      /
        #           join
        #            |
        #          output

        @jit.dataflow(mod, name="diamond")
        def diamond(df, x: UInt[16]) -> UInt[16]:
            dfb = df._df

            # Source task: create token with input data
            with dfb.task("source") as task:
                tok_src = task.create_token(x, UInt(16))
                task.yield_tokens(tok_src)

            # Branch A: multiply by 2
            with dfb.task("branch_a", tokens_in=[tok_src], timing=(1, 2)) as task:
                data = task.token_data(tok_src)
                result_a = task.mul(data, task.const(2, 16))
                tok_a = task.create_token(task.bits(result_a, 15, 0), UInt(16))
                task.yield_tokens(tok_a)

            # Branch B: add 10 (same tok_src - fork pattern)
            with dfb.task("branch_b", tokens_in=[tok_src], timing=(1, 2)) as task:
                data = task.token_data(tok_src)
                result_b = task.add(data, task.const(10, 16))
                tok_b = task.create_token(task.bits(result_b, 15, 0), UInt(16))
                task.yield_tokens(tok_b)

            # Join task: wait for both branches and sum results
            with dfb.task("join", tokens_in=[tok_a, tok_b], timing=(2, 3)) as task:
                val_a = task.token_data(tok_a)
                val_b = task.token_data(tok_b)
                sum_val = task.add(val_a, val_b)
                tok_sum = task.create_token(task.bits(sum_val, 15, 0), UInt(16))
                task.yield_tokens(tok_sum)

            # Output task: final result
            with dfb.task("output", tokens_in=[tok_sum], timing=(3, 4)) as task:
                result = task.token_data(tok_sum)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def create_multiway_fork():
    """Create a 3-way fork pattern."""
    print("\n" + "=" * 60)
    print("Multi-way Fork Pattern")
    print("=" * 60)

    circuit = Circuit("MultiwayFork")

    with jit.module(circuit, "ThreeWayFork") as mod:
        clk = mod.clock()
        rst = mod.reset()

        @jit.dataflow(mod, name="three_way")
        def three_way(df, input: UInt[8]) -> UInt[8]:
            dfb = df._df

            # Source
            with dfb.task("source") as task:
                tok = task.create_token(input, UInt(8))
                task.yield_tokens(tok)

            # Three parallel branches consuming the same token
            with dfb.task("inc1", tokens_in=[tok]) as task:
                data = task.token_data(tok)
                result = task.add(data, task.const(1, 8))
                tok1 = task.create_token(task.bits(result, 7, 0), UInt(8))
                task.yield_tokens(tok1)

            with dfb.task("inc2", tokens_in=[tok]) as task:
                data = task.token_data(tok)
                result = task.add(data, task.const(2, 8))
                tok2 = task.create_token(task.bits(result, 7, 0), UInt(8))
                task.yield_tokens(tok2)

            with dfb.task("inc3", tokens_in=[tok]) as task:
                data = task.token_data(tok)
                result = task.add(data, task.const(3, 8))
                tok3 = task.create_token(task.bits(result, 7, 0), UInt(8))
                task.yield_tokens(tok3)

            # Join all three
            with dfb.task("join_all", tokens_in=[tok1, tok2, tok3]) as task:
                v1 = task.token_data(tok1)
                v2 = task.token_data(tok2)
                v3 = task.token_data(tok3)
                # Sum all three: (x+1) + (x+2) + (x+3) = 3x + 6
                sum12 = task.add(v1, v2)
                sum_all = task.add(sum12, v3)
                task.return_values(task.bits(sum_all, 7, 0))

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def create_simulatable_diamond():
    """Create a simulatable DiamondPipeline circuit.

    Diamond dataflow pattern:
              source
             /      \
        branch_a   branch_b
             \      /
              join
               |
             output

    Computation: result = (x * 2) + (x + 10)
    For x=5: result = 10 + 15 = 25
    """
    from circt.pycmt2.types import SyncToken

    clear_stl_registry()
    circuit = Circuit("DiamondPipelineSim")

    with jit.module(circuit, "DiamondPipeline") as mod:
        clk = mod.clock()
        rst = mod.reset()

        @jit.dataflow(mod, name="diamond")
        def diamond(df, x: UInt[16]) -> UInt[16]:
            dfb = df._df

            # Source task: create token with input data
            with dfb.task("source", timing=(0, 1), tokens_out=[SyncToken(UInt(16))]) as task:
                tok_src = task.create_token(x, UInt(16))
                task.yield_tokens(tok_src)

            # Branch A: multiply by 2
            with dfb.task(
                "branch_a", tokens_in=[tok_src], timing=(1, 2), tokens_out=[SyncToken(UInt(16))]
            ) as task:
                data = task.token_data(tok_src)
                result_a = task.mul(data, task.const(2, 16))
                tok_a = task.create_token(task.bits(result_a, 15, 0), UInt(16))
                task.yield_tokens(tok_a)

            # Branch B: add 10 (same tok_src - fork pattern)
            with dfb.task(
                "branch_b", tokens_in=[tok_src], timing=(1, 2), tokens_out=[SyncToken(UInt(16))]
            ) as task:
                data = task.token_data(tok_src)
                result_b = task.add(data, task.const(10, 16))
                tok_b = task.create_token(task.bits(result_b, 15, 0), UInt(16))
                task.yield_tokens(tok_b)

            # Join task: wait for both branches and sum results
            with dfb.task(
                "join", tokens_in=[tok_a, tok_b], timing=(2, 3), tokens_out=[SyncToken(UInt(16))]
            ) as task:
                val_a = task.token_data(tok_a)
                val_b = task.token_data(tok_b)
                sum_val = task.add(val_a, val_b)
                tok_sum = task.create_token(task.bits(sum_val, 15, 0), UInt(16))
                task.yield_tokens(tok_sum)

            # Output task: final result
            with dfb.task("output", tokens_in=[tok_sum], timing=(3, 4)) as task:
                result = task.token_data(tok_sum)
                task.return_values(result)

    return circuit


def create_dataflow_testbench(circuit):
    """Create testbench for diamond dataflow pipeline.

    Dataflow pipelines expose ports directly:
    - diamond_source_x: input to the pipeline
    - diamond_output_result_0: output from the pipeline
    """
    tb = Testbench(circuit, auto_debug_ports=False)

    # Test cases: (input, expected_output)
    # Expected result: (x * 2) + (x + 10) = 3*x + 10
    test_cases = [
        (5, 25),     # (5*2) + (5+10) = 10 + 15 = 25
        (10, 40),    # (10*2) + (10+10) = 20 + 20 = 40
        (0, 10),     # (0*2) + (0+10) = 0 + 10 = 10
        (100, 310),  # (100*2) + (100+10) = 200 + 110 = 310
    ]

    # Pipeline latency: 4 cycles (timing 0-1, 1-2, 2-3, 3-4)
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
    # Test Sequences: Diamond Dataflow Pipeline Tests
    # =========================================================================
    for i, (input_val, expected) in enumerate(test_cases):
        with tb.sequence(f"test_diamond_{i+1}") as seq:
            seq.comment(f"Test: input={input_val}, expected={expected}")
            seq.comment(f"Computation: ({input_val}*2) + ({input_val}+10) = {expected}")
            seq.reset(5)

            # Drive input to the dataflow pipeline
            seq.comment("Drive input to dataflow pipeline")
            seq.drive("diamond_source_x", input_val)

            # Clock through the pipeline latency
            seq.record_cycle(f"start_{i}")
            seq.wait(PIPELINE_LATENCY)
            seq.record_cycle(f"end_{i}")

            # Read result from the dataflow output
            seq.expect("diamond_output_result_0", expected,
                      f"({input_val}*2)+({input_val}+10)={expected}")
            seq.print_cycle_diff(f"start_{i}", f"end_{i}", f"Test {i+1} latency")
            seq.print(f"Test {i+1} passed: result=", "diamond_output_result_0")

            # Extra cycle between tests
            seq.wait(1)

    return tb


def run_simulation():
    """Run E2E simulation for diamond dataflow."""
    print("\n" + "=" * 60)
    print("E2E Simulation: Diamond Fork-Join Dataflow")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_dataflow_forkjoin"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("\n1. Creating diamond pipeline circuit...")
    circuit = create_simulatable_diamond()

    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_dataflow_testbench(circuit)
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
        print("Note: Dataflow lowering may not be fully implemented yet.")
        # Show MLIR for debugging
        mlir = circuit.emit_mlir()
        print(f"\n   MLIR size: {len(mlir)} chars")
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
    """Run all fork-join examples."""
    create_forkjoin_shorthand()
    create_forkjoin_explicit()
    create_multiway_fork()

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
