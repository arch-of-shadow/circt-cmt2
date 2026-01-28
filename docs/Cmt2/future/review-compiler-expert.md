# Compiler Expert Review: CMT2 Python JIT Proposal

**Reviewer**: Compiler Infrastructure Expert  
**Date**: 2026-01-28  
**Proposal Version**: Draft (cmt2-jit branch)  
**Review Scope**: IR Design, Lowering Pipeline, Caching, Code Generation, Performance, CIRCT Integration  

---

## 1. Overall Assessment

The CMT2 JIT Proposal presents a well-researched and thoughtfully designed system for Python-based hardware compilation. Drawing inspiration from JAX, Triton, and TileLang—three of the most successful Python JIT compilation systems—the proposal demonstrates a solid understanding of modern compiler infrastructure patterns.

The **staged compilation pipeline** (Traced → Lowered → Compiled) is architecturally sound and aligns with best practices in the field. The separation of concerns between tracing, lowering, and compilation will enable independent optimization of each stage. However, several critical areas require refinement before implementation, particularly around the custom IR design, caching granularity, and integration with existing CIRCT infrastructure.

---

## 2. Detailed Feedback by Section

### 2.1 IR Design

#### ⚠️ **WARNING**: Custom `Cmt2Jaxpr` IR Definition
- **Issue**: The proposal introduces a custom `Cmt2Jaxpr` IR (see Section 3.2) that sits between Python and MLIR
- **Concern**: This creates an intermediate representation that is not standard MLIR, requiring custom infrastructure for parsing, printing, verification, and transformation
- **Recommendation**: Consider whether this abstraction is necessary, or if direct construction of MLIR via Python bindings is sufficient

#### ✅ **SOUND**: Abstract Value System
- The `Cmt2AbstractValue` design with `dtype`, `shape`, `width`, and `signed` fields correctly captures hardware-specific type information
- The use of abstract interpretation for shape inference is appropriate

#### 🔴 **CRITICAL**: Integration with Existing CMT2 Dialects
- **Issue**: The proposal does not clearly specify how traced IR integrates with the existing CMT2 dialect operations
- **Gap**: No mention of how `SignalTracer` operations map to existing CMT2 operations (`cmt2.rule`, `cmt2.method`, `cmt2.value`)
- **Action Required**: Define the mapping from trace-time primitives to CMT2 dialect operations explicitly

#### ⚠️ **WARNING**: PyTree Abstraction Missing
- JAX's `PyTree` infrastructure is crucial for handling nested data structures as arguments/returns
- Hardware designs may have nested bundles, vectors, or custom types
- **Recommendation**: Consider adding PyTree-like flattening/unflattening for complex hardware types

### 2.2 Lowering Pipeline

#### ✅ **SOUND**: Three-Stage Architecture
- The Traced → Lowered → Compiled pipeline is well-designed and mirrors successful patterns from JAX
- Clear separation of concerns enables stage-specific optimizations

#### ⚠️ **WARNING**: Pass Pipeline Definition Under-Specified
- **Issue**: The proposal mentions `-cmt2-to-firrtl`, `-firrtl-lower-to-hw`, `-hw-to-sv` passes (Section 5.2) but doesn't specify:
  - Where custom JIT-specific passes fit in the pipeline
  - How passes interact with the staged compilation model
  - Which passes run at which stage
- **Recommendation**: Create a detailed pass pipeline diagram showing:
  - Traced stage passes (e.g., dead code elimination, constant folding at trace time)
  - Lowered stage passes (CMT2 dialect transformations)
  - Compiled stage passes (backend-specific optimizations)

#### 🔴 **CRITICAL**: Optimization Placement Ambiguity
- **Issue**: Section 3 mentions optimization but doesn't specify which optimizations happen at which stage
- **Missing**: 
  - Trace-time optimizations (constant propagation, loop unrolling decisions)
  - MLIR-level optimizations (CSE, canonicalization, scheduling)
  - Backend optimizations (technology mapping, retiming)
- **Action Required**: Define optimization strategy for each stage

#### ⚠️ **WARNING**: Proc/Dataflow Lowering Not Addressed
- The existing CMT2 infrastructure supports multi-cycle operations via `proc` (see `docs/Cmt2/features/Proc.md`)
- The proposal doesn't specify how traced control flow maps to proc constructs
- **Recommendation**: Define how Python control flow (if/while/for) is lowered to CMT2 proc operations

### 2.3 Caching and Incremental Compilation

