# CMT2 JIT IR Design Decision

**Status:** Approved  
**Decision Date:** 2026-01-28  
**Decision Owner:** Compiler Lead  
**Related Tasks:** Task 3.1 (Phase 3)  

---

## Executive Summary

This document records the architectural decision to use **Direct MLIR Construction** instead of a custom intermediate representation (Cmt2Jaxpr) for the CMT2 JIT compiler. This decision addresses the compiler expert's concern about custom IR maintenance burden and aligns with CIRCT's existing infrastructure.

---

## Decision: Direct MLIR Construction

We will construct MLIR directly via Python bindings rather than creating a custom intermediate IR (Cmt2Jaxpr).

### Rationale

| Factor | Direct MLIR | Custom Cmt2Jaxpr |
|--------|-------------|------------------|
| **Infrastructure** | Reuse existing MLIR tooling | Custom parser, printer, verifier needed |
| **Integration** | Native CIRCT pass integration | Requires conversion pass to MLIR |
| **Debugging** | Standard MLIR tools work | Custom tooling required |
| **Serialization** | MLIR built-in | Custom format needed |
| **Ecosystem** | Leverages mature MLIR ecosystem | Isolated from MLIR community |
| **Maintenance** | Low (reuse CIRCT bindings) | High (custom IR infrastructure) |

### Key Drivers

1. **Less Infrastructure to Maintain**
   - No custom IR parser/printer needed
   - MLIR's mature verification infrastructure
   - Existing debugging tools (`mlir-opt`, `mlir-translate`)

2. **Direct Integration with CIRCT Passes**
   - CMT2 dialect passes work immediately
   - FIRRTL lowering pipeline already exists
   - No conversion layer required

3. **Existing Tooling Compatibility**
   - Visualizers, debuggers, profilers work out-of-box
   - Standard MLIR passes (CSE, canonicalization, DCE)
   - Existing Python bindings infrastructure

4. **Team Expertise**
   - Team already familiar with MLIR Python bindings
   - Leverages existing PyCMT2 builder patterns
   - Consistent with CIRCT development practices

---

## Alternatives Considered

### Alternative 1: Custom Cmt2Jaxpr IR

