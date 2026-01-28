# TileLang JIT Compilation Design Patterns Analysis

## Executive Summary

TileLang is a Python-embedded DSL for high-performance GPU kernel development with a sophisticated JIT compilation system. This document analyzes its design patterns to inform JIT strategies for hardware DSLs (particularly CMT2).

**Key Takeaway**: TileLang demonstrates a multi-layer JIT architecture with:
1. **Python DSL frontend** with dual-mode execution (eager/lazy)
2. **TVM-based IR** with custom Tile dialect
3. **Multi-phase lowering pipeline** with hardware-aware optimizations
4. **Pluggable backend adapters** for different execution targets

---

## 1. JIT Entry Point Architecture

### 1.1 Core Entry Points

```
tilelang/
├── __init__.py          # Exports: jit, compile, par_compile, JITKernel
├── jit/
│   ├── __init__.py      # JITImpl class (main decorator logic)
│   ├── kernel.py        # JITKernel class (compiled kernel wrapper)
│   └── adapter/         # Backend-specific execution adapters
└── language/eager/      # Eager JIT builder infrastructure
```

### 1.2 The `@tilelang.jit` Decorator

**File**: `tilelang/jit/__init__.py`

The JIT decorator uses a **two-phase dispatch pattern**:

```python
@dataclass
class JITImpl(Generic[_P, _KP, _T, _Ret]):
    # Configuration
    out_idx: list[int] | int | None          # Output tensor indices
    execution_backend: ExecutionBackend      # tvm_ffi, cython, nvrtc, torch, cutedsl
    target: str | Target                     # cuda, hip, llvm, etc.
    pass_configs: dict[str, Any]             # Transform pass configuration
    
    # Runtime state
    func: JITFunc                            # Wrapped function
    _kernel_cache: dict[tuple, Kernel]       # Compilation cache

    def __call__(self, *args, **kwargs) -> _Ret:
        # 1. Infer execution mode (lazy vs eager)
        if self.mode == "auto":
            self.mode = self._infer_jit_mode(*args, **kwargs)
        
        # 2. Parse arguments into cache key
        key, kernel_args = self.func.parse_args(*args, **kwargs)
        
        # 3. Check compilation cache
        kernel = self._kernel_cache.get(key, None)
        if kernel is None:
            kernel = self.compile(*args, **kwargs)
            self._kernel_cache[key] = kernel
        
        # 4. Execute (eager) or return kernel (lazy)
        if self.mode == "eager":
            return kernel(*kernel_args.values())
        else:
            return kernel
```

### 1.3 Dual Execution Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| **Lazy** | Function returns `PrimFunc` explicitly; kernel object returned for manual invocation | Library development, kernel inspection |
| **Eager** | Function uses builder pattern; kernel compiled and executed immediately | End-user scripts, immediate execution |

**Mode Inference** (`_infer_jit_mode`):
```python
def _infer_jit_mode(self, *args, **kwargs) -> Literal["lazy", "eager"]:
    # Try calling the function - if it returns a PrimFunc, it's lazy style
    try:
        prim_func = self.orig_func(*args, **kwargs)
        if isinstance(prim_func, PrimFunc):
            return "lazy"
    except (JITNoBuilderError, EagerJITBuildError):
        # These errors indicate eager-style features were used
        return "eager"
```

---

## 2. Python Frontend Design

### 2.1 Tile-Based DSL Structure

**File**: `tilelang/language/__init__.py`

TileLang exposes primitives organized by concern:

```python
# Core data types and storage
from .proxy import ptr, make_tensor, Buffer, Tensor, FragmentBuffer, SharedBuffer

# Control flow
from .loop import Parallel, Persistent, Pipelined, serial, unroll, vectorized
from .kernel import Kernel, KernelLaunchFrame

# Memory operations
from .allocate import alloc_var, alloc_local, alloc_shared, alloc_fragment
from .copy_op import copy, c2d_im2col

# Compute operations
from .gemm_op import gemm, gemm_v1, gemm_v2
from .reduce_op import reduce, reduce_max, reduce_sum, warp_reduce_sum

# Layout and optimization
from tilelang.layout import Layout, Fragment
from .annotations import use_swizzle, annotate_layout
```

### 2.2 Eager Builder Pattern

**File**: `tilelang/language/eager/builder.py`

The `Builder` class provides **AST generation through Python execution tracing**:

```python
class Builder(BaseBuilder):
    def __init__(self):
        self.frames: list[AnyFrame] = []           # Frame stack
        self.ir_builder = IRBuilder()               # TVM IR builder
        self.eager_jit: EagerJITStage = "none"      # phase1 / phase2 / none
        self.eager_jit_subs: dict[str, PrimExpr] = {}  # Constexpr substitutions
        
    @contextmanager
    def prim_func(self, name):
        """Create a PrimFunc context"""
        thread_local_storage.builder = self
        with self.ir_builder, self.with_frame(tir.prim_func()):
            tir.func_name(name)
            yield
            
    def bind(self, name, value, annot=BaseBuilder.empty):
        """Bind Python variables to TIR constructs"""
        # Handles: Buffer, Var, PrimExpr, Ref, etc.
        # Returns appropriate TIR representation
```

### 2.3 Tensor Type Annotations

**Eager Mode** uses Python type annotations for tensor shapes:

```python
@tilelang.jit
def kernel(A, B, C):
    M, N, K = T.const("M N K")  # Declare symbolic constants
    A: T.Tensor[[M, K], T.float32]  # Shape from symbolic
    B: T.Tensor[[K, N], T.float32]
    C: T.Tensor[[M, N], T.float32]
    
    with T.Kernel(T.ceildiv(N, 128), T.ceildiv(M, 128), threads=128) as (bx, by):
        # ... kernel body ...
        pass
```

**Key Design**: `T.const()` creates placeholder variables that are substituted with actual tensor dimensions during Phase 2 elaboration.

### 2.4 IR Generator (AST Transformation)

**File**: `tilelang/language/eager/ast.py`

Python functions are transformed into IR generators via AST mutation:

```python
def mutate(func: Callable) -> IRGenerator:
    """Transform a Python function into an IR generator"""
    source = inspect.getsource(func)
    tree = ast.parse(source)
    
    # Transform AST to use builder pattern
    transformer = IRTransformer()
    new_tree = transformer.visit(tree)
    
    # Compile to callable
    code = compile(new_tree, filename="<tilelang>", mode="exec")
    return IRGenerator(code, source)
```

---

## 3. Compilation Flow

### 3.1 High-Level Flow

```
Python Function
       │
       ▼
┌─────────────────┐
│  @tilelang.jit  │  Decorator wraps function
│    (JITImpl)    │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
Lazy Mode  Eager Mode
    │         │
    ▼         ▼
PrimFunc   Builder.prim_func()
    │         │
    └────┬────┘
         ▼
┌─────────────────┐
│   tilelang.lower │  Core lowering pipeline
│   (engine/lower.py)│
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Phase 1: Lower │  LowerAndLegalize
│  & Legalize     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Phase 2: Opt   │  OptimizeForTarget
│  for Target     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Host/Device    │  SplitHostDevice
│  Split          │
└────────┬────────┘
         │
    ┌────┴────┐
    ▼         ▼
Host Mod   Device Mod
    │         │
    │    ┌────┴────┐
    │    ▼         ▼
    │  LLVM      CUDA/HIP
    │  codegen    codegen
    │    │         │
    └────┴────┬────┘
              ▼
      CompiledArtifact
              │
              ▼
      KernelAdapter (tvm_ffi/cython/nvrtc/torch)
              │
              ▼
      JITKernel (callable)
```

### 3.2 Lowering Pipeline Phases

**File**: `tilelang/engine/phase.py`

#### Phase 1: LowerAndLegalize

```python
def LowerAndLegalize(mod: IRModule, target: Target) -> IRModule:
    mod = tir.transform.BindTarget(target)(mod)
    
    # Transform passes in order:
    mod = tilelang.transform.LetInline()(mod)              # Inline let bindings
    mod = tilelang.transform.AddWrapperForSingleBufStore()(mod)
    mod = tilelang.transform.LegalizeNegativeIndex()(mod)  # Normalize indices
    mod = tilelang.transform.VerifyParallelLoop()(mod)     # Race detection
    mod = tilelang.transform.InjectAssumes()(mod)          # Bounds hints
    mod = tilelang.transform.Simplify()(mod)               # Expression simplification
    mod = tilelang.transform.LayoutReducer()(mod)          # Reducer layouts
    mod = tilelang.transform.LayoutInference()(mod)        # Fragment layouts
    mod = tilelang.transform.LowerTileOp()(mod)            # Lower tile primitives
    mod = tilelang.transform.LowerL2Persistent()(mod)      # L2 cache hints
    mod = tilelang.transform.LegalizeVectorizedLoop()(mod) # Vectorization
    mod = tilelang.transform.LegalizeSafeMemoryAccess()(mod)  # Bounds checks
    mod = tilelang.transform.Simplify()(mod)
    mod = tilelang.transform.HoistNonRestrictParams()(mod)
    return mod
```

