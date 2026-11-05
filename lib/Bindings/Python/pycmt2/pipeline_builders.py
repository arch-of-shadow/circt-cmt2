#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Pipeline shorthand builders for PyCMT2 EDSL.

This module provides high-level builders for common pipeline patterns,
simplifying the creation of linear pipelines and other common dataflow
topologies.

Example - Simple linear pipeline:
    from pycmt2 import Circuit, UInt, Pipeline

    circuit = Circuit("Adder")
    with circuit.module("AdderPipe") as mod:
        pipe = Pipeline(mod, "add_pipe", UInt(32), stages=3)

        @pipe.stage(0)
        def s0(task, input_data):
            # Stage 0: Compute partial sum
            partial = task.add(input_data, task.const(1, 32))
            return partial

        @pipe.stage(1)
        def s1(task, data):
            # Stage 1: Add more
            partial = task.add(data, task.const(2, 32))
            return partial

        @pipe.stage(2)
        def s2(task, data):
            # Stage 2: Final output
            return task.bits(data, 31, 0)

        pipe.build()  # Creates the dataflow IR
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Callable, Any

from .types import Cmt2Type, UInt, SyncToken
from .signals import Signal
from .dataflow_builders import DataflowBuilder, TaskBuilder, Token

if TYPE_CHECKING:
    from .module import ModuleBuilder


@dataclass
class PipelineStage:
    """Definition of a pipeline stage."""

    index: int
    name: str
    transform_fn: Callable[[TaskBuilder, Signal], Signal]
    latency: int = 1


class Pipeline:
    """High-level builder for linear pipelines.

    Pipeline provides a decorator-based API for defining pipeline stages
    that process data in sequence. Each stage receives data from the
    previous stage and passes transformed data to the next.

    The Pipeline automatically generates the dataflow IR with proper
    token connections between stages.

    Example:
        pipe = Pipeline(mod, "adder", UInt(32), stages=3, interval=1)

        @pipe.stage(0)
        def stage0(task, data):
            return task.add(data, task.const(1, 32))

        @pipe.stage(1)
        def stage1(task, data):
            return task.add(data, task.const(2, 33))

        @pipe.stage(2)
        def stage2(task, data):
            return task.bits(data, 31, 0)

        pipe.build()
    """

    def __init__(
        self,
        module: ModuleBuilder,
        name: str,
        data_type: Cmt2Type,
        stages: int,
        interval: int = 1,
        output_type: Cmt2Type | None = None,
    ):
        """Create a linear pipeline.

        Args:
            module: The module to add the pipeline to.
            name: Pipeline name.
            data_type: Type of input data.
            stages: Number of pipeline stages.
            interval: Initiation interval (default 1 = fully pipelined).
            output_type: Output type (default: same as input).
        """
        self._module = module
        self._name = name
        self._data_type = data_type
        self._stages_count = stages
        self._interval = interval
        self._output_type = output_type or data_type
        self._stages: dict[int, PipelineStage] = {}

    def stage(
        self, index: int, name: str | None = None, latency: int = 1
    ) -> Callable:
        """Decorator to define a pipeline stage.

        Args:
            index: Stage index (0 to stages-1).
            name: Optional stage name (auto-generated if not provided).
            latency: Stage latency in cycles (default 1).

        Returns:
            A decorator function.

        Example:
            @pipe.stage(0)
            def process_input(task, data):
                return task.add(data, task.const(1, 32))
        """
        if index < 0 or index >= self._stages_count:
            raise ValueError(
                f"Stage index {index} out of range [0, {self._stages_count})"
            )

        def decorator(fn: Callable[[TaskBuilder, Signal], Signal]) -> Callable:
            stage_name = name or f"stage{index}"
            self._stages[index] = PipelineStage(
                index=index,
                name=stage_name,
                transform_fn=fn,
                latency=latency,
            )
            return fn

        return decorator

    def build(self) -> None:
        """Build the pipeline dataflow IR.

        This generates the complete dataflow with all stages connected
        by tokens. Must be called after all stages are defined.

        Raises:
            ValueError: If not all stages are defined.
        """
        # Verify all stages are defined
        for i in range(self._stages_count):
            if i not in self._stages:
                raise ValueError(
                    f"Pipeline '{self._name}' missing stage {i}. "
                    f"Define all stages 0 to {self._stages_count - 1}."
                )

        # Build the dataflow
        with self._module.dataflow(
            self._name,
            args=[("input", self._data_type)],
            returns=[self._output_type],
            interval=self._interval,
        ) as df:
            # Track tokens between stages
            prev_token: Token | None = None

            for i in range(self._stages_count):
                stage = self._stages[i]
                is_first = i == 0
                is_last = i == self._stages_count - 1

                # Determine timing for this stage
                timing = None
                if stage.latency == 1:
                    timing = (i, i + 1)

                # Create the task.
                #
                # Important: `df.task(..., tokens_out=...)` must declare the number and
                # types of yielded tokens up front, otherwise MLIR verification fails
                # (`dataflow.yield` operand count must match task result count).
                tokens_in = [] if prev_token is None else [prev_token]
                tokens_out = [] if is_last else [SyncToken(self._data_type)]

                with df.task(
                    stage.name, tokens_in=tokens_in, tokens_out=tokens_out, timing=timing
                ) as task:
                    # Get input data
                    if is_first:
                        # First stage gets data from dataflow input
                        input_data = df.input
                    else:
                        # Subsequent stages get data from previous token
                        input_data = task.token_data(prev_token)

                    # Call the user's transform function
                    output_data = stage.transform_fn(task, input_data)

                    if is_last:
                        # Last stage returns the final value
                        task.return_values(output_data)
                    else:
                        # Intermediate stages yield tokens
                        # Tokens between pipeline stages are declared to carry the
                        # pipeline's `data_type`. Stage functions should return a
                        # value compatible with that type (use bits/truncate as
                        # needed).
                        prev_token = task.create_token(output_data, self._data_type)
                        task.yield_tokens(prev_token)