**Description:** Create a Python-native IR (similar to JAX's Jaxpr) that captures traced operations before lowering to MLIR.

**Pros:**
- Complete control over IR semantics
- Potential for custom optimizations at trace level
- Decoupled from MLIR for flexibility

**Cons:**
- Significant infrastructure burden (parser, printer, verifier)
- Custom serialization format needed
- Debugging tools must be built from scratch
- Team would maintain parallel IR ecosystem
- Conversion pass to MLIR adds complexity

**Verdict:** ❌ **REJECTED** - Maintenance burden outweighs benefits

---

### Alternative 2: Python AST Directly

**Description:** Use Python's AST module to analyze code and lower directly to CMT2 operations.

**Pros:**
- No tracing overhead
- Static analysis possible

**Cons:**
- Python AST not suitable for hardware semantics
- Control flow (if/for/while) needs special handling
- Type inference difficult without execution
- Doesn't capture runtime values for static arguments

**Verdict:** ❌ **REJECTED** - Not suitable for hardware design patterns

---

### Alternative 3: Direct FIRRTL

**Description:** Skip CMT2 dialect and construct FIRRTL operations directly.

**Pros:**
- One less abstraction layer
- Direct path to Verilog

**Cons:**
- Loses CMT2's hardware abstractions (rules, methods, interfaces)
- Scheduling and rule composition harder
- Less flexibility for hardware-specific optimizations
- Diverges from existing CMT2 tooling

**Verdict:** ❌ **REJECTED** - CMT2 dialect provides essential abstractions

---

## Architecture

### Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         CMT2 JIT Compilation Flow                        │
└─────────────────────────────────────────────────────────────────────────┘

  Python Source                    Direct MLIR Construction
  ─────────────                    ───────────────────────
       │                                    
       ▼                                    
  ┌─────────┐                              
  │  @jit   │                              
  │decorator│                              
  └────┬────┘                              
       │                                    
       ▼                                    
  ┌─────────┐    ┌─────────────────────────────────────────────────────┐
  │ Tracing │───▶│  MLIR Python Bindings (circt.ir, circt.dialects)   │
  │  Phase  │    │                                                     │
  └─────────┘    │  • Create cmt2.circuit                             │
       │         │  • Create cmt2.module                              │
       │         │  • Create cmt2.rule / cmt2.method                  │
       │         │  • Insert comb/seq operations                      │
       │         └─────────────────────────────────────────────────────┘
       │                                    
       │                                    ▼
       │                         ┌──────────────────────┐
       │                         │   MLIR Module (CMT2) │
       │                         │                      │
       │                         │  cmt2.circuit @Top { │
       │                         │    cmt2.module @Mod {│
       │                         │      cmt2.rule @R {  │
       │                         │        ...           │
       │                         │      }               │
       │                         │    }                 │
       │                         │  }                   │
       │                         └──────────────────────┘
       │                                    │
       │                                    ▼
       │                         ┌──────────────────────┐
       │                         │   CIRCT Passes       │
       │                         │                      │
       │                         │  • cmt2-tdcc         │
       │                         │  • cmt2-to-firrtl    │
       │                         │  • firrtl-lower-to-hw│
       │                         │  • hw-to-sv          │
       │                         └──────────────────────┘
       │                                    │
       │                                    ▼
       │                         ┌──────────────────────┐
       ▼                         │   Target Outputs     │
  ┌─────────┐                   │                      │
  │  Cache  │◀────────────────│  • Verilog (.sv)     │
  │  Store  │                   │  • Simulation binary │
  └─────────┘                   │  • FPGA bitstream    │
                                └──────────────────────┘
```

### Integration with CIRCT

```
┌─────────────────────────────────────────────────────────────────┐
│                    CIRCT Infrastructure                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │  CMT2 JIT    │    │  CMT2 Dialect│    │  Other       │      │
│  │  (Python)    │───▶│  (MLIR)      │───▶│  Dialects    │      │
│  │              │    │              │    │              │      │
│  │ • Tracing    │    │ • cmt2.rule  │    │ • firrtl     │      │
│  │ • MLIR Build │    │ • cmt2.call  │    │ • comb       │      │
│  │ • Caching    │    │ • cmt2.reg   │    │ • seq        │      │
│  └──────────────┘    └──────────────┘    │ • hw         │      │
│         │                   │            │ • sv         │      │
│         │                   ▼            └──────────────┘      │
│         │            ┌──────────────┐            │              │
│         │            │ CMT2 Passes  │            │              │
│         └───────────▶│              │◀───────────┘              │
│                      │ • cmt2-tdcc  │                           │
│                      │ • cmt2-inline│                           │
│                      └──────────────┘                           │
│                               │                                  │
│                               ▼                                  │
│                      ┌──────────────┐                           │
│                      │   Backends   │                           │
│                      │              │                           │
│                      │ • Verilog    │                           │
│                      │ • Simulation │                           │
│                      │ • FPGA       │                           │
│                      └──────────────┘                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Implementation Architecture

### Layer Structure

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 3: User API (cmt2.jit)                                 │
│  • @elaborate decorator                                      │
│  • @simulate decorator                                       │
│  • Circuit, Module, Rule builders                            │
├─────────────────────────────────────────────────────────────┤
│ Layer 2: MLIR Construction (cmt2.ir)                         │
│  • MLIRBuilder - Helper for constructing MLIR                │
│  • CMT2OpBuilder - CMT2-specific operations                  │
│  • Type conversion utilities                                 │
├─────────────────────────────────────────────────────────────┤
│ Layer 1: CIRCT Python Bindings (circt)                       │
│  • circt.ir.Context, Module, Operation                       │
│  • circt.dialects.cmt2                                       │
│  • circt.dialects.comb, seq, hw, firrtl                      │
├─────────────────────────────────────────────────────────────┤
│ Layer 0: MLIR C++ Core                                       │
│  • MLIR IR infrastructure                                    │
│  • Pass management                                           │
│  • Dialect registration                                      │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

#### 1. MLIRBuilder (python/cmt2/ir/_core.py)

Central utility for constructing MLIR operations:

```python
class MLIRBuilder:
    """Helper for constructing MLIR from Python."""
    
    def create_circuit(self, name: str) -> cmt2.CircuitOp:
        """Create a cmt2.circuit operation."""
        
    def create_module(self, name: str, circuit: cmt2.CircuitOp) -> cmt2.ModuleOp:
        """Create a cmt2.module operation."""
        
    def create_rule(self, name: str, parent: cmt2.ModuleOp) -> cmt2.RuleOp:
        """Create a cmt2.rule operation with guard and body regions."""
        
    def create_method(self, name: str, parent: cmt2.ModuleOp) -> cmt2.MethodOp:
        """Create a cmt2.method operation."""
```

#### 2. CMT2OpBuilder (python/cmt2/ir/_core.py)

CMT2-specific operation builders:

```python
class CMT2OpBuilder:
    """Builder for CMT2 dialect operations."""
    
    def create_reg(self, dtype, init=None, name=None) -> ir.Value:
        """Create a cmt2.reg operation."""
        
    def create_call(self, instance, method, args) -> list[ir.Value]:
        """Create a cmt2.call operation."""
        
    def create_instance(self, module_name, instance_name) -> ir.Value:
        """Create a cmt2.instance operation."""
```

#### 3. Type Conversion (python/cmt2/ir/_core.py)

Utilities for converting between Python types and MLIR types:

```python
class TypeConverter:
    """Convert Python/CMT2 types to MLIR types."""
    
    def to_firrtl_type(self, dtype) -> ir.Type:
        """Convert to FIRRTL type."""
        
    def to_cmt2_type(self, dtype) -> ir.Type:
        """Convert to CMT2 type."""
        
    def from_python_type(self, py_type) -> ir.Type:
        """Convert Python type to MLIR type."""
```

---

## Integration with Existing CIRCT Infrastructure

### Reusing Existing Components

| Component | Reuse Strategy | Rationale |
|-----------|---------------|-----------|
| `circt.ir` | Direct use | Core MLIR Python bindings |
| `circt.dialects.cmt2` | Direct use | CMT2 dialect operations |
| `circt.dialects.firrtl` | Direct use | FIRRTL operations for primitives |
| `circt.passmanager` | Direct use | Pass pipeline execution |
| `pycmt2.Circuit` | Extend | Build on existing circuit builder |
| `pycmt2.ModuleBuilder` | Extend | Reuse module construction patterns |

### MLIR Context Management

```python
# Context lifecycle managed automatically
class MLIRContext:
    """Manages MLIR context lifecycle for JIT compilation."""
    
    def __init__(self):
        self._ctx = ir.Context()
        circt.register_dialects(self._ctx)
        
    def __enter__(self):
        self._ctx.__enter__()
        return self
        
    def __exit__(self, *args):
        self._ctx.__exit__(*args)
```

### Pass Pipeline Integration

```python
# Direct use of existing CIRCT passes
CMT2_PASS_PIPELINE = [
    # CMT2 compilation passes (existing)
    "cmt2-compile-invoke",
    "cmt2-dataflow-lowering",
    "cmt2-token-lowering",
    "cmt2-tdcc",
    "cmt2-proc-stmt-to-action",
    "cmt2-proc-to-gaa",
    # Lowering (existing)
    "lower-cmt2-to-firrtl",
    "firrtl-lower-to-hw",
    "lower-seq-to-sv",
]
```

---

## Migration from Custom IR

For any code that was designed around a custom Cmt2Jaxpr IR:

1. **Custom Operations** → MLIR Operations via Python bindings
2. **Custom Types** → MLIR Types via type converter
3. **Custom Passes** → MLIR Passes (Python or C++)
4. **Custom Serialization** → MLIR bytecode/text format

---

## Testing Strategy

### Unit Tests

```python
def test_create_circuit():
    """Test MLIR circuit creation."""
    builder = MLIRBuilder()
    circuit = builder.create_circuit("Test")
    assert circuit is not None
    assert "cmt2.circuit" in str(circuit)
```

### Integration Tests

```python
def test_tracing_to_mlir():
    """Test full tracing pipeline produces valid MLIR."""
    @cmt2.elaborate
    def design():
        circuit = Circuit("Test")
        # ... build circuit ...
        return circuit
    
    mlir = design.elaborate().emit_mlir()
    assert "cmt2.circuit" in mlir
```

---

## Future Considerations

### Potential Extensions

1. **Custom Attributes**: If needed, define custom MLIR attributes (not custom IR)
2. **Dialect Extensions**: Extend CMT2 dialect for JIT-specific operations
3. **Python-Only Passes**: For passes that are easier in Python, use MLIR's Python pass infrastructure

### Performance Optimization

If Python binding overhead becomes an issue:

1. Profile hot paths
2. Consider C++ extensions for critical paths (like JAX's `xc._xla`)
3. Batch operation creation where possible

---

## References

- [Compiler Expert Review](./review-compiler-expert.md) - Original concerns about custom IR
- [CMT2 JIT Implementation Plan](./Implementation-Plan.md) - Task 3.1
- [MLIR Python Bindings](https://mlir.llvm.org/docs/Bindings/Python/) - Official documentation
- [CIRCT Python Bindings](../../../lib/Bindings/Python/) - Existing bindings
- [PyCMT2 Builders](../../../lib/Bindings/Python/pycmt2/builders.py) - Existing builder patterns

---

**Decision Log:**

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-01-28 | Direct MLIR Construction | Address compiler expert concern, leverage existing infrastructure |

---

*End of Document*
