# JAX JIT Compilation Design Patterns Analysis

This document analyzes the JIT compilation design patterns in JAX (references/jax/) with a focus on applicability to hardware DSL JIT compilation.

## Executive Summary

JAX implements a sophisticated multi-stage JIT compilation pipeline that transforms Python functions into optimized XLA executables. The key innovation is the **staged computation model**: Python → Traced (jaxpr) → Lowered (MLIR/StableHLO) → Compiled (XLA executable). This design enables aggressive optimization while maintaining Python's usability.

---

## 1. JIT Entry Point Architecture

### 1.1 Key Classes and Responsibilities

```python
# From jax/_src/api.py and jax/_src/pjit.py

class JitWrapped(stages.Wrapped):
    """Public interface for jitted functions."""
    def eval_shape(self, *args, **kwargs) -> ShapeDtypeStruct: ...
    def trace(self, *args, **kwargs) -> stages.Traced: ...
    def lower(self, *args, **kwargs) -> stages.Lowered: ...
    def __call__(self, *args, **kwargs): ...

class PjitInfo(NamedTuple):
    """Pre-processed JIT configuration that serves as cache key."""
    fun_sourceinfo: str
    fun_signature: inspect.Signature | None
    user_specified_in_shardings: bool
    in_shardings_treedef: PyTreeDef
    in_shardings_leaves: tuple[Any, ...]
    out_shardings_treedef: PyTreeDef
    out_shardings_leaves: tuple[Any, ...]
    static_argnums: tuple[int, ...]
    static_argnames: tuple[str, ...]
    donate_argnums: tuple[int, ...]
    donate_argnames: tuple[str, ...]
    device: xc.Device | None
    backend: str | None
    keep_unused: bool
    inline: bool
    use_resource_env: bool
    compiler_options_kvs: tuple[tuple[str, Any], ...]
    
    # Hash by identity for cache key purposes
    def __hash__(self): return id(self)
    def __eq__(self, other): return self is other
```

### 1.2 JIT Flow Pseudocode

```python
def jit(fun, *, static_argnums=None, static_argnames=None, 
        donate_argnums=None, device=None, backend=None, ...):
    # 1. Parse and validate JIT arguments
    jit_info = _parse_jit_arguments(
        fun, static_argnums=static_argnums, 
        static_argnames=static_argnames, ...)
    
    # 2. Create C++-backed JIT function with cache
    return _cpp_pjit(fun, jit_info)

def _cpp_pjit(fun, jit_info):
    @api_boundary  # Marks API entry point for errors/debugging
    def cache_miss(*args, **kwargs):
        # Called on cache miss - performs tracing + compilation
        p, args_flat = _trace_for_jit(fun, jit_info, args, kwargs)
        return _run_python_pjit(p, args_flat, fun, args, kwargs)
    
    # Create cache key from JIT configuration
    cache_key = pxla.JitGlobalCppCacheKeys(
        donate_argnums=jit_info.donate_argnums,
        in_shardings_treedef=jit_info.in_shardings_treedef,
        ...)
    
    # Return C++ wrapper that manages caching
    return xc._xla.pjit(
        fun_name(fun), fun, cache_miss, 
        jit_info.static_argnums, jit_info.static_argnames,
        cache_key, tree_util.dispatch_registry, ...)
```

### 1.3 Parameter Handling (static_argnums/static_argnames)

```python
def _trace_for_jit(fun, ji: PjitInfo, args, kwargs):
    # 1. Separate static from dynamic arguments using FlatTree
    args_ft = FlatTree.flatten_static_argnums_argnames(
        args, kwargs, ji.static_argnums, ji.static_argnames)
    
    # 2. Infer abstract types for dynamic arguments only
    avals = _infer_input_type(fun, dbg, args_ft.vals)
    
    # 3. Trace function with abstract values
    jaxpr, out_avals = pe.trace_to_jaxpr(fun, in_type, dbg, qdd_token)
    
    # 4. Return params for compilation
    return PjitParams(consts, params, avals_ft.vals, ...)

# FlatTree handles static/dynamic separation
class FlatTree:
    vals: list[Any]           # Dynamic values (traced)
    tree: PyTreeDef           # Tree structure
    tree_without_statics: PyTreeDef  # Tree without static args
```

