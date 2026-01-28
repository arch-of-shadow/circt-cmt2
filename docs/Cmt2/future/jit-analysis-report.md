# JIT Compilation Design Patterns (JAX, Triton, TileLang) — Notes for a Cmt2 Python JIT

**Last updated:** 2026-01-27  
**Scope:** This report analyzes the JIT design patterns as they appear in the repository snapshots under `references/jax/`, `references/triton/`, and `references/tilelang/`. File paths below refer to those snapshots.

---

## 1) Executive Summary (Patterns Found)

Across JAX, Triton, and TileLang, a “Python JIT” is consistently implemented as:

- **A staging boundary**: user Python runs in a restricted “compile-time” mode and produces a stable IR (or IR-like graph) that becomes the unit of compilation (JAX: `jaxpr`; Triton: `ttir` MLIR module; TileLang: TVM `tir.PrimFunc` / `IRModule`).
- **Specialization on “static” information**: each call selects a compilation key derived from static parameters (shapes/dtypes, constexprs, options/targets). A “new” static configuration triggers compilation; dynamic values do not.
- **A multi-stage lowering pipeline**: frontend IR → progressively more concrete IR → target code. Each stage is a natural seam for debugging dumps, pass overrides, and caching.
- **A compiled handle with introspection**: the object returned by `jit` is not “just a function”; it is a wrapper that can expose tracing/lowering/compiled artifacts and supports cache management.
- **Caching as a first-class system**: in-memory caches for dispatch speed, plus disk caches for expensive compilation, with explicit invalidation inputs (tool versions, env vars, source hashes).

Key differences:

- **Tracing-by-execution (JAX)** vs **AST interpretation (Triton)** vs **builder-to-IR (TileLang)**.
- “Kernel JIT” (Triton/TileLang) assumes a single device function boundary and can forbid most Python dynamism; “program JIT” (JAX) supports richer composition but needs a more elaborate tracer system.

For **Cmt2**, the most transferable ideas are:

- Treat a **parameterized module/circuit** as the JIT unit (analogous to a kernel).
- Make **static parameters explicit** (widths, depths, protocol variants, schedule/timing knobs), and keep runtime objects as “signals”/ports.
- Provide **stage objects** (`trace → lower → emit`) and a cache keyed by **(function source + static params + toolchain/pass config)**.

---

## 2) Detailed Analysis Per Project

### 2.1 JAX (`references/jax/`)

#### How JIT compilation works (`jax.jit`)

- User entrypoint: `references/jax/jax/_src/api.py` defines `jit(...)` but delegates almost immediately to `pjit.make_jit(...)`.
- The “real” wrapper construction is in `references/jax/jax/_src/pjit.py`:
  - `make_jit(...)` parses and normalizes arguments (`static_argnums`, shardings/layouts, donation, compiler options) into a `PjitInfo`.
  - `_cpp_pjit(fun, jit_info)` constructs the callable using a C++ entrypoint `xc._xla.pjit(...)` and supplies a Python `cache_miss` callback for the slow path.

In other words: **Python owns argument normalization + tracing + compilation logic, but dispatch and caching fast paths are implemented in a C++ binding**.

#### Tracing mechanism (abstract values, shape inference)

Core staging idea: execute `fun(*args)` once, but with inputs replaced by **tracers** that carry **abstract values** (avals).

- Input abstraction:
  - `_infer_input_type(...)` in `references/jax/jax/_src/pjit.py` uses `core.shaped_abstractify(x)` to map runtime values to `core.AbstractValue` (e.g. `ShapedArray(shape, dtype, ...)`).
- Jaxpr tracing:
  - `pe.trace_to_jaxpr(...)` in `references/jax/jax/_src/interpreters/partial_eval.py` constructs a `DynamicJaxprTrace`, creates input tracers, executes the Python function, then converts the traced operations into a `ClosedJaxpr` (graph IR) plus output avals.