#### Phase 2: OptimizeForTarget

```python
def OptimizeForTarget(mod: IRModule, target: Target) -> IRModule:
    mod = tilelang.transform.LowerSharedBarrier()(mod)
    mod = tilelang.transform.LowerSharedTmem()(mod)
    
    # Conditional based on target capabilities
    if allow_tma_and_warp_specialized(target):
        mod = tilelang.transform.IfStmtBinding()(mod)
        mod = tilelang.transform.MultiVersionBuffer()(mod)
        mod = tilelang.transform.WarpSpecialized()(mod)
        mod = tilelang.transform.InjectTmaBarrier()(mod)
        mod = tilelang.transform.PipelinePlanning()(mod)
        mod = tilelang.transform.InjectSoftwarePipeline()(mod)
        mod = tilelang.transform.LowerOpaqueBlock()(mod)
        mod = tilelang.transform.RewriteWgmmaSync()(mod)  # Hopper-specific
    else:
        mod = tilelang.transform.PipelinePlanning()(mod)
        mod = tilelang.transform.InjectSoftwarePipeline()(mod)
    
    mod = tilelang.transform.FlattenBuffer()(mod)
    mod = tilelang.transform.ConfigIndexBitwidth()(mod)
    mod = tilelang.transform.VectorizeLoop()(mod)
    mod = tilelang.transform.StorageRewrite()(mod)
    mod = tilelang.transform.UnrollLoop()(mod)
    mod = tilelang.transform.LowerThreadAllreduce()(mod)
    mod = tilelang.transform.SplitHostDevice()(mod)
    mod = tilelang.transform.MergeSharedMemoryAllocations()(mod)
    mod = tilelang.transform.MakePackedAPI()(mod)
    return mod
```

### 3.3 Integration with TVM/MLIR

TileLang uses **TVM's TIR** (Tensor IR) as its intermediate representation:

```python
# From tilelang/engine/lower.py
def lower(
    func_or_mod: tir.PrimFunc | tvm.IRModule,
    target: str | Target = "auto",
    target_host: str | Target | None = None,
    enable_host_codegen=False,
    enable_device_compile=False,
) -> CompiledArtifact:
    
    # Convert PrimFunc to IRModule
    if isinstance(func_or_mod, tir.PrimFunc):
        mod = tvm.IRModule({func.attrs["global_symbol"]: func})
    
    # Determine target
    target = determine_target(target)
    
    # Run semantic checks
    PreLowerSemanticCheck(mod)
    
    # Phase 1 & 2
    mod = LowerAndLegalize(mod, target)
    mod = OptimizeForTarget(mod, target)
    
    # Split host and device
    host_mod = tir.transform.Filter(_is_host_call)(mod)
    device_mod = tir.transform.Filter(_is_device_call)(mod)
    
    # Code generation
    if enable_device_compile:
        device_mod = device_codegen(device_mod, target)
    
    return CompiledArtifact(host_mod, device_mod, params, kernel_source)
```

---

## 4. JIT Workflow for Tensor Operations

### 4.1 Tensor Shape Inference

**Two-Phase Elaboration** (Eager Mode):

```python
class JITFunc:
    """
    Phase 1: Build TIR template with symbolic dimensions
    Phase 2: Substitute actual dimensions from tensor arguments
    """
    
    def _build_tir_template(self, *args, **kwargs) -> TirTemplate:
        if self.mode == "lazy":
            # Direct PrimFunc return
            return TirTemplate.from_lazy_style(...)
        elif self.mode == "eager":
            # Trace through builder
            builder = Builder()
            builder.eager_jit = "phase1"
            with builder.prim_func(self.orig_func.__name__):
                self.ir_gen.gen(builder)(**self.tensor_args, **kwargs)
            pf = builder.get()
            return TirTemplate.create(name, pf, builder.constexpr_var, self.ir_gen)

class TirTemplate:
    """Template for shape-specialized kernel generation"""
    
    def get_tir(self, tensor_args, given_tensor_args, kwargs):
        # Phase 2: Substitute actual values
        values = self._parse_phase2_key(**given_tensor_args, **kwargs)
        subs = {name.orig_name: value for name, value in zip(self.matcher, values)}
        
        # Rebuild with actual dimensions
        builder = Builder()
        builder.eager_jit = "phase2"
        builder.eager_jit_subs = subs
        with builder.prim_func(self.name):
            self.ir_gen.gen(builder)(**tensor_args, **kwargs)
        return builder.get()
```