**Key Insight**: Static arguments are evaluated at trace time and become part of the cache key. Dynamic arguments are traced with abstract values (avals) and compiled once per unique shape/dtype signature.

---

## 2. Tracing Mechanism

### 2.1 Core Abstractions

```python
# From jax/_src/core.py

class AbstractValue:
    """Base class for abstract values (avals)."""
    pass

class ShapedArray(AbstractValue):
    """Most common abstract value - represents arrays with known shape/dtype."""
    shape: tuple[int, ...]
    dtype: np.dtype
    weak_type: bool
    sharding: Sharding
    
class Tracer:
    """Proxy object that intercepts operations during tracing."""
    _trace: Trace
    aval: AbstractValue
    
    def __getattr__(self, name):
        # Forward attribute access to aval
        return getattr(self.aval, name)

class Trace(Generic[TracerType]):
    """Context for tracing - processes primitives."""
    def process_primitive(self, primitive, tracers, params):
        raise NotImplementedError
```

### 2.2 Trace-to-Jaxpr Process

```python
# From jax/_src/interpreters/partial_eval.py

@profiler.annotate_function
def trace_to_jaxpr_nounits(
    fun: lu.WrappedFun, 
    pvals: Sequence[PartialVal],
    instantiate: bool | Sequence[bool] = False
) -> tuple[Jaxpr, list[PartialVal], list[core.Value]]:
    """
    Main entry point for tracing Python functions to jaxpr.
    
    Args:
        fun: Wrapped Python function
        pvals: Partial values - (aval, None) for unknown, (None, const) for known
        instantiate: Which outputs to ensure are instantiated (not dropped)
    
    Returns:
        jaxpr: The traced jaxpr
        out_pvals: Output partial values
        consts: Constant values extracted during tracing
    """
    current_name_stack = source_info_util.current_name_stack()
    with core.take_current_trace() as parent_trace:
        # Create jaxpr tracing context
        trace = JaxprTrace(parent_trace, current_name_stack, TraceTag())
        with core.ensure_no_leaks(trace):
            # Transform fun to trace-aware version
            fun = trace_to_subjaxpr_nounits(fun, trace, instantiate, fun.debug_info)
            with core.set_current_trace(trace):
                # Execute function - operations are intercepted and recorded
                jaxpr, (out_pvals, consts, env) = fun.call_wrapped(pvals)
            return jaxpr, out_pvals, consts

# PartialVal represents "partially known" values
class PartialVal(tuple):
    """Either (aval, None) for unknown or (None, const) for known."""
    @classmethod
    def known(cls, const: core.Value) -> PartialVal:
        return PartialVal((None, const))
    
    @classmethod
    def unknown(cls, aval: AbstractValue) -> PartialVal:
        return PartialVal((aval, None))
```

### 2.3 DynamicJaxprTrace - The Modern Tracing Implementation

```python
# From jax/_src/interpreters/partial_eval.py

class DynamicJaxprTrace(core.Trace):
    """Modern trace implementation with automatic DCE."""
    
    frame: JaxprStackFrame  # Accumulates equations during tracing
    
    def process_primitive(self, primitive, tracers, params):
        # 1. Convert to jaxpr tracers (handle nested tracers)
        jaxpr_tracers = map(self.to_jaxpr_tracer, tracers)
        
        # 2. Call abstract evaluation to get output types
        out_avals, effs = _cached_abstract_eval(primitive, *aval_qdds, **params)
        
        # 3. Try constant folding
        maybe_consts = try_constant_folding(primitive, tracers, params, out_avals)
        if maybe_consts is not None:
            return [self.new_const(c, aval=aval) for c, aval in zip(maybe_consts, out_avals)]
        
        # 4. Create equation and output tracers
        eqn, out_tracers = self.make_eqn(tracers, out_avals, primitive, params, effs)
        
        # 5. Apply input-to-output forwarding optimization
        if primitive in forwarding_rules:
            in_fwd, eqn = forwarding_rules[primitive](eqn)
            for out_idx, in_idx in enumerate(in_fwd):
                if in_idx is not None:
                    out_tracers[out_idx] = tracers[in_idx]  # Forward input
        
        self.frame.add_eqn(eqn)
        return out_tracers
```

