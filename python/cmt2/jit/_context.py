#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Context objects for Cmt2 JIT with clear guard/body separation.

This module provides RuleContext, MethodContext, and ValueContext
that enable clean context-manager syntax:

    @jit.rule(m)
    def increment(r):
        with r.guard:
            r.always()
        
        with r.body:
            count.next = count.read + 1
"""

from __future__ import annotations

from typing import Any

from ._method_ref import BuilderContext, _get_current_builder


class _GuardContext:
    """Context manager for guard region."""
    
    def __init__(self, rule_context: "RuleContext"):
        self._ctx = rule_context
    
    def __enter__(self):
        self._ctx._enter_guard()
        return self._ctx
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self._ctx._exit_guard()


class _BodyContext:
    """Context manager for body region."""
    
    def __init__(self, rule_context: "RuleContext"):
        self._ctx = rule_context
    
    def __enter__(self):
        self._ctx._enter_body()
        return self._ctx
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self._ctx._exit_body()


class RuleContext:
    """Context for building a rule with clear guard/body separation.
    
    Provides guard and body context managers for explicit region marking.
    
    Example:
        @jit.rule(m)
        def increment(r):
            with r.guard:
                r.always()
            
            with r.body:
                count.next = count.read + 1
    """
    
    def __init__(self, rule: Any):
        self._rule = rule
        self._guard_builder: Any = None
        self._body_builder: Any = None
        self.guard = _GuardContext(self)
        self.body = _BodyContext(self)

    @property
    def name(self) -> str:
        return getattr(self._rule, "name", "<unnamed>")

    def ref(self) -> Any:
        return self._rule.ref()
    
    def _enter_guard(self):
        """Enter guard region."""
        self._guard_cm = self._rule.guard()
        self._guard_builder = self._guard_cm.__enter__()
        self._guard_builder_ctx = BuilderContext(self._guard_builder)
        self._guard_builder_ctx.__enter__()
    
    def _exit_guard(self):
        """Exit guard region."""
        if self._guard_builder is not None:
            guard_builder_ctx = self._guard_builder_ctx
            guard_cm = self._guard_cm
            try:
                guard_builder_ctx.__exit__(None, None, None)
            finally:
                self._guard_builder_ctx = None
                self._guard_builder = None
            guard_cm.__exit__(None, None, None)
    
    def _enter_body(self):
        """Enter body region."""
        self._body_cm = self._rule.body()
        self._body_builder = self._body_cm.__enter__()
        self._body_builder_ctx = BuilderContext(self._body_builder)
        self._body_builder_ctx.__enter__()
    
    def _exit_body(self):
        """Exit body region."""
        if self._body_builder is not None:
            body_builder_ctx = self._body_builder_ctx
            body_cm = self._body_cm
            try:
                body_builder_ctx.__exit__(None, None, None)
            finally:
                self._body_builder_ctx = None
                self._body_builder = None
            body_cm.__exit__(None, None, None)
    
    def _get_guard(self) -> Any:
        """Get guard builder (must be in guard region)."""
        if self._guard_builder is None:
            raise RuntimeError("Not in guard region. Use 'with r.guard:'")
        return self._guard_builder
    
    def _get_body(self) -> Any:
        """Get body builder (must be in body region)."""
        if self._body_builder is None:
            raise RuntimeError("Not in body region. Use 'with r.body:'")
        return self._body_builder
    
    # Guard operations (only valid in guard region)
    def always(self) -> None:
        """Guard that always fires."""
        g = self._get_guard()
        g.always()

    def ready(self, condition: Any) -> None:
        """Set the guard condition (i.e. when the rule can fire)."""
        g = self._get_guard()
        g.returns(condition)

    def equals(self, a: Any, b: Any) -> None:
        """Convenience: set guard to `a == b`."""
        g = self._get_guard()
        self.ready(g.eq(a, b))

    def returns(self, *values: Any) -> None:
        """Region-sensitive `returns`.

        - In guard: `returns(condition)`
        - In body: forwards to underlying BodyBuilder (rare for rules)
        """
        if self._guard_builder is not None:
            if len(values) != 1:
                raise TypeError("Guard returns expects exactly 1 condition")
            self._guard_builder.returns(values[0])
            return
        if self._body_builder is not None:
            self._body_builder.returns(*values)
            return
        raise RuntimeError("Not in any region")

    def call(self, target: Any, method_or_value: Any, *args: Any, **kwargs: Any) -> Any:
        """Call a method/value on an instance via the active builder.

        This wrapper exists so `b.call(signal_ref, "...")` works even when
        `signal_ref` is a JIT `SignalRef` (which must be unwrapped).
        """
        builder = _get_current_builder()
        if builder is None:
            raise RuntimeError("Not in any region. Use 'with r.guard:' or 'with r.body:'")
        return builder.call(target, method_or_value, *args, **kwargs)
    
    def const(self, value: int, width: int) -> Any:
        """Create a constant (works in both regions)."""
        if self._guard_builder is not None:
            return self._guard_builder.const(value, width)
        elif self._body_builder is not None:
            return self._body_builder.const(value, width)
        else:
            raise RuntimeError("Not in any region. Use 'with r.guard:' or 'with r.body:'")

    def __getattr__(self, name: str) -> Any:
        """Delegate unknown attributes to the active PyCMT2 builder."""
        if self._guard_builder is not None:
            return getattr(self._guard_builder, name)
        if self._body_builder is not None:
            return getattr(self._body_builder, name)
        raise AttributeError(name)
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        # Exit body first, then guard
        self._exit_body()
        self._exit_guard()


class ValueContext:
    """Context for building a value method with clear guard/body separation.
    
    Example:
        @jit.value(m, returns=[UInt(32)])
        def get_count(r):
            with r.guard:
                r.always()
            
            with r.body:
                r.returns(count.read)
    """
    
    def __init__(self, value: Any):
        self._value = value
        self._guard_builder: Any = None
        self._body_builder: Any = None
        self.guard = _GuardContext(self)
        self.body = _BodyContext(self)

    @property
    def name(self) -> str:
        return getattr(self._value, "name", "<unnamed>")

    def ref(self) -> Any:
        return self._value.ref()
    
    def _enter_guard(self):
        self._guard_cm = self._value.guard()
        self._guard_builder = self._guard_cm.__enter__()
        self._guard_builder_ctx = BuilderContext(self._guard_builder)
        self._guard_builder_ctx.__enter__()
    
    def _exit_guard(self):
        if self._guard_builder is not None:
            guard_builder_ctx = self._guard_builder_ctx
            guard_cm = self._guard_cm
            try:
                guard_builder_ctx.__exit__(None, None, None)
            finally:
                self._guard_builder_ctx = None
                self._guard_builder = None
            guard_cm.__exit__(None, None, None)
    
    def _enter_body(self):
        self._body_cm = self._value.body()
        self._body_builder = self._body_cm.__enter__()
        self._body_builder_ctx = BuilderContext(self._body_builder)
        self._body_builder_ctx.__enter__()
    
    def _exit_body(self):
        if self._body_builder is not None:
            body_builder_ctx = self._body_builder_ctx
            body_cm = self._body_cm
            try:
                body_builder_ctx.__exit__(None, None, None)
            finally:
                self._body_builder_ctx = None
                self._body_builder = None
            body_cm.__exit__(None, None, None)

    def call(self, target: Any, method_or_value: Any, *args: Any, **kwargs: Any) -> Any:
        builder = _get_current_builder()
        if builder is None:
            raise RuntimeError("Not in any region. Use 'with r.guard:' or 'with r.body:'")
        return builder.call(target, method_or_value, *args, **kwargs)
    
    def _get_guard(self) -> Any:
        if self._guard_builder is None:
            raise RuntimeError("Not in guard region. Use 'with r.guard:'")
        return self._guard_builder
    
    def _get_body(self) -> Any:
        if self._body_builder is None:
            raise RuntimeError("Not in body region. Use 'with r.body:'")
        return self._body_builder
    
    # Guard operations
    def always(self) -> None:
        """Guard that always fires."""
        g = self._get_guard()
        g.always()

    def ready(self, condition: Any) -> None:
        """Set the guard condition (i.e. when the value method is ready)."""
        g = self._get_guard()
        g.returns(condition)
    
    def returns(self, *values: Any) -> None:
        """Region-sensitive `returns`.

        - In guard: `returns(condition)`
        - In body: `returns(*values)`
        """
        if self._guard_builder is not None:
            if len(values) != 1:
                raise TypeError("Guard returns expects exactly 1 condition")
            self._guard_builder.returns(values[0])
            return
        if self._body_builder is not None:
            self._body_builder.returns(*values)
            return
        raise RuntimeError("Not in any region")
    
    def const(self, value: int, width: int) -> Any:
        """Create a constant."""
        if self._guard_builder is not None:
            return self._guard_builder.const(value, width)
        elif self._body_builder is not None:
            return self._body_builder.const(value, width)
        else:
            raise RuntimeError("Not in any region")

    def __getattr__(self, name: str) -> Any:
        """Delegate unknown attributes to the active PyCMT2 builder."""
        if self._guard_builder is not None:
            return getattr(self._guard_builder, name)
        if self._body_builder is not None:
            return getattr(self._body_builder, name)
        raise AttributeError(name)
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self._exit_body()
        self._exit_guard()


class MethodContext:
    """Context for building an action method with clear guard/body separation.
    
    Example:
        @jit.method(m, args=[("data", UInt(32))])
        def write(r):
            with r.guard:
                r.always()
            
            with r.body:
                reg.write(r.data)
    """
    
    def __init__(self, method: Any, args: list | None = None):
        self._method = method
        self._args = args or []
        self._arg_names = {name for name, _ in self._args}
        self._guard_builder: Any = None
        self._body_builder: Any = None
        self._arg_values: dict[str, Any] = {}
        self.guard = _GuardContext(self)
        self.body = _BodyContext(self)

    @property
    def name(self) -> str:
        return getattr(self._method, "name", "<unnamed>")

    def ref(self) -> Any:
        return self._method.ref()
    
    def _enter_guard(self):
        self._guard_cm = self._method.guard()
        self._guard_builder = self._guard_cm.__enter__()
        self._guard_builder_ctx = BuilderContext(self._guard_builder)
        self._guard_builder_ctx.__enter__()
    
    def _exit_guard(self):
        if self._guard_builder is not None:
            guard_builder_ctx = self._guard_builder_ctx
            guard_cm = self._guard_cm
            try:
                guard_builder_ctx.__exit__(None, None, None)
            finally:
                self._guard_builder_ctx = None
                self._guard_builder = None
            guard_cm.__exit__(None, None, None)
    
    def _enter_body(self):
        self._body_cm = self._method.body()
        self._body_builder = self._body_cm.__enter__()
        self._body_builder_ctx = BuilderContext(self._body_builder)
        self._body_builder_ctx.__enter__()
        # Extract argument values
        for arg_name, _ in self._args:
            self._arg_values[arg_name] = self._body_builder.arg(arg_name)
    
    def _exit_body(self):
        if self._body_builder is not None:
            body_builder_ctx = self._body_builder_ctx
            body_cm = self._body_cm
            try:
                body_builder_ctx.__exit__(None, None, None)
            finally:
                self._body_builder_ctx = None
                self._body_builder = None
            body_cm.__exit__(None, None, None)

    def call(self, target: Any, method_or_value: Any, *args: Any, **kwargs: Any) -> Any:
        builder = _get_current_builder()
        if builder is None:
            raise RuntimeError("Not in any region. Use 'with r.guard:' or 'with r.body:'")
        return builder.call(target, method_or_value, *args, **kwargs)
    
    def _get_guard(self) -> Any:
        if self._guard_builder is None:
            raise RuntimeError("Not in guard region. Use 'with r.guard:'")
        return self._guard_builder
    
    def _get_body(self) -> Any:
        if self._body_builder is None:
            raise RuntimeError("Not in body region. Use 'with r.body:'")
        return self._body_builder

    def __getattr__(self, name: str) -> Any:
        """Expose method arguments as attributes inside guard/body regions.

        PyCMT2 exposes arguments in the guard builder as attributes, and in the
        body builder via `b.arg(name)`. JIT mirrors that as `r.<argname>`.
        """
        if name in self._arg_names:
            if self._guard_builder is not None:
                return getattr(self._guard_builder, name)
            if self._body_builder is not None:
                return self._arg_values[name]
            raise RuntimeError(
                f"Cannot access method argument '{name}' outside of guard/body context."
            )
        if self._guard_builder is not None:
            return getattr(self._guard_builder, name)
        if self._body_builder is not None:
            return getattr(self._body_builder, name)
        raise AttributeError(name)
    
    def arg(self, name: str) -> Any:
        """Get an argument value by name."""
        if name not in self._arg_values:
            raise RuntimeError(f"Unknown argument: {name}")
        return self._arg_values[name]
    
    # Guard operations
    def always(self) -> None:
        """Guard that always fires."""
        g = self._get_guard()
        g.always()

    def ready(self, condition: Any) -> None:
        """Set the guard condition (i.e. when the method is ready)."""
        g = self._get_guard()
        g.returns(condition)

    def returns(self, *values: Any) -> None:
        """Region-sensitive `returns` (guard condition vs body results)."""
        if self._guard_builder is not None:
            if len(values) != 1:
                raise TypeError("Guard returns expects exactly 1 condition")
            self._guard_builder.returns(values[0])
            return
        if self._body_builder is not None:
            self._body_builder.returns(*values)
            return
        raise RuntimeError("Not in any region")
    
    def const(self, value: int, width: int) -> Any:
        """Create a constant."""
        if self._guard_builder is not None:
            return self._guard_builder.const(value, width)
        elif self._body_builder is not None:
            return self._body_builder.const(value, width)
        else:
            raise RuntimeError("Not in any region")
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self._exit_body()
        self._exit_guard()