Key pattern: the tracer is **not** “an IR node”; it’s a runtime value that *behaves like* an array to user code but records primitive operations and performs shape/dtype propagation.

#### IR generation and lowering pipeline

The compilation unit is a `ClosedJaxpr` + metadata (shardings/layouts, donation, platforms).

Representative pipeline (names taken from the code paths referenced above):

1. **Python function** + **abstract inputs** → `ClosedJaxpr` (partial eval tracer)
2. `ClosedJaxpr` → **MLIR module**:
   - `mlir.lower_jaxpr_to_module(...)` in `references/jax/jax/_src/interpreters/mlir.py`
3. MLIR module → **backend executable** (XLA compile, including platform-specific details)
4. Executable + trees/metadata → callable wrapper

JAX also exposes “staging objects” (useful as a design pattern even if Cmt2 doesn’t mirror the API):

- `references/jax/jax/_src/stages.py` defines `Traced`, `Lowered`, and `Compiled` objects, each with introspection like `.as_text(...)`, `.compiler_ir(...)`, `.cost_analysis()`, and `.runtime_executable()`.

#### Python binding approach

Key integration boundary: **Python defines semantics and compilation policy, but the execution and fast dispatch are in a native runtime**.

- The `jit` wrapper uses a C++ binding `xc._xla.pjit(...)` (`references/jax/jax/_src/pjit.py`) that:
  - maintains a global cache of compiled callables, and
  - calls back into Python on cache misses (to trace + compile).
- `references/jax/jax/_src/api.py` also wires post-hooks into a native module via `jax_jit.set_post_hook_state(...)` (debug/nan-check plumbing).

This split is a recurring pattern in production JITs: *Python is “policy & IR construction”; native code is “runtime, caches, fast paths”*.

#### Caching and specialization

JAX caches at multiple layers, each with a different key:

- **Trace cache**: `pe.trace_to_jaxpr` is a `@weakref_lru_cache(...)` keyed (conceptually) by `(fun identity, input avals, debug info, extra context)`. This makes repeated tracing cheap and enables “re-trace only on new shapes/dtypes/statics.”
- **Callable cache**: `_cpp_pjit_cache_fun_only` / `_cpp_pjit_cache_explicit_attributes` in `references/jax/jax/_src/pjit.py` are global caches implemented on the C++ side (`xc._xla.PjitFunctionCache`).
- **Lowering/compile caches**: helper caches like `@util.cache(max_size=4096, ...)` for sharding/layout processing reduce overhead for repeated launches.

Notable tradeoff: caching is **weakref-aware** to avoid “leaking” user functions/graphs, but still aggressive enough to make the common case fast.

---

### 2.2 Triton (`references/triton/`)

#### JIT kernel compilation flow

User entrypoint: `@triton.jit` in `references/triton/python/triton/runtime/jit.py`.

The wrapper object is `JITFunction`, which is launched using the idiom:

```python
kernel[grid](*args, **kwargs)
```

Key runtime flow (from `JITFunction.run(...)` in `references/triton/python/triton/runtime/jit.py`):

1. Determine device/stream and parse backend options.
2. Create a **specialization signature** describing argument types and constexpr values.
3. Compute an **in-memory cache key** (`compute_cache_key(...)`) based on specialization + options.
4. On cache miss:
   - construct an `ASTSource` (compiler input),
   - compute a **disk cache key** (`get_cache_key(...)`),
   - run compilation pipeline, store artifacts, return a `CompiledKernel`.
5. Validate that “interesting” globals used during compilation have not changed.
6. Launch the compiled kernel via a driver interface.

Triton is a “kernel JIT” more than a general program JIT: it expects a single entry function that becomes a GPU kernel, and it intentionally restricts what Python features are allowed inside.

#### AST to MLIR conversion

Unlike JAX, Triton does not trace by executing Python with tracers. Instead it:

