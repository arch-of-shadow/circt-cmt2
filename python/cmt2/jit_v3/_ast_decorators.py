#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""AST-based decorators for zero-boilerplate hardware design.

These decorators use AST manipulation to:
1. Auto-infer names from function definitions
2. Eliminate `def _` boilerplate
3. Inject code into guard/body regions
"""

from __future__ import annotations

import ast
import functools
import inspect
import textwrap
from typing import Any, Callable, TypeVar

from ._method_ref import BuilderContext, wrap_instance

F = TypeVar("F", bound=Callable[..., Any])


def _get_function_name(func: Callable) -> str:
    """Get the name of a function.
    
    Args:
        func: Function to get name from
        
    Returns:
        Function name
    """
    return func.__name__


def _extract_function_body(func: Callable) -> list[ast.stmt]:
    """Extract the body statements from a function.
    
    This uses AST parsing to get the function body for injection.
    
    Args:
        func: Function to extract body from
        
    Returns:
        List of AST statements
    """
    try:
        source = inspect.getsource(func)
    except (OSError, TypeError):
        raise RuntimeError(f"Cannot get source for function {func}")
    
    # Dedent to handle nested functions
    source = textwrap.dedent(source)
    
    # Parse into AST
    try:
        tree = ast.parse(source)
    except SyntaxError as e:
        raise RuntimeError(f"Cannot parse source: {e}")
    
    # Get function definition
    func_def = tree.body[0]
    if not isinstance(func_def, ast.FunctionDef):
        raise RuntimeError(f"Expected function definition, got {type(func_def)}")
    
    return func_def.body


def _analyze_body(stmts: list[ast.stmt]) -> tuple[list[ast.stmt], list[ast.stmt]]:
    """Analyze function body to separate guard and body statements.
    
    Looks for patterns like:
        guard.always()  -> Goes to guard region
        count.next = 1  -> Goes to body region
    
    Args:
        stmts: List of AST statements
        
    Returns:
        Tuple of (guard_stmts, body_stmts)
    """
    guard_stmts = []
    body_stmts = []
    
    for stmt in stmts:
        # Check if this is a guard method call
        if _is_guard_call(stmt):
            guard_stmts.append(stmt)
        else:
            body_stmts.append(stmt)
    
    return guard_stmts, body_stmts


def _is_guard_call(stmt: ast.stmt) -> bool:
    """Check if a statement is a guard method call.
    
    Detects patterns like:
        guard.always()
        guard.equals(a, b)
    
    Args:
        stmt: AST statement
        
    Returns:
        True if it's a guard call
    """
    if isinstance(stmt, ast.Expr):
        value = stmt.value
        if isinstance(value, ast.Call):
            func = value.func
            if isinstance(func, ast.Attribute):
                # Check if object name is "guard"
                if isinstance(func.value, ast.Name) and func.value.id == "guard":
                    return True
    return False


def rule(module_builder: Any, name: str | None = None):
    """Decorator for creating a rule with auto-inferred name.
    
    The rule name is inferred from the function name unless explicitly provided.
    The function receives `guard` and `body` parameters for clean syntax.
    
    Example:
        @jit.rule(m)  # Name inferred as "increment"
        def increment(guard, body):
            guard.always()
            count.next = count.read + 1
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Optional explicit rule name (inferred from function if not given)
        
    Returns:
        Decorator function
    """
    def decorator(func: F) -> F:
        # Infer name from function if not provided
        rule_name = name or _get_function_name(func)
        
        @functools.wraps(func)
        def wrapper():
            # Extract and analyze function body
            try:
                body_stmts = _extract_function_body(func)
                guard_ast, body_ast = _analyze_body(body_stmts)
            except RuntimeError:
                # Fallback: execute function directly
                with module_builder.rule(rule_name) as rule:
                    with rule.guard() as g:
                        with BuilderContext(g) as guard_proxy:
                            with rule.body() as b:
                                with BuilderContext(b) as body_proxy:
                                    func(guard_proxy, body_proxy)
                return
            
            # Create the rule
            with module_builder.rule(rule_name) as rule:
                # Execute guard statements
                with rule.guard() as g:
                    with BuilderContext(g) as guard_proxy:
                        # Create guard context with 'guard' variable
                        guard_ctx = {"guard": guard_proxy}
                        for stmt in guard_ast:
                            # Compile and execute guard statement
                            try:
                                code = compile(ast.Module(body=[stmt], type_ignores=[]), "<rule>", "exec")
                                exec(code, guard_ctx)
                            except Exception as e:
                                raise RuntimeError(f"Error in guard: {e}")
                
                # Execute body statements
                with rule.body() as b:
                    with BuilderContext(b) as body_proxy:
                        # Create body context
                        body_ctx = {"body": body_proxy}
                        for stmt in body_ast:
                            try:
                                code = compile(ast.Module(body=[stmt], type_ignores=[]), "<rule>", "exec")
                                exec(code, body_ctx)
                            except Exception as e:
                                raise RuntimeError(f"Error in body: {e}")
        
        # Execute immediately when used as decorator
        wrapper()
        
        return func  # Return original function
    
    return decorator


def method(module_builder: Any, name: str | None = None, args: list | None = None, returns: list | None = None):
    """Decorator for creating an action method.
    
    Example:
        @jit.method(m)  # Name inferred from function
        def write(guard, body, data):
            guard.always()
            reg.write(data)
    
    Args:
        module_builder: PyCMT2 ModuleBuilder
        name: Optional explicit method name
        args: List of (name, type) tuples
        returns: List of return types
        
    Returns:
        Decorator function
    """
    args = args or []
    returns = returns or []
    
    def decorator(func: F) -> F:
        method_name = name or _get_function_name(func)
        
        @functools.wraps(func)
        def wrapper():
            with module_builder.method(method_name, args=args, returns=returns) as method:
                with method.guard() as g:
                    with BuilderContext(g) as guard_proxy:
                        with method.body() as b:
                            with BuilderContext(b) as body_proxy:
                                # Build argument values
                                arg_values = []
                                for arg_name, _ in args:
                                    arg_values.append(b.arg(arg_name))
                                func(guard_proxy, body_proxy, *arg_values)
        
        wrapper()
        return func
    
    return decorator


def value(module_builder: Any, name: str | None = None, returns: list | None = None):
    """Decorator for creating a value method.
    
    Example:
        @jit.value(m, returns=[UInt(32)])
        def read(guard, body):
            guard.always()
            body.returns(reg.read)
    
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
        
        @functools.wraps(func)
        def wrapper():
            with module_builder.value(value_name, returns=returns) as value:
                with value.guard() as g:
                    with BuilderContext(g) as guard_proxy:
                        with value.body() as b:
                            with BuilderContext(b) as body_proxy:
                                func(guard_proxy, body_proxy)
        
        wrapper()
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
