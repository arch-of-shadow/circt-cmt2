#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT compilation support for CMT2.

This subpackage provides just-in-time compilation decorators for CMT2 designs:
- @elaborate: Staged compilation that returns a Circuit object
- @simulate: Immediate execution that returns simulation results

Example:
    from cmt2 import elaborate, simulate
    from pycmt2 import Circuit

    @elaborate
    def my_design(width: int):
        circuit = Circuit("Test")
        # ... build circuit ...
        return circuit

    @simulate
    def my_sim(width: int):
        circuit = Circuit("Test")
        # ... build and simulate ...
        return circuit.run()
"""

from ._cache import (
    JITCache,
    CacheEntry,
    CacheInfo,
    CacheStats,
    WeakRefCacheEntry,
    cache_clear,
    cache_info,
    canonical_hash,
    get_function_ast_hash,
    get_global_cache,
    set_global_cache,
    weakref_lru_cache,
)
from ._decorator import (
    elaborate,
    simulate,
    ElaboratedFunction,
    SimulatedFunction,
)
from ._stages import (
    ElaboratedCircuit,
    LoweredCircuit,
    CompiledCircuit,
    create_elaborated_circuit,
    is_elaborated,
    is_lowered,
    is_compiled,
)
from ._tracer import (
    Cmt2Trace,
    SignalTracer,
    TracingError,
    get_current_trace,
    trace_function,
)
from ._primitive_ops import (
    OperationType,
    PrimitiveOpRegistry,
    get_primitive_registry,
    emit_primitive_op,
    get_op_type_for_operator,
)
from ._static_args import (
    StaticArgMarker,
    StaticArgSpec,
    compute_cache_key,
    compute_static_hash,
    partition_args,
    parse_static_arg_specs,
    static,
    validate_static_args,
)
from ._incremental import (
    DependencyGraph,
    IncrementalCompiler,
    ModuleInterface,
    SignalType,
    CompiledModule,
    create_incremental_compiler,
)
from ._debug_info import (
    SourceLocation,
    DebugInfo,
    DebugInfoContext,
    get_current_location,
    format_error_with_source,
    Cmt2TracingError,
    add_debug_info_to_circuit,
)

__all__ = [
    # Decorators
    "elaborate",
    "simulate",
    # Function wrappers
    "ElaboratedFunction",
    "SimulatedFunction",
    # Staged compilation classes
    "ElaboratedCircuit",
    "LoweredCircuit",
    "CompiledCircuit",
    # Stage utilities
    "create_elaborated_circuit",
    "is_elaborated",
    "is_lowered",
    "is_compiled",
    # Static argument handling
    "static",
    "StaticArgMarker",
    "StaticArgSpec",
    "partition_args",
    "parse_static_arg_specs",
    "compute_static_hash",
    "compute_cache_key",
    "validate_static_args",
    # Caching
    "JITCache",
    "CacheEntry",
    "WeakRefCacheEntry",
    "CacheStats",
    "CacheInfo",
    "get_global_cache",
    "set_global_cache",
    "cache_info",
    "cache_clear",
    "get_function_ast_hash",
    "canonical_hash",
    "weakref_lru_cache",
    # Incremental compilation
    "IncrementalCompiler",
    "DependencyGraph",
    "ModuleInterface",
    "SignalType",
    "CompiledModule",
    "create_incremental_compiler",
    # Debug info
    "SourceLocation",
    "DebugInfo",
    "DebugInfoContext",
    "get_current_location",
    "format_error_with_source",
    "Cmt2TracingError",
    "add_debug_info_to_circuit",
]
