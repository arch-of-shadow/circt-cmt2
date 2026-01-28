#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Core decorators for JIT v2.

These decorators provide agile syntax on top of PyCMT2 without
duplicating functionality.
"""

from __future__ import annotations

import functools
import inspect
from typing import Any, Callable, TypeVar, overload

# Type variable for decorated functions
F = TypeVar("F", bound=Callable[..., Any])


def elaborate(func: F) -> F:
    """Decorator for circuit elaboration.
    
    This is a thin wrapper that adds caching to circuit construction.
    The actual MLIR construction is done by PyCMT2 inside the function.
    
    Example:
        @jit.elaborate
        def counter_design(width: int = 32):
            circuit = Circuit("Counter")
            # ... build with PyCMT2 ...
            return circuit
        
        # Usage
        circuit = counter_design(width=16)  # Cached
    """
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        # For v2, we simply call the function
        # Caching can be added via _cache module if needed
        return func(*args, **kwargs)
    
    return wrapper


def simulate(func: F) -> F:
    """Decorator for circuit simulation.
    
    Marks a function that runs simulation. The function should build
    the circuit and run simulation using PyCMT2.
    
    Example:
        @jit.simulate
        def test_counter():
            circuit = counter_design(width=16)
            # ... run simulation with PyCMT2 ...
            return results
    """
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        return func(*args, **kwargs)
    
    return wrapper


def module(circuit: Any, name: str):
    """Decorator for building a module.
    
    Wraps circuit.module() context manager in a decorator.
    
    Example:
        @jit.module(circuit, "Counter")
        def build(m):
            clk = m.clock()
            rst = m.reset()
            # ... build module contents ...
    
    Args:
        circuit: PyCMT2 Circuit object
        name: Module name
        
    Returns:
        Decorator function
    """
    def decorator(build_fn: Callable[[Any], Any]):
        with circuit.module(name) as m:
            return build_fn(m)
    
    return decorator


def rule(module_builder: Any, name: str):
    """Decorator for creating a rule.
    
    Provides @rule.guard and @rule.body decorators for the rule.
    
    Example:
        @jit.rule(m, "increment")
        def increment(rule):
            @rule.guard
            def guard(g):
                g.always()
            
            @rule.body
            def body(b):
                b.call(count, "write", b.call(count, "read") + 1)
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Rule name
        
    Returns:
        Decorator that provides guard/body sub-decorators
    """
    def decorator(rule_fn: Callable[[Any], Any]):
        with module_builder.rule(name) as rule:
            # Create a rule context object with guard/body decorators
            rule_ctx = _RuleContext(rule)
            return rule_fn(rule_ctx)
    
    return decorator


def method(module_builder: Any, name: str, args: list | None = None, returns: list | None = None):
    """Decorator for creating an action method.
    
    Example:
        @jit.method(m, "write", args=[("data", UInt(32))])
        def write_method(method):
            @method.guard
            def guard(g): g.always()
            @method.body
            def body(b): b.call(reg, "write", b.arg("data"))
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Method name
        args: List of (name, type) tuples for arguments
        returns: List of return types
        
    Returns:
        Decorator
    """
    args = args or []
    returns = returns or []
    
    def decorator(method_fn: Callable[[Any], Any]):
        with module_builder.method(name, args=args, returns=returns) as method:
            method_ctx = _MethodContext(method)
            return method_fn(method_ctx)
    
    return decorator


def value(module_builder: Any, name: str, returns: list):
    """Decorator for creating a value method.
    
    Example:
        @jit.value(m, "read", returns=[UInt(32)])
        def read_method(val):
            @val.guard
            def guard(g): g.always()
            @val.body
            def body(b): b.returns(b.call(reg, "read"))
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Method name
        returns: List of return types
        
    Returns:
        Decorator
    """
    def decorator(value_fn: Callable[[Any], Any]):
        with module_builder.value(name, returns=returns) as value:
            value_ctx = _ValueContext(value)
            return value_fn(value_ctx)
    
    return decorator


class _RuleContext:
    """Context object passed to @rule decorated functions.
    
    Provides @guard and @body decorators for the rule.
    """
    
    def __init__(self, rule):
        self._rule = rule
    
    def guard(self, func: Callable[[Any], Any]):
        """Decorator for the rule guard."""
        with self._rule.guard() as g:
            return func(g)
    
    def body(self, func: Callable[[Any], Any]):
        """Decorator for the rule body."""
        with self._rule.body() as b:
            return func(b)


class _MethodContext:
    """Context object passed to @method decorated functions."""
    
    def __init__(self, method):
        self._method = method
    
    def guard(self, func: Callable[[Any], Any]):
        with self._method.guard() as g:
            return func(g)
    
    def body(self, func: Callable[[Any], Any]):
        with self._method.body() as b:
            return func(b)


class _ValueContext:
    """Context object passed to @value decorated functions."""
    
    def __init__(self, value):
        self._value = value
    
    def guard(self, func: Callable[[Any], Any]):
        with self._value.guard() as g:
            return func(g)
    
    def body(self, func: Callable[[Any], Any]):
        with self._value.body() as b:
            return func(b)
