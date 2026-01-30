#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from __future__ import annotations

import inspect
from dataclasses import dataclass
from typing import Any, Callable, TypeVar

from ._decorators import _unwrap_module_builder, _get_function_name
from ._method_ref import BuilderContext
from ._typing import parse_typed_signature

F = TypeVar("F", bound=Callable[..., Any])


@dataclass
class _TaskDef:
    df: Any
    name: str | None = None
    tokens_in: list[Any] | None = None
    tokens_out: list[Any] | None = None
    timing: tuple[int, int] | None = None

    def __call__(self, func: F) -> F:
        task_name = self.name or _get_function_name(func)
        with self.df._df.task(  # noqa: SLF001
            task_name,
            tokens_in=self.tokens_in,
            tokens_out=self.tokens_out,
            timing=self.timing,
        ) as task:
            with BuilderContext(task):
                ret = func(task)

            # Optional convenience: allow returning tokens/values instead of
            # explicitly calling yield_tokens()/return_values().
            if ret is not None:
                if getattr(task, "_yield_tokens_set", False) or getattr(task, "_return_values_set", False):
                    raise RuntimeError(
                        f"Task '{task_name}' returned a value but already has a terminator"
                    )
                if isinstance(ret, tuple):
                    # Try tokens first, then values.
                    if ret and all(hasattr(x, "value") and hasattr(x, "type") for x in ret):
                        task.return_values(*ret)
                    else:
                        task.yield_tokens(*ret)
                else:
                    if hasattr(ret, "value") and hasattr(ret, "type"):
                        task.return_values(ret)
                    else:
                        task.yield_tokens(ret)

            setattr(func, "_cmt2_name", task_name)
            setattr(func, "_cmt2_tokens", tuple(getattr(task, "_output_tokens", [])))
        return func


class DataflowContext:
    """JIT wrapper around a PyCMT2 `DataflowBuilder`."""

    def __init__(self, df: Any):
        self._df = df

    def task(
        self,
        func: F | None = None,
        *,
        name: str | None = None,
        tokens_in: list[Any] | None = None,
        tokens_out: list[Any] | None = None,
        timing: tuple[int, int] | None = None,
    ):
        """Define a task inside this dataflow (decorator form)."""
        if func is not None:
            return _TaskDef(self, name=name, tokens_in=tokens_in, tokens_out=tokens_out, timing=timing)(func)
        return _TaskDef(self, name=name, tokens_in=tokens_in, tokens_out=tokens_out, timing=timing)

    def __getattr__(self, name: str) -> Any:
        return getattr(self._df, name)


class _DataflowDef:
    def __init__(self, module_builder: Any, name: str | None = None, interval: int | None = None):
        self._builder = _unwrap_module_builder(module_builder)
        self._name = name
        self._interval = interval

    def __call__(self, func: F) -> F:
        df_name = self._name or _get_function_name(func)
        frame = inspect.currentframe()
        definition_locals = None
        if frame is not None and frame.f_back is not None:
            definition_locals = dict(frame.f_back.f_locals)
        del frame

        arg_types, return_types = parse_typed_signature(
            func, require_return=True, definition_locals=definition_locals
        )
        with self._builder.dataflow(df_name, args=arg_types, returns=return_types, interval=self._interval) as df:
            ctx = DataflowContext(df)
            arg_values = [getattr(ctx, name) for name, _ in arg_types]
            func(ctx, *arg_values)
            setattr(func, "_cmt2_name", df_name)
        return func


def dataflow(module_builder: Any, name: str | None = None, interval: int | None = None) -> _DataflowDef:
    """Define a dataflow pipeline (decorator form)."""
    return _DataflowDef(module_builder, name=name, interval=interval)
