#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Core decorators for Cmt2 JIT with clear guard/body separation.

These decorators provide a clean API that:
1. Eliminates `def _:` boilerplate
2. Keeps guard/body operations clearly separated
3. Auto-infers names from function definitions
4. Eliminates string-based method calls

Example:
    @jit.rule(m)  # Name: "increment" (auto-inferred)
    def increment(r):
        with r.guard:
            r.always()

        with r.body:
            count.next = count.read + 1
"""

from __future__ import annotations

import functools
import inspect
from dataclasses import dataclass
from types import SimpleNamespace
from typing import Any, Callable, TypeVar

from ._context import RuleContext, MethodContext, ValueContext
from ._typing import ArgProxy, parse_typed_signature

F = TypeVar("F", bound=Callable[..., Any])

def _infer_assignment_name(*, depth: int) -> str | None:
    try:
        from circt.pycmt2.circuit import _get_assignment_target
    except Exception:
        _get_assignment_target = None

    if _get_assignment_target is None:
        return None
    return _get_assignment_target(depth=depth)


@dataclass(frozen=True)
class Elaborated:
    """Result of `@jit.elaborate`.

    JIT is stacked on PyCMT2 and elaboration fundamentally produces a PyCMT2
    `Circuit`. Some examples also need to export *typed handles* (e.g. an
    `InterfaceDecl` object) for testbench usage without string lookups; those
    can be carried in `handles`.
    """

    circuit: Any
    handles: Any | None = None

    def __iter__(self):
        yield self.circuit
        yield self.handles


def handles(**kwargs: Any) -> Any:
    """Create a handle bundle for `Elaborated(handles=...)`.

    This avoids stringly-typed lookups in examples by using attribute access:
      `h.writer`, `h.reader0`, ...
    """
    return SimpleNamespace(**kwargs)


def _get_function_name(func: Callable) -> str:
    """Get the name of a function."""
    return func.__name__

def _unwrap_module_builder(module_or_ctx: Any) -> Any:
    """Accept either `ModuleContext` or a raw PyCMT2 ModuleBuilder."""
    return getattr(module_or_ctx, "builder", module_or_ctx)


class _RuleDef:
    def __init__(self, module_builder: Any, *, alias: str | None = None):
        self._builder = _unwrap_module_builder(module_builder)
        self._alias = alias
        self._cm = None
        self._rule = None
        self._ctx: RuleContext | None = None

    def __call__(self, func: F) -> F:
        rule_name = self._alias or _get_function_name(func)
        with self._builder.rule(rule_name) as rule:
            ctx = RuleContext(rule)
            with ctx:
                func(ctx)
            setattr(func, "_cmt2_ref", rule.ref())
            setattr(func, "_cmt2_name", rule_name)
            # Convenience for scheduling APIs: `fn.ref()` mirrors PyCMT2 builders.
            setattr(func, "ref", lambda: getattr(func, "_cmt2_ref"))
        return func

    def __enter__(self) -> RuleContext:
        # For context-manager form, infer the symbol name from the `as <name>`
        # target (or require `alias=...`). Do not rely on PyCMT2's internal
        # naming JIT here, since this wrapper adds extra stack frames.
        name = self._alias or _infer_assignment_name(depth=3)
        if not name:
            raise TypeError("Cannot infer rule name; use `alias=...`")
        self._cm = self._builder.rule(name)
        self._rule = self._cm.__enter__()
        self._ctx = RuleContext(self._rule)
        self._ctx.__enter__()
        return self._ctx

    def __exit__(self, exc_type, exc_val, exc_tb):
        assert self._cm is not None and self._ctx is not None
        self._ctx.__exit__(exc_type, exc_val, exc_tb)
        return self._cm.__exit__(exc_type, exc_val, exc_tb)


def rule(module_builder: Any, name: str | None = None, *, alias: str | None = None) -> _RuleDef:
    """Define a rule (decorator or context manager).

    Decorator form:
        @jit.rule(m)
        def increment(r):
            with r.guard:
                r.always()
            with r.body:
                ...

    Context-manager form:
        with jit.rule(m) as increment:
            with increment.guard as g:
                g.always()
            with increment.body as b:
                ...
    """
    if name is not None:
        raise TypeError(
            "JIT rule naming is inferred from the `as <name>` target (or the "
            "decorated function name). Use `alias=...` only when you need an "
            "explicit name (e.g. inside loops)."
        )
    return _RuleDef(module_builder, alias=alias)


class _MethodDef:
    def __init__(
        self,
        module_builder: Any,
        *,
        alias: str | None = None,
    ):
        self._builder = _unwrap_module_builder(module_builder)
        self._alias = alias
        self._cm = None
        self._method = None
        self._ctx: MethodContext | None = None

    def __call__(self, func: F) -> F:
        method_name = self._alias or _get_function_name(func)
        frame = inspect.currentframe()
        definition_locals = None
        if frame is not None and frame.f_back is not None:
            definition_locals = dict(frame.f_back.f_locals)
        del frame

        arg_types, return_types = parse_typed_signature(
            func, require_return=True, definition_locals=definition_locals
        )
        with self._builder.method(method_name, args=arg_types, returns=return_types) as method:
            ctx = MethodContext(method, args=arg_types)
            with ctx:
                proxies = [ArgProxy(name) for name, _ in arg_types]
                func(ctx, *proxies)
            setattr(func, "_cmt2_ref", method.ref())
            setattr(func, "_cmt2_name", method_name)
            setattr(func, "ref", lambda: getattr(func, "_cmt2_ref"))
        return func

    def __enter__(self) -> MethodContext:
        raise TypeError(
            "Context-manager form of @jit.method is not supported. "
            "Use the decorator form with typed Python annotations."
        )


def method(
    module_builder: Any,
    name: str | None = None,
    *,
    alias: str | None = None,
) -> _MethodDef:
    """Define an action method (decorator or context manager)."""
    if name is not None:
        raise TypeError(
            "JIT method naming is inferred from the decorated function name. "
            "Use `alias=...` only when you need an explicit name (e.g. inside loops)."
        )
    return _MethodDef(module_builder, alias=alias)


class _ValueDef:
    def __init__(self, module_builder: Any, *, alias: str | None = None):
        self._builder = _unwrap_module_builder(module_builder)
        self._alias = alias
        self._cm = None
        self._value = None
        self._ctx: ValueContext | None = None

    def __call__(self, func: F) -> F:
        value_name = self._alias or _get_function_name(func)
        frame = inspect.currentframe()
        definition_locals = None
        if frame is not None and frame.f_back is not None:
            definition_locals = dict(frame.f_back.f_locals)
        del frame

        arg_types, return_types = parse_typed_signature(
            func, require_return=True, definition_locals=definition_locals
        )
        if arg_types:
            raise TypeError("@jit.value functions cannot take arguments")
        with self._builder.value(value_name, returns=return_types) as value:
            ctx = ValueContext(value)
            with ctx:
                func(ctx)
            setattr(func, "_cmt2_ref", value.ref())
            setattr(func, "_cmt2_name", value_name)
            setattr(func, "ref", lambda: getattr(func, "_cmt2_ref"))
        return func

    def __enter__(self) -> ValueContext:
        raise TypeError(
            "Context-manager form of @jit.value is not supported. "
            "Use the decorator form with a typed return annotation."
        )


def value(module_builder: Any, name: str | None = None, *, alias: str | None = None) -> _ValueDef:
    """Define a value method (decorator or context manager)."""
    if name is not None:
        raise TypeError(
            "JIT value naming is inferred from the decorated function name. "
            "Use `alias=...` only when you need an explicit name (e.g. inside loops)."
        )
    return _ValueDef(module_builder, alias=alias)


def elaborate(func: F) -> F:
    """Decorator for circuit elaboration.
    
    Example:
        @jit.elaborate
        def counter(width: int = 32):
            circuit = Circuit("Counter")
            # ... build circuit ...
            return circuit
    """
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        # Provide a default source location for root ops created during
        # elaboration (circuit/module containers), improving diagnostics.
        try:
            from circt.pycmt2.location import default_python_location
        except Exception:
            default_python_location = None

        if default_python_location is None:
            result = func(*args, **kwargs)
        else:
            with default_python_location(depth=2):
                result = func(*args, **kwargs)

        if isinstance(result, tuple) and len(result) == 2:
            result = Elaborated(result[0], result[1])

        try:
            from circt.pycmt2 import Circuit as PyCmt2Circuit
        except Exception:
            PyCmt2Circuit = None

        if PyCmt2Circuit is not None:
            if isinstance(result, Elaborated):
                if not isinstance(result.circuit, PyCmt2Circuit):
                    raise TypeError(
                        "@jit.elaborate must return a `circt.pycmt2.Circuit` "
                        "(or `Elaborated(circuit=...)`)."
                    )
            elif not isinstance(result, PyCmt2Circuit):
                raise TypeError(
                    "@jit.elaborate must return a `circt.pycmt2.Circuit` "
                    "(or `(Circuit, handles)` / `Elaborated`)."
                )

        return result
    
    return wrapper


__all__ = [
    "elaborate",
    "Elaborated",
    "handles",
    "rule",
    "method",
    "value",
]