### 2.4 Shape Inference via Abstract Evaluation

```python
# From jax/_src/core.py

class Primitive:
    """Base class for all JAX operations."""
    name: str
    multiple_results: bool = False
    
    def bind(self, *args, **params):
        """Entry point - called when primitive is invoked."""
        return self._true_bind(*args, **params)
    
    def _true_bind(self, *args, **params):
        # Get current trace and process
        prev_trace = trace_ctx.trace
        trace_ctx.set_trace(eval_trace)
        try:
            return self.bind_with_trace(prev_trace, args, params)
        finally:
            trace_ctx.set_trace(prev_trace)
    
    def bind_with_trace(self, trace, args, params):
        return trace.process_primitive(self, args, params)
    
    def abstract_eval(self, *avals, **params):
        """Compute output abstract values from input abstract values."""
        raise NotImplementedError

# Example: add primitive abstract eval
def add_abstract_eval(x_aval, y_aval):
    # Shape/dtype inference for addition
    out_shape = broadcast_shapes(x_aval.shape, y_aval.shape)
    out_dtype = result_type(x_aval.dtype, y_aval.dtype)
    return ShapedArray(out_shape, out_dtype)
```

---

## 3. Staging/Compilation Pipeline

### 3.1 Three-Stage Architecture

```python
# From jax/_src/stages.py

class Traced(Stage):
    """
    Stage 1: Traced computation.
    - Python function has been staged to jaxpr
    - Ready for lowering but not yet lowered
    - Contains jaxpr + metadata needed for compilation
    """
    _params: dict[str, Any]  # Contains 'jaxpr': ClosedJaxpr
    _in_tree: PyTreeDef
    out_tree: PyTreeDef
    _consts: list[ArrayLike]
    
    def lower(self, *, lowering_platforms=None, _private_parameters=None) -> Lowered:
        """Lower to compiler input."""
        lo = self.lojax  # Get low-level jax representation
        lowering = _resolve_and_lower(lo._meta_tys_flat, **lo._params)
        return Lowered(lowering, lo.args_info, lo.out_tree, ...)

class Lowered(Stage):
    """
    Stage 2: Lowered computation.
    - Translated to compiler input (StableHLO/MLIR)
    - Ready for compilation but not yet compiled
    - Target-independent representation
    """
    _lowering: Lowering  # Internal lowering representation
    args_info: Any       # PyTree of ArgInfo
    out_tree: PyTreeDef
    
    def compile(self, compiler_options=None, *, device_assignment=None) -> Compiled:
        """Compile to executable."""
        return Compiled(
            self._lowering.compile(compiler_options=compiler_options, ...),
            self._lowering.const_args,
            self.args_info, ...)
    
    def as_text(self, dialect="stablehlo") -> str:
        """Get human-readable representation."""
        return self._lowering.as_text(dialect)

class Compiled(Stage):
    """
    Stage 3: Compiled computation.
    - Compiled to target executable (XLA executable)
    - Ready for execution
    - Contains device-specific optimizations
    """
    _executable: Executable
    
    def __call__(self, *args, **kwargs):
        """Execute compiled function."""
        if self._call is None:
            self._call = self._executable.create_cpp_call(self._params)
        return self._call(*args, **kwargs)
```

### 3.2 Lowering to MLIR/StableHLO

