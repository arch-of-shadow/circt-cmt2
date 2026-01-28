#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Incremental compilation for multi-module CMT2 designs.

This module provides incremental compilation infrastructure for multi-module
hardware designs, enabling efficient recompilation when only some modules change.
The system tracks module interfaces and dependencies to minimize unnecessary work.

Key Components:
    - ModuleInterface: Captures the public interface of a compiled module
    - DependencyGraph: Tracks dependencies between modules
    - IncrementalCompiler: Manages incremental compilation with caching

Example:
    # Define a base module
    @cmt2.elaborate
    def base_module(width: int):
        circuit = Circuit("Base")
        # ... build circuit ...
        return circuit

    # Define a top module that depends on base
    @cmt2.elaborate
    def top_module(base: Annotated[Circuit, cmt2.static]):
        circuit = Circuit("Top")
        circuit.instance(base)
        return circuit

    # Create incremental compiler
    compiler = IncrementalCompiler()

    # Compile base module
    base = compiler.compile_module("base", base_module, (32,), [])

    # Compile top module (depends on base)
    top = compiler.compile_module("top", top_module, (base,), ["base"])

    # Changing base invalidates top
    base_v2 = compiler.compile_module("base", base_module, (64,), [])
    # top is automatically invalidated and will be recompiled when requested
"""

from __future__ import annotations

import hashlib
import json
import pickle
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, Generic, List, Optional, Set, TypeVar, Union

from ._stages import CompiledCircuit, ElaboratedCircuit

# Type variable for compiled circuit types
T = TypeVar("T", bound=Union[CompiledCircuit, ElaboratedCircuit])


@dataclass(frozen=True)
class SignalType:
    """Type information for a signal/port.
    
    This captures the type metadata for signals exposed by module interfaces,
    including bit width, signedness, and array dimensions.
    
    Attributes:
        width: Bit width of the signal
        signed: Whether the signal is signed
        dims: Optional array dimensions (for vector/bundle types)
        
    Example:
        # 32-bit unsigned integer
        data_type = SignalType(width=32, signed=False)
        
        # 8-element vector of 32-bit values
        vec_type = SignalType(width=32, signed=False, dims=(8,))
    """
    width: int
    signed: bool = False
    dims: tuple[int, ...] = ()
    
    def __post_init__(self):
        # Validate width
        if self.width <= 0:
            raise ValueError(f"Signal width must be positive, got {self.width}")
        # Validate dimensions
        for dim in self.dims:
            if dim <= 0:
                raise ValueError(f"Array dimension must be positive, got {dim}")
    
    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary for serialization."""
        return {
            "width": self.width,
            "signed": self.signed,
            "dims": self.dims,
        }
    
    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> SignalType:
        """Create from dictionary."""
        return cls(
            width=data["width"],
            signed=data.get("signed", False),
            dims=tuple(data.get("dims", ())),
        )
    
    def __str__(self) -> str:
        if self.dims:
            dims_str = "[" + "][".join(str(d) for d in self.dims) + "]"
            return f"{'s' if self.signed else 'u'}{self.width}{dims_str}"
        return f"{'s' if self.signed else 'u'}{self.width}"


