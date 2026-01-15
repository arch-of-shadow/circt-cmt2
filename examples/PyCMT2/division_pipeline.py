#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Division Pipeline Example using PyCMT2 Dataflow Builders

This example demonstrates a pipelined division operation using the
new dataflow pipeline builders. The division is performed using
restoring division algorithm across multiple pipeline stages.

The example shows:
- Using the Pipeline shorthand builder for linear pipelines
- Token-based synchronization between stages
- Timing annotation for each pipeline stage

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/division_pipeline.py
"""

from circt.pycmt2 import (
    Circuit,
    UInt,
    Pipeline,
    timing_interval,
    pipeline_timing,
)


def create_division_pipeline_shorthand():
    """Create a division pipeline using the Pipeline shorthand builder."""
    print("=" * 60)
    print("Division Pipeline - Shorthand Builder")
    print("=" * 60)

    circuit = Circuit("DivisionPipelineShorthand")

    with circuit.module("Divider") as mod:
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


def create_division_pipeline_explicit():
    """Create a division pipeline using explicit dataflow builders."""
    print("\n" + "=" * 60)
    print("Division Pipeline - Explicit Dataflow Builder")
    print("=" * 60)

    circuit = Circuit("DivisionPipelineExplicit")

    with circuit.module("Divider") as mod:
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


def main():
    """Run both division pipeline examples."""
    # Example using the shorthand Pipeline builder
    create_division_pipeline_shorthand()

    # Example using explicit dataflow builders
    create_division_pipeline_explicit()

    print("\n" + "=" * 60)
    print("Examples completed successfully!")
    print("=" * 60)


if __name__ == "__main__":
    main()