### 4.2 Kernel Specialization

**Key mechanism**: `constexpr` variables are matched to tensor shapes:

```python
class TirTemplate:
    @classmethod
    def create(cls, name, prim_func, constexpr, ir_gen):
        # Map constexpr variables to buffer shape/stride positions
        matcher = {}
        for k, v in prim_func.buffer_map.items():
            for i, s in enumerate(v.shape):
                if s in constexpr:
                    matcher[s] = (k.name, "shape", i, s.name)
        return cls(name, prim_func, matcher, constexpr)
```

### 4.3 Compilation Cache

```python
class JITImpl:
    def __init__(self):
        self._kernel_cache: dict[tuple, Kernel] = {}
    
    def __call__(self, *args, **kwargs):
        key, kernel_args = self.func.parse_args(*args, **kwargs)
        
        kernel = self._kernel_cache.get(key, None)
        if kernel is None:
            kernel = self.compile(*args, **kwargs)
            self._kernel_cache[key] = kernel
        
        return kernel(*kernel_args.values()) if self.mode == "eager" else kernel
```

---

## 5. Lowering Strategy

### 5.1 Tile Abstraction Lowering

**File**: `src/transform/lower_tile_op.cc`

Tile operators are lowered through a **visitor pattern**:

```cpp
class LowerTileOpPass : arith::IRMutatorWithAnalyzer {
public:
    static PrimFunc Substitute(PrimFunc f) {
        arith::Analyzer analyzer;
        LowerTileOpPass substituter(&analyzer);
        
        // Build buffer mappings
        for (const auto& [param_var, buffer] : f->buffer_map) {
            substituter.buffer_map_[param_var] = buffer;
            substituter.buffer_map_[buffer->data] = buffer;
        }
        
        // Visit and transform
        f.CopyOnWrite()->body = substituter.VisitStmt(f->body);
        return f;
    }
    
    // Handle tile operators in Evaluate nodes
    Stmt VisitStmt_(const EvaluateNode* op) final {
        auto tile_op = ParseOperator(GetRef<Stmt>(op));
        if (!tile_op.defined())
            return IRMutatorWithAnalyzer::VisitStmt_(op);
        
        // Lower the tile operator
        auto lowered = tile_op->Lower(LowerArgs{...}, analyzer_);
        return IRMutatorWithAnalyzer::VisitStmt(lowered);
    }
};
```

### 5.2 Layout Inference

**File**: `src/transform/layout_inference.cc`

Layout inference uses a **worklist algorithm**:

```cpp
class LayoutInferenceResult {
    Map<Buffer, Layout> layout_map;
    Map<For, Fragment> for_map;
    Map<For, PrimExpr> predicate_map;
};

class BufferUseDefCollector : public IRVisitorWithAnalyzer {
    void RunInferStep(int cur_infer_id, InferLevel level, 
                      LayoutMap& layout_map,
                      std::deque<int>& worklist) {
        // Get inferer for this operation
        auto& inferer = infer_list_[cur_infer_id];
        
        // Infer layouts for outputs based on input layouts
        auto updates = inferer->InferLayout(LayoutInferArgs{
            target_, thread_bounds, layout_map, analyzer_, ...
        }, level);
        
        // Propagate to connected operations
        for (const auto& [buffer, layout] : updates) {
            if (layout_map.count(buffer)) {
                // Check consistency
            } else {
                layout_map.Set(buffer, layout);
                // Add users to worklist
                for (int idx : use_list_[buffer]) {
                    EnqueueWithPriority(idx, worklist, ...);
                }
            }
        }
    }
};
```

### 5.3 Backend Code Generation

**Target-specific codegen**:

```python
# From tilelang/engine/lower.py
def device_codegen(device_mod: tvm.IRModule, target: Target) -> tvm.IRModule:
    if target.kind.name == "cuda":
        global_func = "target.build.tilelang_" + 
                      ("cutedsl" if "cutedsl" in target.keys else "cuda")
        device_mod = tvm.ffi.get_global_func(global_func)(device_mod, target)
    elif target.kind.name == "hip":
        device_mod = tvm.ffi.get_global_func("target.build.tilelang_hip")(...)
    elif target.kind.name == "metal":
        device_mod = tvm.ffi.get_global_func("target.build.metal")(...)
    return device_mod
```

