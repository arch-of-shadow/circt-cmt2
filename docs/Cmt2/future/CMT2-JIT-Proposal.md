# CMT2 Python JIT Proposal

**Status:** Draft  
**Branch:** cmt2-jit  
**Last Updated:** 2026-01-28  

---

## Executive Summary

This proposal outlines a **Python JIT (Just-In-Time) compilation system** for CMT2 that enables hardware designers to write concise, Pythonic code while targeting efficient hardware implementations. The design synthesizes best practices from JAX, Triton, and TileLang.

### Key Goals

1. **Ergonomic Python Frontend**: Natural Python syntax for hardware design
2. **Staged Compilation**: Python → CMT2 IR → Optimized Hardware
3. **Automatic Specialization**: Compile-time constants, shape inference
4. **Multi-Target Execution**: Simulation (PyCMT2), Verilog generation, FPGA synthesis
5. **Developer Experience**: Fast iteration, clear error messages, introspection

### Core Design Principles

| Principle | Description | Inspiration |
|-----------|-------------|-------------|
| **Trace, Don't Parse** | Execute Python to build IR (like JAX) | JAX tracers |
| **Explicit Stages** | Clear Traced → Lowered → Compiled pipeline | JAX stages |
| **Source Capture** | Extract source for caching/debugging | Triton AST |
| **Builder Pattern** | Fluent API for hardware construction | TileLang eager |
| **Dual Execution** | Lazy (return IR) vs Eager (execute) modes | TileLang modes |

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        CMT2 JIT Architecture                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                  │
│  │   Python     │───▶│   Traced     │───▶│  Lowered     │                  │
│  │   (User Code)│    │   (CMT2 IR)  │    │  (MLIR)      │                  │
│  └──────────────┘    └──────────────┘    └──────┬───────┘                  │
│         │                    │                   │                          │
│         │ @cmt2.jit          │ .lower()         │ .compile()               │
│         ▼                    ▼                   ▼                          │
│  ┌──────────────────────────────────────────────────────────┐              │
│  │                    Compilation Cache                      │              │
│  │  Key: (source_hash, static_args, target_config)          │              │
│  └──────────────────────────────────────────────────────────┘              │
│                              │                                              │
│         ┌────────────────────┼────────────────────┐                        │
│         ▼                    ▼                    ▼                        │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                  │
│  │  Simulation  │    │   Verilog    │    │    FPGA      │                  │
│  │  (PyCMT2)    │    │  Generation  │    │  Bitstream   │                  │
│  └──────────────┘    └──────────────┘    └──────────────┘                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. User-Facing API Design

### 2.1 Basic JIT Decorator

```python
import cmt2
from cmt2 import jit, Circuit, UInt, Bits
from cmt2.stl import Reg, FIFO

@cmt2.jit
def counter_module(max_count: int = 100):
    """A simple counter with parameterized max value."""
    circuit = Circuit("Counter")
    
    # Static parameter: max_count is compile-time constant
    count = Reg(UInt(32), init=0)
    
    @circuit.rule("increment")
    def increment():
        count.next = count + 1
    
    @circuit.rule("wrap")
    def wrap():
        with count == max_count:
            count.next = 0
    
    return circuit

# Usage: compile with specific parameter
counter_100 = counter_module(max_count=100)  # Creates circuit
counter_1000 = counter_module(max_count=1000)  # Different circuit (cache miss)
```

### 2.2 Static vs Dynamic Arguments

Following JAX's pattern, arguments can be marked as static (compile-time) or dynamic (runtime):

```python
@cmt2.jit(static_argnums=[0, 1], static_argnames=["target_freq"])
def fifo_module(depth: int, width: int, target_freq: str = "100MHz"):
    """
    Args:
        depth: Compile-time constant (affects FIFO storage)
        width: Compile-time constant (affects data width)
        target_freq: Compile-time constant (affects timing constraints)
    """
    circuit = Circuit(f"FIFO_{depth}x{width}")
    fifo = FIFO(Bits(width), depth=depth)
    # ...
    return circuit

# Static args become part of cache key
fifo_8x32 = fifo_module(8, 32, target_freq="200MHz")  # Cache miss
fifo_8x32_again = fifo_module(8, 32, target_freq="200MHz")  # Cache hit
fifo_16x32 = fifo_module(16, 32)  # Cache miss (different depth)
```

### 2.3 Staged API (Traced → Lowered → Compiled)

