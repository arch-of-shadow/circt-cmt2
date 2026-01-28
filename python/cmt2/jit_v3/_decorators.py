#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Core decorators for JIT v3 with clear guard/body separation.

These decorators provide a clean API that:
1. Eliminates `def _:` boilerplate
2. Keeps guard/body operations clearly separated
3. Auto-infers names from function definitions
4. Eliminates string-based method calls

Example:
    @jit.rule(m)  # Name: "increment" (auto-inferred)
    def increment(r):
        @r.guard
        r.always()
        
        @r.body
        count.next = count.read + 1
"""

from __future__ import annotations

import functools
from typing import Any, Callable, TypeVar

from ._context import RuleContext, MethodContext, ValueContext
from ._method_ref import wrap_instance

F = TypeVar("F", bound=Callable[..., Any])


def _get_function_name(func: Callable) -> str:
    """Get the name of a function."""
    return func.__name__


def rule(module_builder: Any, name: str | None = None):
    """Decorator for creating a rule with auto-inferred name.
    
    The rule name is inferred from the function name unless explicitly provided.
    The decorated function receives a RuleContext `r` with @guard and @body decorators.
    
    Example:
        @jit.rule(m)  # Name: "increment"
        def increment(r):
            @r.guard
            r.always()
            
            @r.body
            count.next = count.read + 1
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Optional explicit rule name (inferred from function if not given)
        
    Returns:
        Decorator function
    """
    def decorator(func: F) -> F:
        rule_name = name or _get_function_name(func)
        
        # Execute immediately
        with module_builder.rule(rule_name) as rule:
            ctx = RuleContext(rule)
            with ctx:
                func(ctx)
        
        return func
    
    return decorator


def method(module_builder: Any, name: str | None = None, args: list | None = None, returns: list | None = None):
    """Decorator for creating an action method.
    
    Example:
        @jit.method(m, args=[("data", UInt(32))])
        def write(r, data):
            @r.guard
            r.always()
            
            @r.body
            reg.write(data)
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Optional explicit method name
        args: List of (name, type) tuples for arguments
        returns: List of return types
        
    Returns:
        Decorator function
    """
    args = args or []
    returns = returns or []
    
    def decorator(func: F) -> F:
        method_name = name or _get_function_name(func)
        
        with module_builder.method(method_name, args=args, returns=returns) as method:
            ctx = MethodContext(method, args=args)
            with ctx:
                # Build argument list
                arg_values = [ctx.arg(name) for name, _ in args]
                func(ctx, *arg_values)
        
        return func
    
    return decorator


def value(module_builder: Any, name: str | None = None, returns: list | None = None):
    """Decorator for creating a value method.
    
    Example:
        @jit.value(m, returns=[UInt(32)])
        def get_count(r):
            @r.guard
            r.always()
            
            @r.body
            r.returns(count.read)
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Optional explicit method name
        returns: List of return types
        
    Returns:
        Decorator function
    """
    returns = returns or []
    
    def decorator(func: F) -> F:
        value_name = name or _get_function_name(func)
        
        with module_builder.value(value_name, returns=returns) as value:
            ctx = ValueContext(value)
            with ctx:
                func(ctx)
        
        return func
    
    return decorator


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
        return func(*args, **kwargs)
    
    return wrapper


def simulate(func: F) -> F:
    """Decorator for circuit simulation.
    
    Example:
        @jit.simulate
        def test_counter():
            circuit = counter()
            # ... run simulation ...
            return results
    """
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        return func(*args, **kwargs)
    
    return wrapper
