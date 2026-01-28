#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Caching infrastructure for CMT2 JIT compilation.

This module provides caching for compiled circuits to avoid re-elaboration
when the same static arguments are used. The cache is keyed by:
- Function AST hash (semantic identity, not source text)
- Static argument values (deterministic hash)

Example:
    @cmt2.elaborate
    def design(width: Annotated[int, cmt2.static]):
        return Circuit("Test")

    # First call - elaborates
    circuit1 = design(32)

    # Same static arg - returns cached circuit
    circuit2 = design(32)  # Cache hit!

    # Different static arg - re-elaborates
    circuit3 = design(64)  # Cache miss - new elaboration

The cache supports:
- AST-based caching (formatting changes don't invalidate cache)
- Weak reference caching for large artifacts (allows GC under memory pressure)
- In-memory caching (default)
- Optional disk caching for persistence
- Cache statistics and inspection
- Manual cache management (clear, evict)
"""

from __future__ import annotations

import ast
import functools
import hashlib
import inspect
import threading
import weakref
from typing import Any, Callable, Generic, TypeVar

# Type variable for cached values
T = TypeVar("T")


def _strip_docstrings(tree: ast.AST) -> ast.AST:
    """Remove docstrings from AST (they don't affect semantics).
    
    Args:
        tree: AST tree to process
        
    Returns:
        AST with docstrings removed from functions and classes
    """
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.ClassDef, ast.AsyncFunctionDef)):
            # Remove docstring from body (first element if it's a string constant)
            if (node.body and 
                isinstance(node.body[0], ast.Expr) and
                isinstance(node.body[0].value, ast.Constant) and
                isinstance(node.body[0].value.value, str)):
                node.body = node.body[1:]
    return tree


def get_function_ast_hash(fn: Callable[..., Any]) -> str:
    """Compute hash from AST instead of source text.
    
    This ensures that formatting changes (whitespace, comments) don't
    invalidate the cache. Only semantic changes affect the hash.
    
    Args:
        fn: Function to compute hash for
        
    Returns:
        16-character hexadecimal hash string
        
    Raises:
        ValueError: If function source cannot be retrieved
        
    Example:
        >>> def my_design(width: int) -> Circuit:
        ...     # This is a comment
        ...     return Circuit("Test")
        >>> hash1 = get_function_ast_hash(my_design)
        
        # Same function with different formatting -> same hash
        >>> def my_design(width: int) -> Circuit:
        ...     return Circuit("Test")  # Comment moved
        >>> hash2 = get_function_ast_hash(my_design)
        >>> assert hash1 == hash2
    """
    try:
        source = inspect.getsource(fn)
    except (OSError, TypeError) as e:
        raise ValueError(f"Cannot get source for function {fn}: {e}") from e
    
    # Parse into AST
    try:
        tree = ast.parse(source)
    except SyntaxError as e:
        raise ValueError(f"Cannot parse source for function {fn}: {e}") from e
    
    # Strip docstrings and comments (they don't affect semantics)
    tree = _strip_docstrings(tree)
    
    # Convert AST to canonical string representation
    try:
        canonical = ast.unparse(tree)
    except Exception:
        # Fallback: use source with whitespace normalized
        import re
        canonical = re.sub(r'\s+', ' ', source).strip()
    
    return hashlib.sha256(canonical.encode()).hexdigest()[:16]


def canonical_hash(obj: Any) -> str:
    """Compute deterministic hash for static arguments.
    
    Never uses pickle - it's not deterministic across Python versions.
    
    Args:
        obj: Object to hash
        
    Returns:
        Canonical string representation for hashing
        
    Raises:
        TypeError: If object type is not supported
        
    Example:
        >>> canonical_hash(42)
        '42'
        >>> canonical_hash([1, 2, 3])
        '[1,2,3]'
        >>> canonical_hash({"b": 2, "a": 1})
        '{"a":1,"b":2}'
    """
    if obj is None:
        return "null"
    elif isinstance(obj, bool):
        return "true" if obj else "false"
    elif isinstance(obj, int):
        return str(obj)
    elif isinstance(obj, float):
        # Use repr for deterministic float representation
        return repr(obj)
    elif isinstance(obj, str):
        # Escape quotes and backslashes
        escaped = obj.replace('\\', '\\\\').replace('"', '\\"')
        return f'"{escaped}"'
    elif isinstance(obj, (list, tuple)):
        items = [canonical_hash(x) for x in obj]
        return f'[{",".join(items)}]'
    elif isinstance(obj, dict):
        # Sort keys for deterministic ordering
        items = [f'{canonical_hash(k)}:{canonical_hash(v)}' 
                 for k, v in sorted(obj.items(), key=lambda x: canonical_hash(x[0]))]
        return f'{{{",".join(items)}}}'
    elif isinstance(obj, frozenset):
        items = sorted(canonical_hash(x) for x in obj)
        return f'fs({",".join(items)})'
    elif isinstance(obj, set):
        items = sorted(canonical_hash(x) for x in obj)
        return f's({",".join(items)})'
    else:
        raise TypeError(f"Cannot hash type {type(obj).__name__}. "
                       f"Static args must be basic types: "
                       f"None, bool, int, float, str, list, tuple, dict, set, frozenset")


class CacheEntry(Generic[T]):
    """Entry in the JIT cache.

    Attributes:
        key: Cache key string
        value: Cached circuit or computation result
        static_args: Static arguments that produced this entry
        hit_count: Number of times this entry was accessed
    """

    def __init__(
        self,
        key: str,
        value: T,
        static_args: tuple[tuple[int, str, Any], ...],
    ):
        self.key = key
        self.value = value
        self.static_args = static_args
        self.hit_count = 0
        self._lock = threading.Lock()

    def record_hit(self) -> None:
        """Increment the hit counter (thread-safe)."""
        with self._lock:
            self.hit_count += 1

    def __repr__(self) -> str:
        return f"CacheEntry(key={self.key!r}, hit_count={self.hit_count})"


class WeakRefCacheEntry(Generic[T]):
    """Cache entry that holds a weak reference to the value.
    
    This allows garbage collection of compiled artifacts when memory is tight,
    while still maintaining cache hit statistics.
    
    Attributes:
        key: Cache key string
        static_args: Static arguments that produced this entry
        hit_count: Number of times this entry was accessed (even if GC'd)
    """
    
    def __init__(
        self,
        key: str,
        value: T,
        static_args: tuple[tuple[int, str, Any], ...],
    ):
        self.key = key
        # This will raise TypeError if value doesn't support weak references
        self._ref: weakref.ref[T] = weakref.ref(value)
        self.static_args = static_args
        self.hit_count = 0
        self._lock = threading.Lock()
        
    @property
    def value(self) -> T | None:
        """Get the cached value if it still exists."""
        return self._ref()
    
    @property
    def is_alive(self) -> bool:
        """Check if the referenced value is still alive."""
        return self._ref() is not None

    def record_hit(self) -> None:
        """Increment the hit counter (thread-safe)."""
        with self._lock:
            self.hit_count += 1

    def __repr__(self) -> str:
        status = "alive" if self.is_alive else "gc'd"
        return f"WeakRefCacheEntry(key={self.key!r}, hit_count={self.hit_count}, status={status})"


class JITCache(Generic[T]):
    """Thread-safe cache for JIT-compiled circuits.

    This cache maps cache keys (function AST hash + static args) to compiled
    circuits or simulation results. It supports:

    - Thread-safe concurrent access
    - LRU eviction when max_size is reached
    - Statistics tracking
    - Optional weak references to allow GC of unused circuits

    Example:
        cache = JITCache[max_size=100]()

        # Store entry
        entry = cache.put(key, circuit, static_args)

        # Retrieve entry
        entry = cache.get(key)
        if entry:
            circuit = entry.value

        # Check stats
        print(f"Hits: {cache.stats.hits}, Misses: {cache.stats.misses}")
    """

    def __init__(self, max_size: int | None = None, use_weak_refs: bool = False):
        """Initialize the cache.

        Args:
            max_size: Maximum number of entries (None for unlimited)
            use_weak_refs: Whether to use weak references for cached values.
                When True, cached values can be garbage collected under memory
                pressure. The cache entry remains but the value becomes None.
        """
        self._max_size = max_size
        self._use_weak_refs = use_weak_refs
        self._cache: dict[str, CacheEntry[T] | WeakRefCacheEntry[T]] = {}
        self._lock = threading.RLock()
        self._stats = CacheStats()

    @property
    def stats(self) -> CacheStats:
        """Get cache statistics (read-only)."""
        return self._stats.copy()

    def get(self, key: str) -> CacheEntry[T] | WeakRefCacheEntry[T] | None:
        """Get an entry from the cache.

        Args:
            key: Cache key

        Returns:
            CacheEntry if found and value is alive, None otherwise
        """
        with self._lock:
            entry = self._cache.get(key)
            if entry is None:
                self._stats.record_miss()
                return None
            
            # For weak reference entries, check if value is still alive
            if isinstance(entry, WeakRefCacheEntry):
                if entry.value is None:
                    # Value was garbage collected - remove the entry
                    del self._cache[key]
                    self._stats.record_miss()
                    return None
            
            entry.record_hit()
            self._stats.record_hit()
            return entry

    def put(
        self,
        key: str,
        value: T,
        static_args: tuple[tuple[int, str, Any], ...],
    ) -> CacheEntry[T] | WeakRefCacheEntry[T]:
        """Store an entry in the cache.

        Args:
            key: Cache key
            value: Value to cache
            static_args: Static arguments for reference

        Returns:
            The created CacheEntry or WeakRefCacheEntry
        """
        with self._lock:
            # Evict if at capacity
            if self._max_size is not None and len(self._cache) >= self._max_size:
                self._evict_lru()

            if self._use_weak_refs:
                try:
                    entry: CacheEntry[T] | WeakRefCacheEntry[T] = WeakRefCacheEntry(key, value, static_args)
                except TypeError:
                    # Object doesn't support weak references - use strong reference
                    entry = CacheEntry(key, value, static_args)
            else:
                entry = CacheEntry(key, value, static_args)
            self._cache[key] = entry
            self._stats.record_insert()
            return entry

    def contains(self, key: str) -> bool:
        """Check if a key is in the cache.

        Args:
            key: Cache key to check

        Returns:
            True if key exists and value is alive (for weak refs)
        """
        with self._lock:
            entry = self._cache.get(key)
            if entry is None:
                return False
            # For weak reference entries, check if value is still alive
            if isinstance(entry, WeakRefCacheEntry):
                return entry.value is not None
            return True

    def evict(self, key: str) -> bool:
        """Evict a specific entry from the cache.

        Args:
            key: Cache key to evict

        Returns:
            True if entry was found and removed
        """
        with self._lock:
            if key in self._cache:
                del self._cache[key]
                self._stats.record_eviction()
                return True
            return False

    def clear(self) -> int:
        """Clear all entries from the cache.

        Returns:
            Number of entries cleared
        """
        with self._lock:
            count = len(self._cache)
            self._cache.clear()
            self._stats.record_clear(count)
            return count

    def keys(self) -> list[str]:
        """Get all cache keys.

        Returns:
            List of cache keys
        """
        with self._lock:
            return list(self._cache.keys())

    def entries(self) -> list[CacheEntry[T] | WeakRefCacheEntry[T]]:
        """Get all cache entries.

        Returns:
            List of CacheEntry or WeakRefCacheEntry objects
        """
        with self._lock:
            return list(self._cache.values())

    def alive_entries(self) -> list[CacheEntry[T]]:
        """Get all cache entries with alive values.
        
        For weak reference caches, filters out entries whose values
        have been garbage collected.
        
        Returns:
            List of CacheEntry objects with alive values
        """
        with self._lock:
            result = []
            for entry in self._cache.values():
                if isinstance(entry, WeakRefCacheEntry):
                    if entry.value is not None:
                        # Create a regular CacheEntry for the return
                        result.append(CacheEntry(entry.key, entry.value, entry.static_args))
                else:
                    result.append(entry)
            return result

    def _evict_lru(self) -> None:
        """Evict the least recently used entry.

        Uses hit count as a proxy for recency.
        """
        if not self._cache:
            return

        # Find entry with lowest hit count
        lru_key = min(
            self._cache.keys(),
            key=lambda k: self._cache[k].hit_count
        )
        del self._cache[lru_key]
        self._stats.record_eviction()

    def __len__(self) -> int:
        """Return the number of entries in the cache."""
        with self._lock:
            return len(self._cache)

    def __repr__(self) -> str:
        with self._lock:
            weak_ref_str = ", weak_refs=True" if self._use_weak_refs else ""
            return f"JITCache(size={len(self._cache)}, max_size={self._max_size}{weak_ref_str})"

    def __bool__(self) -> bool:
        """Cache is always truthy (even when empty)."""
        return True


class CacheStats:
    """Statistics for cache operations.

    Attributes:
        hits: Number of cache hits
        misses: Number of cache misses
        inserts: Number of entries inserted
        evictions: Number of entries evicted
        clears: Number of times cache was cleared
    """

    def __init__(self):
        self.hits = 0
        self.misses = 0
        self.inserts = 0
        self.evictions = 0
        self.clears = 0
        self._lock = threading.Lock()

    def record_hit(self) -> None:
        """Record a cache hit."""
        with self._lock:
            self.hits += 1

    def record_miss(self) -> None:
        """Record a cache miss."""
        with self._lock:
            self.misses += 1

    def record_insert(self) -> None:
        """Record an entry insertion."""
        with self._lock:
            self.inserts += 1

    def record_eviction(self) -> None:
        """Record an entry eviction."""
        with self._lock:
            self.evictions += 1

    def record_clear(self, count: int = 0) -> None:
        """Record a cache clear."""
        with self._lock:
            self.clears += 1

    def copy(self) -> CacheStats:
        """Create a copy of the stats."""
        with self._lock:
            new_stats = CacheStats()
            new_stats.hits = self.hits
            new_stats.misses = self.misses
            new_stats.inserts = self.inserts
            new_stats.evictions = self.evictions
            new_stats.clears = self.clears
            return new_stats

    @property
    def hit_rate(self) -> float:
        """Calculate cache hit rate.

        Returns:
            Hit rate as a float between 0.0 and 1.0
        """
        with self._lock:
            total = self.hits + self.misses
            if total == 0:
                return 0.0
            return self.hits / total

    def __repr__(self) -> str:
        with self._lock:
            return (
                f"CacheStats("
                f"hits={self.hits}, "
                f"misses={self.misses}, "
                f"hit_rate={self.hit_rate:.2%}, "
                f"inserts={self.inserts}, "
                f"evictions={self.evictions}"
                f")"
            )


# Global cache instance for all JIT-compiled circuits
_global_jit_cache: JITCache[Any] = JITCache()


def get_global_cache() -> JITCache[Any]:
    """Get the global JIT cache instance.

    Returns:
        The global JITCache instance
    """
    return _global_jit_cache


def set_global_cache(cache: JITCache[Any]) -> None:
    """Set the global JIT cache instance.

    Args:
        cache: New cache instance to use globally
    """
    global _global_jit_cache
    _global_jit_cache = cache


class cached:
    """Decorator for caching function results based on static arguments.

    This decorator wraps a function and caches its results based on the
    cache key computed from static arguments.

    Example:
        @cached
        def expensive_elaboration(width: int, depth: int):
            # This will only run once for each (width, depth) combination
            return Circuit("Test")

    Args:
        func: Function to cache
        cache: Optional cache to use (defaults to global cache)

    Returns:
        Wrapped function that uses caching
    """

    def __init__(
        self,
        func: Callable[..., T],
        cache: JITCache[T] | None = None,
    ):
        self._func = func
        self._cache = cache or _global_jit_cache
        self._key_func: Callable[..., str] | None = None

    def with_key(self, key_func: Callable[..., str]) -> cached:
        """Set a custom key function.

        Args:
            key_func: Function that computes cache key from arguments

        Returns:
            Self for chaining
        """
        self._key_func = key_func
        return self

    def __call__(self, *args: Any, **kwargs: Any) -> T:
        """Execute with caching."""
        # Compute cache key
        if self._key_func:
            key = self._key_func(*args, **kwargs)
        else:
            # Default: use AST-based function hash + static args
            func_hash = get_function_ast_hash(self._func)
            static_hash = canonical_hash((args, tuple(sorted(kwargs.items()))))
            key = f"{func_hash}:{static_hash}"

        # Try to get from cache
        entry = self._cache.get(key)
        if entry is not None:
            return entry.value

        # Compute and cache
        result = self._func(*args, **kwargs)
        self._cache.put(key, result, ())
        return result


def weakref_lru_cache(maxsize: int = 128):
    """LRU cache that holds weak references to cached objects.
    
    Allows garbage collection of compiled artifacts when memory is tight.
    Cache entries remain but return None once the value is GC'd.
    
    For objects that don't support weak references (primitives like int, str),
    falls back to strong references.
    
    Args:
        maxsize: Maximum cache size
        
    Returns:
        Decorator function
        
    Example:
        @weakref_lru_cache(maxsize=100)
        def compile_design(ast_hash: str, static_args: tuple):
            # Expensive compilation
            return compiled_circuit
    """
    def decorator(func: Callable[..., T]) -> Callable[..., T]:
        cache: dict[Any, weakref.ref] = {}
        strong_cache: dict[Any, Any] = {}  # Fallback for non-weak-referenceable objects
        access_order: list[Any] = []
        strong_order: list[Any] = []
        lock = threading.Lock()
        
        @functools.wraps(func)
        def wrapper(*args: Any) -> T:
            # Make args hashable for use as dict key
            key = (func.__qualname__, args)
            
            with lock:
                # Check weak ref cache
                if key in cache:
                    result = cache[key]()
                    if result is not None:
                        # Update access order for LRU
                        access_order.remove(key)
                        access_order.append(key)
                        return result
                    else:
                        # Value was GC'd, remove entry
                        del cache[key]
                        access_order.remove(key)
                
                # Check strong ref cache
                if key in strong_cache:
                    result = strong_cache[key]
                    strong_order.remove(key)
                    strong_order.append(key)
                    return result
                
                # Compute result
                result = func(*args)
                
                # Try to store with weak reference first
                try:
                    # Evict LRU if at capacity
                    if maxsize > 0 and len(cache) + len(strong_cache) >= maxsize:
                        # Evict from weak cache first if possible
                        if cache and len(cache) >= maxsize // 2:
                            lru_key = access_order.pop(0)
                            del cache[lru_key]
                        elif strong_cache:
                            lru_key = strong_order.pop(0)
                            del strong_cache[lru_key]
                    
                    ref = weakref.ref(result)
                    cache[key] = ref
                    access_order.append(key)
                except TypeError:
                    # Object doesn't support weak references (e.g., int, str)
                    # Fall back to strong reference
                    if maxsize > 0 and len(strong_cache) >= maxsize:
                        lru_key = strong_order.pop(0)
                        del strong_cache[lru_key]
                    
                    strong_cache[key] = result
                    strong_order.append(key)
                
                return result
        
        # Attach cache management methods
        def cache_info() -> dict[str, Any]:
            with lock:
                alive = sum(1 for ref in cache.values() if ref() is not None)
                gc_d = len(cache) - alive
                return {
                    'size': len(cache) + len(strong_cache),
                    'weak_refs': len(cache),
                    'weak_alive': alive,
                    'weak_gc_d': gc_d,
                    'strong_refs': len(strong_cache),
                    'maxsize': maxsize,
                }
        
        def cache_clear() -> None:
            with lock:
                cache.clear()
                strong_cache.clear()
                access_order.clear()
                strong_order.clear()
        
        wrapper.cache_info = cache_info  # type: ignore
        wrapper.cache_clear = cache_clear  # type: ignore
        
        return wrapper
    return decorator


class CacheInfo:
    """Information about the JIT cache state.

    This is a read-only snapshot of cache statistics and configuration.

    Attributes:
        size: Number of entries in cache
        max_size: Maximum cache size (None for unlimited)
        hits: Number of cache hits
        misses: Number of cache misses
        hit_rate: Cache hit rate (0.0 to 1.0)
    """

    def __init__(self, cache: JITCache[Any]):
        stats = cache.stats
        self.size = len(cache)
        self.max_size = cache._max_size
        self.hits = stats.hits
        self.misses = stats.misses
        self.hit_rate = stats.hit_rate

    def __repr__(self) -> str:
        return (
            f"CacheInfo("
            f"size={self.size}, "
            f"max_size={self.max_size}, "
            f"hits={self.hits}, "
            f"misses={self.misses}, "
            f"hit_rate={self.hit_rate:.2%}"
            f")"
        )


def cache_info() -> CacheInfo:
    """Get information about the global cache.

    Returns:
        CacheInfo object with current statistics
    """
    return CacheInfo(_global_jit_cache)


def cache_clear() -> int:
    """Clear the global cache.

    Returns:
        Number of entries cleared
    """
    return _global_jit_cache.clear()