```python
@cmt2.jit
def complex_design(data_width: int, num_stages: int):
    # ... define circuit ...
    return circuit

# Stage 1: Trace to CMT2 IR
traced = complex_design.trace(data_width=32, num_stages=4)
print(traced.jaxpr)  # Inspect the traced IR

# Stage 2: Lower to MLIR
lowered = traced.lower(target="verilog")
print(lowered.mlir)  # Inspect MLIR output

# Stage 3: Compile to target
compiled = lowered.compile()

# Execute (for simulation targets)
result = compiled.run(cycles=1000)

# Or generate hardware
verilog_code = compiled.codegen(format="systemverilog")
```

### 2.4 Dual Execution Modes

Following TileLang's pattern:

```python
# Mode 1: Lazy (returns IR for inspection)
@cmt2.jit(mode="lazy")
def inspectable_design(width: int):
    circuit = Circuit("Test")
    # ...
    return circuit

cmt2_ir = inspectable_design(width=32)  # Returns CMT2 IR object
print(cmt2_ir.dump())  # Pretty-print IR

# Mode 2: Eager (compile and execute immediately)
@cmt2.jit(mode="eager", target="simulation")
def runnable_design(width: int):
    circuit = Circuit("Test")
    # ...
    return circuit

result = runnable_design(width=32)  # Compiles and runs immediately
```

---

## 3. Implementation Architecture

### 3.1 Core Components

```python
# cmt2/jit/_core.py

class Cmt2JitInfo(NamedTuple):
    """Configuration for JIT compilation - serves as cache key."""
    fn_sourceinfo: str
    fn_signature: inspect.Signature
    static_argnums: tuple[int, ...]
    static_argnames: tuple[str, ...]
    target: str  # "simulation", "verilog", "fpga"
    target_config: dict[str, Any]
    optimization_level: int

class JitWrapped:
    """Public interface for jitted functions."""
    
    def __init__(self, fn, jit_info: Cmt2JitInfo):
        self.fn = fn
        self.jit_info = jit_info
        self._cache = {}  # (static_key, dynamic_signature) -> CompiledCircuit
    
    def __call__(self, *args, **kwargs):
        # 1. Separate static/dynamic args
        static_args, dynamic_args = self._partition_args(args, kwargs)
        
        # 2. Compute cache key
        cache_key = self._compute_cache_key(static_args)
        
        # 3. Check cache
        if cache_key in self._cache:
            return self._cache[cache_key](dynamic_args)
        
        # 4. Trace with abstract values
        traced = self._trace(static_args, dynamic_args)
        
        # 5. Lower to target
        lowered = traced.lower(target=self.jit_info.target)
        
        # 6. Compile
        compiled = lowered.compile()
        
        # 7. Cache and execute
        self._cache[cache_key] = compiled
        return compiled(dynamic_args)
    
    def trace(self, *args, **kwargs) -> "TracedCircuit":
        """Trace to CMT2 IR without compiling."""
        static_args, dynamic_args = self._partition_args(args, kwargs)
        return self._trace(static_args, dynamic_args)
    
    def _trace(self, static_args, dynamic_args) -> "TracedCircuit":
        """Core tracing logic."""
        # Create tracer context
        with TracingContext() as ctx:
            # Bind static args as constants
            bound_args = self._bind_with_statics(static_args)
            
            # Create abstract values for dynamic args
            avals = [Cmt2AbstractValue.from_arg(arg) for arg in dynamic_args]
            
            # Execute function - operations are intercepted
            with ctx.trace():
                result = self.fn(*bound_args, *dynamic_args)
            
            # Extract traced IR
            jaxpr = ctx.to_jaxpr()
            
        return TracedCircuit(jaxpr, self.jit_info)
```

### 3.2 Tracing Mechanism

