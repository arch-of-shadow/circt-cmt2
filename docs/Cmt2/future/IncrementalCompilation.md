# Incremental Compilation Strategy for CMT2

**Status:** Implementation in Progress  
**Priority:** P0 (Blocking)  
**Owner:** Compiler Team  
**Last Updated:** 2026-01-28

## Overview

This document describes the incremental compilation strategy for CMT2 multi-module hardware designs. The strategy addresses the critical concern raised by compiler experts regarding compilation performance in large designs where only a subset of modules change between iterations.

## Problem Statement

In hardware design workflows, designers frequently:
1. Make small changes to individual modules
2. Recompile to test those changes
3. Expect fast turnaround times

Without incremental compilation, any change requires re-elaborating and recompiling the entire design from scratch. For large designs with thousands of modules, this becomes prohibitively slow.

### Key Challenges

| Challenge | Impact | Solution |
|-----------|--------|----------|
| Dependency tracking | Changes in base modules affect dependents | Dependency graph with transitive invalidation |
| Interface stability | Need to detect when module interfaces change | Versioned interfaces with content hashing |
| Cache invalidation | Determine what to rebuild when things change | BFS-based invalidation of dependent modules |
| Cross-session persistence | Cache should survive across Python sessions | Disk-based cache with interface metadata |

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     IncrementalCompiler                          │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │   Memory    │  │    Disk     │  │    Dependency Graph     │ │
│  │    Cache    │  │    Cache    │  │  (modules → deps)       │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │  Interface  │  │  Interface  │  │    Module Interface     │ │
│  │   Cache     │  │  Versioning │  │    Definitions          │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### Core Components

#### 1. ModuleInterface

The `ModuleInterface` class captures the public contract of a compiled module:

```python
@dataclass
class ModuleInterface:
    name: str
    version: str  # Hash of interface content
    inputs: Dict[str, SignalType]
    outputs: Dict[str, SignalType]
    parameters: Dict[str, Any]
```

**Interface Versioning:**
- Version is computed as a SHA-256 hash of the canonical interface representation
- Includes: name, sorted inputs, sorted outputs, sorted parameters
- Any interface change produces a different version string
- Used to detect when dependent modules need recompilation

**Example Interface:**
```python
interface = ModuleInterface(
    name="Counter",
    inputs={
        "clk": SignalType(width=1),
        "rst": SignalType(width=1),
        "en": SignalType(width=1),
    },
    outputs={
        "count": SignalType(width=32),
    },
    parameters={"MAX_COUNT": 100}
)
# version = "a3f7e2b9c8d1e5f2"
```

#### 2. DependencyGraph

The `DependencyGraph` maintains bidirectional dependency relationships:

```python
class DependencyGraph:
    modules: Dict[str, CompiledModule]       # module name → compiled
    dependencies: Dict[str, Set[str]]        # module → deps
    dependents: Dict[str, Set[str]]          # module → dependents
```

**Key Operations:**

| Operation | Time Complexity | Description |
|-----------|-----------------|-------------|
| `add_module()` | O(d) where d = #deps | Add module with dependencies |
| `get_invalidated()` | O(V + E) | BFS to find all affected modules |
| `get_dependencies()` | O(1) | Direct dependencies lookup |
| `get_dependents()` | O(1) | Direct dependents lookup |

**Invalidation Algorithm:**

```python
def get_invalidated(changed_module: str) -> Set[str]:
    """Find all modules that need recompilation."""
    invalidated = set()
    to_process = [changed_module]
    
    while to_process:
        module = to_process.pop(0)
        if module not in invalidated:
            invalidated.add(module)
            for dependent in dependents.get(module, []):
                to_process.append(dependent)
    
    return invalidated
```

#### 3. IncrementalCompiler

The main compiler class orchestrates the incremental compilation process:

**Two-Level Caching:**

1. **Memory Cache**: Fast access to recently used modules (LRU eviction)
2. **Disk Cache**: Persistent storage across sessions (pickled modules)

**Cache Key Computation:**
```
cache_key = "{name}_{elaborator_hash}_{static_args_hash}"
```

- `name`: Module name
- `elaborator_hash`: Hash of elaborator function bytecode
- `static_args_hash`: Hash of static argument values

**Compilation Flow:**

