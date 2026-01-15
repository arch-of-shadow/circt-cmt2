#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Fork-Join Dataflow Example using PyCMT2 Dataflow Builders

This example demonstrates fork-join dataflow patterns where a single
source task produces tokens consumed by multiple parallel branches,
which then rejoin at a sink task.

The example shows:
- Fork pattern: one token consumed by multiple tasks
- Join pattern: one task waiting for multiple input tokens
- Using the ForkJoinPipeline shorthand builder
- Using explicit dataflow builders for complex patterns

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/dataflow_forkjoin.py
"""

from circt.pycmt2 import (
    Circuit,
    UInt,
    ForkJoinPipeline,
)


def create_forkjoin_shorthand():
    """Create a fork-join pipeline using the ForkJoinPipeline shorthand."""
    print("=" * 60)
    print("Fork-Join Pipeline - Shorthand Builder")
    print("=" * 60)

    circuit = Circuit("ForkJoinShorthand")

    with circuit.module("ParallelAdd") as mod:
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


def create_forkjoin_explicit():
    """Create a complex fork-join pattern using explicit dataflow builders."""
    print("\n" + "=" * 60)
    print("Fork-Join Pipeline - Explicit Dataflow Builder")
    print("=" * 60)

    circuit = Circuit("ForkJoinExplicit")

    with circuit.module("DiamondPipeline") as mod:
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

        with mod.dataflow(
            "diamond",
            args=[("x", UInt(16))],
            returns=[UInt(16)],
        ) as df:
            # Source task: create token with input data
            with df.task("source") as task:
                tok_src = task.create_token(df.x, UInt(16))
                task.yield_tokens(tok_src)

            # Branch A: multiply by 2
            with df.task("branch_a", tokens_in=[tok_src], timing=(1, 2)) as task:
                data = task.token_data(tok_src)
                result_a = task.mul(data, task.const(2, 16))
                tok_a = task.create_token(task.bits(result_a, 15, 0), UInt(16))
                task.yield_tokens(tok_a)

            # Branch B: add 10 (same tok_src - fork pattern)
            with df.task("branch_b", tokens_in=[tok_src], timing=(1, 2)) as task:
                data = task.token_data(tok_src)
                result_b = task.add(data, task.const(10, 16))
                tok_b = task.create_token(task.bits(result_b, 15, 0), UInt(16))
                task.yield_tokens(tok_b)

            # Join task: wait for both branches and sum results
            with df.task("join", tokens_in=[tok_a, tok_b], timing=(2, 3)) as task:
                val_a = task.token_data(tok_a)
                val_b = task.token_data(tok_b)
                sum_val = task.add(val_a, val_b)
                tok_sum = task.create_token(task.bits(sum_val, 15, 0), UInt(16))
                task.yield_tokens(tok_sum)

            # Output task: final result
            with df.task("output", tokens_in=[tok_sum], timing=(3, 4)) as task:
                result = task.token_data(tok_sum)
                task.return_values(result)

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def create_multiway_fork():
    """Create a 3-way fork pattern."""
    print("\n" + "=" * 60)
    print("Multi-way Fork Pattern")
    print("=" * 60)

    circuit = Circuit("MultiwayFork")

    with circuit.module("ThreeWayFork") as mod:
        clk = mod.clock()
        rst = mod.reset()

        with mod.dataflow(
            "three_way",
            args=[("input", UInt(8))],
            returns=[UInt(8)],
        ) as df:
            # Source
            with df.task("source") as task:
                tok = task.create_token(df.input, UInt(8))
                task.yield_tokens(tok)

            # Three parallel branches consuming the same token
            with df.task("inc1", tokens_in=[tok]) as task:
                data = task.token_data(tok)
                result = task.add(data, task.const(1, 8))
                tok1 = task.create_token(task.bits(result, 7, 0), UInt(8))
                task.yield_tokens(tok1)

            with df.task("inc2", tokens_in=[tok]) as task:
                data = task.token_data(tok)
                result = task.add(data, task.const(2, 8))
                tok2 = task.create_token(task.bits(result, 7, 0), UInt(8))
                task.yield_tokens(tok2)

            with df.task("inc3", tokens_in=[tok]) as task:
                data = task.token_data(tok)
                result = task.add(data, task.const(3, 8))
                tok3 = task.create_token(task.bits(result, 7, 0), UInt(8))
                task.yield_tokens(tok3)

            # Join all three
            with df.task("join_all", tokens_in=[tok1, tok2, tok3]) as task:
                v1 = task.token_data(tok1)
                v2 = task.token_data(tok2)
                v3 = task.token_data(tok3)
                # Sum all three: (x+1) + (x+2) + (x+3) = 3x + 6
                sum12 = task.add(v1, v2)
                sum_all = task.add(sum12, v3)
                task.return_values(task.bits(sum_all, 7, 0))

    print("\n=== CMT2 MLIR ===")
    print(circuit.emit_mlir())


def main():
    """Run all fork-join examples."""
    create_forkjoin_shorthand()
    create_forkjoin_explicit()
    create_multiway_fork()

    print("\n" + "=" * 60)
    print("Examples completed successfully!")
    print("=" * 60)


if __name__ == "__main__":
    main()
