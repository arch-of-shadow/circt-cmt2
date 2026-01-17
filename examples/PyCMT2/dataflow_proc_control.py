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

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/dataflow_proc_control.py
"""

import os
import sys

# Add circt Python packages to path
build_dir = os.path.dirname(os.path.abspath(__file__))
while build_dir and not os.path.exists(os.path.join(build_dir, "build")):
    build_dir = os.path.dirname(build_dir)
if build_dir:
    sys.path.insert(0, os.path.join(build_dir, "build/tools/circt/python_packages/circt_core"))

from circt.pycmt2 import Circuit, UInt


def example_sequential_task():
    """Example 1: Sequential operations within a dataflow task.

    Shows how to use proc.seq for multi-cycle operations within a
    single pipeline stage.
    """
    print("=" * 60)
    print("Example 1: Sequential Operations in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("SeqTaskExample")

    with circuit.module("SeqTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Define steps that the task will use
        with mod.static_step(1, "load"):
            pass

        with mod.static_step(2, "compute"):
            pass

        with mod.static_step(1, "store"):
            pass

        # Dataflow with sequential control in tasks
        with mod.dataflow(
            "seq_pipeline",
            args=[("input", UInt(32))],
            returns=[UInt(32)],
        ) as df:
            # Task with sequential multi-cycle processing
            with df.task("process") as task:
                # Multi-cycle sequence: load -> compute -> store
                with task.seq():
                    task.enable("load")
                    task.enable("compute")
                    task.enable("store")

                # Create output token
                data = task.const(100, 32)
                tok = task.create_token(data, UInt(32))
                task.yield_tokens(tok)

            # Final task
            with df.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def example_parallel_task():
    """Example 2: Parallel operations within a dataflow task.

    Shows how to use proc.par for concurrent operations that must
    all complete before the task produces its output token.
    """
    print("\n" + "=" * 60)
    print("Example 2: Parallel Operations in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("ParTaskExample")

    with circuit.module("ParTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Define parallel steps
        with mod.static_step(2, "op_a"):
            pass

        with mod.static_step(3, "op_b"):
            pass

        with mod.static_step(1, "op_c"):
            pass

        with mod.dataflow(
            "par_pipeline",
            args=[("input", UInt(16))],
            returns=[UInt(16)],
        ) as df:
            # Task with parallel operations
            with df.task("parallel_ops") as task:
                # Three operations run concurrently
                # Task completion waits for longest (3 cycles)
                with task.par():
                    task.enable("op_a")
                    task.enable("op_b")
                    task.enable("op_c")

                data = task.const(42, 16)
                tok = task.create_token(data, UInt(16))
                task.yield_tokens(tok)

            with df.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def example_conditional_task():
    """Example 3: Conditional control within a dataflow task.

    Shows how to use proc.if for data-dependent branching within
    a pipeline stage.
    """
    print("\n" + "=" * 60)
    print("Example 3: Conditional Control in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("CondTaskExample")

    with circuit.module("CondTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.static_step(1, "fast_path"):
            pass

        with mod.static_step(2, "slow_path_a"):
            pass

        with mod.static_step(2, "slow_path_b"):
            pass

        with mod.dataflow(
            "cond_pipeline",
            args=[("input", UInt(8))],
            returns=[UInt(8)],
        ) as df:
            with df.task("conditional_process") as task:
                # Condition based on constant (in real use, would be data-dependent)
                cond = task.const(1, 1)

                # Conditional execution
                with task.if_(cond) as (then_b, else_b):
                    with then_b:
                        # Fast path: 1 cycle
                        task.enable("fast_path")
                    with else_b:
                        # Slow path: sequential 2-cycle operations
                        with task.seq():
                            task.enable("slow_path_a")
                            task.enable("slow_path_b")

                data = task.const(7, 8)
                tok = task.create_token(data, UInt(8))
                task.yield_tokens(tok)

            with df.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def example_iterative_task():
    """Example 4: Iterative computation within a dataflow task.

    Shows how to use proc.static_repeat for compile-time known
    iteration counts within a pipeline stage.
    """
    print("\n" + "=" * 60)
    print("Example 4: Iterative Computation in Dataflow Task")
    print("=" * 60)

    circuit = Circuit("IterTaskExample")

    with circuit.module("IterTaskModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.static_step(1, "multiply"):
            pass

        with mod.dataflow(
            "iter_pipeline",
            args=[("base", UInt(32))],
            returns=[UInt(32)],
        ) as df:
            # Task that computes base^4 using repeated multiplication
            with df.task("power_of_4") as task:
                # 4 multiplication iterations
                with task.static_repeat(4):
                    task.enable("multiply")

                data = task.const(16, 32)  # Result placeholder
                tok = task.create_token(data, UInt(32))
                task.yield_tokens(tok)

            with df.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def example_complex_pipeline():
    """Example 5: Complex multi-stage pipeline with proc control.

    Shows a realistic pipeline where different stages have
    different internal control patterns.
    """
    print("\n" + "=" * 60)
    print("Example 5: Complex Pipeline with Proc Control")
    print("=" * 60)

    circuit = Circuit("ComplexPipeExample")

    with circuit.module("ComplexPipeModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Define various steps
        with mod.static_step(1, "fetch"):
            pass

        with mod.static_step(1, "decode"):
            pass

        with mod.static_step(2, "execute"):
            pass

        with mod.static_step(3, "memory"):
            pass

        with mod.static_step(1, "writeback"):
            pass

        with mod.dataflow(
            "processor_pipeline",
            args=[("instruction", UInt(32))],
            returns=[UInt(32)],
        ) as df:
            # Stage 1: Fetch and decode (sequential)
            with df.task("frontend") as task:
                with task.seq():
                    task.enable("fetch")
                    task.enable("decode")

                data = task.const(0, 32)
                tok1 = task.create_token(data, UInt(32))
                task.yield_tokens(tok1)

            # Stage 2: Execute and memory access (parallel for some ops)
            with df.task("backend", tokens_in=[tok1]) as task:
                with task.par():
                    task.enable("execute")
                    task.enable("memory")

                data = task.token_data(tok1)
                tok2 = task.create_token(data, UInt(32))
                task.yield_tokens(tok2)

            # Stage 3: Writeback
            with df.task("commit", tokens_in=[tok2]) as task:
                with task.seq():
                    task.enable("writeback")

                result = task.token_data(tok2)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def example_nested_control():
    """Example 6: Nested proc control structures.

    Shows deeply nested control: seq containing par containing if.
    """
    print("\n" + "=" * 60)
    print("Example 6: Nested Proc Control Structures")
    print("=" * 60)

    circuit = Circuit("NestedCtrlExample")

    with circuit.module("NestedCtrlModule") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.static_step(1, "init"):
            pass

        with mod.static_step(2, "proc_a"):
            pass

        with mod.static_step(2, "proc_b"):
            pass

        with mod.static_step(1, "finalize"):
            pass

        with mod.dataflow(
            "nested_pipeline",
            args=[("x", UInt(8))],
            returns=[UInt(8)],
        ) as df:
            with df.task("complex_task") as task:
                # Level 1: Sequential
                with task.seq():
                    task.enable("init")

                    # Level 2: Parallel branches
                    with task.par():
                        # Branch A: Iterative
                        with task.static_repeat(2):
                            task.enable("proc_a")

                        # Branch B: Conditional
                        cond = task.const(1, 1)
                        with task.if_(cond) as (then_b, else_b):
                            with then_b:
                                task.enable("proc_b")
                            with else_b:
                                pass  # Skip

                    task.enable("finalize")

                data = task.const(99, 8)
                tok = task.create_token(data, UInt(8))
                task.yield_tokens(tok)

            with df.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def main():
    """Run all examples."""
    print("=" * 60)
    print("PyCMT2 Dataflow-Proc Compatibility Examples")
    print("Phase 7 C1/C7: Proc Control Inside Dataflow Tasks")
    print("=" * 60)

    example_sequential_task()
    example_parallel_task()
    example_conditional_task()
    example_iterative_task()
    example_complex_pipeline()
    example_nested_control()

    print("\n" + "=" * 60)
    print("All examples completed successfully!")
    print("=" * 60)


if __name__ == "__main__":
    main()