```python
# From jax/_src/pjit.py and jax/_src/interpreters/mlir.py

def _resolve_and_lower(args, jaxpr: core.ClosedJaxpr, 
                       in_shardings, out_shardings, 
                       in_layouts, out_layouts, ...):
    """Resolve shardings/layouts and lower to MLIR."""
    # 1. Resolve input shardings based on argument types
    in_shardings = _resolve_in_shardings(args, in_shardings)
    in_layouts = _resolve_in_layouts(args, in_layouts, in_shardings, jaxpr.in_avals)
    out_layouts = _resolve_out_layouts(out_layouts, out_shardings, jaxpr.out_avals)
    
    # 2. Lower to MLIR/StableHLO
    return _pjit_lower(jaxpr, in_shardings, out_shardings, 
                       in_layouts, out_layouts, ...)

@weakref_lru_cache
def _pjit_lower(jaxpr: core.ClosedJaxpr, in_shardings, out_shardings, 
                in_layouts, out_layouts, donated_invars, ...):
    """
    Cached lowering - same jaxpr + shardings -> reuse lowering.
    Uses weakref_lru_cache to allow GC when jaxpr no longer referenced.
    """
    return pxla.lower_sharding_computation(
        jaxpr, 'jit', name, in_shardings, out_shardings,
        in_layouts, out_layouts, tuple(donated_invars), ...)
```

---

## 4. Caching Strategy

### 4.1 Multi-Level Caching Architecture

```python
# From jax/_src/pjit.py

# C++-level caches (fast path)
_cpp_pjit_cache_fun_only = xc._xla.PjitFunctionCache(capacity=8192)
_cpp_pjit_cache_explicit_attributes = xc._xla.PjitFunctionCache(capacity=8192)

# Python-level caches (lowering)
@weakref_lru_cache
def _pjit_lower(jaxpr, in_shardings, out_shardings, ...):
    """Cache lowering results."""
    ...

@util.cache(max_size=4096, trace_context_in_key=False)
def _process_in_axis_resources(in_shardings_treedef, in_shardings_leaves, ...):
    """Cache sharding processing."""
    ...

# Trace cache
trace_to_jaxpr = weakref_lru_cache(pe._trace_to_jaxpr_cached)
```

### 4.2 Cache Key Computation

```python
# From jax/_src/pxla.py

class JitGlobalCppCacheKeys:
    """Cache key for C++ dispatch cache."""
    donate_argnums: tuple[int, ...]
    donate_argnames: tuple[str, ...] | None
    device: xc.Device | None
    backend: str | None
    in_shardings_treedef: PyTreeDef | None
    in_shardings_leaves: tuple[Any, ...]
    out_shardings_treedef: PyTreeDef | None
    out_shardings_leaves: tuple[Any, ...]
    in_layouts_treedef: PyTreeDef | None
    in_layouts_leaves: tuple[Any, ...]
    out_layouts_treedef: PyTreeDef | None
    out_layouts_leaves: tuple[Any, ...]
    compiler_options_kvs: tuple[tuple[str, Any], ...]
    
    @property
    def contains_explicit_attributes(self) -> bool:
        """Determine which cache to use based on whether non-default attrs are set."""
        return (
            self.donate_argnums or self.donate_argnames or
            self.device is not None or self.backend is not None or
            self.in_shardings_treedef is not None or
            self.out_shardings_treedef is not None or
            self.compiler_options_kvs
        )

# Abstract value-based cache keys
def shaped_abstractify(x) -> ShapedArray:
    """Convert value to abstract value for cache key."""
    return ShapedArray(x.shape, x.dtype, weak_type=False)
```

### 4.3 Cache Miss Flow

```python
def cache_miss(*args, **kwargs):
    """Called when C++ cache misses."""
    if config.no_tracing.value:
        raise RuntimeError(f"re-tracing function but 'no_tracing' is set")
    
    # 1. Trace Python function to jaxpr
    p, args_flat = _trace_for_jit(fun, jit_info, args, kwargs)
    
    # 2. Lower jaxpr to MLIR/StableHLO
    (outs, out_flat, out_tree, args_flat, jaxpr,
     executable, profiler, const_args) = _run_python_pjit(p, args_flat, fun, args, kwargs)
    
    # 3. Extract fast path data for C++ dispatch
    maybe_fastpath_data = _get_fastpath_data(
        executable, out_tree, args_flat, out_flat, 
        jaxpr.effects, jaxpr.consts, profiler, const_args)
    
    # 4. Return results + fastpath data + whether to rebuild with FDO
    return outs, maybe_fastpath_data, _need_to_rebuild_with_fdo(profiler)
```