#### ✅ **SOUND**: Multi-Level Cache Design
- L1 (memory), L2 (disk), L3 (remote) cache hierarchy is appropriate
- The cache key structure `(source_hash, static_args, target_config)` follows established patterns

#### 🔴 **CRITICAL**: Cache Key Granularity Too Coarse
- **Issue**: The proposal uses source code hash as part of the cache key (Section 4.1)
- **Problem**: Small changes (e.g., comments, formatting) invalidate the entire cache
- **Better Approach**: Use abstract syntax tree (AST) hash or MLIR module hash instead of raw source
- **See**: Triton's approach using AST-based dependency analysis (`dependencies_finder` in jit-analysis-triton.md)

#### ⚠️ **WARNING**: Static Argument Hashing Incomplete
- **Issue**: The `_hash_static_args` function (Section 4.2) uses pickle as a fallback
- **Problem**: Pickle is not deterministic across Python versions/implementations
- **Recommendation**: Define a canonical serialization for supported static argument types

#### ⚠️ **WARNING**: No Mention of Weak Reference Caching
- JAX uses `weakref_lru_cache` for lowering results to enable garbage collection
- Without this, long-running sessions may experience memory bloat
- **Recommendation**: Adopt weak reference caching for large compiled artifacts

#### 🔴 **CRITICAL**: Incremental Compilation Not Defined
- **Issue**: The proposal doesn't specify how incremental compilation works
- **Questions**:
  - What happens when a dependent module changes?
  - How are module interfaces versioned?
  - Can we reuse compiled sub-modules when parent changes?
- **Action Required**: Define incremental compilation strategy for multi-module designs

### 2.4 Code Generation

#### ✅ **SOUND**: Multi-Target Backend Architecture
- Simulation (PyCMT2), Verilog generation, and FPGA synthesis targets are appropriate
- The backend abstraction pattern (Section 5) enables pluggable code generation

#### ⚠️ **WARNING**: Debug Information Strategy Missing
- **Issue**: No discussion of source-to-IR-to-hardware debug information
- **Gap**: How do we map simulation results back to Python source lines?
- **Recommendation**: Design debug info propagation through all stages:
  - Python → Traced (source locations)
  - Traced → Lowered (MLIR locations)
  - Lowered → Compiled (SV line directives)

#### ⚠️ **WARNING**: Source Mapping Not Addressed
- **Issue**: Hardware debugging requires mapping between abstraction levels
- **Gap**: No mention of how to correlate:
  - Python variables with hardware signals
  - Python line numbers with RTL lines
  - Simulation traces with source code
- **See**: MLIR's `Location` infrastructure and FIRRTL's debug info support

#### 🔴 **CRITICAL**: Target-Specific Optimization Gap
- **Issue**: The proposal treats Verilog and FPGA backends similarly
- **Problem**: These have very different optimization requirements:
  - Verilog: Human readability, simulation speed
  - FPGA: Technology mapping, DSP inference, BRAM inference
  - ASIC: Retiming, clock gating, power optimization
- **Action Required**: Define target-specific optimization pipelines

### 2.5 Performance

#### ⚠️ **WARNING**: Tracing Overhead Analysis Missing
- **Issue**: No quantitative analysis of tracing overhead
- **Key Metrics Needed**:
  - Time to trace vs time to compile for typical designs
  - Memory overhead of maintaining tracer state
  - Comparison with direct builder API
- **Recommendation**: Benchmark tracing overhead against existing PyCMT2 API

#### ⚠️ **WARNING**: Memory Usage for Large Designs
- **Issue**: No discussion of memory usage for large designs
- **Concern**: Keeping traced IR, lowered IR, and compiled artifacts in memory simultaneously
- **Recommendation**: Design streaming/lowering-on-demand for large designs

#### ⚠️ **WARNING**: Compilation Parallelism Not Addressed
- The proposal mentions parallel compilation (Section 8.2) but doesn't specify:
  - Which compilation steps are parallelizable
  - Thread safety requirements
  - Overhead of parallel compilation for small designs

#### ⚠️ **WARNING**: Fast Path Optimization Insufficient
- The fast path (Section 8.1) only checks "args match last call"
- JAX uses C++ fast path with pre-computed dispatch tables
- **Recommendation**: Consider C++ extension for hot path, similar to JAX's `xc._xla.pjit`

### 2.6 Integration with CIRCT

#### 🔴 **CRITICAL**: Existing Pass Reuse Not Specified
- **Issue**: CIRCT has extensive pass infrastructure that should be leveraged
- **Missing**: Which existing passes are reused vs which are custom:
  - CMT2 dialect passes (`cmt2-inline`, `cmt2-to-firrtl`)
  - FIRRTL passes (`lower-to-hw`, optimization)
  - HW/SV passes (canonicalization, emission)