```python
# cmt2/jit/_tracer.py

class Cmt2AbstractValue:
    """Abstract representation of a hardware value."""
    dtype: "DataType"
    shape: tuple[int, ...]  # For arrays/vectors
    width: int  # Bit width
    signed: bool

class SignalTracer:
    """Proxy object that intercepts operations during tracing."""
    
    def __init__(self, aval: Cmt2AbstractValue, trace: "Cmt2Trace"):
        self.aval = aval
        self._trace = trace
    
    def __add__(self, other):
        # Intercept addition, record primitive
        other = self._trace.to_tracer(other)
        return self._trace.process_primitive(
            "add", [self, other], {"width": max(self.aval.width, other.aval.width)}
        )
    
    def __eq__(self, other):
        # Intercept comparison
        other = self._trace.to_tracer(other)
        return self._trace.process_primitive(
            "eq", [self, other], {"width": 1}
        )
    
    def __getitem__(self, idx):
        # Intercept indexing/slicing
        return self._trace.process_primitive(
            "slice", [self], {"idx": idx}
        )

class Cmt2Trace:
    """Context for building CMT2 IR through tracing."""
    
    def __init__(self):
        self.equations: list["PrimitiveEqn"] = []
        self.consts: list[Any] = []
    
    def process_primitive(self, name: str, tracers: list[SignalTracer], params: dict):
        """Record a primitive operation."""
        # Compute output abstract value
        out_aval = self._abstract_eval(name, [t.aval for t in tracers], params)
        
        # Create output tracer
        out_tracer = SignalTracer(out_aval, self)
        
        # Record equation
        eqn = PrimitiveEqn(name, tracers, params, out_tracer)
        self.equations.append(eqn)
        
        return out_tracer
    
    def to_jaxpr(self) -> "Cmt2Jaxpr":
        """Convert traced operations to CMT2 Jaxpr."""
        return Cmt2Jaxpr(
            consts=self.consts,
            equations=self.equations,
            in_avals=[...],
            out_avals=[...]
        )
```

### 3.3 Builder Pattern (Alternative to Tracing)

For explicit IR construction (inspired by TileLang):

```python
# cmt2/jit/_builder.py

class Cmt2Builder:
    """Builder pattern for explicit circuit construction."""
    
    def __init__(self):
        self.module = ir.Module()
        self.current_block = None
    
    @contextmanager
    def circuit(self, name: str):
        """Create a circuit context."""
        circuit_op = cmt2_dialect.CircuitOp(name)
        with ir.InsertionPoint(circuit_op.body):
            yield CircuitContext(self)
    
    @contextmanager
    def rule(self, name: str, guard=None):
        """Create a rule context."""
        rule_op = cmt2_dialect.RuleOp(name, guard)
        with ir.InsertionPoint(rule_op.body):
            yield RuleContext(self)
    
    def reg(self, dtype, init=None):
        """Create a register."""
        return cmt2_dialect.RegOp(dtype, init)
    
    def add(self, lhs, rhs):
        """Create addition operation."""
        return comb_dialect.AddOp(lhs, rhs)

# Usage
@cmt2.jit(mode="builder")
def design_with_builder(width: int):
    builder = Cmt2Builder()
    
    with builder.circuit("MyDesign"):
        count = builder.reg(UInt(width), init=0)
        
        with builder.rule("increment"):
            count.next = builder.add(count, 1)
    
    return builder.module
```

---

## 4. Caching Strategy

### 4.1 Multi-Level Cache

```python
# cmt2/jit/_cache.py

class Cmt2CacheManager:
    """Multi-level caching for JIT compilation."""
    
    def __init__(self):
        # L1: In-memory cache for active session
        self._memory_cache: dict = {}
        
        # L2: Disk cache for persistent storage
        self._disk_cache_path = Path.home() / ".cmt2_cache"
        
        # L3: Optional remote cache for CI/CD
        self._remote_cache = None
    
    def compute_cache_key(self, fn, static_args, jit_info) -> str:
        """Compute deterministic cache key."""
        # 1. Function source hash
        source = inspect.getsource(fn)
        source_hash = hashlib.sha256(source.encode()).hexdigest()[:16]
        
        # 2. Static args hash
        static_hash = self._hash_static_args(static_args)
        
        # 3. JIT config hash
        config_hash = self._hash_config(jit_info)
        
        return f"{source_hash}_{static_hash}_{config_hash}"
    
    def get(self, key: str) -> Optional["CompiledCircuit"]:
        # Check L1 (memory)
        if key in self._memory_cache:
            return self._memory_cache[key]
        
        # Check L2 (disk)
        disk_path = self._disk_cache_path / f"{key}.cm2"
        if disk_path.exists():
            compiled = self._load_from_disk(disk_path)
            self._memory_cache[key] = compiled
            return compiled
        
        return None
    
    def put(self, key: str, compiled: "CompiledCircuit"):
        # Update L1
        self._memory_cache[key] = compiled
        
        # Update L2
        disk_path = self._disk_cache_path / f"{key}.cm2"
        self._save_to_disk(compiled, disk_path)
```

### 4.2 Cache Invalidation