---

## 5. Python Bindings and Dispatch

### 5.1 C++ Dispatch Mechanism

```python
# From jax/_src/pjit.py

def _cpp_pjit(fun: Callable, jit_info: PjitInfo):
    @api_boundary
    def cache_miss(*args, **kwargs):
        # Python-side cache miss handler
        p, args_flat = _trace_for_jit(fun, jit_info, args, kwargs)
        (outs, out_flat, out_tree, args_flat, jaxpr,
         executable, profiler, const_args) = _run_python_pjit(...)
        
        # Build fastpath data for future C++ dispatches
        maybe_fastpath_data = _get_fastpath_data(...)
        return outs, maybe_fastpath_data, _need_to_rebuild_with_fdo(profiler)
    
    # Create C++ JIT function
    cache_key = pxla.JitGlobalCppCacheKeys(...)
    cpp_pjit_f = xc._xla.pjit(
        fun_name(fun),           # Function name for debugging
        fun,                     # Original Python function
        cache_miss,              # Python callback on cache miss
        jit_info.static_argnums,
        jit_info.static_argnames,
        cache_key,               # Determines cache entry
        tree_util.dispatch_registry,  # For argument flattening
        pxla.cc_shard_arg,       # C++ argument sharding function
        _get_cpp_global_cache(cache_key.contains_explicit_attributes)
    )
    
    # Add methods to returned function object
    cpp_pjitted_f = wraps(fun)(cpp_pjit_f)
    cpp_pjitted_f._fun = fun
    cpp_pjitted_f._jit_info = jit_info
    cpp_jitted_f_class = type(cpp_pjitted_f)
    cpp_jitted_f_class.clear_cache = jit_evict_fn
    cpp_jitted_f_class.lower = jit_lower
    cpp_jitted_f_class.trace = jit_trace
    cpp_jitted_f_class.eval_shape = jit_eval_shape
    
    return cpp_pjitted_f
```

### 5.2 Fast Path Data Structure

```python
# From jax/_src/pxla.py

class MeshExecutableFastpathData:
    """Data needed for fast C++ dispatch without Python overhead."""
    xla_executable: xc.LoadedExecutable
    out_tree: PyTreeDef
    in_shardings: list[Sharding]
    out_shardings: list[Sharding]
    out_avals: list[core.ShapedArray]
    out_committed: list[bool]
    kept_var_bitvec: list[bool]  # Which inputs are used (for DCE)
    in_layouts: list[Layout | None]
    const_args: list[ArrayLike]  # Hoisted constants
```

---

## 6. Design Patterns Summary

### 6.1 Key Patterns Identified

| Pattern | Description | Application in JAX |
|---------|-------------|-------------------|
| **Staged Computation** | Separate tracing, lowering, compilation | Traced → Lowered → Compiled |
| **Abstract Interpretation** | Compute types/shapes without values | `abstract_eval` methods on primitives |
| **Partial Evaluation** | Evaluate known values, stage unknowns | `PartialVal` - (aval, None) or (None, const) |
| **Multi-Level Caching** | Cache at each transformation stage | C++ dispatch cache, lowering cache, trace cache |
| **Weak Reference Caching** | Allow GC of cached objects | `weakref_lru_cache` decorator |
| **Trace Context** | Thread-local tracing state | `trace_ctx` with stack of traces |
| **Primitive Registry** | Global registry of operations | `Primitive` class with `bind()` method |
| **PyTree** | Generic tree structure handling | Flatten/unflatten for args/returns |

### 6.2 JIT Flow Summary

```
User calls jitted function
        ↓
[C++ Cache Check] 
    Hit → Fast path execution (no Python overhead)
    Miss → cache_miss() in Python
                ↓
        [Trace Python Function]
            - Separate static/dynamic args
            - Create tracers for dynamic args
            - Execute function, intercept operations
            - Build jaxpr (intermediate representation)
                ↓
        [Lower to MLIR/StableHLO]
            - Resolve shardings/layouts
            - Convert jaxpr to MLIR
            - Cache lowering result
                ↓
        [Compile XLA Executable]
            - Call XLA compiler
            - Generate optimized executable
            - Cache compiled result
                ↓
        [Return] + [Store fastpath data for C++ cache]
```