- **Action Required**: Explicitly map JIT pipeline to existing CIRCT passes

#### ⚠️ **WARNING**: Python Bindings Architecture Unclear
- **Issue**: The proposal shows Python code but doesn't specify binding architecture
- **Options**:
  1. Pure Python with MLIR Python bindings (slow but flexible)
  2. Pybind11 C++ extensions (fast but more code)
  3. Hybrid approach (like JAX's C++ dispatch)
- **Recommendation**: Use hybrid approach—Python for API, C++ for performance-critical paths

#### ⚠️ **WARNING**: MLIR Context Management Not Addressed
- **Issue**: MLIR requires careful context management for thread safety
- **Gap**: No mention of how `TracingContext` interacts with MLIR `Context`
- **Recommendation**: Define context lifecycle and thread safety guarantees

#### 🔴 **CRITICAL**: Existing PyCMT2 Migration Path Under-Specified
- **Issue**: Section 6 mentions "gradual migration" but lacks specifics
- **Questions**:
  - How do JIT modules interoperate with existing PyCMT2 modules?
  - Can existing designs be incrementally converted?
  - Is there a compatibility layer?
- **Action Required**: Design detailed migration strategy with examples

---

## 3. Specific Recommendations

### 3.1 IR Design Recommendations

1. **Eliminate or Minimize Custom IR**
   - **Rationale**: Every custom IR requires custom tooling
   - **Action**: Directly construct MLIR operations via Python bindings where possible
   - **Fallback**: If custom IR is needed, define it as MLIR dialect attributes/operations

2. **Define Primitive-to-CMT2 Operation Mapping**
   - **Rationale**: Clear mapping ensures consistent lowering
   - **Action**: Create table mapping trace-time primitives to CMT2 dialect operations:
     | Primitive | CMT2 Operation | Notes |
     |-----------|---------------|-------|
     | `add` | `comb.add` | Via CMT2 expression |
     | `rule` | `cmt2.rule` | Create rule region |
     | `reg.write` | `cmt2.call` | Method call on register instance |

3. **Add Clock Domain to Abstract Values**
   - **Rationale**: Hardware requires clock domain tracking
   - **Action**: Extend `Cmt2AbstractValue` with `clock_domain` field
   - **Benefit**: Enables CDC (Clock Domain Crossing) analysis during tracing

### 3.2 Lowering Pipeline Recommendations

4. **Define Explicit Pass Pipeline**
   - **Rationale**: Enables reproducible compilation and debugging
   - **Action**: Create pipeline configuration:
     ```python
     TRACED_PASSES = [
         "cmt2-trace-canonicalize",  # DCE, constant folding
         "cmt2-trace-shape-infer",   # Width inference
     ]
     
     LOWERED_PASSES = [
         "cmt2-proc-lower",          # Lower control flow
         "cmt2-scheduling",          # Rule scheduling
         "cmt2-to-firrtl",           # FIRRTL conversion
     ]
     
     COMPILED_PASSES = [
         "firrtl-lower-to-hw",
         "hw-to-sv",
         "sv-export-verilog",
     ]
     ```

5. **Integrate with Existing TDCC Lowering**
   - **Rationale**: CMT2 already has TDCC (Time-Domain Control Compilation) for proc lowering
   - **Action**: Reuse existing TDCC infrastructure instead of building parallel system

### 3.3 Caching Recommendations

6. **Use AST-Based Cache Keys**
   - **Rationale**: More stable than source text
   - **Implementation**:
     ```python
     import ast
     def compute_source_hash(fn):
         source = inspect.getsource(fn)
         tree = ast.parse(source)
         # Normalize: remove comments, standardize formatting
         normalized = ast.unparse(tree)
         return hashlib.sha256(normalized.encode()).hexdigest()
     ```

7. **Implement Weak Reference Caching**
   - **Rationale**: Prevents memory bloat in long-running sessions
   - **Implementation**: Use `weakref` module for lowering/compilation caches

8. **Define Module Interface Versioning**
   - **Rationale**: Enables incremental compilation
   - **Action**: Include interface hash in cache key for dependent modules

### 3.4 Code Generation Recommendations

9. **Design Debug Info Propagation**
   - **Rationale**: Essential for hardware debugging
   - **Action**: Propagate `mlir::Location` through all stages:
     - Python line info → MLIR Location
     - MLIR Location → FIRRTL source locator
     - FIRRTL → SV line directives

10. **Define Target-Specific Pipelines**
    - **Rationale**: Different targets need different optimizations
    - **Action**: Create backend-specific optimization configurations:
      ```python
      TARGET_CONFIGS = {
          "simulation": {"opt_level": 0, "debug": True},
          "verilog": {"opt_level": 2, "readable": True},
          "fpga": {"opt_level": 3, "dsp_inference": True},
      }
      ```

### 3.5 Performance Recommendations

11. **Implement Streaming for Large Designs**
    - **Rationale**: Avoid memory issues with large designs
    - **Action**: Support module-at-a-time lowering for designs >10k operations

12. **Consider C++ Fast Path**
    - **Rationale**: Python overhead is significant for small designs
    - **Action**: Evaluate pybind11 extension for cache lookup and dispatch

### 3.6 CIRCT Integration Recommendations

13. **Leverage Existing Python Bindings**
    - **Rationale**: CIRCT already has Python bindings infrastructure
    - **Action**: Build on `circt.python_packages.circt_core` instead of separate bindings

14. **Reuse Existing Pass Infrastructure**
    - **Rationale**: Avoid duplicating existing functionality
    - **Action**: Audit existing passes and reuse:
      - `cmt2-to-firrtl` (already exists)
      - `firrtl-lower-to-hw` (already exists)
      - `hw-to-sv` (already exists)

---

## 4. Questions for Other Experts

### For PL (Programming Languages) Experts:
1. **Static vs Dynamic Typing**: Should we use Python type hints for hardware types (`def f(x: UInt(32))`) or runtime type construction (`def f(x): ...` with type inference)?
2. **Effect System**: Should the tracer track effects (state reads/writes, clock domains) for optimization purposes?
3. **Control Flow**: How should Python control flow (if/while/for) be represented in the traced IR? As dataflow graphs or as control operations?

### For Architecture Experts:
1. **Scheduling Integration**: How should the JIT interact with CMT2's rule scheduler? Should scheduling happen at trace time or lowering time?
2. **Multi-Cycle Lowering**: How should traced sequential operations map to CMT2's proc/dataflow constructs?
3. **Interface Contracts**: How do we validate that JIT-generated modules satisfy interface contracts when composed with hand-written modules?

### For Hardware Experts:
1. **Clock Domain Handling**: How should clock domains be specified in the JIT API? As parameters, type annotations, or inferred?
2. **Reset Strategies**: How should reset behavior be specified for JIT-generated registers?
3. **Timing Constraints**: How should timing constraints be propagated through the JIT pipeline?

---

## 5. Approval Status

### **CONDITIONAL APPROVAL**

The CMT2 JIT Proposal demonstrates a solid architectural foundation and draws appropriately from successful JIT systems (JAX, Triton, TileLang). However, several critical issues must be addressed before implementation:

#### Must Fix (Blocking):
1. **Define custom IR vs MLIR integration strategy** - Eliminate or justify the custom `Cmt2Jaxpr` IR
2. **Specify incremental compilation strategy** - How do module dependencies affect caching?
3. **Map trace-time primitives to CMT2 operations** - Ensure compatibility with existing dialect
4. **Design debug info propagation** - Essential for hardware development workflow

#### Should Fix (High Priority):
5. **Use AST-based cache keys** - More stable than source hashing
6. **Define explicit pass pipeline** - Integration with existing CIRCT passes
7. **Specify target-specific optimizations** - Different pipelines for sim/FPGA/ASIC
8. **Address memory management** - Weak reference caching for long-running sessions

#### Nice to Have (Medium Priority):
9. **Quantify tracing overhead** - Benchmark against existing PyCMT2
10. **Design migration path** - Detailed examples of JIT/non-JIT interoperability
11. **Consider C++ fast path** - Performance optimization for hot paths

---

## 6. References

1. **JAX JIT Analysis**: `docs/Cmt2/future/jit-analysis-jax.md` - Staged compilation, abstract interpretation, caching
2. **Triton JIT Analysis**: `docs/Cmt2/future/jit-analysis-triton.md` - AST capture, backend abstraction, caching
3. **TileLang JIT Analysis**: `docs/Cmt2/future/jit-analysis-tilelang.md` - Dual-mode execution, builder pattern, lowering phases
4. **CMT2 Concepts**: `docs/Cmt2/features/Concepts.md` - GAA semantics, rules, methods, values
5. **CMT2 Lowering**: `docs/Cmt2/features/Lowering.md` - Proc/dataflow lowering pipeline

---

*End of Review*
