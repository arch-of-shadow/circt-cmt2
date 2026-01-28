#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Dataflow decorators for JIT v2.

Provides agile syntax for PyCMT2 dataflow builders.
"""

from __future__ import annotations

from typing import Any, Callable, List, Tuple


def dataflow(
    module_builder: Any,
    name: str,
    args: List[Tuple[str, Any]] | None = None,
    returns: List[Any] | None = None,
    interval: int = 1,
):
    """Decorator for creating a dataflow pipeline.
    
    Wraps module_builder.dataflow() context manager.
    
    Example:
        @jit.dataflow(m, "pipeline", args=[("input", UInt(32))], returns=[UInt(32)])
        def build_pipeline(df):
            @jit.task(df, "stage1")
            def stage1(task):
                # ... process ...
                task.yield_tokens(tok_out)
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Dataflow name
        args: List of (name, type) tuples for inputs
        returns: List of return types
        interval: Pipeline interval
        
    Returns:
        Decorator function
    """
    args = args or []
    returns = returns or []
    
    def decorator(df_fn: Callable[[Any], Any]):
        with module_builder.dataflow(name, args=args, returns=returns, interval=interval) as df:
            return df_fn(df)
    
    return decorator


def task(dataflow_builder: Any, name: str, tokens_in: List[Any] | None = None, timing: Tuple[int, int] | None = None):
    """Decorator for creating a dataflow task.
    
    Example:
        @jit.task(df, "compute", timing=(0, 2))
        def compute_task(task):
            # ... computation ...
            task.yield_tokens(output_token)
    
    Args:
        dataflow_builder: PyCMT2 DataflowBuilder
        name: Task name
        tokens_in: List of input tokens
        timing: (start, end) timing tuple
        
    Returns:
        Decorator function
    """
    tokens_in = tokens_in or []
    
    def decorator(task_fn: Callable[[Any], Any]):
        with dataflow_builder.task(name, tokens_in=tokens_in, timing=timing) as task:
            return task_fn(task)
    
    return decorator


def forkjoin(module_builder: Any, name: str, data_type: Any, output_type: Any | None = None):
    """Decorator for creating a fork-join pipeline.
    
    Wraps PyCMT2's ForkJoinPipeline builder.
    
    Example:
        @jit.forkjoin(m, "parallel", UInt(32), UInt(33))
        def build_parallel(fjp):
            @fjp.source
            def source(task, data):
                return data
            
            @fjp.branch("branch_a")
            def branch_a(task, data):
                return task.add(data, task.const(1, 32))
            
            @fjp.branch("branch_b")
            def branch_b(task, data):
                return task.add(data, task.const(2, 32))
            
            @fjp.sink
            def sink(task, results):
                a, b = results
                return task.add(a, b)
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Pipeline name
        data_type: Input data type
        output_type: Output data type (defaults to data_type)
        
    Returns:
        Decorator that provides source/branch/sink sub-decorators
    """
    from circt.pycmt2 import ForkJoinPipeline
    
    output_type = output_type or data_type
    
    def decorator(fj_fn: Callable[[Any], Any]):
        fjp = ForkJoinPipeline(module_builder, name, data_type, output_type)
        fj_ctx = _ForkJoinContext(fjp)
        result = fj_fn(fj_ctx)
        fjp.build()
        return result
    
    return decorator


class _ForkJoinContext:
    """Context for building fork-join pipelines.
    
    Provides @source, @branch, @sink decorators.
    """
    
    def __init__(self, fjp):
        self._fjp = fjp
    
    def source(self, func: Callable[[Any, Any], Any]):
        """Decorator for the source task."""
        @self._fjp.source()
        def wrapper(task, input_data):
            return func(task, input_data)
        return wrapper
    
    def branch(self, name: str):
        """Decorator for a branch task."""
        def decorator(func: Callable[[Any, Any], Any]):
            @self._fjp.branch(name)
            def wrapper(task, data):
                return func(task, data)
            return wrapper
        return decorator
    
    def sink(self, func: Callable[[Any, List[Any]], Any]):
        """Decorator for the sink task."""
        @self._fjp.sink()
        def wrapper(task, results):
            return func(task, results)
        return wrapper
