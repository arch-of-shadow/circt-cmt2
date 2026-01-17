#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Dataflow builders for PyCMT2 EDSL.

This module provides builders for constructing dataflow pipelines using
token-based synchronization. Dataflow pipelines consist of tasks connected
by tokens that carry data and synchronization signals.

Example:
    with mod.dataflow("pipeline", args=[("input", UInt(32))],
                     returns=[UInt(32)]) as df:
        # Stage 0: Process input
        with df.task("stage0") as task:
            tok0 = task.create_token(df.input, UInt(32))
            task.yield_tokens(tok0)

        # Stage 1: Transform data
        with df.task("stage1", tokens_in=[tok0]) as task:
            data = task.token_data(tok0)
            result = task.add(data, task.const(1, 32))
            tok1 = task.create_token(result, UInt(32))
            task.yield_tokens(tok1)

        # Final stage: Output result
        with df.task("final", tokens_in=[tok1]) as task:
            output = task.token_data(tok1)
            task.return_values(output)
"""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
from typing import TYPE_CHECKING, Iterator, Sequence

from .types import Cmt2Type, UInt, SyncToken
from .signals import Signal
from .builders import RegionBuilder
from .location import get_python_location

if TYPE_CHECKING:
    from .module import ModuleBuilder


@dataclass
class Token:
    """Reference to a dataflow token.

    Tokens are produced by tasks via create_token() and yield_tokens(),
    and consumed by downstream tasks via their tokens_in parameter.
    """

    value: object  # MLIR Value
    token_type: SyncToken
    name: str  # Task name that produced this token

    @property
    def data_type(self) -> Cmt2Type | None:
        """Get the data type carried by this token."""
        return self.token_type.data_type


class TaskBuilder(RegionBuilder):
    """Builder for dataflow tasks.

    Tasks are the atomic units of computation in a dataflow pipeline.
    They consume input tokens, perform computation, and produce output tokens.

    Example:
        with df.task("multiply", tokens_in=[tok_a, tok_b]) as task:
            a = task.token_data(tok_a)
            b = task.token_data(tok_b)
            result = task.mul(a, b)
            tok_out = task.create_token(result, UInt(64))
            task.yield_tokens(tok_out)
    """

    def __init__(
        self,
        dataflow: DataflowBuilder,
        name: str | None,
        tokens_in: list[Token],
        timing: tuple[int, int] | None = None,
    ):
        self._dataflow = dataflow
        self._name = name
        self._tokens_in = tokens_in
        self._timing = timing
        self._op = None
        self._output_tokens: list[Token] = []
        self._return_values_set = False
        self._yield_tokens_set = False

        # Capture Python source location
        self._python_loc = get_python_location(depth=4)

        # Create the task op (but don't finalize yet)
        self._block = None  # Will be set in _create_task_op

    def _create_task_op(self, output_token_types: list[SyncToken]):
        """Create the MLIR task operation with known output types."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, Attribute, Operation
        from circt.dialects import cmt2

        ctx = self._dataflow._module._circuit._ctx
        mlir_loc = self._python_loc.to_mlir_location(ctx.mlir_context)

        # Build token input operands and types
        token_operands = [tok.value for tok in self._tokens_in]
        token_in_types = [
            tok.token_type.to_firrtl_type(ctx.mlir_context)
            for tok in self._tokens_in
        ]

        # Build token input names (use producer task names)
        token_in_name_attrs = [StringAttr.get(tok.name, context=ctx.mlir_context) for tok in self._tokens_in]

        # Build output token types
        output_mlir_types = [
            ty.to_firrtl_type(ctx.mlir_context) for ty in output_token_types
        ]

        # Build attributes dict
        attrs = {
            "sym_name": StringAttr.get(self.name, context=ctx.mlir_context),
            "token_in_names": ArrayAttr.get(token_in_name_attrs, context=ctx.mlir_context),
        }

        # Add timing attribute if specified
        if self._timing is not None:
            start, end = self._timing
            attr_str = f"#cmt2.timing<[{start}, {end}]>"
            attrs["timing"] = Attribute.parse(attr_str, ctx.mlir_context)

        with InsertionPoint(self._dataflow._body_block):
            # Use Operation.create for more control
            self._op = Operation.create(
                "cmt2.dataflow.task",
                results=output_mlir_types,
                operands=token_operands,
                attributes=attrs,
                regions=1,  # Single body region
                loc=mlir_loc,
            )

            # Create empty body block - DataflowTaskOp is NOT IsolatedFromAbove
            # so token inputs are accessed directly from the outer scope via operands
            self._block = Block.create_at_start(
                self._op.regions[0], [], []
            )

        # Create a mapping from input tokens to the task's operands
        # (Don't mutate the original tokens - they may be used by other tasks in fork patterns)
        # Use id() as key since Token may not be hashable
        # The task can access token operands directly (not IsolatedFromAbove)
        self._token_to_block_arg = {}
        for i, tok in enumerate(self._tokens_in):
            # Map to the task's operand (token_inputs[i])
            self._token_to_block_arg[id(tok)] = self._op.operands[i]

        # Initialize RegionBuilder
        super().__init__(self._block, mlir_loc, ctx)

    @property
    def name(self) -> str:
        """Get the task name."""
        if self._name is None:
            from .circuit import _get_assignment_target

            jit_name = _get_assignment_target(depth=6)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"task_{id(self):x}"
        return self._name

    def token_valid(self, token: Token) -> Signal:
        """Extract the valid signal from a token.

        Args:
            token: The token to extract from.

        Returns:
            A 1-bit Signal indicating token validity.
        """
        from circt.ir import InsertionPoint, Operation
        from circt.dialects import firrtl

        if token not in self._tokens_in:
            raise ValueError(f"Token {token.name} is not an input to this task")

        # Use block argument from mapping (not token.value which may be the original SSA value)
        block_arg = self._token_to_block_arg[id(token)]

        with InsertionPoint(self._block):
            result_ty = firrtl.UIntType.get(self._ctx.mlir_context, 1)
            valid_op = Operation.create(
                "cmt2.token.valid",
                results=[result_ty],
                operands=[block_arg],
                loc=self._loc,
            )
            return Signal(valid_op.result, UInt(1), self)

    def token_data(self, token: Token) -> Signal:
        """Extract the data payload from a token.

        Args:
            token: The token to extract data from.

        Returns:
            A Signal containing the token's data.

        Raises:
            ValueError: If token doesn't carry data.
        """
        from circt.ir import InsertionPoint, Operation

        if token not in self._tokens_in:
            raise ValueError(f"Token {token.name} is not an input to this task")

        if not token.token_type.has_data():
            raise ValueError(f"Token {token.name} does not carry data")

        # Use block argument from mapping (not token.value which may be the original SSA value)
        block_arg = self._token_to_block_arg[id(token)]
        data_type = token.token_type.data_type

        with InsertionPoint(self._block):
            result_ty = data_type.to_firrtl_type(self._ctx.mlir_context)
            data_op = Operation.create(
                "cmt2.token.data",
                results=[result_ty],
                operands=[block_arg],
                loc=self._loc,
            )
            return Signal(data_op.result, data_type, self)

    def create_token(
        self,
        data: Signal | None = None,
        data_type: Cmt2Type | None = None,
        mode: str = "ls",
    ) -> Token:
        """Create a new token, optionally with data.

        Args:
            data: Optional data payload signal.
            data_type: Type of data (required if data is provided, inferred otherwise).
            mode: Token mode - "ls" (latency sensitive) or "li" (latency insensitive).

        Returns:
            A Token reference for use in downstream tasks.
        """
        from circt.ir import InsertionPoint, Operation

        # Determine token type
        if data is not None:
            if data_type is None:
                data_type = data.type
            token_type = SyncToken(data_type, mode)
        else:
            token_type = SyncToken(None, mode)

        with InsertionPoint(self._block):
            token_mlir_type = token_type.to_firrtl_type(self._ctx.mlir_context)

            if data is not None:
                create_op = Operation.create(
                    "cmt2.token.create",
                    results=[token_mlir_type],
                    operands=[data.value],
                    loc=self._loc,
                )
            else:
                create_op = Operation.create(
                    "cmt2.token.create",
                    results=[token_mlir_type],
                    operands=[],
                    loc=self._loc,
                )

            token = Token(create_op.result, token_type, self.name)
            return token

    def join_tokens(self, *tokens: Token, mode: str = "ls") -> Token:
        """Join multiple tokens into a single synchronization token.

        This is useful for synchronization points where multiple
        upstream tasks must complete before proceeding.

        Args:
            *tokens: Tokens to join.
            mode: Mode for the output token.

        Returns:
            A new Token that is valid when all inputs are valid.
        """
        from circt.ir import InsertionPoint, Operation

        if len(tokens) < 1:
            raise ValueError("join_tokens requires at least one token")

        for tok in tokens:
            if tok not in self._tokens_in:
                raise ValueError(f"Token {tok.name} is not an input to this task")

        token_type = SyncToken(None, mode)

        with InsertionPoint(self._block):
            token_mlir_type = token_type.to_firrtl_type(self._ctx.mlir_context)
            # Use block arguments from mapping (not token.value for fork pattern support)
            token_values = [self._token_to_block_arg[id(tok)] for tok in tokens]

            join_op = Operation.create(
                "cmt2.token.join",
                results=[token_mlir_type],
                operands=token_values,
                loc=self._loc,
            )

            return Token(join_op.result, token_type, self.name)

    # ===================================================================
    # Proc Control Methods (Phase 7 C7: Proc control in dataflow tasks)
    # These enable multi-cycle control flow within dataflow tasks
    # ===================================================================

    @contextmanager
    def seq(self) -> Iterator[TaskBuilder]:
        """Create a sequential control block.

        Operations in a seq block execute one after another.

        Example:
            with task.seq() as s:
                task.enable("step1")
                task.enable("step2")

        Yields:
            This TaskBuilder for chaining.
        """
        from circt.ir import InsertionPoint, Block, Operation

        with InsertionPoint(self._block):
            seq_op = Operation.create(
                "cmt2.proc.seq",
                results=[],
                operands=[],
                regions=1,
                loc=self._loc,
            )
            seq_block = Block.create_at_start(seq_op.regions[0])

        # Save current block and switch to seq block
        saved_block = self._block
        self._block = seq_block
        try:
            yield self
        finally:
            self._block = saved_block

    @contextmanager
    def par(self) -> Iterator[TaskBuilder]:
        """Create a parallel control block.

        Operations in a par block execute concurrently.

        Example:
            with task.par() as p:
                task.enable("op_a")
                task.enable("op_b")

        Yields:
            This TaskBuilder for chaining.
        """
        from circt.ir import InsertionPoint, Block, Operation

        with InsertionPoint(self._block):
            par_op = Operation.create(
                "cmt2.proc.par",
                results=[],
                operands=[],
                regions=1,
                loc=self._loc,
            )
            par_block = Block.create_at_start(par_op.regions[0])

        saved_block = self._block
        self._block = par_block
        try:
            yield self
        finally:
            self._block = saved_block

    @contextmanager
    def if_(self, condition: Signal) -> Iterator[tuple[TaskBuilder, TaskBuilder]]:
        """Create a conditional control block.

        Example:
            with task.if_(cond) as (then_b, else_b):
                with then_b:
                    task.enable("step_true")
                with else_b:
                    task.enable("step_false")

        Args:
            condition: Boolean condition signal.

        Yields:
            Tuple of (then_builder, else_builder).
        """
        from circt.ir import InsertionPoint, Block, Operation, TypeAttr

        cond_type = condition.type.to_firrtl_type(self._ctx.mlir_context)

        with InsertionPoint(self._block):
            if_op = Operation.create(
                "cmt2.proc.if",
                results=[],
                operands=[condition.value],
                attributes={"cond_type": TypeAttr.get(cond_type)},
                regions=2,
                loc=self._loc,
            )
            then_block = Block.create_at_start(if_op.regions[0])
            else_block = Block.create_at_start(if_op.regions[1])

        # Create sub-builders for then/else blocks
        class SubBuilder:
            def __init__(sub_self, block):
                sub_self._block = block
                sub_self._parent = self

            def __enter__(sub_self):
                sub_self._saved_block = self._block
                self._block = sub_self._block
                return sub_self

            def __exit__(sub_self, *args):
                self._block = sub_self._saved_block
                return False

        yield SubBuilder(then_block), SubBuilder(else_block)

    @contextmanager
    def static_repeat(self, count: int, latency: int | None = None) -> Iterator[TaskBuilder]:
        """Create a compile-time unrolled loop.

        Example:
            with task.static_repeat(4) as r:
                task.enable("multiply")

        Args:
            count: Number of iterations (must be constant).
            latency: Optional latency per iteration for static timing.

        Yields:
            This TaskBuilder for chaining.
        """
        from circt.ir import InsertionPoint, Block, Operation, IntegerAttr, IntegerType

        attrs = {
            "count": IntegerAttr.get(IntegerType.get_signless(64), count),
        }
        if latency is not None:
            attrs["latency"] = IntegerAttr.get(IntegerType.get_signless(64), latency)

        with InsertionPoint(self._block):
            repeat_op = Operation.create(
                "cmt2.proc.static_repeat",
                results=[],
                operands=[],
                attributes=attrs,
                regions=1,
                loc=self._loc,
            )
            repeat_block = Block.create_at_start(repeat_op.regions[0])

        saved_block = self._block
        self._block = repeat_block
        try:
            yield self
        finally:
            self._block = saved_block

    def enable(self, step_name: str) -> None:
        """Enable a step by name.

        This is used within proc control blocks to activate steps.

        Example:
            with task.seq():
                task.enable("step_a")
                task.enable("step_b")

        Args:
            step_name: Name of the step to enable.
        """
        from circt.ir import InsertionPoint, Operation, FlatSymbolRefAttr

        with InsertionPoint(self._block):
            Operation.create(
                "cmt2.proc.enable",
                results=[],
                operands=[],
                attributes={
                    "step": FlatSymbolRefAttr.get(step_name),
                },
                loc=self._loc,
            )

    def yield_tokens(self, *tokens: Token) -> None:
        """Yield tokens from this task for downstream consumption.

        This terminates the task body and makes the tokens available
        to other tasks in the dataflow.

        Args:
            *tokens: Tokens to yield.
        """
        from circt.ir import InsertionPoint, Operation

        if self._yield_tokens_set or self._return_values_set:
            raise RuntimeError("Task already has a terminator")

        with InsertionPoint(self._block):
            token_values = [tok.value for tok in tokens]
            Operation.create(
                "cmt2.dataflow.yield",
                results=[],
                operands=token_values,
                loc=self._loc,
            )

        self._output_tokens = list(tokens)
        self._yield_tokens_set = True

    def return_values(self, *values: Signal) -> None:
        """Return values from the dataflow pipeline.

        This should only be used in the final task of a dataflow
        to produce the pipeline's output values.

        Args:
            *values: Values to return from the pipeline.
        """
        from circt.ir import InsertionPoint, Operation

        if self._yield_tokens_set or self._return_values_set:
            raise RuntimeError("Task already has a terminator")

        with InsertionPoint(self._block):
            value_mlir = [v.value for v in values]
            Operation.create(
                "cmt2.dataflow.return",
                results=[],
                operands=value_mlir,
                loc=self._loc,
            )

        self._return_values_set = True

    def _finalize(self):
        """Finalize task construction."""
        if not self._yield_tokens_set and not self._return_values_set:
            raise ValueError(
                f"Task '{self.name}' must call yield_tokens() or return_values()"
            )