```python
def _hash_static_args(args) -> str:
    """Hash static arguments for cache key."""
    # Handle common types
    if isinstance(args, (int, float, str, bool)):
        return hashlib.sha256(str(args).encode()).hexdigest()[:8]
    
    if isinstance(args, (list, tuple)):
        # Recursive hashing for sequences
        hashes = [_hash_static_args(a) for a in args]
        return hashlib.sha256("".join(hashes).encode()).hexdigest()[:8]
    
    if isinstance(args, dict):
        # Sort keys for deterministic hashing
        items = sorted(args.items())
        hashes = [f"{k}:{_hash_static_args(v)}" for k, v in items]
        return hashlib.sha256("".join(hashes).encode()).hexdigest()[:8]
    
    # For objects, use __repr__ or pickle
    try:
        data = pickle.dumps(args)
        return hashlib.sha256(data).hexdigest()[:8]
    except:
        return hashlib.sha256(repr(args).encode()).hexdigest()[:8]
```

---

## 5. Backend Targets

### 5.1 Simulation Backend (PyCMT2)

```python
# cmt2/jit/backends/simulation.py

class SimulationBackend:
    """Backend for PyCMT2 simulation."""
    
    def compile(self, lowered: "LoweredCircuit") -> "SimulationExecutable":
        """Compile to simulation executable."""
        # 1. Convert MLIR to simulation objects
        sim_module = self._mlir_to_sim(lowered.mlir)
        
        # 2. Create executable wrapper
        return SimulationExecutable(sim_module)
    
    def _mlir_to_sim(self, mlir_module: ir.Module) -> "SimModule":
        """Convert MLIR to PyCMT2 simulation objects."""
        # Use existing cmt2-to-firrtl and interpret
        # Or direct conversion to simulation objects
        pass

class SimulationExecutable:
    """Compiled simulation ready to run."""
    
    def __init__(self, sim_module: "SimModule"):
        self.sim_module = sim_module
        self._initialized = False
    
    def run(self, cycles: int = 100, inputs: dict = None) -> "SimulationResult":
        """Run simulation for specified cycles."""
        if not self._initialized:
            self.sim_module.initialize()
            self._initialized = True
        
        return self.sim_module.run(cycles=cycles, inputs=inputs)
    
    def step(self) -> "SimulationResult":
        """Run single cycle."""
        return self.sim_module.step()
```

### 5.2 Verilog Backend

```python
# cmt2/jit/backends/verilog.py

class VerilogBackend:
    """Backend for SystemVerilog generation."""
    
    def compile(self, lowered: "LoweredCircuit") -> "VerilogExecutable":
        """Compile to Verilog output."""
        # 1. Run CIRCT passes
        pm = PassManager()
        pm.add("cmt2-to-firrtl")
        pm.add("firrtl-lower-to-hw")
        pm.add("hw-to-sv")
        pm.run(lowered.mlir_module)
        
        # 2. Return executable wrapper
        return VerilogExecutable(lowered.mlir_module)

class VerilogExecutable:
    """Compiled Verilog ready for output."""
    
    def codegen(self, format: str = "systemverilog") -> str:
        """Generate Verilog/SystemVerilog code."""
        if format == "systemverilog":
            return self._generate_sv()
        elif format == "verilog":
            return self._generate_v()
        else:
            raise ValueError(f"Unknown format: {format}")
    
    def write(self, path: str):
        """Write Verilog to file."""
        code = self.codegen()
        Path(path).write_text(code)
    
    def synthesize(self, target: str = "xilinx") -> "SynthesisResult":
        """Run synthesis (if tools available)."""
        # Integration with Vivado, Quartus, etc.
        pass
```

### 5.3 FPGA Backend

```python
# cmt2/jit/backends/fpga.py

class FPGABackend:
    """Backend for FPGA bitstream generation."""
    
    def compile(self, lowered: "LoweredCircuit", target: str) -> "FPGAExecutable":
        """Compile to FPGA bitstream."""
        # 1. Generate Verilog
        verilog = VerilogBackend().compile(lowered)
        
        # 2. Run synthesis flow
        if target == "xilinx":
            return self._synthesize_xilinx(verilog)
        elif target == "intel":
            return self._synthesize_intel(verilog)
        else:
            raise ValueError(f"Unknown FPGA target: {target}")
```

---

## 6. Integration with Existing PyCMT2

### 6.1 Gradual Migration Path

```python
# Existing PyCMT2 API (kept for compatibility)
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg

circuit = Circuit("Legacy")
count = Reg(UInt(32), init=0)

# New JIT API (enhanced)
@cmt2.jit
def modern_design(width: int):
    circuit = Circuit("Modern")
    count = Reg(UInt(width), init=0)  # Width is compile-time constant
    return circuit

# Both can coexist
```

### 6.2 Interoperability