- extracts the function source (`inspect.getsourcelines(...)` in `JITCallable.__init__`),
- parses it into a Python AST (`JITCallable.parse()`),
- interprets that AST to build an MLIR module:
  - `ast_to_ttir(...)` in `references/triton/python/triton/compiler/code_generator.py` creates a `CodeGenerator(...)` and runs `generator.visit(fn.parse())`.

This produces a Triton-specific MLIR module (commonly referred to as TTIR). Subsequent compilation stages are target/backend-specific.

This AST-first strategy has two strong consequences:

- **Great control over allowed Python constructs** (and thus deterministic compilation).
- **More implementation complexity** (you are implementing a compiler frontend for a Python subset).

#### Python DSL design

The DSL is embedded in Python, but compilation is “syntax-directed”:

- You write normal-looking Python code using `triton.language` operations.
- The compiler frontend interprets the AST and lowers recognized constructs into IR.

Triton also uses a strong notion of **constexpr**:

- Kernel parameters can be annotated so that they participate in specialization and become compile-time constants.
- Specialization is also influenced by pointer alignment and backend attributes (see `KernelParam` and related specialization logic in `references/triton/python/triton/runtime/jit.py`).

#### IR generation and transformation pipeline

The compilation pipeline is explicitly staged in `references/triton/python/triton/compiler/compiler.py`:

- `ASTSource.make_ir(...)` calls `ast_to_ttir(...)` to build an MLIR module.
- A backend provides a stage map via `backend.add_stages(stages, options, src.language)` (see `references/triton/python/triton/backends/compiler.py` for the abstract contract).
- The compiler iterates stages sequentially, emitting/storing per-stage IR artifacts (`.ttir`, `.ttgir`, `.llir`, `.ptx`, `.cubin` / `.hsaco`, etc. depending on backend).

On the native side, the repository snapshot includes MLIR dialects and conversions under:

- `references/triton/lib/Dialect/*`
- `references/triton/lib/Conversion/*` (e.g. `TritonGPUToLLVM`)

#### Compilation cache mechanism

Triton uses both:

- **In-memory caching** per device (`self.device_caches` and `kernel_cache` in `JITFunction`).
- **Disk caching** keyed by:
  - a “Triton build key” that hashes the Triton Python frontend, backends, language modules, and `libtriton` binary (`triton_key()` in `references/triton/python/triton/runtime/cache.py`),
  - the kernel source/dependency hash (`src.hash()`), and
  - backend options + cache-invalidating env vars (`get_cache_key(...)`).

Disk caching stores a **group** of artifacts (IR dumps + metadata JSON + final binary) and uses atomic writes for robustness (`FileCacheManager.put(...)` in `references/triton/python/triton/runtime/cache.py`).

#### Python binding approach

Triton’s Python interacts with native code via `triton._C.libtriton` (see imports in `references/triton/python/triton/runtime/jit.py` and `references/triton/python/triton/compiler/compiler.py`), providing:

- an MLIR context + dialect loading (`ir.context()`, `ir.load_dialects(...)`, `ir.parse_mlir_module(...)`),
- IR building utilities used by the code generator, and
- runtime/driver integration to load and launch compiled binaries.

---

### 2.3 TileLang (`references/tilelang/`)

#### JIT workflow for tensor operations

User entrypoint: `@tilelang.jit` in `references/tilelang/tilelang/jit/__init__.py`.

TileLang supports two user-facing modes under a unified wrapper (`JITImpl`):

- **Lazy mode**: the decorated function returns a `PrimFunc` explicitly; calling the wrapper returns a compiled kernel object.
- **Eager mode**: the decorated function uses a builder-style DSL; calling the wrapper compiles *and executes* immediately.

Mode selection happens automatically:

- `JITImpl._infer_jit_mode(...)` checks whether the wrapped object is a `JITFunc` and whether it behaves “lazy style.”
- The wrapper caches compiled kernels in `_kernel_cache` keyed by parsed args (`func.parse_args(...)`).