class DataflowBuilder:
    """Builder for dataflow pipelines.

    Dataflow pipelines describe concurrent computations connected
    by token-based synchronization. Each task in the pipeline
    executes when its input tokens are valid.

    Example:
        with mod.dataflow("adder_pipe", args=[("a", UInt(32)), ("b", UInt(32))],
                         returns=[UInt(32)], interval=1) as df:
            # First stage
            with df.task("add") as task:
                sum_val = task.add(df.a, df.b)
                tok = task.create_token(sum_val, UInt(33))
                task.yield_tokens(tok)

            # Final stage
            with df.task("output", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                truncated = task.bits(result, 31, 0)
                task.return_values(truncated)
    """

    def __init__(
        self,
        module: ModuleBuilder,
        name: str | None,
        args: list[tuple[str, Cmt2Type]],
        returns: list[Cmt2Type],
        interval: int | None = None,
    ):
        self._module = module
        self._name = name
        self._arg_types = args
        self._return_types = returns
        self._interval = interval
        self._op = None
        self._body_block = None
        self._arg_signals: dict[str, Signal] = {}
        self._tasks: list[TaskBuilder] = []
        self._pending_tasks: list[tuple[TaskBuilder, list[SyncToken]]] = []

        # Capture Python source location
        self._python_loc = get_python_location(depth=3)

        # Create the dataflow op
        self._create_dataflow_op()

    def _create_dataflow_op(self):
        """Create the MLIR dataflow operation."""
        from circt.ir import (
            InsertionPoint,
            StringAttr,
            ArrayAttr,
            Block,
            FunctionType,
            TypeAttr,
            IntegerAttr,
            IntegerType,
        )
        from circt.dialects import cmt2

        ctx = self._module._circuit._ctx
        mlir_loc = self._python_loc.to_mlir_location(ctx.mlir_context)

        # Build function type (need to pass context)
        arg_mlir_types = [
            ty.to_firrtl_type(ctx.mlir_context) for _, ty in self._arg_types
        ]
        ret_mlir_types = [
            ty.to_firrtl_type(ctx.mlir_context) for ty in self._return_types
        ]
        func_type = FunctionType.get(arg_mlir_types, ret_mlir_types, context=ctx.mlir_context)

        # Build argNames array
        arg_name_attrs = [StringAttr.get(name, context=ctx.mlir_context) for name, _ in self._arg_types]

        # Build attributes
        attrs = {
            "sym_name": StringAttr.get(self.name, context=ctx.mlir_context),
            "function_type": TypeAttr.get(func_type, context=ctx.mlir_context),
            "argNames": ArrayAttr.get(arg_name_attrs, context=ctx.mlir_context),
        }

        # Add interval attribute if specified
        if self._interval is not None:
            attrs["interval"] = IntegerAttr.get(
                IntegerType.get_signless(64, context=ctx.mlir_context), self._interval
            )

        with InsertionPoint(self._module._op.body):
            from circt.ir import Operation

            self._op = Operation.create(
                "cmt2.proc.dataflow",
                results=[],
                operands=[],
                attributes=attrs,
                regions=1,  # Single body region
                loc=mlir_loc,
            )

            # Create body block with arguments
            arg_locs = [mlir_loc] * len(arg_mlir_types)
            self._body_block = Block.create_at_start(
                self._op.regions[0], arg_mlir_types, arg_locs
            )

            # Create signals for arguments (accessible via df.argname)
            from .builders import RegionBuilder

            dummy_builder = RegionBuilder(self._body_block, mlir_loc, ctx)
            for i, (arg_name, arg_ty) in enumerate(self._arg_types):
                sig = Signal(self._body_block.arguments[i], arg_ty, dummy_builder)
                self._arg_signals[arg_name] = sig

    @property
    def name(self) -> str:
        """Get the dataflow name."""
        if self._name is None:
            from .circuit import _get_assignment_target

            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"dataflow_{id(self):x}"
        return self._name

    def __getattr__(self, name: str) -> Signal:
        """Access dataflow arguments by name."""
        if name.startswith("_"):
            raise AttributeError(name)
        if name in self._arg_signals:
            return self._arg_signals[name]
        raise AttributeError(f"Dataflow has no argument '{name}'")

    @contextmanager
    def task(
        self,
        name: str | None = None,
        tokens_in: list[Token] | None = None,
        tokens_out: list[SyncToken] | None = None,
        timing: tuple[int, int] | None = None,
    ) -> Iterator[TaskBuilder]:
        """Create a dataflow task.

        Args:
            name: Optional task name (auto-generated if not provided).
            tokens_in: List of input tokens from upstream tasks.
            tokens_out: List of output token types (SyncToken). Required if task
                       will yield tokens. For final tasks using return_values(),
                       leave this as None or empty.
            timing: Optional (start, end) cycle timing for this task.

        Yields:
            A TaskBuilder for defining the task body.

        Example:
            # Task with token output
            with df.task("process", tokens_in=[upstream_tok],
                        tokens_out=[SyncToken(UInt(32))]) as task:
                data = task.token_data(upstream_tok)
                result = task.add(data, task.const(1, 32))
                out_tok = task.create_token(result)
                task.yield_tokens(out_tok)

            # Final task with return (no tokens_out)
            with df.task("final", tokens_in=[tok]) as task:
                result = task.token_data(tok)
                task.return_values(result)
        """
        if tokens_in is None:
            tokens_in = []
        if tokens_out is None:
            tokens_out = []

        # Create task builder
        task = TaskBuilder(self, name, tokens_in, timing)

        # Create the op with specified output token types
        task._create_task_op(tokens_out)

        yield task

        # Finalize and record produced tokens
        task._finalize()

        # Update op result types based on yielded tokens
        if task._output_tokens:
            # The tokens were created inside the task, their values are
            # the TokenCreateOp results, which were yielded
            # We need to update the task op's results to match
            self._update_task_outputs(task)

        self._tasks.append(task)

    def _update_task_outputs(self, task: TaskBuilder):
        """Update task op to reflect yielded token types.

        Since MLIR ops are immutable after creation, we need to handle
        this differently. The tokens are created inside the task body
        and yielded via DataflowYieldOp. The task op's results should
        match these yielded values.

        For our Python builder, we track the tokens via the Token objects
        and their values reference the TaskOp results after op creation.
        """
        # After the task body is built, the yield_tokens call has already
        # created the DataflowYieldOp terminator. The tokens reference
        # the op results.

        # Update token values to point to task op results
        for i, tok in enumerate(task._output_tokens):
            if i < len(task._op.results):
                tok.value = task._op.results[i]

    def _finalize(self):
        """Finalize dataflow construction."""
        # Verify we have at least one task with return_values
        has_return = False
        for task in self._tasks:
            if task._return_values_set:
                has_return = True
                break

        if self._return_types and not has_return:
            raise ValueError(
                f"Dataflow '{self.name}' has return types but no task "
                "calls return_values()"
            )


# Convenience function for creating dataflow with pipeline semantics
def pipeline_dataflow(
    module: ModuleBuilder,
    name: str,
    stages: int,
    data_type: Cmt2Type,
    interval: int = 1,
) -> DataflowBuilder:
    """Create a simple linear pipeline dataflow.

    This is a convenience function for creating pipelines where
    data flows linearly through N stages.

    Args:
        module: The module to add the pipeline to.
        name: Pipeline name.
        stages: Number of pipeline stages.
        data_type: Type of data flowing through the pipeline.
        interval: Initiation interval (default 1 = fully pipelined).

    Returns:
        A DataflowBuilder configured for linear pipeline.

    Note:
        For more complex dataflow patterns (fork/join), use
        module.dataflow() directly.
    """
    return DataflowBuilder(
        module,
        name,
        args=[("input", data_type)],
        returns=[data_type],
        interval=interval,
    )
