#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT decorator implementation for CMT2.

This module provides decorators for CMT2 hardware design elaboration and simulation:
- @elaborate: Staged compilation that returns a Circuit object
- @simulate: Immediate compilation and execution that returns simulation results

Example:
    import cmt2

    @cmt2.elaborate
    def my_design(width: int):
        circuit = Circuit("Test")
        # ... build circuit ...
        return circuit

    @cmt2.simulate
    def my_sim(width: int):
        circuit = Circuit("Test")
        # ... build and simulate ...
        return circuit.run()
"""

from __future__ import annotations

import functools
import inspect
from typing import Any, Callable, TypeVar, overload

# Import static argument handling
from ._static_args import (
    StaticArgSpec,
    compute_cache_key,
    partition_args,
    parse_static_arg_specs,
    validate_static_args,
)
from ._cache import JITCache, get_global_cache

# Type variable for the decorated function
F = TypeVar("F", bound=Callable[..., Any])

# Forward reference for Circuit type
CircuitType = Any


class ElaboratedFunction:
    """Wrapper for functions decorated with @elaborate.
    
    This class captures the elaboration function and provides methods
    for staged compilation. The circuit is built lazily on first call
    or can be explicitly triggered.
    
    Attributes:
        _func: The original elaboration function
        _static_spec: Specification of static arguments
        _signature: Function signature for argument binding
        _cache: Cache for compiled circuits
        _cached_key: Key of currently cached circuit
    """
    
    def __init__(
        self,
        func: Callable[..., CircuitType],
        static_argnums: tuple[int, ...] | None = None,
        static_argnames: tuple[str, ...] | None = None,
        cache: JITCache[CircuitType] | None = None,
    ):
        """Initialize the elaborated function wrapper.
        
        Args:
            func: The elaboration function that returns a Circuit
            static_argnums: Indices of arguments to treat as static (compile-time)
            static_argnames: Names of arguments to treat as static (compile-time)
            cache: Cache to use (defaults to global cache)
        """
        functools.update_wrapper(self, func)
        self._func = func
        self._signature = inspect.signature(func)
        self._cache = cache if cache is not None else get_global_cache()
        
        # Parse static argument specifications
        self._static_spec = parse_static_arg_specs(
            self._signature,
            static_argnums=static_argnums,
            static_argnames=static_argnames,
        )
        
        self._cached_key: str | None = None
    
    def __call__(self, *args: Any, **kwargs: Any) -> CircuitType:
        """Execute the elaboration and return the Circuit.
        
        This performs staged compilation:
        1. Separates static arguments (compile-time) from dynamic arguments (runtime)
        2. Checks cache for existing circuit with same static args
        3. Builds the circuit if not cached
        4. Returns the Circuit object for further use
        
        Args:
            *args: Positional arguments to the elaboration function
            **kwargs: Keyword arguments to the elaboration function
            
        Returns:
            The Circuit object built by the elaboration function
            
        Raises:
            TypeError: If static arguments are not hashable
        """
        # Partition arguments into static and dynamic
        static_args, dynamic_args = partition_args(
            args, kwargs, self._static_spec, self._signature
        )
        
        # Validate static arguments are hashable
        validate_static_args(static_args)
        
        # Compute cache key
        cache_key = compute_cache_key(self._func, static_args)
        
        # Check cache
        cache_entry = self._cache.get(cache_key)
        if cache_entry is not None:
            self._cached_key = cache_key
            return cache_entry.value
        
        # Build the circuit - pass all arguments (static values available at compile time)
        bound = self._signature.bind(*args, **kwargs)
        bound.apply_defaults()
        circuit = self._func(*bound.args, **bound.kwargs)
        
        # Cache the result
        self._cache.put(cache_key, circuit, static_args)
        self._cached_key = cache_key
        
        return circuit
    
    def lower(self, *args: Any, **kwargs: Any) -> str:
        """Lower the circuit to MLIR/FIRRTL.
        
        This is a convenience method that elaborates the circuit
        and returns its MLIR representation.
        
        Args:
            *args: Positional arguments to the elaboration function
            **kwargs: Keyword arguments to the elaboration function
            
        Returns:
            The MLIR representation of the circuit
        """
        circuit = self(*args, **kwargs)
        return circuit.emit_mlir()
    
    def compile(self, *args: Any, **kwargs: Any) -> str:
        """Compile the circuit to Verilog.
        
        This is a convenience method that elaborates the circuit
        and returns its Verilog representation.
        
        Args:
            *args: Positional arguments to the elaboration function
            **kwargs: Keyword arguments to the elaboration function
            
        Returns:
            The Verilog representation of the circuit
        """
        circuit = self(*args, **kwargs)
        return circuit.emit_verilog()
    
    @property
    def cache_key(self) -> str | None:
        """Get the cache key of the most recently compiled circuit."""
        return self._cached_key


class SimulatedFunction:
    """Wrapper for functions decorated with @simulate.
    
    This class captures the simulation function and executes it
    immediately, returning the simulation results. Unlike @elaborate,
    this does not cache the circuit - it runs the full simulation
    pipeline each time.
    
    Attributes:
        _func: The original simulation function
        _static_spec: Specification of static arguments
        _signature: Function signature for argument binding
    """
    
    def __init__(
        self,
        func: Callable[..., Any],
        static_argnums: tuple[int, ...] | None = None,
        static_argnames: tuple[str, ...] | None = None,
    ):
        """Initialize the simulated function wrapper.
        
        Args:
            func: The simulation function that runs the circuit
            static_argnums: Indices of arguments to treat as static (compile-time)
            static_argnames: Names of arguments to treat as static (compile-time)
        """
        functools.update_wrapper(self, func)
        self._func = func
        self._signature = inspect.signature(func)
        
        # Parse static argument specifications
        self._static_spec = parse_static_arg_specs(
            self._signature,
            static_argnums=static_argnums,
            static_argnames=static_argnames,
        )
    
    def __call__(self, *args: Any, **kwargs: Any) -> Any:
        """Execute the simulation and return results.
        
        This performs immediate execution:
        1. Separates static arguments (compile-time) from dynamic arguments (runtime)
        2. Validates static arguments
        3. Builds and runs the circuit
        4. Returns the simulation results directly
        
        Args:
            *args: Positional arguments to the simulation function
            **kwargs: Keyword arguments to the simulation function
            
        Returns:
            The simulation results (type depends on the simulation function)
            
        Raises:
            TypeError: If static arguments are not hashable
        """
        # Partition arguments into static and dynamic
        static_args, dynamic_args = partition_args(
            args, kwargs, self._static_spec, self._signature
        )
        
        # Validate static arguments are hashable
        validate_static_args(static_args)
        
        # Run the simulation - pass all arguments
        bound = self._signature.bind(*args, **kwargs)
        bound.apply_defaults()
        return self._func(*bound.args, **bound.kwargs)


# =============================================================================
# Public Decorator Functions
# =============================================================================

@overload
def elaborate(func: F) -> ElaboratedFunction:
    """Decorator for circuit elaboration (staged compilation).
    
    Use this decorator when you want to:
    - Build a circuit and then work with it (emit MLIR, Verilog, etc.)
    - Cache the circuit for reuse
    - Inspect the circuit before simulation
    
    The decorated function should return a Circuit object.
    
    Example:
        @cmt2.elaborate
        def counter_design(width: int):
            circuit = Circuit("Counter")
            with circuit.module("Counter") as mod:
                # ... build module ...
            return circuit
        
        # Use the elaborated circuit
        circuit = counter_design(32)
        print(circuit.emit_verilog())
        
        # Or compile directly
        verilog = counter_design.compile(32)
    
    Args:
        func: The elaboration function to decorate
        
    Returns:
        An ElaboratedFunction wrapper that returns a Circuit when called
    """
    ...


@overload
def elaborate(
    *,
    static_argnums: int | tuple[int, ...] | None = None,
    static_argnames: tuple[str, ...] | None = None,
) -> Callable[[F], ElaboratedFunction]:
    """Decorator for circuit elaboration with static arguments.
    
    Args:
        static_argnums: Argument indices to treat as static (compile-time constants).
            These arguments will trigger re-compilation when they change.
            Can be a single int or a tuple of ints.
        static_argnames: Argument names to treat as static (compile-time constants).
            These arguments will trigger re-compilation when they change.
            
    Returns:
        A decorator that wraps the function in an ElaboratedFunction
    """
    ...


def elaborate(
    func: F | None = None,
    *,
    static_argnums: int | tuple[int, ...] | None = None,
    static_argnames: tuple[str, ...] | None = None,
) -> ElaboratedFunction | Callable[[F], ElaboratedFunction]:
    """Decorator for circuit elaboration (staged compilation).
    
    This decorator enables staged compilation of hardware designs:
    - Python code runs at "elaboration time" to build the circuit structure
    - The resulting Circuit object can be inspected, transformed, or emitted
    - Arguments marked as static trigger re-elaboration when changed
    
    The key difference from @simulate is that @elaborate returns the Circuit
    object itself, not simulation results. This gives you full control over
    the compilation pipeline.
    
    Example:
        @cmt2.elaborate
        def my_design(width: int, depth: int):
            circuit = Circuit("MyDesign")
            # ... build circuit using width and depth ...
            return circuit
        
        # Elaborate with specific parameters
        circuit = my_design(32, 16)
        
        # Access circuit properties
        print(f"Circuit: {circuit.name}")
        print(circuit.emit_mlir())
        print(circuit.emit_verilog())
    
    With static arguments:
        @cmt2.elaborate(static_argnums=0)  # width is static
        def my_design(width: int, data: np.ndarray):
            circuit = Circuit("MyDesign")
            # width determines circuit structure (static)
            # data is used at runtime (dynamic)
            return circuit
    
    With Annotated types (preferred):
        from typing import Annotated
        
        @cmt2.elaborate
        def my_design(
            width: Annotated[int, cmt2.static],  # Static via annotation
            data: np.ndarray                      # Dynamic
        ):
            circuit = Circuit("MyDesign")
            return circuit
    
    Args:
        func: The elaboration function to decorate (when used without parentheses)
        static_argnums: Argument indices to treat as static (compile-time constants)
        static_argnames: Argument names to treat as static (compile-time constants)
        
    Returns:
        An ElaboratedFunction wrapper, or a decorator if used with arguments
        
    Raises:
        ValueError: If static_argnums and static_argnames conflict
    """
    # Normalize static_argnums to a tuple
    if static_argnums is None:
        normalized_static_nums: tuple[int, ...] = ()
    elif isinstance(static_argnums, int):
        normalized_static_nums = (static_argnums,)
    else:
        normalized_static_nums = static_argnums
    
    # Normalize static_argnames to a tuple
    if static_argnames is None:
        normalized_static_names: tuple[str, ...] = ()
    else:
        normalized_static_names = static_argnames
    
    def decorator(f: F) -> ElaboratedFunction:
        return ElaboratedFunction(
            f,
            static_argnums=normalized_static_nums,
            static_argnames=normalized_static_names,
        )
    
    if func is not None:
        # Used without parentheses: @elaborate
        return decorator(func)
    else:
        # Used with parentheses: @elaborate() or @elaborate(static_argnums=...)
        return decorator


@overload
def simulate(func: F) -> SimulatedFunction:
    """Decorator for circuit simulation (immediate execution).
    
    Use this decorator when you want to:
    - Run a complete simulation in one call
    - Get simulation results directly
    - Not worry about circuit caching or reuse
    
    The decorated function should run the simulation and return results.
    
    Example:
        @cmt2.simulate
        def test_counter(width: int):
            circuit = Circuit("Counter")
            # ... build circuit ...
            
            ws = SimulationWorkspace(circuit, "./sim")
            ws.generate_placeholder()
            success, output = ws.build_and_run()
            return {"success": success, "output": output}
        
        # Run simulation
        results = test_counter(32)
        assert results["success"]
    
    Args:
        func: The simulation function to decorate
        
    Returns:
        A SimulatedFunction wrapper that runs the simulation when called
    """
    ...


@overload
def simulate(
    *,
    static_argnums: int | tuple[int, ...] | None = None,
    static_argnames: tuple[str, ...] | None = None,
) -> Callable[[F], SimulatedFunction]:
    """Decorator for circuit simulation with static arguments.
    
    Args:
        static_argnums: Argument indices to treat as static (compile-time constants).
            These arguments will trigger re-compilation when they change.
        static_argnames: Argument names to treat as static (compile-time constants).
            These arguments will trigger re-compilation when they change.
            
    Returns:
        A decorator that wraps the function in a SimulatedFunction
    """
    ...


def simulate(
    func: F | None = None,
    *,
    static_argnums: int | tuple[int, ...] | None = None,
    static_argnames: tuple[str, ...] | None = None,
) -> SimulatedFunction | Callable[[F], SimulatedFunction]:
    """Decorator for circuit simulation (immediate execution).
    
    This decorator enables immediate simulation of hardware designs:
    - Python code runs to build and simulate the circuit
    - Results are returned directly to the caller
    - No circuit object is cached or returned
    - Arguments marked as static trigger re-compilation when changed
    
    The key difference from @elaborate is that @simulate runs the full
    simulation pipeline immediately and returns the results, not the Circuit.
    This is useful for testing and quick iteration.
    
    Example:
        @cmt2.simulate
        def test_design(width: int):
            circuit = Circuit("Test")
            # ... build circuit ...
            
            # Create simulation workspace
            ws = SimulationWorkspace(circuit, "./sim")
            ws.generate_placeholder()
            
            # Run and return results
            success, output = ws.build_and_run()
            return {"success": success, "output": output}
        
        # Run simulation - this compiles and executes immediately
        results = test_design(32)
        print(results["output"])
    
    With static arguments:
        @cmt2.simulate(static_argnums=(0, 1))  # width and depth are static
        def test_design(width: int, depth: int, test_vectors: list):
            circuit = Circuit("Test")
            # width and depth determine circuit structure
            # test_vectors are used at simulation runtime
            return circuit.run()
    
    With Annotated types (preferred):
        from typing import Annotated
        
        @cmt2.simulate
        def test_design(
            width: Annotated[int, cmt2.static],  # Static via annotation
            depth: Annotated[int, cmt2.static],  # Static via annotation
            test_vectors: list                   # Dynamic
        ):
            circuit = Circuit("Test")
            return circuit.run()
    
    Args:
        func: The simulation function to decorate (when used without parentheses)
        static_argnums: Argument indices to treat as static (compile-time constants)
        static_argnames: Argument names to treat as static (compile-time constants)
        
    Returns:
        A SimulatedFunction wrapper, or a decorator if used with arguments
        
    Raises:
        ValueError: If static_argnums and static_argnames conflict
    """
    # Normalize static_argnums to a tuple
    if static_argnums is None:
        normalized_static_nums: tuple[int, ...] = ()
    elif isinstance(static_argnums, int):
        normalized_static_nums = (static_argnums,)
    else:
        normalized_static_nums = static_argnums
    
    # Normalize static_argnames to a tuple
    if static_argnames is None:
        normalized_static_names: tuple[str, ...] = ()
    else:
        normalized_static_names = static_argnames
    
    def decorator(f: F) -> SimulatedFunction:
        return SimulatedFunction(
            f,
            static_argnums=normalized_static_nums,
            static_argnames=normalized_static_names,
        )
    
    if func is not None:
        # Used without parentheses: @simulate
        return decorator(func)
    else:
        # Used with parentheses: @simulate() or @simulate(static_argnums=...)
        return decorator