class ForkJoinPipeline:
    """High-level builder for fork-join dataflow patterns.

    ForkJoinPipeline allows defining parallel branches that fork from
    a common source and join at a sink.

    Example:
        fjp = ForkJoinPipeline(mod, "parallel_add", UInt(32))

        @fjp.source()
        def source(task, input_data):
            return input_data

        @fjp.branch("add1")
        def add1(task, data):
            return task.add(data, task.const(1, 32))

        @fjp.branch("add2")
        def add2(task, data):
            return task.add(data, task.const(2, 32))

        @fjp.sink()
        def sink(task, results):
            a, b = results
            return task.add(a, b)

        fjp.build()
    """

    def __init__(
        self,
        module: ModuleBuilder,
        name: str,
        data_type: Cmt2Type,
        output_type: Cmt2Type | None = None,
        interval: int | None = None,
    ):
        """Create a fork-join pipeline.

        Args:
            module: The module to add the pipeline to.
            name: Pipeline name.
            data_type: Type of input data.
            output_type: Output type (default: same as input).
            interval: Optional initiation interval.
        """
        self._module = module
        self._name = name
        self._data_type = data_type
        self._output_type = output_type or data_type
        self._interval = interval

        self._source_fn: Callable | None = None
        self._source_name: str = "source"
        self._branches: list[tuple[str, Callable]] = []
        self._sink_fn: Callable | None = None
        self._sink_name: str = "sink"

    def source(self, name: str = "source") -> Callable:
        """Decorator to define the source stage.

        Args:
            name: Source task name.

        Returns:
            A decorator function.
        """

        def decorator(fn: Callable[[TaskBuilder, Signal], Signal]) -> Callable:
            self._source_fn = fn
            self._source_name = name
            return fn

        return decorator

    def branch(self, name: str) -> Callable:
        """Decorator to define a parallel branch.

        Args:
            name: Branch task name.

        Returns:
            A decorator function.
        """

        def decorator(fn: Callable[[TaskBuilder, Signal], Signal]) -> Callable:
            self._branches.append((name, fn))
            return fn

        return decorator

    def sink(self, name: str = "sink") -> Callable:
        """Decorator to define the sink (join) stage.

        Args:
            name: Sink task name.

        Returns:
            A decorator function.
        """

        def decorator(
            fn: Callable[[TaskBuilder, list[Signal]], Signal]
        ) -> Callable:
            self._sink_fn = fn
            self._sink_name = name
            return fn

        return decorator

    def build(self) -> None:
        """Build the fork-join dataflow IR.

        Raises:
            ValueError: If source, branches, or sink not defined.
        """
        if self._source_fn is None:
            raise ValueError(f"ForkJoinPipeline '{self._name}' missing source")
        if not self._branches:
            raise ValueError(f"ForkJoinPipeline '{self._name}' has no branches")
        if self._sink_fn is None:
            raise ValueError(f"ForkJoinPipeline '{self._name}' missing sink")

        with self._module.dataflow(
            self._name,
            args=[("input", self._data_type)],
            returns=[self._output_type],
            interval=self._interval,
        ) as df:
            # Source task
            with df.task(self._source_name) as task:
                output_data = self._source_fn(task, df.input)
                source_token = task.create_token(output_data, output_data.type)
                task.yield_tokens(source_token)

            # Branch tasks (all consume the source token)
            branch_tokens = []
            for branch_name, branch_fn in self._branches:
                with df.task(branch_name, tokens_in=[source_token]) as task:
                    input_data = task.token_data(source_token)
                    output_data = branch_fn(task, input_data)
                    branch_token = task.create_token(output_data, output_data.type)
                    task.yield_tokens(branch_token)
                    branch_tokens.append(branch_token)

            # Sink task (consumes all branch tokens)
            with df.task(self._sink_name, tokens_in=branch_tokens) as task:
                branch_data = [task.token_data(tok) for tok in branch_tokens]
                output_data = self._sink_fn(task, branch_data)
                task.return_values(output_data)
