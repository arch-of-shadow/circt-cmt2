#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Dataflow-Proc Compatibility Example - Phase 7 C1/C7

This example demonstrates proc control operations inside dataflow tasks,
enabling multi-cycle computations within pipeline stages.

Key features demonstrated:
1. proc.seq inside dataflow tasks - sequential multi-cycle operations
2. proc.par inside dataflow tasks - parallel operations within a stage
3. proc.if inside dataflow tasks - conditional branching
4. proc.static_repeat - compile-time loop unrolling
5. proc.enable - step activation

Use cases:
- Pipeline stages that require multiple cycles
- Conditional processing based on data values
- Iterative algorithms within a single pipeline stage
- Fork-join patterns with complex internal control

Includes E2E simulation for a simple dataflow pipeline.

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/dataflow_proc_control.py
"""

import cmt2.jit as jit

import os
import shutil
import sys
from pathlib import Path

# Add circt Python packages to path
build_dir = os.path.dirname(os.path.abspath(__file__))
while build_dir and not os.path.exists(os.path.join(build_dir, "build")):
    build_dir = os.path.dirname(build_dir)
if build_dir:
    sys.path.insert(0, os.path.join(build_dir, "build/tools/circt/python_packages/circt_core"))

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.types import SyncToken
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def example_sequential_task():
    """Example 1: Sequential operations within a dataflow task.

    Shows how to use proc.seq for multi-cycle operations within a
    single pipeline stage.
    """
    print("=" * 60)
    print("Example 1: Sequential Operations in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("SeqTaskExample")

    with jit.module(circuit, "SeqTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Define steps that the task will use
        with mod.static_step(1) as load:
            pass

        with mod.static_step(2) as compute:
            pass

        with mod.static_step(1) as store:
            pass

        @jit.dataflow(mod)
        def seq_pipeline(df, input: UInt[32]) -> UInt[32]:
            dfb = df._df

            # Task with sequential multi-cycle processing
            with dfb.task("process") as task:
                # Multi-cycle sequence: load -> compute -> store
                with task.seq():
                    task.enable(load.ref())
                    task.enable(compute.ref())
                    task.enable(store.ref())

                # Create output token
                data = task.const(100, 32)
                tok = task.create_token(data, UInt(32))
                task.yield_tokens(tok)

            # Final task
            with dfb.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def example_parallel_task():
    """Example 2: Parallel operations within a dataflow task.

    Shows how to use proc.par for concurrent operations that must
    all complete before the task produces its output token.
    """
    print("\n" + "=" * 60)
    print("Example 2: Parallel Operations in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("ParTaskExample")

    with jit.module(circuit, "ParTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Define parallel steps
        with mod.static_step(2) as op_a:
            pass

        with mod.static_step(3) as op_b:
            pass

        with mod.static_step(1) as op_c:
            pass

        @jit.dataflow(mod)
        def par_pipeline(df, input: UInt[16]) -> UInt[16]:
            dfb = df._df

            # Task with parallel operations
            with dfb.task("parallel_ops") as task:
                # Three operations run concurrently
                # Task completion waits for longest (3 cycles)
                with task.par():
                    task.enable(op_a.ref())
                    task.enable(op_b.ref())
                    task.enable(op_c.ref())

                data = task.const(42, 16)
                tok = task.create_token(data, UInt(16))
                task.yield_tokens(tok)

            with dfb.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def example_conditional_task():
    """Example 3: Conditional control within a dataflow task.

    Shows how to use proc.if for data-dependent branching within
    a pipeline stage.
    """
    print("\n" + "=" * 60)
    print("Example 3: Conditional Control in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("CondTaskExample")

    with jit.module(circuit, "CondTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.static_step(1) as fast_path:
            pass

        with mod.static_step(2) as slow_path_a:
            pass

        with mod.static_step(2) as slow_path_b:
            pass

        @jit.dataflow(mod)
        def cond_pipeline(df, input: UInt[8]) -> UInt[8]:
            dfb = df._df

            with dfb.task("conditional_process") as task:
                # Condition based on constant (in real use, would be data-dependent)
                cond = task.const(1, 1)

                # Conditional execution
                with task.if_(cond) as (then_b, else_b):
                    with then_b:
                        # Fast path: 1 cycle
                        task.enable(fast_path.ref())
                    with else_b:
                        # Slow path: sequential 2-cycle operations
                        with task.seq():
                            task.enable(slow_path_a.ref())
                            task.enable(slow_path_b.ref())

                data = task.const(7, 8)
                tok = task.create_token(data, UInt(8))
                task.yield_tokens(tok)

            with dfb.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def example_iterative_task():
    """Example 4: Iterative computation within a dataflow task.

    Shows how to use proc.static_repeat for compile-time known
    iteration counts within a pipeline stage.
    """
    print("\n" + "=" * 60)
    print("Example 4: Iterative Computation in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("IterTaskExample")

    with jit.module(circuit, "IterTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.static_step(1) as multiply:
            pass

        @jit.dataflow(mod)
        def iter_pipeline(df, base: UInt[32]) -> UInt[32]:
            dfb = df._df

            # Task that computes base^4 using repeated multiplication
            with dfb.task("power_of_4") as task:
                # 4 multiplication iterations
                with task.static_repeat(4):
                    task.enable(multiply.ref())

                data = task.const(16, 32)  # Result placeholder
                tok = task.create_token(data, UInt(32))
                task.yield_tokens(tok)

            with dfb.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def example_complex_pipeline():
    """Example 5: Complex multi-stage pipeline with proc control.

    Shows a realistic pipeline where different stages have
    different internal control patterns.
    """
    print("\n" + "=" * 60)
    print("Example 5: Complex Pipeline with Proc Control")
    print("=" * 60)

    circuit = Circuit("ComplexPipeExample")

    with jit.module(circuit, "ComplexPipeModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Define various steps
        with mod.static_step(1) as fetch:
            pass

        with mod.static_step(1) as decode:
            pass

        with mod.static_step(2) as execute:
            pass

        with mod.static_step(3) as memory:
            pass

        with mod.static_step(1) as writeback:
            pass

        @jit.dataflow(mod)
        def processor_pipeline(df, instruction: UInt[32]) -> UInt[32]:
            dfb = df._df

            # Stage 1: Fetch and decode (sequential)
            with dfb.task("frontend") as task:
                with task.seq():
                    task.enable(fetch.ref())
                    task.enable(decode.ref())

                data = task.const(0, 32)
                tok1 = task.create_token(data, UInt(32))
                task.yield_tokens(tok1)

            # Stage 2: Execute and memory access (parallel for some ops)
            with dfb.task("backend", tokens_in=[tok1]) as task:
                with task.par():
                    task.enable(execute.ref())
                    task.enable(memory.ref())

                data = task.token_data(tok1)
                tok2 = task.create_token(data, UInt(32))
                task.yield_tokens(tok2)

            # Stage 3: Writeback
            with dfb.task("commit", tokens_in=[tok2]) as task:
                with task.seq():
                    task.enable(writeback.ref())

                result = task.token_data(tok2)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def example_nested_control():
    """Example 6: Nested proc control structures.

    Shows deeply nested control: seq containing par containing if.
    """
    print("\n" + "=" * 60)
    print("Example 6: Nested Proc Control Structures")
    print("=" * 60)

    circuit = Circuit("NestedCtrlExample")

    with jit.module(circuit, "NestedCtrlModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.static_step(1) as init:
            pass

        with mod.static_step(2) as proc_a:
            pass

        with mod.static_step(2) as proc_b:
            pass

        with mod.static_step(1) as finalize:
            pass

        @jit.dataflow(mod)
        def nested_pipeline(df, x: UInt[8]) -> UInt[8]:
            dfb = df._df

            with dfb.task("complex_task") as task:
                # Level 1: Sequential
                with task.seq():
                    task.enable(init.ref())

                    # Level 2: Parallel branches
                    with task.par():
                        # Branch A: Iterative
                        with task.static_repeat(2):
                            task.enable(proc_a.ref())

                        # Branch B: Conditional
                        cond = task.const(1, 1)
                        with task.if_(cond) as (then_b, else_b):
                            with then_b:
                                task.enable(proc_b.ref())
                            with else_b:
                                pass  # Skip

                    task.enable(finalize.ref())

                data = task.const(99, 8)
                tok = task.create_token(data, UInt(8))
                task.yield_tokens(tok)

            with dfb.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())
    return circuit


@jit.elaborate
def create_simulatable_pipeline():
    """Create a simple dataflow pipeline for E2E simulation.

    This is a simpler version of the examples above that generates
    valid RTL for simulation.
    """
    clear_stl_registry()
    circuit = Circuit("DataflowProcControlSim")

    with jit.module(circuit, "SimplePipeline") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Simple dataflow: input -> process (add 10) -> multiply by 2 -> output
        @jit.dataflow(mod)
        def simple(df, x: UInt[16]) -> UInt[16]:
            dfb = df._df

            # Stage 1: Add 10
            with dfb.task("add_stage", timing=(0, 1), tokens_out=[SyncToken(UInt(16))]) as task:
                result = task.add(x, task.const(10, 16))
                tok1 = task.create_token(task.bits(result, 15, 0), UInt(16))
                task.yield_tokens(tok1)

            # Stage 2: Multiply by 2
            with dfb.task("mul_stage", tokens_in=[tok1], timing=(1, 2), tokens_out=[SyncToken(UInt(16))]) as task:
                data = task.token_data(tok1)
                result = task.mul(data, task.const(2, 16))
                tok2 = task.create_token(task.bits(result, 15, 0), UInt(16))
                task.yield_tokens(tok2)

            # Stage 3: Output
            with dfb.task("output", tokens_in=[tok2], timing=(2, 3)) as task:
                result = task.token_data(tok2)
                task.return_values(result)

    return circuit


def create_dataflow_proc_testbench(circuit):
    """Create testbench for the simple pipeline.

    Computation: output = (x + 10) * 2
    """
    tb = Testbench(circuit, auto_debug_ports=False)

    # Test cases: (input, expected_output)
    # Expected: (x + 10) * 2
    test_cases = [
        (5, 30),     # (5 + 10) * 2 = 30
        (10, 40),    # (10 + 10) * 2 = 40
        (0, 20),     # (0 + 10) * 2 = 20
        (100, 220),  # (100 + 10) * 2 = 220
    ]

    # Pipeline latency: 3 cycles
    PIPELINE_LATENCY = 3

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(2)
        seq.print("Reset test passed")

    # =========================================================================
    # Test Sequences: Pipeline Tests
    # =========================================================================
    for i, (input_val, expected) in enumerate(test_cases):
        with tb.sequence(f"test_pipeline_{i+1}") as seq:
            seq.comment(f"Test: ({input_val}+10)*2 = {expected}")
            seq.reset(5)

            # Drive input
            seq.drive("simple_add_stage_x", input_val)

            # Wait for pipeline
            seq.record_cycle(f"start_{i}")
            seq.wait(PIPELINE_LATENCY)
            seq.record_cycle(f"end_{i}")

            # Verify result
            seq.expect("simple_output_result_0", expected,
                      f"({input_val}+10)*2={expected}")
            seq.print_cycle_diff(f"start_{i}", f"end_{i}", f"Test {i+1} latency")
            seq.print(f"Test {i+1} result: ", "simple_output_result_0")

            seq.wait(1)

    return tb


def run_simulation():
    """Run E2E simulation for the simple dataflow pipeline."""
    print("\n" + "=" * 60)
    print("E2E Simulation: Simple Dataflow Pipeline")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_dataflow_proc_control"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("\n1. Creating simple dataflow pipeline circuit...")
    circuit = create_simulatable_pipeline()

    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_dataflow_proc_testbench(circuit)
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
        print("Note: Dataflow-proc control may not be fully implemented yet.")
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
    """Run all examples."""
    print("=" * 60)
    print("JIT Dataflow-Proc Compatibility Examples (stacked on PyCMT2)")
    print("Phase 7 C1/C7: Proc Control Inside Dataflow Tasks")
    print("=" * 60)

    example_sequential_task()
    example_parallel_task()
    example_conditional_task()
    # Note: example_iterative_task and example_complex_pipeline and example_nested_control
    # have context issues with static_repeat - skipping for now
    # example_iterative_task()
    # example_complex_pipeline()
    # example_nested_control()

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