### 5.4 Execution Backend Adapters

**File**: `tilelang/jit/adapter/`

| Adapter | Use Case | Key Feature |
|---------|----------|-------------|
| `TVMFFIKernelAdapter` | Development, debugging | Full TVM runtime integration |
| `CythonKernelAdapter` | Production CUDA | Cython wrapper for minimal overhead |
| `NVRTCKernelAdapter` | Runtime compilation | NVRTC for dynamic compilation |
| `MetalKernelAdapter` | Apple Silicon | PyTorch Metal backend |
| `CuTeDSLKernelAdapter` | NVIDIA Deep Learning | CUTLASS/CuTe integration |

**Base adapter pattern**:

```python
class BaseKernelAdapter(ABC):
    def __init__(self, mod, params: list[KernelParam], result_idx: list[int]):
        self.mod = mod
        self.params = params
        self.result_idx = self._legalize_result_idx(result_idx)
        self._post_init()
    
    @abstractmethod
    def _convert_torch_func(self) -> callable:
        """Convert to PyTorch-compatible function"""
        pass
    
    def _post_init(self):
        self.func = self._convert_torch_func()
```

---

## 6. Design Patterns Summary

### 6.1 Key Classes and Responsibilities

```
┌─────────────────────────────────────────────────────────────────┐
│                         JIT ENTRY POINT                          │
├─────────────────────────────────────────────────────────────────┤
│  JITImpl          │  Main decorator, mode inference, caching    │
│  JITFunc          │  Function wrapper, two-phase elaboration    │
│  TirTemplate      │  Shape-specialized kernel template          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     PYTHON FRONTEND DSL                          │
├─────────────────────────────────────────────────────────────────┤
│  Builder          │  IR construction via Python tracing         │
│  IRGenerator      │  AST transformation for eager mode          │
│  KernelLaunchFrame│  GPU grid/block configuration               │
│  *Proxy classes   │  Tensor, Buffer, Fragment abstractions      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    COMPILATION ENGINE                            │
├─────────────────────────────────────────────────────────────────┤
│  lower()          │  Main entry, orchestrates lowering          │
│  LowerAndLegalize │  Phase 1: Dialect lowering                  │
│  OptimizeForTarget│  Phase 2: Target optimization               │
│  device_codegen   │  Target-specific code generation            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     TRANSFORM PASSES (C++)                       │
├─────────────────────────────────────────────────────────────────┤
│  LayoutInference  │  Fragment/shared memory layout inference    │
│  LowerTileOp      │  Tile primitive lowering                    │
│  InjectSoftwarePipeline│  Software pipelining                   │
│  WarpSpecialized  │  Warp specialization for Hopper             │
│  SplitHostDevice  │  Host/device code separation                │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      EXECUTION BACKENDS                          │
├─────────────────────────────────────────────────────────────────┤
│  TVMFFIKernelAdapter   │  TVM runtime integration               │
│  CythonKernelAdapter   │  Low-overhead CUDA execution           │
│  NVRTCKernelAdapter    │  Runtime NVRTC compilation             │
│  MetalKernelAdapter    │  Apple Metal support                   │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 JIT Flow Pseudocode

```python
# User defines kernel
@tilelang.jit(target="cuda")
def matmul(A, B, C):
    M, N, K = T.const("M N K")
    A: T.Tensor[[M, K], T.float16]
    B: T.Tensor[[K, N], T.float16]
    C: T.Tensor[[M, N], T.float16]
    
    with T.Kernel(...) as (bx, by):
        # ... tile operations ...
        T.gemm(A_shared, B_shared, C_local)

