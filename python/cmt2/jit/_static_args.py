#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Static argument handling for CMT2 JIT compilation.

This module provides the static argument system that allows compile-time
parameterization of hardware designs. It supports:

1. Annotated type syntax (preferred):
   ```python
   from typing import Annotated
   import cmt2

   @cmt2.elaborate
   def design(
       depth: Annotated[int, cmt2.static],      # Static (compile-time)
       width: Annotated[int, cmt2.static],      # Static
       runtime_data: Bits                       # Dynamic (runtime)
   ):
       ...
   ```

2. Legacy syntax (backward compatible):
   ```python
   @cmt2.elaborate(static_argnums=[0, 1])  # Positional indices
   def design(depth: int, width: int, data: Bits):
       ...

   @cmt2.elaborate(static_argnames=["depth", "width"])  # Parameter names
   def design(depth: int, width: int, data: Bits):
       ...
   ```

The static argument system:
- Separates static (compile-time) from dynamic (runtime) arguments
- Computes deterministic hashes for cache keys (no pickle!)
- Validates argument specifications to detect conflicts
"""

from __future__ import annotations

import hashlib
import inspect
from typing import Any, get_args, get_origin

# Try to import Annotated from typing (Python 3.9+)
# Fall back to typing_extensions if needed
try:
    from typing import Annotated
except ImportError:
    try:
        from typing_extensions import Annotated
    except ImportError:
        Annotated = None  # type: ignore


class StaticArgMarker:
    """Marker class for static arguments using Annotated syntax.

    Use this marker with `typing.Annotated` to mark arguments as static
    (compile-time constants):

        def design(depth: Annotated[int, cmt2.static]):
            ...

    The static marker is a singleton - use `cmt2.static` directly.
    """

    def __repr__(self) -> str:
        return "<static>"

    def __eq__(self, other: object) -> bool:
        return isinstance(other, StaticArgMarker)

    def __hash__(self) -> int:
        return hash("cmt2.static")


# Singleton instance of the static marker
static = StaticArgMarker()


class StaticArgSpec:
    """Specification for static arguments.

    This class encapsulates all ways of specifying which arguments
    are static: Annotated types, positional indices, and parameter names.

    Attributes:
        arg_indices: Set of positional indices that are static
        arg_names: Set of parameter names that are static
        from_annotated: Whether any specs came from Annotated types
    """

    def __init__(
        self,
        arg_indices: set[int] | None = None,
        arg_names: set[str] | None = None,
        from_annotated: bool = False,
    ):
        self.arg_indices: set[int] = arg_indices or set()
        self.arg_names: set[str] = arg_names or set()
        self.from_annotated: bool = from_annotated

    def __repr__(self) -> str:
        parts = []
        if self.arg_indices:
            parts.append(f"indices={sorted(self.arg_indices)}")
        if self.arg_names:
            parts.append(f"names={sorted(self.arg_names)}")
        if self.from_annotated:
            parts.append("from_annotated=True")
        return f"StaticArgSpec({', '.join(parts)})"


def _extract_annotated_static_params(signature: inspect.Signature) -> set[str]:
    """Extract parameter names marked as static via Annotated types.

    Args:
        signature: Function signature to analyze

    Returns:
        Set of parameter names that have cmt2.static in their Annotated type
    """
    static_names: set[str] = set()

    if Annotated is None:
        return static_names

    for name, param in signature.parameters.items():
        annotation = param.annotation
        if annotation is inspect.Parameter.empty:
            continue

        # Check if it's an Annotated type
        origin = get_origin(annotation)
        if origin is not Annotated:
            continue

        # Get the metadata args from Annotated[type, metadata...]
        metadata = get_args(annotation)
        if len(metadata) < 2:
            continue

        # Check if cmt2.static is in the metadata
        for meta in metadata[1:]:
            if isinstance(meta, StaticArgMarker) or meta is static:
                static_names.add(name)
                break

    return static_names


def parse_static_arg_specs(
    signature: inspect.Signature,
    static_argnums: tuple[int, ...] | None = None,
    static_argnames: tuple[str, ...] | None = None,
) -> StaticArgSpec:
    """Parse all static argument specifications.

    This function combines specs from:
    1. Annotated types in the function signature (preferred)
    2. Legacy static_argnums parameter
    3. Legacy static_argnames parameter

    Args:
        signature: Function signature
        static_argnums: Positional indices of static arguments (legacy)
        static_argnames: Parameter names of static arguments (legacy)

    Returns:
        Combined StaticArgSpec

    Raises:
        ValueError: If specifications conflict (e.g., index 0 and name "depth"
                   when "depth" is at index 0)
    """
    # Extract from Annotated types
    annotated_names = _extract_annotated_static_params(signature)

    # Convert annotated names to indices for validation
    param_list = list(signature.parameters.keys())
    annotated_indices = {param_list.index(name) for name in annotated_names}

    # Parse legacy specs
    legacy_indices = set(static_argnums) if static_argnums else set()
    legacy_names = set(static_argnames) if static_argnames else set()

    # Validate: detect conflicts between index and name specs
    # A conflict occurs when a name maps to an index that's already in indices
    for name in legacy_names:
        if name not in param_list:
            raise ValueError(
                f"static_argnames contains unknown parameter: '{name}'. "
                f"Valid parameters are: {param_list}"
            )
        name_index = param_list.index(name)
        if name_index in legacy_indices:
            raise ValueError(
                f"Conflicting static argument specification: index {name_index} "
                f"(parameter '{name}') is specified in both static_argnums "
                f"and static_argnames. Use only one form."
            )

    # Validate: ensure indices are within bounds
    num_params = len(param_list)
    for idx in legacy_indices:
        if idx < 0 or idx >= num_params:
            raise ValueError(
                f"static_argnums contains out-of-bounds index: {idx}. "
                f"Function has {num_params} parameters."
            )

    # Combine specs
    combined_indices = annotated_indices | legacy_indices
    combined_names = annotated_names | legacy_names

    # Convert any remaining legacy names to indices for unified handling
    for name in legacy_names:
        if name in param_list:
            combined_indices.add(param_list.index(name))

    return StaticArgSpec(
        arg_indices=combined_indices,
        arg_names=combined_names,
        from_annotated=bool(annotated_names),
    )


def partition_args(
    args: tuple[Any, ...],
    kwargs: dict[str, Any],
    static_spec: StaticArgSpec,
    signature: inspect.Signature,
) -> tuple[tuple[tuple[int, str, Any], ...], tuple[tuple[int, str, Any], ...]]:
    """Separate static and dynamic arguments.

    Args:
        args: Positional arguments passed to the function
        kwargs: Keyword arguments passed to the function
        static_spec: Specification of which arguments are static
        signature: Function signature for parameter name lookup

    Returns:
        Tuple of (static_args, dynamic_args) where each is a tuple of
        (index, name, value) tuples, sorted by index.

    Raises:
        TypeError: If arguments don't match the signature
    """
    # Bind arguments to signature
    bound = signature.bind(*args, **kwargs)
    bound.apply_defaults()

    # Get all parameters in order
    param_list = list(signature.parameters.keys())

    static_args: list[tuple[int, str, Any]] = []
    dynamic_args: list[tuple[int, str, Any]] = []

    for idx, (name, value) in enumerate(bound.arguments.items()):
        if idx in static_spec.arg_indices or name in static_spec.arg_names:
            static_args.append((idx, name, value))
        else:
            dynamic_args.append((idx, name, value))

    # Sort by index for deterministic ordering
    return (tuple(sorted(static_args)), tuple(sorted(dynamic_args)))


def _canonicalize_value(value: Any) -> str:
    """Canonicalize a value for hashing.

    Supports: int, float, str, bool, None, tuple, list, dict

    Args:
        value: Value to canonicalize

    Returns:
        Canonical string representation

    Raises:
        TypeError: If value type is not supported
    """
    if value is None:
        return "N"
    elif isinstance(value, bool):
        return f"B:{value}"
    elif isinstance(value, int):
        return f"I:{value}"
    elif isinstance(value, float):
        # Use repr for deterministic float representation
        return f"F:{repr(value)}"
    elif isinstance(value, str):
        # Escape backslashes and colons for safety
        escaped = value.replace("\\", "\\\\").replace(":", "\\:")
        return f"S:{escaped}"
    elif isinstance(value, (list, tuple)):
        items = ",".join(_canonicalize_value(item) for item in value)
        type_char = "L" if isinstance(value, list) else "T"
        return f"{type_char}:[{items}]"
    elif isinstance(value, dict):
        # Sort keys for deterministic ordering
        items = ",".join(
            f"{_canonicalize_value(k)}={_canonicalize_value(v)}"
            for k, v in sorted(value.items(), key=lambda x: str(x[0]))
        )
        return f"D:{{{items}}}"
    elif isinstance(value, (set, frozenset)):
        # Sort elements for deterministic ordering
        items = ",".join(_canonicalize_value(item) for item in sorted(value, key=str))
        type_char = "Set" if isinstance(value, set) else "FSet"
        return f"{type_char}:[{items}]"
    else:
        raise TypeError(
            f"Static argument of type {type(value).__name__} is not hashable. "
            f"Supported types: int, float, str, bool, None, tuple, list, dict, set, frozenset"
        )


def compute_static_hash(static_args: tuple[tuple[int, str, Any], ...]) -> str:
    """Compute a deterministic hash for static arguments.

    Uses SHA-256 of a canonical string representation. No pickle!

    Args:
        static_args: Tuple of (index, name, value) tuples from partition_args

    Returns:
        Hexadecimal hash string (64 characters)

    Example:
        static_args = ((0, "width", 32), (1, "depth", 16))
        hash_str = compute_static_hash(static_args)
        # Returns: "a3f2..." (64 hex chars)
    """
    # Build canonical representation
    parts = []
    for idx, name, value in static_args:
        canonical_value = _canonicalize_value(value)
        parts.append(f"{idx}:{name}={canonical_value}")

    canonical_str = "|".join(parts)

    # Compute SHA-256 hash
    hash_bytes = hashlib.sha256(canonical_str.encode("utf-8")).digest()
    return hash_bytes.hex()


def compute_cache_key(
    func: Any,
    static_args: tuple[tuple[int, str, Any], ...],
) -> str:
    """Compute a cache key for a function with static arguments.

    The cache key includes:
    - Function identity (qualified name)
    - Static argument values

    Args:
        func: The function being cached
        static_args: Static arguments from partition_args

    Returns:
        Cache key string
    """
    # Get function identifier
    func_id = f"{func.__module__}.{func.__qualname__}"

    # Compute static hash
    static_hash = compute_static_hash(static_args)

    return f"{func_id}:{static_hash}"


def validate_static_args(static_args: tuple[tuple[int, str, Any], ...]) -> None:
    """Validate that all static arguments are hashable.

    Args:
        static_args: Static arguments to validate

    Raises:
        TypeError: If any static argument is not hashable
    """
    for idx, name, value in static_args:
        try:
            _canonicalize_value(value)
        except TypeError as e:
            raise TypeError(
                f"Static argument '{name}' at index {idx} is not hashable: {e}"
            ) from e