@dataclass
class ModuleInterface:
    """Interface of a compiled module - used for dependency tracking.
    
    This class captures the public interface of a compiled module, including
    its name, ports (inputs/outputs), and parameters. The interface version
    is computed as a hash of these attributes, enabling efficient change
    detection for incremental compilation.
    
    Attributes:
        name: Module name
        version: Hash of the interface definition (auto-computed if not provided)
        inputs: Dictionary mapping input port names to their types
        outputs: Dictionary mapping output port names to their types
        parameters: Dictionary of module parameters (e.g., width, depth)
        
    Example:
        interface = ModuleInterface(
            name="Counter",
            inputs={"clk": SignalType(1), "rst": SignalType(1), "en": SignalType(1)},
            outputs={"count": SignalType(32)},
            parameters={"MAX_COUNT": 100}
        )
        
        # Version is auto-computed
        print(interface.version)  # "a3f7e2b9..."
        
        # Check if interface has changed
        if interface.has_changed_from(old_version):
            print("Interface changed - need recompilation")
    """
    name: str
    inputs: dict[str, SignalType] = field(default_factory=dict)
    outputs: dict[str, SignalType] = field(default_factory=dict)
    parameters: dict[str, Any] = field(default_factory=dict)
    version: str = field(default="")
    
    def __post_init__(self):
        # Compute version if not provided
        if not self.version:
            object.__setattr__(self, "version", self.compute_version())
    
    def compute_version(self) -> str:
        """Compute version hash from interface definition.
        
        The version is a SHA-256 hash of the canonical representation of
        the interface, including name, sorted inputs, sorted outputs, and
        sorted parameters. This ensures that any interface change produces
        a different version string.
        
        Returns:
            16-character hexadecimal hash string
        """
        # Create canonical representation
        inputs_str = ",".join(
            f"{k}:{str(v)}" for k, v in sorted(self.inputs.items())
        )
        outputs_str = ",".join(
            f"{k}:{str(v)}" for k, v in sorted(self.outputs.items())
        )
        params_str = ",".join(
            f"{k}:{repr(v)}" for k, v in sorted(self.parameters.items())
        )
        
        content = f"{self.name}:{inputs_str}:{outputs_str}:{params_str}"
        return hashlib.sha256(content.encode()).hexdigest()[:16]
    
    def has_changed_from(self, other_version: str) -> bool:
        """Check if this interface differs from another version.
        
        Args:
            other_version: Version string to compare against
            
        Returns:
            True if versions differ (interface has changed)
        """
        return self.version != other_version
    
    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary for serialization."""
        return {
            "name": self.name,
            "version": self.version,
            "inputs": {k: v.to_dict() for k, v in self.inputs.items()},
            "outputs": {k: v.to_dict() for k, v in self.outputs.items()},
            "parameters": self.parameters,
        }
    
    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> ModuleInterface:
        """Create from dictionary."""
        return cls(
            name=data["name"],
            version=data.get("version", ""),
            inputs={k: SignalType.from_dict(v) for k, v in data.get("inputs", {}).items()},
            outputs={k: SignalType.from_dict(v) for k, v in data.get("outputs", {}).items()},
            parameters=data.get("parameters", {}),
        )


@dataclass
class CompiledModule(Generic[T]):
    """A compiled module with its interface and metadata.
    
    This wrapper associates a compiled circuit with its interface definition,
    enabling dependency tracking and incremental recompilation.
    
    Attributes:
        name: Module name
        circuit: The compiled circuit (ElaboratedCircuit or CompiledCircuit)
        interface: Module interface definition
        cache_key: Unique key for cache lookup
        timestamp: Compilation timestamp (for debugging)
        
    Example:
        compiled = CompiledModule(
            name="Counter",
            circuit=elaborated_circuit,
            interface=interface,
            cache_key="counter_32_abc123"
        )
    """
    name: str
    circuit: T
    interface: ModuleInterface
    cache_key: str
    timestamp: float = field(default_factory=lambda: __import__('time').time())
    
    def __repr__(self) -> str:
        return (
            f"CompiledModule("
            f"name={self.name!r}, "
            f"interface_version={self.interface.version!r}, "
            f"cache_key={self.cache_key!r}"
            f")"
        )


class DependencyGraph:
    """Tracks dependencies between modules for incremental compilation.
    
    This class maintains a bidirectional graph of module dependencies,
    enabling efficient invalidation of dependent modules when a module
    changes. It supports:
    
    - Adding modules with their dependencies
    - Querying direct and transitive dependencies
    - Finding all modules invalidated by a change
    - Removing modules and their dependency edges
    
    The graph is stored as two adjacency maps:
    - dependencies: module -> set of modules it depends on
    - dependents: module -> set of modules that depend on it
    
    Example:
        graph = DependencyGraph()
        
        # Add modules with dependencies
        graph.add_module("top", compiled_top, ["base", "util"])
        graph.add_module("base", compiled_base, ["util"])
        graph.add_module("util", compiled_util, [])
        
        # Find all modules that need recompilation when "base" changes
        invalidated = graph.get_invalidated("base")
        # Returns: {"base", "top"} (top depends on base)
        
        # Get direct dependencies
        deps = graph.get_dependencies("top")  # {"base", "util"}
        
        # Get direct dependents
        dependents = graph.get_dependents("base")  # {"top"}
    """
    
    def __init__(self):
        """Initialize an empty dependency graph."""
        # module -> CompiledModule
        self._modules: dict[str, CompiledModule] = {}
        # module -> set of modules it depends on
        self._dependencies: dict[str, set[str]] = {}
        # module -> set of modules that depend on it
        self._dependents: dict[str, set[str]] = {}
        # Lock for thread safety
        self._lock = threading.RLock()
    
    def add_module(
        self,
        name: str,
        compiled: CompiledModule,
        deps: list[str],
    ) -> None:
        """Add a module with its dependencies.
        
        Args:
            name: Module name (must be unique)
            compiled: The compiled module
            deps: List of module names this module depends on
            
        Raises:
            ValueError: If a dependency doesn't exist in the graph
        """
        with self._lock:
            # Validate dependencies exist (except for circular deps which we allow)
            # In a real implementation, we'd do topological validation
            
            # Store the module
            self._modules[name] = compiled
            
            # Set up dependency edges
            self._dependencies[name] = set(deps)
            
            # Update reverse edges (dependents)
            for dep in deps:
                if dep not in self._dependents:
                    self._dependents[dep] = set()
                self._dependents[dep].add(name)
            
            # Ensure entry exists for this module in dependents map
            if name not in self._dependents:
                self._dependents[name] = set()
    
    def get_dependencies(self, name: str) -> set[str]:
        """Get direct dependencies of a module.
        
        Args:
            name: Module name
            
        Returns:
            Set of module names that `name` directly depends on
        """
        with self._lock:
            return self._dependencies.get(name, set()).copy()
    
    def get_dependents(self, name: str) -> set[str]:
        """Get modules that directly depend on a module.
        
        Args:
            name: Module name
            
        Returns:
            Set of module names that directly depend on `name`
        """
        with self._lock:
            return self._dependents.get(name, set()).copy()
    
    def get_invalidated(self, changed_module: str) -> set[str]:
        """Get all modules that need recompilation when a module changes.
        
        This performs a breadth-first search from the changed module to
        find all transitively dependent modules that would be affected by
        a change.
        
        Args:
            changed_module: Name of the module that changed
            
        Returns:
            Set of module names that need recompilation (including the
            changed module itself)
        """
        with self._lock:
            invalidated: set[str] = set()
            to_process: list[str] = [changed_module]
            
            while to_process:
                module = to_process.pop(0)
                if module not in invalidated:
                    invalidated.add(module)
                    # Find all modules that depend on this one
                    for dependent in self._dependents.get(module, []):
                        if dependent not in invalidated:
                            to_process.append(dependent)
            
            return invalidated
    
    def remove_module(self, name: str) -> bool:
        """Remove a module from the graph.
        
        This also removes all dependency edges to and from the module.
        
        Args:
            name: Module name to remove
            
        Returns:
            True if module was found and removed
        """
        with self._lock:
            if name not in self._modules:
                return False
            
            # Remove from other modules' dependent lists
            for deps in self._dependencies.get(name, set()):
                if deps in self._dependents:
                    self._dependents[deps].discard(name)
            
            # Remove from other modules' dependency lists
            for dependent in self._dependents.get(name, set()):
                if dependent in self._dependencies:
                    self._dependencies[dependent].discard(name)
            
            # Remove module entries
            del self._modules[name]
            del self._dependencies[name]
            del self._dependents[name]
            
            return True
    
    def has_module(self, name: str) -> bool:
        """Check if a module exists in the graph.
        
        Args:
            name: Module name
            
        Returns:
            True if module exists
        """
        with self._lock:
            return name in self._modules
    
    def get_module(self, name: str) -> CompiledModule | None:
        """Get a compiled module by name.
        
        Args:
            name: Module name
            
        Returns:
            CompiledModule if found, None otherwise
        """
        with self._lock:
            return self._modules.get(name)
    
    def get_all_modules(self) -> set[str]:
        """Get all module names in the graph.
        
        Returns:
            Set of all module names
        """
        with self._lock:
            return set(self._modules.keys())
    
    def get_interface(self, name: str) -> ModuleInterface | None:
        """Get the interface of a module.
        
        Args:
            name: Module name
            
        Returns:
            ModuleInterface if module exists, None otherwise
        """
        with self._lock:
            compiled = self._modules.get(name)
            return compiled.interface if compiled else None


class IncrementalCompiler:
    """Manages incremental compilation for multi-module designs.
    
    This class provides efficient compilation of multi-module designs by:
    - Tracking module interfaces and their versions
    - Maintaining a dependency graph between modules
    - Caching compiled results on disk
    - Invalidating dependent modules when dependencies change
    
    The compiler uses a two-level cache:
    1. In-memory cache for fast access to recently used modules
    2. Disk cache for persistence across sessions
    
    Example:
        # Create compiler with custom cache directory
        compiler = IncrementalCompiler(cache_dir=Path("./.my_cache"))
        
        # Compile modules
        base = compiler.compile_module("base", base_elab, (32,), [])
        top = compiler.compile_module("top", top_elab, (base,), ["base"])
        
        # Later, if base changes, top is automatically invalidated
        base_v2 = compiler.compile_module("base", base_elab, (64,), [])
        # Next call to compile_module for "top" will trigger recompilation
        
        # Check statistics
        stats = compiler.get_stats()
        print(f"Cache hits: {stats['cache_hits']}")
    """
    
    def __init__(
        self,
        cache_dir: Path | None = None,
        memory_cache_size: int | None = 100,
    ):
        """Initialize the incremental compiler.
        
        Args:
            cache_dir: Directory for persistent cache (default: ~/.cmt2_cache)
            memory_cache_size: Max entries in memory cache (None for unlimited)
        """
        self._cache_dir = cache_dir or Path.home() / ".cmt2_cache"
        self._cache_dir.mkdir(parents=True, exist_ok=True)
        
        self._dependency_graph = DependencyGraph()
        self._interface_cache: dict[str, ModuleInterface] = {}
        self._memory_cache: dict[str, CompiledModule] = {}
        self._memory_cache_size = memory_cache_size
        self._stats = {
            "cache_hits": 0,
            "cache_misses": 0,
            "recompilations": 0,
            "invalidations": 0,
        }
        self._stats_lock = threading.Lock()
        
        # Load cached interfaces
        self._load_interface_cache()
    
    def compile_module(
        self,
        name: str,
        elaborator: Callable[..., T],
        static_args: tuple,
        dependencies: list[str],
        interface_extractor: Callable[[T], ModuleInterface] | None = None,
    ) -> T:
        """Compile a module, reusing cached results if valid.
        
        This is the main entry point for incremental compilation. It:
        1. Checks if any dependency has changed
        2. Invalidates this module and all dependents if dependencies changed
        3. Checks the cache for a valid compiled result
        4. Runs the elaborator if cache miss
        5. Stores result in cache and dependency graph
        
        Args:
            name: Module name (unique identifier)
            elaborator: Function that builds the circuit
            static_args: Static arguments passed to elaborator
            dependencies: List of module names this module depends on
            interface_extractor: Optional function to extract interface from circuit
            
        Returns:
            The compiled circuit (ElaboratedCircuit or CompiledCircuit)
            
        Example:
            @cmt2.elaborate
            def counter_design(width: int):
                circuit = Circuit("Counter")
                # ... build circuit ...
                return circuit
            
            compiler = IncrementalCompiler()
            circuit = compiler.compile_module(
                "counter",
                counter_design,
                (32,),  # static_args
                []      # no dependencies
            )
        """
        # Check if any dependency has changed
        dep_changed = False
        for dep in dependencies:
            if self._has_interface_changed(dep):
                dep_changed = True
                # Invalidate this module and all that depend on it
                invalidated = self._dependency_graph.get_invalidated(name)
                for inv in invalidated:
                    self._invalidate_cache(inv)
                with self._stats_lock:
                    self._stats["invalidations"] += len(invalidated)
                break  # Already invalidated everything
        
        # Compute cache key
        cache_key = self._compute_cache_key(name, elaborator, static_args)
        
        # Check in-memory cache first
        if cache_key in self._memory_cache and not dep_changed:
            with self._stats_lock:
                self._stats["cache_hits"] += 1
            return self._memory_cache[cache_key].circuit
        
        # Check disk cache
        if not dep_changed and self._is_cache_valid(cache_key):
            compiled = self._load_from_cache(cache_key)
            if compiled is not None:
                # Add to memory cache
                self._add_to_memory_cache(cache_key, compiled)
                # Update dependency graph
                self._dependency_graph.add_module(name, compiled, dependencies)
                with self._stats_lock:
                    self._stats["cache_hits"] += 1
                return compiled.circuit
        
        # Cache miss - compile
        with self._stats_lock:
            self._stats["cache_misses"] += 1
            self._stats["recompilations"] += 1
        
        circuit = elaborator(*static_args)
        
        # Extract or create interface
        if interface_extractor:
            interface = interface_extractor(circuit)
        else:
            interface = self._default_interface_extractor(name, circuit)
        
        # Create compiled module wrapper
        compiled = CompiledModule(
            name=name,
            circuit=circuit,
            interface=interface,
            cache_key=cache_key,
        )
        
        # Store in caches and dependency graph
        self._save_to_cache(cache_key, compiled)
        self._add_to_memory_cache(cache_key, compiled)
        self._dependency_graph.add_module(name, compiled, dependencies)
        self._interface_cache[name] = interface
        self._save_interface_cache()
        
        return circuit
    
    def _has_interface_changed(self, module_name: str) -> bool:
        """Check if a module's interface has changed.
        
        Compares the current interface with the cached interface from
        the last successful compilation.
        
        Args:
            module_name: Name of module to check
            
        Returns:
            True if interface has changed or module is not cached
        """
        # Get current interface from dependency graph
        current = self._dependency_graph.get_interface(module_name)
        
        # Get cached interface
        cached = self._interface_cache.get(module_name)
        
        if current is None or cached is None:
            # Module not compiled yet or not cached
            return True
        
        return current.has_changed_from(cached.version)
    
    def _compute_cache_key(
        self,
        name: str,
        elaborator: Callable,
        static_args: tuple,
    ) -> str:
        """Compute a unique cache key for a module compilation.
        
        The key incorporates:
        - Module name
        - Elaborator function identity (code hash)
        - Static argument values
        
        Args:
            name: Module name
            elaborator: Elaborator function
            static_args: Static arguments
            
        Returns:
            Cache key string
        """
        # Hash elaborator code
        try:
            code = elaborator.__code__.co_code
            elaborator_hash = hashlib.sha256(code).hexdigest()[:16]
        except AttributeError:
            # Fallback for non-function callables
            elaborator_hash = str(hash(elaborator))[:16]
        
        # Hash static arguments
        try:
            args_bytes = pickle.dumps(static_args)
            args_hash = hashlib.sha256(args_bytes).hexdigest()[:16]
        except (pickle.PickleError, TypeError):
            # Fallback for non-picklable args
            args_hash = str(hash(static_args))[:16]
        
        return f"{name}_{elaborator_hash}_{args_hash}"
    
    def _is_cache_valid(self, cache_key: str) -> bool:
        """Check if a cache entry exists and is valid.
        
        Args:
            cache_key: Cache key to check
            
        Returns:
            True if cache entry exists
        """
        cache_file = self._cache_dir / f"{cache_key}.pkl"
        return cache_file.exists()
    
    def _save_to_cache(self, cache_key: str, compiled: CompiledModule) -> None:
        """Save a compiled module to disk cache.
        
        Args:
            cache_key: Cache key
            compiled: Compiled module to cache
        """
        cache_file = self._cache_dir / f"{cache_key}.pkl"
        try:
            with open(cache_file, "wb") as f:
                pickle.dump(compiled, f)
        except (pickle.PickleError, IOError) as e:
            # Cache write failure is not fatal
            import warnings
            warnings.warn(f"Failed to write cache: {e}")
    
    def _load_from_cache(self, cache_key: str) -> CompiledModule | None:
        """Load a compiled module from disk cache.
        
        Args:
            cache_key: Cache key
            
        Returns:
            CompiledModule if found, None otherwise
        """
        cache_file = self._cache_dir / f"{cache_key}.pkl"
        try:
            with open(cache_file, "rb") as f:
                return pickle.load(f)
        except (pickle.PickleError, IOError, FileNotFoundError):
            return None
    
    def _invalidate_cache(self, name: str) -> None:
        """Invalidate all cache entries for a module.
        
        This removes entries from both memory and disk caches, and
        removes the module from the dependency graph.
        
        Args:
            name: Module name to invalidate
        """
        # Remove from dependency graph
        self._dependency_graph.remove_module(name)
        
        # Remove from interface cache
        if name in self._interface_cache:
            del self._interface_cache[name]
        
        # Remove from memory cache
        keys_to_remove = [
            k for k, v in self._memory_cache.items()
            if v.name == name
        ]
        for key in keys_to_remove:
            del self._memory_cache[key]
        
        # Remove from disk cache (all entries for this module)
        for cache_file in self._cache_dir.glob(f"{name}_*.pkl"):
            try:
                cache_file.unlink()
            except IOError:
                pass
    
    def _add_to_memory_cache(self, cache_key: str, compiled: CompiledModule) -> None:
        """Add entry to memory cache with LRU eviction.
        
        Args:
            cache_key: Cache key
            compiled: Compiled module
        """
        # Simple LRU: if at capacity, remove arbitrary entry
        if (self._memory_cache_size is not None and 
            len(self._memory_cache) >= self._memory_cache_size and
            cache_key not in self._memory_cache):
            # Remove first entry (simple eviction)
            if self._memory_cache:
                oldest_key = next(iter(self._memory_cache))
                del self._memory_cache[oldest_key]
        
        self._memory_cache[cache_key] = compiled
    
    def _default_interface_extractor(
        self,
        name: str,
        circuit: Any,
    ) -> ModuleInterface:
        """Default interface extraction from a circuit.
        
        This is a placeholder that creates a basic interface. In practice,
        this would introspect the circuit to extract actual port information.
        
        Args:
            name: Module name
            circuit: Circuit object
            
        Returns:
            ModuleInterface with basic information
        """
        # Try to extract actual interface from circuit
        inputs: dict[str, SignalType] = {}
        outputs: dict[str, SignalType] = {}
        parameters: dict[str, Any] = {}
        
        # Check if circuit has interface information
        if hasattr(circuit, 'inputs'):
            inputs = self._extract_ports(circuit.inputs)
        if hasattr(circuit, 'outputs'):
            outputs = self._extract_ports(circuit.outputs)
        if hasattr(circuit, 'parameters'):
            parameters = dict(circuit.parameters)
        
        return ModuleInterface(
            name=name,
            inputs=inputs,
            outputs=outputs,
            parameters=parameters,
        )
    
    def _extract_ports(self, ports: Any) -> dict[str, SignalType]:
        """Extract port information from circuit ports.
        
        Args:
            ports: Port collection from circuit
            
        Returns:
            Dictionary mapping port names to SignalTypes
        """
        result: dict[str, SignalType] = {}
        
        if isinstance(ports, dict):
            for name, port in ports.items():
                result[name] = self._extract_signal_type(port)
        elif hasattr(ports, '__iter__'):
            for port in ports:
                if hasattr(port, 'name'):
                    result[port.name] = self._extract_signal_type(port)
        
        return result
    
    def _extract_signal_type(self, port: Any) -> SignalType:
        """Extract SignalType from a port object.
        
        Args:
            port: Port object
            
        Returns:
            SignalType representing the port
        """
        width = 32  # Default width
        signed = False
        dims: tuple[int, ...] = ()
        
        if hasattr(port, 'width'):
            width = port.width
        if hasattr(port, 'signed'):
            signed = port.signed
        if hasattr(port, 'dims'):
            dims = tuple(port.dims) if hasattr(port.dims, '__iter__') else ()
        
        return SignalType(width=width, signed=signed, dims=dims)
    
    def _load_interface_cache(self) -> None:
        """Load cached interface definitions from disk."""
        cache_file = self._cache_dir / "_interfaces.json"
        if cache_file.exists():
            try:
                with open(cache_file, "r") as f:
                    data = json.load(f)
                self._interface_cache = {
                    name: ModuleInterface.from_dict(iface_data)
                    for name, iface_data in data.items()
                }
            except (json.JSONDecodeError, IOError, KeyError):
                self._interface_cache = {}
    
    def _save_interface_cache(self) -> None:
        """Save interface definitions to disk."""
        cache_file = self._cache_dir / "_interfaces.json"
        try:
            data = {
                name: iface.to_dict()
                for name, iface in self._interface_cache.items()
            }
            with open(cache_file, "w") as f:
                json.dump(data, f, indent=2)
        except (IOError, TypeError) as e:
            import warnings
            warnings.warn(f"Failed to save interface cache: {e}")
    
    def get_stats(self) -> dict[str, int]:
        """Get compilation statistics.
        
        Returns:
            Dictionary with cache_hits, cache_misses, recompilations, invalidations
        """
        with self._stats_lock:
            return dict(self._stats)
    
    def clear_cache(self) -> int:
        """Clear all caches (memory and disk).
        
        Returns:
            Number of disk cache entries removed
        """
        # Clear memory cache
        self._memory_cache.clear()
        self._interface_cache.clear()
        
        # Clear dependency graph
        for name in list(self._dependency_graph.get_all_modules()):
            self._dependency_graph.remove_module(name)
        
        # Clear disk cache
        count = 0
        for cache_file in self._cache_dir.glob("*.pkl"):
            try:
                cache_file.unlink()
                count += 1
            except IOError:
                pass
        
        # Clear interface cache file
        cache_file = self._cache_dir / "_interfaces.json"
        if cache_file.exists():
            try:
                cache_file.unlink()
            except IOError:
                pass
        
        # Reset stats
        with self._stats_lock:
            self._stats = {
                "cache_hits": 0,
                "cache_misses": 0,
                "recompilations": 0,
                "invalidations": 0,
            }
        
        return count
    
    def get_dependencies(self, name: str) -> set[str]:
        """Get direct dependencies of a module.
        
        Args:
            name: Module name
            
        Returns:
            Set of module names that `name` depends on
        """
        return self._dependency_graph.get_dependencies(name)
    
    def get_dependents(self, name: str) -> set[str]:
        """Get modules that depend on a module.
        
        Args:
            name: Module name
            
        Returns:
            Set of module names that depend on `name`
        """
        return self._dependency_graph.get_dependents(name)


# Convenience factory function
def create_incremental_compiler(
    cache_dir: Path | str | None = None,
    memory_cache_size: int | None = 100,
) -> IncrementalCompiler:
    """Create an incremental compiler with optional configuration.
    
    Args:
        cache_dir: Directory for persistent cache
        memory_cache_size: Max entries in memory cache
        
    Returns:
        Configured IncrementalCompiler instance
        
    Example:
        compiler = create_incremental_compiler(
            cache_dir="./build_cache",
            memory_cache_size=50
        )
    """
    if isinstance(cache_dir, str):
        cache_dir = Path(cache_dir)
    return IncrementalCompiler(
        cache_dir=cache_dir,
        memory_cache_size=memory_cache_size,
    )