---

## 7. Applicability to Hardware DSL JIT

### 7.1 Directly Applicable Patterns

#### 7.1.1 Staged Compilation Pipeline

```python
# Proposed CMT2 JIT pipeline modeled after JAX

class Cmt2Traced:
    """CMT2 circuit traced to MLIR."""
    mlir_module: ir.Module
    in_types: list[SignalType]
    out_types: list[SignalType]
    
    def lower(self, target: str = "verilog") -> Cmt2Lowered:
        """Lower to target representation."""
        if target == "verilog":
            return lower_to_verilog(self.mlir_module)
        elif target == "sim":
            return lower_to_sim(self.mlir_module)

class Cmt2Lowered:
    """Lowered CMT2 circuit."""
    representation: str  # Verilog, LLVM IR, etc.
    
    def compile(self) -> Cmt2Compiled:
        """Compile to executable."""
        if self.target == "verilog":
            return compile_with_verilator(self.representation)
        elif self.target == "sim":
            return compile_sim_executable(self.representation)

class Cmt2Compiled:
    """Compiled executable ready to run."""
    def __call__(self, *inputs) -> outputs:
        """Execute compiled circuit."""
        return self.executable.run(inputs)
```

#### 7.1.2 Parameter Specialization (static_argnums equivalent)

```python
def cmt2_jit(circuit_fn, *, static_params=None):
    """
    JIT compile CMT2 circuit with parameter specialization.
    
    static_params: Parameters to specialize at compile time
                   (e.g., bit widths, pipeline depths, module configs)
    """
    
    @cache  # Key = (circuit_fn, static_params, input_types)
    def compile_for_config(static_params, input_types):
        # Build circuit with static params baked in
        circuit = circuit_fn(**static_params)
        
        # Trace to MLIR
        mlir_module = trace_circuit(circuit, input_types)
        
        # Lower and compile
        return mlir_module.compile()
    
    def jitted_circuit(*inputs, **runtime_params):
        input_types = [infer_type(x) for x in inputs]
        compiled = compile_for_config(static_params, tuple(input_types))
        return compiled(*inputs, **runtime_params)
    
    return jitted_circuit

# Usage:
@cmt2_jit(static_params={"width": 32, "depth": 8})
def my_fifo(data, push, pop):
    return cmt2.fifo(data, push, pop, width=32, depth=8)
```

#### 7.1.3 Shape/Type Inference via Abstract Values

```python
class SignalType(AbstractValue):
    """Abstract type for hardware signals."""
    width: int
    signed: bool
    shape: tuple[int, ...]  # For vector/bundle types
    
class ClockType(AbstractValue):
    """Clock domain type."""
    domain: str

# Type inference for primitives
def add_abstract_eval(x: SignalType, y: SignalType) -> SignalType:
    """Infer output type of add operation."""
    out_width = max(x.width, y.width) + 1  # For carry
    return SignalType(width=out_width, signed=x.signed or y.signed)

def slice_abstract_eval(x: SignalType, start: int, end: int) -> SignalType:
    """Infer output type of slice operation."""
    return SignalType(width=end-start, signed=x.signed)
```

#### 7.1.4 Multi-Level Caching

```python
# CMT2 caching strategy modeled after JAX

# Level 1: Circuit configuration cache (static_params -> circuit structure)
@lru_cache(maxsize=1024)
def build_circuit_cache(fn_id, static_params):
    """Cache circuit structure for given static parameters."""
    return build_circuit(fn_id, static_params)

# Level 2: Trace cache (circuit + input_types -> mlir)
@weakref_lru_cache
def trace_circuit_cache(circuit_id, input_types):
    """Cache traced MLIR for circuit + input type signature."""
    return trace_to_mlir(circuit_id, input_types)

# Level 3: Lowering cache (mlir + target -> lowered)
@weakref_lru_cache  
def lower_circuit_cache(mlir_module, target, options):
    """Cache lowered representation."""
    return lower_to_target(mlir_module, target, options)

# Level 4: Compilation cache (lowered -> executable)
@lru_cache(maxsize=256)
def compile_circuit_cache(lowered_repr, compiler_options):
    """Cache compiled executable."""
    return compile_to_executable(lowered_repr, compiler_options)
```

