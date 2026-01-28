# Triton JIT Compilation Design Patterns Analysis

This document analyzes the JIT compilation design patterns used in Triton (OpenAI's GPU kernel compiler), focusing on aspects relevant to designing a hardware DSL JIT system.

## Overview

Triton is a Python-based DSL for writing high-performance GPU kernels. It uses a sophisticated JIT compilation system that:
1. Decorates Python functions with `@triton.jit`
2. Captures Python AST at runtime
3. Lowers AST to Triton IR (MLIR-based)
4. Applies backend-specific optimizations
5. Generates GPU binary code (CUDA/ROCm)
6. Caches compiled kernels for reuse

---

## 1. JIT Entry Point

### 1.1 The `@triton.jit` Decorator

**File:** `python/triton/runtime/jit.py`

The decorator is the primary user-facing API. It wraps Python functions for JIT compilation:

```python
@overload
def jit(fn: T) -> JITFunction[T]: ...

def jit(fn: Optional[T] = None, *, version=None, ...):
    def decorator(fn: T) -> JITFunction[T]:
        assert callable(fn)
        if knobs.runtime.interpret:
            from .interpreter import InterpretedFunction
            return InterpretedFunction(fn, ...)
        else:
            return JITFunction(fn, ...)
    
    if fn is not None:
        return decorator(fn)
    else:
        return decorator
```

**Key Design Patterns:**
- **Overload-based decorator**: Supports both `@jit` and `@jit(...)` syntaxes
- **Lazy initialization**: Returns `JITFunction` wrapper, actual compilation happens on first call
- **Interpreter mode fallback**: Can switch to interpreted execution for debugging

### 1.2 Core Classes

#### `JITCallable` (Base Class)

```python
class JITCallable:
    def __init__(self, fn):
        self.fn = fn
        self.signature = inspect.signature(fn)
        self.raw_src, self.starting_line_number = inspect.getsourcelines(fn)
        self._fn_name = get_full_name(fn)
        
        # Extract source code without decorators
        src = textwrap.dedent("".join(self.raw_src))
        src = src[re.search(r"^def\s+\w+\s*\(", src, re.MULTILINE).start():]
        self._src = src
        self.hash = None
```

**Responsibilities:**
- Source code extraction using `inspect` module
- Cache key computation based on source hash
- AST parsing on demand
- Global variable tracking for invalidation

#### `JITFunction` (Main Class)

```python
class JITFunction(JITCallable, KernelInterface[T]):
    def __init__(self, fn, version=None, do_not_specialize=None, ...):
        super().__init__(fn)
        
        # Parameter metadata
        self.params = []
        for i, param in enumerate(self.signature.parameters.values()):
            dns = i in do_not_specialize or param.name in do_not_specialize
            self.params.append(KernelParam(i, param, dns, dns_oa))
        
        # Per-device cache (keyed by device)
        self.device_caches = defaultdict(self.create_binder)
```

**Key Features:**
- **Per-device caching**: Different compilation caches per GPU device
- **Parameter specialization**: Controls which parameters affect cache key
- **Grid-based invocation**: `fn[grid](*args)` pattern for kernel launch

#### `KernelParam` (Parameter Metadata)

```python
class KernelParam:
    def __init__(self, num: int, param: inspect.Parameter, 
                 do_not_specialize: bool, do_not_specialize_on_alignment: bool):
        self.num = num
        self._param = param
        self.do_not_specialize = do_not_specialize
        self.do_not_specialize_on_alignment = do_not_specialize_on_alignment

    @cached_property
    def is_constexpr(self):
        return "constexpr" in self.annotation

    @cached_property
    def is_const(self):
        if self.is_constexpr:
            return False
        return "const" in self.annotation or self.annotation.startswith("*k")
```

**Purpose:** Tracks parameter properties for specialization decisions.

---

## 2. Python DSL Design

### 2.1 Tensor Programming Abstractions

**File:** `python/triton/language/core.py`

Triton defines core abstractions for GPU tensor programming:

#### Type Hierarchy

```python
class base_type:
    """Base class for all Triton types"""
    def _flatten_ir_types(self, builder, out): ...
    def _unflatten_ir(self, handles, cursor): ...
    def mangle(self) -> str: ...

class dtype(base_type):
    """Scalar types (int8, fp16, etc.)"""
    SINT_TYPES = ['int8', 'int16', 'int32', 'int64']
    UINT_TYPES = ['int1', 'uint8', 'uint16', 'uint32', 'uint64']
    FP_TYPES = ['fp8e4b15', 'fp8e4nv', ..., 'fp16', 'bf16', 'fp32', 'fp64']

class pointer_type(dtype):
    """Pointer types with address space"""
    def __init__(self, element_ty: dtype, address_space: int = 1, const: bool = False)

class block_type(dtype):
    """Blocked/tensor types for SIMD operations"""
    def __init__(self, element_ty: dtype, shape: List)
```

#### `constexpr` for Compile-Time Values

```python
class constexpr:
    """Values known at compile-time"""
    def __init__(self, value):
        self.value = value
        self.type = constexpr_type(value)
    
    # Arithmetic operators return constexpr
    def __add__(self, other):
        return constexpr(self.value + _unwrap_if_constexpr(other))
```

#### `tensor` - Core Value Type

```python
class tensor(base_value):
    """Represents a tensor/block value in the IR"""
    def __init__(self, handle, type):
        self.handle = handle  # MLIR value handle
        self.type = type      # block_type or dtype
    
    # Operator overloading for arithmetic
    def __add__(self, other, _semantic=None):
        return _semantic.add(self, other)
```

### 2.2 DSL Builtins

**File:** `python/triton/language/core.py`

Functions marked with `@builtin` decorator:

```python
def builtin(fn: T) -> T:
    """Mark a function as a builtin."""
    @wraps(fn)
    def wrapper(*args, **kwargs):
        if "_semantic" not in kwargs or kwargs["_semantic"] is None:
            raise ValueError("Did you forget to add @triton.jit ?")
        return fn(*args, **kwargs)
    
    setattr(wrapper, TRITON_BUILTIN, True)
    return wrapper

@builtin
def load(ptr, mask=None, other=None, _semantic=None):
    return _semantic.load(ptr, mask, other)

@builtin
def store(ptr, value, mask=None, _semantic=None):
    return _semantic.store(ptr, value, mask)
```

**Key Pattern:** The `_semantic` parameter is injected by the code generator to provide backend-specific implementations.

---

## 3. Compilation Flow

### 3.1 AST to Triton IR Conversion

**File:** `python/triton/compiler/code_generator.py`

#### Entry Point: `ast_to_ttir`

```python
def ast_to_ttir(fn, src, context, options, codegen_fns, module_map, module=None):
    # Convert signature strings to types
    arg_types = [None] * len(fn.arg_names)
    for k, v in src.signature.items():
        idx = fn.arg_names.index(k)
        arg_types[idx] = str_to_ty(v, None)
    
    # Apply constexpr types
    for path, value in src.constants.items():
        apply_constexpr_types(arg_types, list(path)[::-1], value)
    
    # Create function prototype
    prototype = ASTFunction([], arg_types, src.attrs)
    
    # Generate code
    generator = CodeGenerator(
        context, prototype, 
        gscope=fn.get_capture_scope(),
        function_name=fn.repr(proxy),
        jit_fn=fn, is_kernel=True,
        options=options,
        codegen_fns=codegen_fns,
        module_map=module_map,
        module=module
    )
    generator.visit(fn.parse())
    
    # Verify and return
    module = generator.module
    if not module.verify():
        raise RuntimeError("error encountered during parsing")
    return module
```

#### `CodeGenerator` Class (AST Visitor)

```python
class CodeGenerator(ast.NodeVisitor):
    def __init__(self, context, prototype, gscope, function_name, 
                 jit_fn, options, codegen_fns, module_map, ...):
        # Builder setup
        self.builder = ir.builder(context)
        self.semantic = TritonSemantic(self.builder)
        
        # Scope management
        self.gscope = gscope  # Global scope
        self.lscope = {}      # Local scope
        self.local_defs = {}  # SSA definitions
        
        # Function state
        self.prototype = prototype
        self.function_name = function_name
        self.module = module
        
    # AST Visitor methods
    def visit_FunctionDef(self, node): ...
    def visit_Assign(self, node): ...
    def visit_BinOp(self, node): ...
    def visit_If(self, node): ...
    def visit_For(self, node): ...
    def visit_While(self, node): ...
```

#### Key Visitor Patterns

**Binary Operations:**
```python
def visit_BinOp(self, node):
    lhs = self.visit(node.left)
    rhs = self.visit(node.right)
    method_name = self._method_name_for_bin_op.get(type(node.op))
    return self._apply_binary_method(node, method_name, lhs, rhs)

_method_name_for_bin_op = {
    ast.Add: '__add__',
    ast.Sub: '__sub__',
    ast.Mult: '__mul__',
    # ...
}
```

**Control Flow (If):**
```python
def visit_If(self, node):
    cond = self.visit(node.test)
    
    if _is_triton_tensor(cond):
        # Dynamic condition -> SCF if
        cond = cond.to(language.int1, _semantic=self.semantic)
        if ContainsReturnChecker(self.gscope).visit(node):
            self.visit_if_top_level(cond, node)
        else:
            self.visit_if_scf(cond, node)
    else:
        # Static condition -> constant fold
        cond = _unwrap_if_constexpr(cond)
        active_block = node.body if cond else node.orelse
        self.visit_compound_statement(active_block)
```

### 3.2 Compilation Pipeline

**File:** `python/triton/compiler/compiler.py`

```python
def compile(src, target=None, options=None, _env_vars=None):
    # Determine target and backend
    if target is None:
        target = driver.active.get_current_target()
    backend = make_backend(target)
    
    # Check cache
    key = get_cache_key(src, backend, options, env_vars=env_vars)
    hash = hashlib.sha256(key.encode("utf-8")).hexdigest()
    fn_cache_manager = get_cache_manager(hash)
    
    metadata_path = fn_cache_manager.get_group(metadata_filename)
    if metadata_path is not None:
        return CompiledKernel(src, metadata_group, hash)  # Cache hit
    
    # Define compilation stages
    stages = dict()
    backend.add_stages(stages, options, src.language)
    # stages = {
    #     "ttir": make_ttir,    # Triton IR
    #     "ttgir": make_ttgir,  # Triton GPU IR
    #     "llir": make_llir,    # LLVM IR
    #     "ptx": make_ptx,      # PTX assembly
    #     "cubin": make_cubin,  # Binary
    # }
    
    # Run pipeline
    module = src.make_ir(target, options, codegen_fns, module_map, context)
    
    for ext, compile_ir in stages.items():
        next_module = compile_ir(module, metadata)
        # Cache intermediate results
        metadata_group[ir_filename] = fn_cache_manager.put(next_module, ir_filename)
        module = next_module
    
    return CompiledKernel(src, metadata_group, hash)
```

### 3.3 Backend Stages (NVIDIA Example)

**File:** `third_party/nvidia/backend/compiler.py`

```python
class CUDABackend(BaseBackend):
    @staticmethod
    def make_ttir(mod, metadata, opt, capability):
        """Triton IR optimizations"""
        pm = ir.pass_manager(mod.context)
        pm.enable_debug()
        passes.common.add_inliner(pm)
        passes.ttir.add_rewrite_tensor_pointer(pm)
        passes.common.add_canonicalizer(pm)
        passes.ttir.add_combine(pm)
        pm.run(mod, 'make_ttir')
        return mod

    @staticmethod
    def make_ttgir(mod, metadata, opt, capability):
        """Convert to GPU IR and optimize"""
        pm = ir.pass_manager(mod.context)
        
        # Convert to TritonGPU
        passes.ttir.add_convert_to_ttgpuir(pm, f"cuda:{capability}", opt.num_warps, 32, opt.num_ctas)
        
        # GPU-specific optimizations
        passes.ttgpuir.add_coalesce(pm)
        passes.ttgpuir.add_accelerate_matmul(pm)
        passes.ttgpuir.add_pipeline(pm, opt.num_stages, dump_enabled)
        
        pm.run(mod, 'make_ttgir')
        return mod

    def make_llir(self, src, metadata, options, capability):
        """Convert to LLVM IR"""
        pm = ir.pass_manager(mod.context)
        
        passes.ttgpuir.add_allocate_shared_memory_nv(pm, capability, ptx_version)
        nvidia.passes.ttgpuir.add_to_llvmir(pm, capability, ptx_version)
        passes.common.add_canonicalizer(pm)
        passes.convert.add_nvvm_to_llvm(pm)
        
        pm.run(mod, 'make_llir')
        return mod
```

---

## 4. Caching Mechanism

### 4.1 Cache Key Construction

**File:** `python/triton/runtime/cache.py`

```python
def get_cache_key(src, backend, backend_options, env_vars):
    key = f"{triton_key()}-{src.hash()}-{backend.hash()}-{backend_options.hash()}-{str(sorted(env_vars.items()))}"
    return key

@functools.lru_cache()
def triton_key():
    """Hash of Triton installation (version + source files)"""
    contents = []
    # Frontend
    with open(__file__, "rb") as f:
        contents += [hashlib.sha256(f.read()).hexdigest()]
    # Compiler
    for lib in pkgutil.walk_packages([path], prefix=prefix):
        with open(lib.module_finder.find_spec(lib.name).origin, "rb") as f:
            contents += [hashlib.sha256(f.read()).hexdigest()]
    # Backend binary
    with open(os.path.join(TRITON_PATH, "_C", f"libtriton.{ext}"), "rb") as f:
        libtriton_hash.update(chunk)
    return f'{__version__}' + '-'.join(contents)
```

**Cache Key Components:**
1. **Triton version + source hash** (catches compiler changes)
2. **Source hash** (function source code)
3. **Backend hash** (target-specific compiler)
4. **Options hash** (compilation options)
5. **Environment variables** (env vars that affect compilation)

### 4.2 Source Hash (`JITCallable.cache_key`)

```python
@property
def cache_key(self) -> str:
    with self._hash_lock:
        if self.hash is not None:
            return self.hash
        
        # Compute hash from source + dependencies
        nonlocals = inspect.getclosurevars(self.fn).nonlocals
        dependencies_finder = DependenciesFinder(
            name=self._fn_name,
            globals=self.__globals__,
            nonlocals=nonlocals,
            src=self.src
        )
        dependencies_finder.visit(self.parse())
        
        self.hash = dependencies_finder.ret + str(self.starting_line_number)
        
        # Include constexpr values
        self.hash += str([(name, val) for (name, _), (val, _) 
                          in self.used_global_vals.items()
                          if isinstance(val, constexpr)])
        self.hash = hashlib.sha256(self.hash.encode("utf-8")).hexdigest()
    return self.hash
```

### 4.3 Cache Storage

```python
class FileCacheManager(CacheManager):
    def __init__(self, key, override=False, dump=False):
        self.key = key
        self.cache_dir = os.path.join(knobs.cache.dir, self.key)
        os.makedirs(self.cache_dir, exist_ok=True)

    def put(self, data, filename, binary=True) -> str:
        # Atomic write using temp file + rename
        rnd_id = str(uuid.uuid4())
        pid = os.getpid()
        temp_dir = os.path.join(self.cache_dir, f"tmp.pid_{pid}_{rnd_id}")
        os.makedirs(temp_dir, exist_ok=True)
        temp_path = os.path.join(temp_dir, filename)
        
        with open(temp_path, mode) as f:
            f.write(data)
        
        os.replace(temp_path, filepath)  # Atomic
        os.removedirs(temp_dir)
        return filepath

    def get_file(self, filename) -> Optional[str]:
        if self.has_file(filename):
            return self._make_path(filename)
        return None
```

---

## 5. Runtime Execution

### 5.1 Kernel Launch Flow

**File:** `python/triton/runtime/jit.py`

```python
class JITFunction(JITCallable, KernelInterface[T]):
    def run(self, *args, grid, warmup, **kwargs):
        # Get device and stream
        device = driver.active.get_current_device()
        stream = driver.active.get_current_stream(device)
        
        # Get cached binder for this device
        kernel_cache, kernel_key_cache, target, backend, binder = self.device_caches[device]
        
        # Bind arguments and compute specialization
        bound_args, specialization, options = binder(*args, **kwargs)
        
        # Compute cache key
        key = compute_cache_key(kernel_key_cache, specialization, options)
        kernel = kernel_cache.get(key, None)
        
        # Compile if not cached
        if kernel is None:
            options, signature, constexprs, attrs = self._pack_args(...)
            kernel = self._do_compile(key, signature, device, constexprs, options, attrs, warmup)
        
        # Launch
        if not warmup:
            grid_0, grid_1, grid_2 = grid
            kernel.run(grid_0, grid_1, grid_2, stream, 
                      kernel.function, kernel.packed_metadata, 
                      launch_metadata, *bound_args.values())
```

### 5.2 Compiled Kernel

**File:** `python/triton/compiler/compiler.py`

```python
class CompiledKernel:
    def __init__(self, src, metadata_group, hash):
        # Load metadata
        metadata_path = next(Path(p) for c, p in metadata_group.items() if c.endswith(".json"))
        metadata = json.loads(metadata_path.read_text())
        self.metadata = KernelMetadata(**metadata)
        
        # Load assembly files
        self.asm = AsmDict({
            file.suffix[1:]: file.read_bytes() if file.suffix[1:] == binary_ext else file.read_text()
            for file in asm_files
        })
        self.kernel = self.asm[binary_ext]  # Binary executable
        
        # Lazy initialization
        self.module = None
        self.function = None
        self._run = None

    def _init_handles(self):
        """Lazy loading of GPU handles"""
        if self.module is not None:
            return
        
        device = driver.active.get_current_device()
        
        # Create launcher
        self._run = driver.active.launcher_cls(self.src, self.metadata)
        
        # Load binary to GPU
        self.module, self.function, self.n_regs, self.n_spills, self.n_max_threads = \
            driver.active.utils.load_binary(
                self.name, self.kernel, self.metadata.shared, device
            )

    @property
    def run(self):
        if self._run is None:
            self._init_handles()
        return self._run
```

### 5.3 Driver Abstraction

**File:** `python/triton/backends/driver.py`, `python/triton/runtime/driver.py`

```python
class DriverBase(metaclass=ABCMeta):
    @classmethod
    @abstractmethod
    def is_active(self): ...

    @abstractmethod
    def get_current_target(self): ...

    @abstractmethod
    def get_current_device(self): ...

    @abstractmethod
    def get_current_stream(self, device): ...


class GPUDriver(DriverBase):
    def __init__(self):
        import torch
        self.get_device_capability = torch.cuda.get_device_capability
        self.get_current_stream = _cuda_getCurrentRawStream
        self.get_current_device = torch.cuda.current_device
```

---

## 6. Design Patterns Summary

### 6.1 Key Classes and Responsibilities

| Class | Responsibility | Key Method |
|-------|---------------|------------|
| `JITCallable` | Source code capture & hash computation | `cache_key` |
| `JITFunction` | Kernel lifecycle & caching | `run()` |
| `KernelParam` | Parameter metadata & specialization | `is_constexpr` |
| `CodeGenerator` | AST to IR conversion | `visit_*` |
| `BaseBackend` | Backend-specific compilation | `add_stages()` |
| `ASTSource` | Source code representation | `make_ir()` |
| `CompiledKernel` | Compiled binary management | `run` property |
| `FileCacheManager` | Persistent caching | `put()`, `get_file()` |

### 6.2 JIT Flow Pseudocode

```python
# 1. Decoration
@triton.jit
def kernel(x_ptr, BLOCK_SIZE: tl.constexpr):
    ...

# 2. Wrapper creation
kernel = JITFunction(kernel_fn)

# 3. Invocation (first call)
kernel[grid](x)
  
  # 3a. Argument binding & specialization
  bound_args, specialization, options = binder(*args, **kwargs)
  
  # 3b. Cache lookup
  key = compute_cache_key(specialization, options)
  if key in kernel_cache:
      return kernel_cache[key]
  
  # 3c. AST to IR
  module = ast_to_ttir(fn, src, context, options, ...)
  
  # 3d. Compilation pipeline
  for stage_name, stage_fn in stages.items():
      module = stage_fn(module, metadata, options)
      cache_manager.put(module, f"{name}.{stage_name}")
  
  # 3e. Create CompiledKernel
  kernel = CompiledKernel(src, metadata_group, hash)
  kernel_cache[key] = kernel
  
  # 3f. Launch
  kernel.run(grid, stream, function, *args)

# 4. Subsequent invocations (cache hit)
kernel[grid](x)  # Uses cached kernel, skips compilation
```

### 6.3 Design Patterns Used

| Pattern | Usage |
|---------|-------|
| **Decorator** | `@triton.jit` marks functions for JIT compilation |
| **Visitor** | `CodeGenerator` visits Python AST nodes |
| **Strategy** | Different `BaseBackend` implementations for CUDA/ROCm |
| **Factory** | `make_backend()` creates appropriate backend |
| **Lazy Initialization** | `CompiledKernel` delays GPU handle creation |
| **Cache** | `FileCacheManager` persists compiled binaries |
| **Template Method** | `BaseBackend.add_stages()` defines pipeline |
| **Builder** | `ir.builder` constructs MLIR operations |
| **Proxy** | `KernelInterface` provides launch interface |
| **Context Manager** | `enter_sub_region` manages SSA scopes |

---

## 7. Applicability for Hardware DSL JIT

### 7.1 Reusable Components

| Component | Reusability | Notes |
|-----------|-------------|-------|
| **AST Capture** | High | `inspect.getsourcelines()` + AST parsing |
| **Source Hashing** | High | Include source + global dependencies |
| **Cache Management** | High | File-based with atomic writes |
| **Per-Device Caching** | Medium | Adapt to target hardware model |
| **Parameter Specialization** | High | `constexpr` concept is general |
| **Pass Pipeline** | Medium | MLIR pass infrastructure reusable |

### 7.2 Adaptations Needed

| Aspect | Triton Approach | Hardware DSL Adaptation |
|--------|----------------|------------------------|
| **Target** | GPU (CUDA/ROCm) | Custom hardware backend |
| **IR** | Triton IR (MLIR) | CIRCT dialects (Cmt2) |
| **Types** | Tensors/blocks | Signals, registers, wires |
| **Execution** | Kernel launch | Hardware simulation/synthesis |
| **Scheduling** | Warps/CTAs | Rules, methods, pipelines |
| **Memory** | Shared/global | Registers, memories, FIFOs |

### 7.3 Recommended Architecture for CMT2 JIT

```python
# Proposed CMT2 JIT structure

class Cmt2JITFunction(JITCallable):
    """JIT-compiled CMT2 module"""
    
    def run(self, *args, **kwargs):
        # 1. Compute specialization from args
        # 2. Check cache
        # 3. If miss: compile via CIRCT
        # 4. Return compiled module/interpreter

@cmt2.jit
def my_module(input: Signal, SIZE: constexpr):
    # Python DSL code
    ...

# Usage
module = my_module(input, SIZE=16)  # Returns compiled CMT2 module
```

### 7.4 Key Implementation Considerations

1. **AST to MLIR**: Use `code_generator.py` pattern with CMT2 dialect builders
2. **Type System**: Map Python types to CMT2 types (signals, clocks, resets)
3. **Constexpr**: Use for hardware parameters (bitwidths, depths, etc.)
4. **Caching**: Adapt Triton's file cache for CIRCT compilation artifacts
5. **Backend**: Create CMT2 backend following `BaseBackend` pattern
6. **Simulation**: Replace GPU driver with PyCMT2 simulation backend

---

## References

- Triton Repository: `references/triton/`
- Key Files:
  - `python/triton/runtime/jit.py` - JIT infrastructure
  - `python/triton/compiler/code_generator.py` - AST to IR
  - `python/triton/compiler/compiler.py` - Compilation pipeline
  - `python/triton/runtime/cache.py` - Caching system
  - `python/triton/language/core.py` - DSL types and builtins
  - `python/triton/backends/compiler.py` - Backend abstraction
  - `third_party/nvidia/backend/compiler.py` - CUDA backend example