```python
# Mix JIT and non-JIT components

@cmt2.jit
def jit_component(width: int):
    """JIT-compiled component."""
    return SubCircuit("JIT", width=width)

def manual_component():
    """Manually constructed component."""
    return SubCircuit("Manual")

# Combine in larger design
top = Circuit("Top")
jit_inst = top.instance(jit_component(width=32))
manual_inst = top.instance(manual_component())
```

---

## 7. Error Handling and Debugging

### 7.1 Trace-Time Errors

```python
class Cmt2TracingError(Exception):
    """Error during tracing."""
    
    def __init__(self, message: str, source_info: "SourceInfo"):
        super().__init__(message)
        self.source_info = source_info
    
    def __str__(self):
        return f"""
CMT2 Tracing Error at {self.source_info.file}:{self.source_info.line}:
{self.args[0]}

Python Source:
{self.source_info.get_source_lines()}
        """

# Usage in tracer
def process_primitive(self, name, tracers, params):
    try:
        return self._abstract_eval(name, tracers, params)
    except TypeError as e:
        raise Cmt2TracingError(
            f"Invalid operation: {name} with types {[t.aval for t in tracers]}",
            source_info=current_source_info()
        ) from e
```

### 7.2 Debugging Tools

```python
@cmt2.jit(debug=True)
def debuggable_design(width: int):
    """Enable verbose tracing."""
    pass

# Debug output:
# [TRACE] Entering function: debuggable_design
# [TRACE] Static arg: width = 32
# [TRACE] Creating Reg: UInt(32)
# [TRACE] Processing primitive: add
# [TRACE] ...

# Inspect intermediate stages
traced = debuggable_design.trace(width=32)
traced.visualize()  # Graphviz output
```

---

## 8. Performance Considerations

### 8.1 Fast Path Optimization

```python
class JitWrapped:
    """Optimized for repeated calls with same signature."""
    
    def __init__(self, ...):
        self._fast_path_data = None
    
    def __call__(self, *args, **kwargs):
        # Fast path: check if args match last call
        if self._fast_path_data is not None:
            if self._args_match_fast_path(args, kwargs):
                return self._fast_path_data.compiled(args)
        
        # Slow path: full cache lookup
        return self._slow_call(args, kwargs)
```

### 8.2 Parallel Compilation

```python
def compile_parallel(designs: list) -> list:
    """Compile multiple designs in parallel."""
    with ThreadPoolExecutor() as executor:
        futures = [executor.submit(d.compile) for d in designs]
        return [f.result() for f in futures]
```

---

## 9. Implementation Roadmap

### Phase 1: Foundation (2-3 weeks)
- [ ] Core `JitWrapped` class
- [ ] Basic tracing infrastructure
- [ ] Static/dynamic argument separation
- [ ] Simple simulation backend

### Phase 2: IR Integration (2-3 weeks)
- [ ] CMT2 IR generation from traces
- [ ] MLIR integration
- [ ] Lowering pipeline
- [ ] Verilog backend

### Phase 3: Caching & Optimization (2 weeks)
- [ ] Multi-level caching
- [ ] Cache persistence
- [ ] Fast path optimizations
- [ ] Parallel compilation

### Phase 4: Advanced Features (2-3 weeks)
- [ ] Builder pattern API
- [ ] Debugging tools
- [ ] Error handling improvements
- [ ] FPGA backend

### Phase 5: Polish & Documentation (1-2 weeks)
- [ ] API documentation
- [ ] Tutorial notebooks
- [ ] Performance benchmarks
- [ ] Migration guide from PyCMT2

---

## 10. Open Questions

1. **Tracing vs Building**: Should we support both tracing (JAX-style) and explicit builder (TileLang-style) patterns?

2. **Partial Evaluation**: How deep should partial evaluation go? Should we support loop unrolling at trace time?

3. **Custom Primitives**: How do users define custom primitives that integrate with tracing?

4. **State Management**: How to handle mutable state (registers, memories) during tracing?

5. **Type System**: Should we use Python's type hints or a custom type annotation system?

---

## 11. References

- JAX JIT Analysis: [jit-analysis-jax.md](./jit-analysis-jax.md)
- Triton JIT Analysis: [jit-analysis-triton.md](./jit-analysis-triton.md)
- TileLang JIT Analysis: [jit-analysis-tilelang.md](./jit-analysis-tilelang.md)

---

**Next Steps:**
1. Review by PL expert (round 1)
2. Review by Computer Architecture expert (round 2)
3. Review by Compiler expert (round 3)
4. Create detailed implementation tasks
5. Begin implementation