### 7.2 Hardware-Specific Adaptations

#### 7.2.1 Clock Domain Crossing (CDC) Tracking

```python
class SignalType(AbstractValue):
    width: int
    clock_domain: ClockDomain  # Added for hardware
    
def cdc_abstract_eval(src: SignalType, dst: SignalType) -> SignalType:
    """Abstract eval for clock domain crossing."""
    if src.clock_domain == dst.clock_domain:
        return src  # No CDC needed
    # CDC creates new signal in destination domain
    return SignalType(width=src.width, clock_domain=dst.clock_domain)
```

#### 7.2.2 Resource Estimation During Tracing

```python
class ResourceCounter:
    """Track resource usage during tracing."""
    lut_count: int
    ff_count: int
    dsp_count: int
    bram_count: int

def track_resources(primitive, input_avals, output_avals):
    """Estimate resources for primitive operation."""
    if primitive.name == "mul":
        width = input_avals[0].width
        # Estimate DSP usage based on width
        dsp_count = (width + 17) // 18  # DSP48 can do 18x18
        return ResourceCounter(dsp_count=dsp_count)
```

### 7.3 Implementation Recommendations

| JAX Feature | CMT2 Equivalent | Priority |
|-------------|-----------------|----------|
| `jit()` decorator | `@cmt2.jit(static_params=...)` | High |
| `Traced/Lowered/Compiled` stages | MLIR trace → Verilog/LLVM lower → Sim/FPGA exec | High |
| `static_argnums` | `static_params` dict for compile-time constants | High |
| `abstract_eval` | Signal type inference with width/shape | High |
| `weakref_lru_cache` | Multi-level caching for circuit configs | Medium |
| C++ dispatch | C++ pybind for fast simulation dispatch | Medium |
| `vmap` | Vectorization for multi-module instantiation | Low |
| `grad` | Formal verification property extraction | Future |

---

## 8. Key Files Reference

| File | Purpose |
|------|---------|
| `jax/_src/api.py` | Public JIT API (`jax.jit`) |
| `jax/_src/pjit.py` | Core JIT implementation, `PjitInfo`, `_cpp_pjit` |
| `jax/_src/stages.py` | `Traced`, `Lowered`, `Compiled` stage classes |
| `jax/_src/core.py` | `Tracer`, `Trace`, `Primitive`, `AbstractValue` base classes |
| `jax/_src/interpreters/partial_eval.py` | `trace_to_jaxpr`, `JaxprTrace`, `DynamicJaxprTrace` |
| `jax/_src/interpreters/mlir.py` | MLIR lowering logic |
| `jax/_src/pxla.py` | Parallel execution and device placement |
| `jax/_src/dispatch.py` | Runtime dispatch and execution |
| `jax/_src/util.py` | `weakref_lru_cache`, `cache` decorators |

---

## 9. Conclusion

JAX's JIT design demonstrates several key principles applicable to hardware DSL compilation:

1. **Separation of concerns**: Tracing, lowering, and compilation are distinct stages with clear interfaces
2. **Aggressive caching**: Multiple cache levels with weak references enable both performance and memory efficiency
3. **Abstract interpretation**: Type/shape inference without concrete values enables ahead-of-time optimization
4. **Python-first UX**: Complex compilation pipeline hidden behind simple decorator API
5. **Extensibility**: Primitive-based design allows easy addition of new operations

For CMT2, the staged compilation model (Traced → Lowered → Compiled) with parameter specialization via `static_params` would provide the foundation for an effective JIT system. The key adaptation needed is replacing JAX's array-centric abstractions with hardware-specific types (signals, clocks, resets) and adding hardware-specific analyses (resource estimation, timing) to the compilation pipeline.
