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
from typing import Any, Callable, TypeVar

from ._context import RuleContext, MethodContext, ValueContext

F = TypeVar("F", bound=Callable[..., Any])


def _get_function_name(func: Callable) -> str:
    """Get the name of a function."""
    return func.__name__

def _unwrap_module_builder(module_or_ctx: Any) -> Any:
    """Accept either `ModuleContext` or a raw PyCMT2 ModuleBuilder."""
    return getattr(module_or_ctx, "builder", module_or_ctx)


class _RuleDef:
    def __init__(self, module_builder: Any, name: str | None = None):
        self._builder = _unwrap_module_builder(module_builder)
        self._name = name
        self._cm = None
        self._rule = None
        self._ctx: RuleContext | None = None

    def __call__(self, func: F) -> F:
        rule_name = self._name or _get_function_name(func)
        with self._builder.rule(rule_name) as rule:
            ctx = RuleContext(rule)
            with ctx:
                func(ctx)
            setattr(func, "_cmt2_ref", rule.ref())
            setattr(func, "_cmt2_name", rule_name)
        return func

    def __enter__(self) -> RuleContext:
        self._cm = self._builder.rule(self._name)
        self._rule = self._cm.__enter__()
        self._ctx = RuleContext(self._rule)
        self._ctx.__enter__()
        return self._ctx

    def __exit__(self, exc_type, exc_val, exc_tb):
        assert self._cm is not None and self._ctx is not None
        self._ctx.__exit__(exc_type, exc_val, exc_tb)
        return self._cm.__exit__(exc_type, exc_val, exc_tb)


def rule(module_builder: Any, name: str | None = None) -> _RuleDef:
    """Define a rule (decorator or context manager).

    Decorator form:
        @jit.rule(m)
        def increment(r):
            with r.guard:
                r.always()
            with r.body:
                ...

    Context-manager form (easier for porting PyCMT2 examples):
        with jit.rule(m, "increment") as rule:
            with rule.guard as g:
                g.always()
            with rule.body as b:
                ...
    """
    return _RuleDef(module_builder, name=name)


class _MethodDef:
    def __init__(
        self,
        module_builder: Any,
        name: str | None = None,
        args: list | None = None,
        returns: list | None = None,
    ):
        self._builder = _unwrap_module_builder(module_builder)
        self._name = name
        self._args = args or []
        self._returns = returns or []
        self._cm = None
        self._method = None
        self._ctx: MethodContext | None = None

    def __call__(self, func: F) -> F:
        method_name = self._name or _get_function_name(func)
        with self._builder.method(method_name, args=self._args, returns=self._returns) as method:
            ctx = MethodContext(method, args=self._args)
            with ctx:
                sig = inspect.signature(func)
                if len(sig.parameters) != 1:
                    raise TypeError(
                        f"@jit.method expects a function of the form `def {method_name}(r): ...`.\n"
                        "Access method arguments inside guard/body as `r.<argname>`.\n"
                        f"Declared args: {[name for name, _ in self._args]}"
                    )
                func(ctx)
            setattr(func, "_cmt2_ref", method.ref())
            setattr(func, "_cmt2_name", method_name)
        return func

    def __enter__(self) -> MethodContext:
        self._cm = self._builder.method(self._name, args=self._args, returns=self._returns)
        self._method = self._cm.__enter__()
        self._ctx = MethodContext(self._method, args=self._args)
        self._ctx.__enter__()
        return self._ctx

    def __exit__(self, exc_type, exc_val, exc_tb):
        assert self._cm is not None and self._ctx is not None
        self._ctx.__exit__(exc_type, exc_val, exc_tb)
        return self._cm.__exit__(exc_type, exc_val, exc_tb)


def method(
    module_builder: Any,
    name: str | None = None,
    args: list | None = None,
    returns: list | None = None,
) -> _MethodDef:
    """Define an action method (decorator or context manager)."""
    return _MethodDef(module_builder, name=name, args=args, returns=returns)


class _ValueDef:
    def __init__(self, module_builder: Any, name: str | None = None, returns: list | None = None):
        self._builder = _unwrap_module_builder(module_builder)
        self._name = name
        self._returns = returns or []
        self._cm = None
        self._value = None
        self._ctx: ValueContext | None = None

    def __call__(self, func: F) -> F:
        value_name = self._name or _get_function_name(func)
        with self._builder.value(value_name, returns=self._returns) as value:
            ctx = ValueContext(value)
            with ctx:
                sig = inspect.signature(func)
                if len(sig.parameters) != 1:
                    raise TypeError(
                        f"@jit.value expects a function of the form `def {value_name}(r): ...`."
                    )
                func(ctx)
            setattr(func, "_cmt2_ref", value.ref())
            setattr(func, "_cmt2_name", value_name)
        return func

    def __enter__(self) -> ValueContext:
        self._cm = self._builder.value(self._name, returns=self._returns)
        self._value = self._cm.__enter__()
        self._ctx = ValueContext(self._value)
        self._ctx.__enter__()
        return self._ctx

    def __exit__(self, exc_type, exc_val, exc_tb):
        assert self._cm is not None and self._ctx is not None
        self._ctx.__exit__(exc_type, exc_val, exc_tb)
        return self._cm.__exit__(exc_type, exc_val, exc_tb)


def value(module_builder: Any, name: str | None = None, returns: list | None = None) -> _ValueDef:
    """Define a value method (decorator or context manager)."""
    return _ValueDef(module_builder, name=name, returns=returns)


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

        try:
            from circt.pycmt2 import Circuit as PyCmt2Circuit
        except Exception:
            PyCmt2Circuit = None

        if PyCmt2Circuit is not None and not isinstance(result, PyCmt2Circuit):
            raise TypeError(
                "@jit.elaborate functions must return a `circt.pycmt2.Circuit`."
            )

        return result
    
    return wrapper


__all__ = [
    "elaborate",
    "rule",
    "method",
    "value",
]