```
┌─────────────────┐
│  compile_module │
└────────┬────────┘
         │
         ▼
┌─────────────────┐     ┌──────────────┐
│ Check if any    │────►│ Invalidate   │
│ deps changed?   │ No  │ dependents   │
└────────┬────────┘     └──────────────┘
         │ Yes
         ▼
┌─────────────────┐
│ Check memory    │────►┌──────────────┐
│ cache (hit?)    │ Yes │ Return cached│
└────────┬────────┘     └──────────────┘
         │ No
         ▼
┌─────────────────┐
│ Check disk      │────►┌──────────────┐
│ cache (hit?)    │ Yes │ Load & return│
└────────┬────────┘     └──────────────┘
         │ No
         ▼
┌─────────────────┐
│ Run elaborator  │
└────────┬────────┘
         ▼
┌─────────────────┐
│ Extract/Create  │
│ interface       │
└────────┬────────┘
         ▼
┌─────────────────┐
│ Store in caches │
│ Update deps     │
└─────────────────┘
```

## Usage Examples

### Basic Usage

```python
from cmt2 import elaborate
from cmt2.jit import IncrementalCompiler
from pycmt2 import Circuit

# Define modules
@elaborate
def base_module(width: int):
    circuit = Circuit("Base")
    # ... build circuit ...
    return circuit

@elaborate  
def top_module(base: Circuit):
    circuit = Circuit("Top")
    circuit.instance("base_inst", base)
    # ... build circuit ...
    return circuit

# Create incremental compiler
compiler = IncrementalCompiler()

# Compile base module
base = compiler.compile_module(
    name="base",
    elaborator=base_module,
    static_args=(32,),
    dependencies=[]
)

# Compile top module (depends on base)
top = compiler.compile_module(
    name="top",
    elaborator=top_module,
    static_args=(base,),
    dependencies=["base"]
)

# Change base - top will be invalidated on next compile
base_v2 = compiler.compile_module(
    name="base",
    elaborator=base_module,
    static_args=(64,),  # Changed width!
    dependencies=[]
)

# Top is automatically invalidated
# Next compile_module("top", ...) will trigger recompilation
```

### Multi-Level Dependencies

```python
# Module hierarchy: util -> base -> middle -> top

compiler = IncrementalCompiler()

# Build dependency chain
util = compiler.compile_module("util", util_elab, (), [])
base = compiler.compile_module("base", base_elab, (util,), ["util"])
middle = compiler.compile_module("middle", middle_elab, (base,), ["base"])
top = compiler.compile_module("top", top_elab, (middle,), ["middle"])

# Changing base invalidates base, middle, and top
base_v2 = compiler.compile_module("base", base_elab_v2, (util,), ["util"])

# Invalidated modules:
# - base (changed)
# - middle (depends on base)
# - top (depends on middle, which depends on base)
# util is NOT invalidated (doesn't depend on base)
```

### Custom Cache Directory

```python
from pathlib import Path

# Use project-local cache
compiler = IncrementalCompiler(
    cache_dir=Path("./build/.cmt2_cache"),
    memory_cache_size=50  # Keep 50 modules in memory
)

# Or use shared cache
compiler = IncrementalCompiler(
    cache_dir=Path.home() / ".cmt2_cache" / "my_project"
)
```

### Checking Dependencies

```python
compiler = IncrementalCompiler()

# ... compile modules ...

# Check what a module depends on
deps = compiler.get_dependencies("top")
print(f"top depends on: {deps}")  # {"base", "util"}

# Check what depends on a module
dependents = compiler.get_dependents("base")
print(f"modules depending on base: {dependents}")  # {"top", "middle"}

# Get compilation statistics
stats = compiler.get_stats()
print(f"Cache hits: {stats['cache_hits']}")
print(f"Cache misses: {stats['cache_misses']}")
print(f"Recompilations: {stats['recompilations']}")
```

### Cache Management

```python
compiler = IncrementalCompiler()

# ... do some compilation ...

# Clear all caches (memory and disk)
num_removed = compiler.clear_cache()
print(f"Removed {num_removed} cache entries")

# Stats are reset after clear
stats = compiler.get_stats()
assert stats['cache_hits'] == 0
```

## Interface Extraction

The compiler extracts interfaces from compiled circuits. The default extractor uses introspection:

```python
def _default_interface_extractor(name: str, circuit: Any) -> ModuleInterface:
    inputs = {}
    outputs = {}
    parameters = {}
    
    if hasattr(circuit, 'inputs'):
        inputs = extract_ports(circuit.inputs)
    if hasattr(circuit, 'outputs'):
        outputs = extract_ports(circuit.outputs)
    if hasattr(circuit, 'parameters'):
        parameters = dict(circuit.parameters)
    
    return ModuleInterface(
        name=name,
        inputs=inputs,
        outputs=outputs,
        parameters=parameters,
    )
```

Users can provide custom extractors for specialized circuit types:

```python
def custom_extractor(circuit: MyCircuit) -> ModuleInterface:
    return ModuleInterface(
        name=circuit.module_name,
        inputs={port.name: SignalType(port.width) for port in circuit.input_ports},
        outputs={port.name: SignalType(port.width) for port in circuit.output_ports},
        parameters=circuit.generics,
    )

compiled = compiler.compile_module(
    name="my_mod",
    elaborator=my_elab,
    static_args=(32,),
    dependencies=[],
    interface_extractor=custom_extractor
)
```

## Cache Invalidation Strategy

### When to Invalidate

| Trigger | Action |
|---------|--------|
| Dependency interface changes | Invalidate module + all transitive dependents |
| Elaborator function changes (code hash) | Invalidate all modules using that elaborator |
| Static arguments change | Invalidate specific compilation |
| Explicit clear_cache() | Invalidate everything |

### Invalidation Scope

```
Scenario: base module interface changes

Before:
  util ──► base ──► middle ──► top
           ▲
           └─── other_top

After base changes:
  util ──► base* ──► middle* ──► top*
                      (all starred modules invalidated)
           
  other_top* (also invalidated - depends on base)
```

## Thread Safety

The incremental compiler is thread-safe:

- All graph operations use `threading.RLock`
- Cache operations are atomic
- Statistics use thread-local counters

```python
from concurrent.futures import ThreadPoolExecutor

compiler = IncrementalCompiler()

def compile_worker(name):
    return compiler.compile_module(name, elaborators[name], (), deps[name])

# Safe to use from multiple threads
with ThreadPoolExecutor() as executor:
    results = list(executor.map(compile_worker, module_names))
```

## Performance Considerations

### Expected Performance

| Operation | Expected Time | Notes |
|-----------|---------------|-------|
| Cache hit (memory) | ~1 μs | Direct dictionary lookup |
| Cache hit (disk) | ~10-100 ms | Deserialization overhead |
| Cache miss | Elaboration time | Same as non-incremental |
| Invalidation check | O(deps) | Linear in #dependencies |

### Optimization Tips

1. **Keep memory cache appropriately sized**: Too small = frequent disk reads; too large = memory pressure
2. **Use fast interface extractors**: Custom extractors can be faster than introspection
3. **Group related changes**: Compile multiple changed modules together to avoid repeated invalidation
4. **Use SSD for cache directory**: Disk cache performance depends on I/O speed

## Future Enhancements

### Planned Features

| Feature | Priority | Description |
|---------|----------|-------------|
| Parallel compilation | P1 | Compile independent modules in parallel |
| Fine-grained invalidation | P2 | Track internal dependencies within modules |
| Remote caching | P2 | Share cache across machines |
| Incremental lowering | P2 | Cache intermediate lowering results |
| Watch mode | P3 | Auto-recompile on file changes |

### Research Areas

1. **Semantic change detection**: Detect when interface changes don't actually affect dependents
2. **Speculative compilation**: Pre-compile likely-to-be-used module variants
3. **Distributed compilation**: Compile different modules on different machines

## Implementation Status

| Component | Status | File |
|-----------|--------|------|
| SignalType | ✅ Implemented | `_incremental.py` |
| ModuleInterface | ✅ Implemented | `_incremental.py` |
| DependencyGraph | ✅ Implemented | `_incremental.py` |
| IncrementalCompiler | ✅ Implemented | `_incremental.py` |
| Disk caching | ✅ Implemented | `_incremental.py` |
| Interface versioning | ✅ Implemented | `_incremental.py` |
| Custom extractors | ✅ Implemented | `_incremental.py` |
| Tests | 🔄 In Progress | `test/test_incremental.py` |
| Integration with @elaborate | ⏳ Planned | `_decorator.py` |

## References

- [CMT2 JIT Implementation Plan](../../CLAUDE.md)
- [Module System Documentation](../features/Modules.md)
- [Caching Strategy](./CachingStrategy.md)
- [Python JIT API](../reference/PythonJITAPI.md)