#### Python frontend design

TileLang’s frontend is builder-first:

- The decorator wraps the function using `prim_func(func, eager_jit=True)` (see the `decorator(...)` inside `jit(...)`).
- The builder produces a TVM TIR `PrimFunc` / `IRModule` rather than a bespoke IR graph type.

This is closer to “structured IR building” (like an embedded DSL) than to JAX-style tracing or Triton-style AST interpretation.

#### Lowering to underlying compiler

TileLang’s compilation is built on TVM:

- `JITKernel._compile_and_create_adapter(...)` in `references/tilelang/tilelang/jit/kernel.py` calls `tilelang.lower(...)`.
- `tilelang.lower(...)` in `references/tilelang/tilelang/engine/lower.py`:
  - performs semantic checks and two major lowering phases (`PreLowerSemanticCheck`, `LowerAndLegalize`, `OptimizeForTarget`),
  - splits host and device code paths, and
  - invokes TVM build functions (via `tvm.ffi.get_global_func("target.build....")`) to produce device binaries / runtime modules.

The result is a `CompiledArtifact` plus an execution adapter (`TVMFFI`, `NVRTC`, `Cython`, `Torch`, `CuTeDSL`, etc.).

#### Caching and code generation strategies

TileLang has a fairly explicit persistent cache:

- High-level compile calls go through `tilelang.cache.cached(...)` (`references/tilelang/tilelang/cache/__init__.py`), which dispatches to a backend-specific cache object.
- The base cache logic is in `KernelCache` (`references/tilelang/tilelang/cache/kernel_cache.py`):
  - Cache keys include a hash of `PrimFunc.script(show_meta=True)` plus compilation arguments (targets, pass configs, flags) and library version info.
  - The cache stores multiple artifacts on disk (device kernel source, host wrapper source, compiled library, serialized params), using atomic writes for safety.
  - Caching can be disabled globally or per subsystem via environment variables (`references/tilelang/tilelang/env.py`).

#### Python binding approach

TileLang is “Python orchestration over a native compiler/runtime”:

- The package loads a TileLang shared library via `ctypes` (see `references/tilelang/tilelang/__init__.py`).
- Compilation and lowering leverage TVM’s Python API and FFI.
- Backend device compilation can call out to toolchains (e.g., `nvcc`) via registered callbacks (see `tilelang_callback_cuda_compile` in `references/tilelang/tilelang/engine/lower.py`).

---

## 3) Comparative Analysis

### 3.1 Staging strategy (how Python becomes IR)

| Project | Staging Mechanism | “Static” Inputs | Unit of Compilation |
|---|---|---|---|
| JAX | Trace-by-execution with tracers | avals (shape/dtype), static args, sharding/layouts, compiler opts | `ClosedJaxpr` (+ metadata) |
| Triton | Parse + interpret Python AST | argument types, constexprs, backend options, env vars | TTIR MLIR module (`.ttir`) |
| TileLang | Builder produces TVM TIR | PrimFunc script/meta + compile args | TVM `tir.PrimFunc` / `IRModule` |

Implication for Cmt2:

- If you want “normal Python” control flow and composability, you drift toward **tracing** (JAX pattern).
- If you want tight control and predictable kernels, you drift toward **AST / structured builder** (Triton/TileLang patterns).

### 3.2 IR pipeline and transformation seams

All three projects have a clear **multi-stage lowering** story, even if the IRs differ:

- A frontend IR that matches the Python DSL well (jaxpr / TTIR / TIR).
- One or more legalization/optimization phases.
- A codegen backend that produces a runtime artifact (executable/binary/library).

This is the critical takeaway for Cmt2: the JIT boundary should produce an IR that fits your semantics (rules/methods/values + scheduling metadata), and everything after that is “compiler territory.”

### 3.3 Python frontend patterns that scale

Common frontend patterns:

- **Explicit separation of static vs dynamic values**
  - JAX: `static_argnums/static_argnames` + aval abstraction for arrays.
  - Triton: `constexpr` parameters and type annotations drive specialization.
  - TileLang: builder variables and pass configs are compile-time; runtime tensors are execution-time.
- **User-visible compilation objects**
  - JAX stages (`Traced/Lowered/Compiled`), Triton `CompiledKernel`, TileLang `JITKernel`.
- **Debuggability hooks**
  - Disable JIT / export IR / dump compilation stages / override IR stages.
- **Environment-driven configuration with explicit invalidation**
  - Triton and TileLang in particular bake env vars into cache keys or use env vars to disable cache.

### 3.4 Caching strategies (what “good” looks like)

Shared “good cache” traits:

- Key includes **source hash** (plus transitive dependencies where possible).
- Key includes **compiler/runtime version** and **target options**.
- Writes are **atomic** and robust to crashes.
- Provides ways to:
  - disable cache for debugging,
  - inspect or dump artifacts, and
  - override stages for testing.

Key tradeoff: caching correctness vs convenience.

- Triton chooses to *error* if globals used in a kernel changed after compilation, rather than silently recompile.
- JAX chooses to re-trace/recompile when abstract inputs differ.

---

## 4) Recommendations for a Cmt2 Python JIT Design

### 4.1 Pick the compilation unit: “parameterized module/circuit” (kernel-like)

For Cmt2, the closest analog to Triton/TileLang kernels is:

- a **module generator** that takes static parameters (widths, depths, protocol variants, timing knobs) and returns a Cmt2 module (or circuit) definition.

This keeps the JIT’s scope aligned with:

- Cmt2’s scheduling semantics (often whole-module),
- the existing lowering pipeline (Cmt2 → FIRRTL/HW/SV), and
- realistic caching granularity (modules are reusable artifacts).

### 4.2 Make static parameters explicit and enforce purity

Recommended API sketch (conceptual):

- `@cmt2.jit(static_argnames=..., cache=..., target=..., passes=...)`
  - Static args: widths, depths, clocking variants, timing annotations, scheduling policies, and any Python objects that affect elaboration.
  - Dynamic args: runtime “signal” objects, ports, or external module instances (depending on how you model instantiation/execution).

Like JAX and Triton, enforce that jitted functions are “pure” with respect to compilation:

- avoid reading mutable globals (or track and include them in the cache key / validation),
- forbid non-deterministic name generation unless seeded and included in key.

### 4.3 Provide stage objects for introspection (borrow JAX’s ergonomics)

Even if the implementation is not JAX-like, the user experience benefits from JAX’s staging object idea:

- `.trace(...)` → returns a “Traced” object (Cmt2 IR + type/shape/width info)
- `.lower(...)` → returns a “Lowered” object (post-pass Cmt2 IR, or FIRRTL/HW IR)
- `.emit_verilog(...)` / `.emit_mlir(...)` → materializes artifacts

This is especially valuable for a hardware DSL where users frequently need:

- “why did this schedule?” explanations,
- pass-by-pass IR dumps, and
- reproducible codegen for debugging synthesis/sim issues.

### 4.4 Reuse existing Cmt2 pipelines as backend stages (borrow Triton’s “stages map”)

Cmt2 already has a clear lowering pipeline (see `docs/Cmt2/features/Lowering.md`). A Cmt2 JIT should treat that as “backend stages”:

1. Frontend elaboration: Python → Cmt2 IR
2. Cmt2 transforms (proc/dataflow/timing/scheduling-related)
3. Conversion to FIRRTL/HW/SV
4. Emit SV (+ ModuleLibrary RTL as needed)
5. (Optional) Build/run simulation workspace

Expose stage boundaries to:

- dump intermediate IR,
- override or inject passes,
- and cache at appropriate levels.

### 4.5 Implement a two-tier cache (fast in-memory + robust disk)

Suggested cache design:

- **In-memory**: keyed by `(function identity, static args, pass config, target)` for fast repeated use in a single process.
- **On-disk**: content-addressed store keyed by:
  - function source hash (plus dependency hashes, if you can compute them),
  - static args (serialized deterministically),
  - CIRCT/Cmt2 version + toolchain version,
  - pass pipeline config,
  - target (sim vs synth, backend selection).

Write artifacts atomically; store a single metadata JSON that points to:

- emitted SV,
- emitted MLIR snapshots (optional),
- pass logs,
- and any build products (e.g., Verilator outputs) if you choose to cache those.

### 4.6 Prefer “builder-first JIT” as an initial implementation

Given that Cmt2 already has PyCMT2/ECMT2 builder APIs, a pragmatic path is:

- Start with a **builder-first JIT** (TileLang pattern): the user function returns/builds a Cmt2 `Circuit`/`Module` object (or an IR module handle), and the JIT manages compilation + caching.
- Add tracing/AST approaches later if you need richer Python ergonomics.

This keeps complexity low while still providing:

- specialization + caching,
- stage dumps,
- and integration with the existing Cmt2 compiler pipeline.

---

## 5) Key Implementation Considerations (Cmt2-Specific)

### Static vs dynamic boundary

Decide and document what is “static” for hardware elaboration:

- widths/depths/layouts
- presence/absence of features (proc/dataflow/multicycle)
- scheduling policies and timing attributes
- external module bindings and interface choices

Everything else should be modeled as *signals/ports* inside the IR, not Python values.

### Naming, determinism, and reproducibility

Caching and debug diffs require stable naming:

- deterministic module instance names when possible,
- deterministic lowering pipelines,
- ability to “freeze” random/seeds if any exist (prefer: none).

### Source locations and error reporting

Both Triton and TileLang invest in mapping compilation errors back to user code (e.g., file/line capture in Triton’s `ast_to_ttir`).

For Cmt2, consider:

- attaching Python file/line metadata to MLIR ops/regions when building IR,
- surfacing verifier/pass errors with those locations.

### Pass configurability without API explosion

Avoid “a new Python keyword for every pass option.” Instead:

- accept a dictionary of pass configs (TileLang style),
- define a stable set of “profiles” (e.g., `debug`, `fast-sim`, `synth`) that map to pass pipelines.

### Concurrency and multi-process builds

If you cache on disk and users run multiple processes:

- use file locks per cache key,
- avoid partially written artifacts (atomic replace),
- and treat cache corruption as a “miss” (rebuild safely).

### Integration targets

A Cmt2 JIT likely needs more than one “target”:

- **simulation** (fast iteration, debug ports, interpreter/Verilator)
- **synthesis** (SV emission, constraints/attributes preserved)
- optionally: **analysis-only** (schedule checks, call-info dumps)

Encode target choice into the cache key and expose it in the staged APIs.

---

## Appendix: Key Files Referenced

- JAX:
  - `references/jax/jax/_src/api.py`
  - `references/jax/jax/_src/pjit.py`
  - `references/jax/jax/_src/interpreters/partial_eval.py`
  - `references/jax/jax/_src/interpreters/mlir.py`
  - `references/jax/jax/_src/stages.py`
- Triton:
  - `references/triton/python/triton/runtime/jit.py`
  - `references/triton/python/triton/runtime/cache.py`
  - `references/triton/python/triton/compiler/compiler.py`
  - `references/triton/python/triton/compiler/code_generator.py`
  - `references/triton/lib/Conversion/TritonGPUToLLVM/*` (native lowering examples)
- TileLang:
  - `references/tilelang/tilelang/jit/__init__.py`
  - `references/tilelang/tilelang/jit/kernel.py`
  - `references/tilelang/tilelang/engine/lower.py`
  - `references/tilelang/tilelang/cache/__init__.py`
  - `references/tilelang/tilelang/cache/kernel_cache.py`