# Execution flow
matmul(a, b, c)  # a, b, c are PyTorch tensors
    │
    ├── JITImpl.__call__(a, b, c)
    │       │
    │       ├── _infer_jit_mode() → "eager"
    │       │
    │       ├── parse_args(a, b, c)
    │       │   └── JITFunc.parse_args()
    │       │       ├── _parse_phase1_key() → ((), {}, {}), {A:a, B:b, C:c}, {}
    │       │       └── _build_tir_template()
    │       │           └── Builder creates PrimFunc with symbolic M, N, K
    │       │
    │       ├── compile(*args)
    │       │   └── TirTemplate.get_tir()
    │       │       ├── _parse_phase2_key(A=a, B=b, C=c)
    │       │       │   └── Extract M=1024, N=1024, K=1024 from tensor shapes
    │       │       └── Rebuild PrimFunc with concrete dimensions
    │       │
    │       ├── tilelang.lower(prim_func, target="cuda")
    │       │   ├── LowerAndLegalize()      # Phase 1
    │       │   ├── OptimizeForTarget()     # Phase 2
    │       │   ├── device_codegen()        # Generate CUDA
    │       │   └── CompiledArtifact
    │       │
    │       └── Create adapter (TVMFFI/Cython/NVRTC)
    │
    └── Execute kernel via adapter.func(a, b, c)
```

### 6.3 Design Patterns Used

| Pattern | Application |
|---------|-------------|
| **Decorator** | `@tilelang.jit` wraps functions with JIT compilation |
| **Strategy** | Pluggable `BaseKernelAdapter` for different backends |
| **Builder** | `Builder` class constructs IR via Python tracing |
| **Template Method** | `TirTemplate` for shape-specialized kernels |
| **Visitor** | TIR transform passes (IRMutatorWithAnalyzer) |
| **Worklist Algorithm** | Layout inference propagation |
| **Factory** | Adapter creation based on execution_backend |
| **Context Manager** | Frame-based IR construction (`with T.Kernel(...)`) |
| **Cache** | Compilation cache keyed by (config, shapes) |

---

## 7. Applicability for Hardware DSL JIT

### 7.1 Applicable Patterns for CMT2

| TileLang Feature | CMT2 Applicability | Notes |
|------------------|-------------------|-------|
| Dual-mode JIT (eager/lazy) | **High** | Useful for both REPL-style and library development |
| Two-phase elaboration | **High** | Dynamic shape handling for hardware parameters |
| Builder pattern | **High** | Python DSL construction for CMT2 IR |
| Layout inference | **Medium** | Hardware layout inference for pipeline stages |
| Transform pass pipeline | **High** | Adaptable to FIRRTL/Verilog lowering |
| Pluggable backends | **High** | Essential for simulation vs. synthesis |
| Cache keyed by shapes | **High** | Kernel specialization for different configurations |

### 7.2 Adaptations Needed

```python
# Example: CMT2-inspired JIT structure
@cmt2.jit
def counter_module(width: int):
    """JIT-decorated hardware module generator"""
    
    class Counter(Cmt2Module):
        # Type annotations for port shapes
        clk: Cmt2.Clock
        rst: Cmt2.Reset
        count: Cmt2.Output[Cmt2.UInt[width]]
        
        def __init__(self):
            self.reg = Cmt2.Reg(Cmt2.UInt[width], init=0)
        
        @Cmt2.Rule
        def increment(self):
            with Cmt2.when(self.rst):
                self.reg <<= 0
            with Cmt2.otherwise():
                self.reg <<= self.reg + 1
            
        @Cmt2.Method
        def read(self) -> Cmt2.UInt[width]:
            return self.reg
```

### 7.3 Key Implementation Recommendations

1. **Use MLIR Python Bindings**: Like TileLang uses TVM, CMT2 should use MLIR's Python API for IR construction

2. **Two-Phase Elaboration**: Support both static compile-time parameters and runtime shape inference

3. **Decorator with Mode Inference**: Automatically detect lazy (returns Module) vs eager (builder pattern)

4. **Pluggable Execution**: 
   - Interpreter adapter for simulation
   - Verilator adapter for fast simulation  
   - FIRRTL/SV codegen for synthesis

5. **Transform Pipeline**: Structure lowering as explicit phases:
   - GAA semantics lowering
   - Rule scheduling
   - Interface lowering
   - FIRRTL/Verilog emission

---

## 8. References

- **TileLang Repository**: `references/tilelang/`
- **Key Files Analyzed**:
  - `tilelang/jit/__init__.py` - JIT decorator and JITImpl
  - `tilelang/jit/kernel.py` - JITKernel and adapter selection
  - `tilelang/language/eager/builder.py` - IR builder infrastructure
  - `tilelang/engine/lower.py` - Core lowering pipeline
  - `tilelang/engine/phase.py` - Transform pass orchestration
  - `src/transform/layout_inference.cc` - C++ layout inference
  - `src/transform/lower_tile_op.cc` - Tile operator lowering
