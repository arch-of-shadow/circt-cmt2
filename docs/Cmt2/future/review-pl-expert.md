# CMT2 JIT Proposal - Programming Languages Expert Review

**Reviewer:** PL Expert  
**Date:** 2026-01-28  
**Document Version:** Draft (CMT2-JIT-Proposal.md)  

---

## 1. Overall Assessment

The CMT2 JIT Proposal presents a well-researched and thoughtfully designed Python JIT compilation system for hardware design. The architecture draws heavily from successful DSLs like JAX, Triton, and TileLang, which is generally a sound approach given their proven track records in similar domains. The staged compilation pipeline (Traced → Lowered → Compiled) follows established best practices and provides users with valuable introspection capabilities.

However, there are several significant PL concerns that need addressing before implementation. The proposal conflates two distinct use cases—circuit **definition** (static elaboration) and circuit **execution** (simulation)—which have different characteristics and requirements. The tracing approach, while powerful, introduces fundamental challenges with Python control flow that are not adequately addressed. Additionally, the type system design is underspecified, particularly regarding width inference and the interaction between Python's gradual typing and hardware's strict bit-level semantics.

---

## 2. Detailed Feedback by Section

### 2.1 API Design Quality

#### ⚠️ **WARNING: Decorator API Clarity**

```python
@cmt2.jit
def counter_module(max_count: int = 100):
    ...
    return circuit
```

**Issue:** The decorator name `@cmt2.jit` is misleading. In JAX/Triton, `jit` implies "just-in-time compilation" of **executable code**. Here, the function returns a `Circuit` object—a data structure, not an executable. This is more akin to ** staged programming** or **compile-time function execution (CTFE)**.

**Recommendation:** Consider `@cmt2.elaborate`, `@cmt2.define`, or `@cmt2.template` to better reflect the semantic intent. Reserve `jit` for actual JIT-compiled simulation kernels (if/when those exist).

#### ⚠️ **WARNING: Static/Dynamic Argument Pattern Complexity**

```python
@cmt2.jit(static_argnums=[0, 1], static_argnames=["target_freq"])
```

**Issue:** The dual specification (`static_argnums` + `static_argnames`) creates cognitive overhead and potential conflicts. Users must mentally map positional indices to semantic names.

**Recommendation:** Provide a unified mechanism:

```python
# Option 1: Type-based annotation
from typing import Annotated

def fifo_module(
    depth: Annotated[int, cmt2.static],
    width: Annotated[int, cmt2.static],
    target_freq: Annotated[str, cmt2.static] = "100MHz"
):
    ...

# Option 2: Single parameter with type hints
@cmt2.jit(static_args={int, str})  # All int/str args are static

# Option 3: Keep current but add validation
@cmt2.jit(static_argnums=[0], static_argnames=["depth"])  # ERROR: index 0 IS "depth"
```

#### ✅ **GOOD: Staged API Design**

```python
traced = complex_design.trace(...)
lowered = traced.lower(...)
compiled = lowered.compile()
```

The explicit staging is excellent and follows industry best practices (JAX, Triton). The immutable intermediate representations (`TracedCircuit`, `LoweredCircuit`) enable safe caching and introspection.

**Suggestion:** Consider adding `__repr__` implementations that show salient information:

```python
>>> traced
TracedCircuit(name="complex_design", num_ops=47, inputs=["data_width", "num_stages"])
>>> lowered
LoweredCircuit(target="verilog", mlir_size=1240, optimization_level=2)
```

#### ⚠️ **WARNING: Dual Execution Mode Confusion**

```python
@cmt2.jit(mode="lazy")   # Returns IR
@cmt2.jit(mode="eager")  # Compiles and runs
```

**Issue:** This is mixing concerns. "Lazy" mode returns a circuit definition; "eager" mode returns simulation results. These are fundamentally different return types that confuse the type system and IDE support.

**Recommendation:** Separate these into distinct decorators:

```python
@cmt2.define     # Returns Circuit (always)
@cmt2.simulate   # Returns simulation results (compile + run)
```

---

### 2.2 Type System Considerations

#### 🔴 **CRITICAL: Type Inference Underspecified**

The proposal mentions `Cmt2AbstractValue` with `dtype`, `shape`, `width`, `signed`, but doesn't specify how type inference works:

```python
def add(self, other):
    return self._trace.process_primitive(
        "add", [self, other], {"width": max(self.aval.width, other.aval.width)}
    )
```

**Issues:**
1. **Width inference**: `max(width1, width2)` is naive. Hardware addition requires `max(width1, width2) + 1` for the result to avoid overflow.
2. **Signedness**: No discussion of signed vs. unsigned arithmetic rules.
3. **Type promotion**: What happens when adding `UInt(8)` and `UInt(16)`? What's the result type?

**Recommendation:** Define explicit type promotion rules:

```python
class TypePromotionRules:
    """Hardware-aware type promotion following SystemVerilog conventions."""
    
    @staticmethod
    def promote(t1: SignalType, t2: SignalType) -> SignalType:
        # Width: max(width1, width2) + (1 if operation_adds_bits else 0)
        # Signedness: unsigned unless both signed
        pass
```

#### 🔴 **CRITICAL: Python Type Hints vs. Hardware Types**

```python
def counter_module(max_count: int = 100) -> Circuit:
    count = Reg(UInt(32), init=0)  # Runtime value, not type
```

**Issue:** Python's `int` type is unbounded and signed. Hardware's `UInt(32)` is bounded and unsigned. This semantic mismatch is a source of confusion.

**Recommendation:** Use type hints that accurately reflect hardware semantics:

```python
from cmt2.types import uint, sint, bits

def counter_module(max_count: uint[32]) -> Circuit:
    count = Reg(uint[32], init=0)  # Type is part of the type annotation
```

Or use Python 3.12+ type parameter syntax:

```python
def counter_module(max_count: UInt[32]) -> Circuit:
    ...
```

#### ⚠️ **WARNING: Width Inference for Constants**

```python
count.next = count + 1  # What width is the constant 1?
```

**Issue:** The proposal doesn't specify how literal constants are typed. Options:
1. **Minimal width**: `1` → `UInt(1)`
2. **Contextual inference**: `1` → same width as other operand
3. **Default width**: `1` → `UInt(32)`

Each has tradeoffs. Minimal width is precise but may require frequent casts. Contextual inference is convenient but complex. Default width is simple but wasteful.

**Recommendation:** Adopt contextual inference with explicit overrides:

```python
count.next = count + 1           # Infers width from count
count.next = count + uint[8](1)  # Explicit width
```

---

### 2.3 Metaprogramming Patterns

#### 🔴 **CRITICAL: Python Control Flow Problem**

The proposal acknowledges "Trace, Don't Parse" but doesn't adequately address Python control flow:

```python
@cmt2.jit
def conditional_counter(width: int, use_reset: bool):
    circuit = Circuit("Counter")
    count = Reg(UInt(width), init=0)
    
    @circuit.rule("increment")
    def increment():
        count.next = count + 1
    
    if use_reset:  # Python if - executed at trace time!
        @circuit.rule("reset")
        def reset():
            count.next = 0
    
    return circuit
```

**Issue:** The `if use_reset` is evaluated during tracing, not during hardware execution. This is correct for static specialization, but users may expect hardware conditionals.

**The bigger problem:** What about data-dependent control flow?

```python
@cmt2.jit
def problematic(data: UInt(8)):
    if data > 10:  # Cannot evaluate at trace time!
        ...
```

**Recommendation:** 

1. **Document the distinction clearly** between:
   - **Trace-time control flow**: Python `if/for/while` evaluated during tracing
   - **Hardware control flow**: Must use hardware constructs

2. **Provide hardware control flow constructs:**

```python
from cmt2.control import when

@cmt2.define
def correct(data: UInt(8)):
    circuit = Circuit("Example")
    
    @circuit.rule("process")
    def process():
        with when(data > 10):  # Hardware conditional
            ...
    
    return circuit
```

3. **Consider adding a linter/guard** that warns about potentially misused Python control flow:

```python
@cmt2.define(warn_python_control_flow=True)
def my_design(...):
    if some_condition:  # WARNING: Python 'if' is evaluated at compile time
        ...
```

#### ⚠️ **WARNING: Tracing vs. AST Transformation Tradeoffs**

The proposal mentions both tracing and AST transformation but doesn't justify the choice:

| Approach | Pros | Cons |
|----------|------|------|
| **Tracing** | Simple, works with arbitrary Python, good for dynamic shapes | Loses source structure, hard to map errors, control flow issues |
| **AST Transform** | Preserves source, better error messages, can handle control flow | Complex implementation, Python version sensitive, can't handle dynamic code |

**Recommendation:** 

- **Start with tracing** (as proposed) for faster iteration
- **Plan for hybrid approach**: Use tracing for the main path, AST analysis for better error messages
- **Consider PyShEx/TorchDynamo-style approach**: Use frame evaluation callbacks for more control

#### ⚠️ **WARNING: Mutable State During Tracing**

```python
class SignalTracer:
    def __add__(self, other):
        ...
        return self._trace.process_primitive("add", ...)
```

**Issue:** The tracer relies on operator overloading, which means Python objects are mutated during tracing. This can lead to surprising behavior:

```python
@cmt2.jit
def bug_demo():
    a = Reg(UInt(8), init=0)
    b = a  # b is a tracer wrapping a
    a = a + 1  # a is now a NEW tracer (result of __add__)
    # b still references the original 'a', not the incremented value!
    return a, b
```

**Recommendation:** Document this clearly or consider value semantics where mutations return new values explicitly.

---

### 2.4 Language Integration

#### 🔴 **CRITICAL: IDE and Type Checker Support**

The current API will confuse static analysis tools:

```python
@cmt2.jit
def counter_module(max_count: int = 100):
    ...
    return circuit

result = counter_module(max_count=100)  # Type checker thinks result is Circuit
result = counter_module(max_count=1000) # But it's also cache-related internals
```

**Issues:**
1. Return type changes based on `mode` parameter
2. Internal types (`JitWrapped`, cache entries) leak to user code
3. Tracers masquerade as actual values during tracing

**Recommendation:** Use Python 3.10+ `ParamSpec` and proper generics:

```python
from typing import TypeVar, ParamSpec, Generic

P = ParamSpec('P')

class DefinedCircuit(Generic[P]):
    """A circuit definition that can be elaborated."""
    
    def __init__(self, fn: Callable[P, Circuit]):
        self._fn = fn
    
    def __call__(self, *args: P.args, **kwargs: P.kwargs) -> Circuit:
        return self._fn(*args, **kwargs)
    
    def elaborate(self, *args: P.args, **kwargs: P.kwargs) -> ElaboratedCircuit:
        ...

def define(fn: Callable[P, Circuit]) -> DefinedCircuit[P]:
    return DefinedCircuit(fn)

@define
def counter_module(max_count: int) -> Circuit:
    ...

circuit: Circuit = counter_module(100)  # Type checker understands
```

#### ⚠️ **WARNING: Error Reporting for Traced Code**

The proposal includes `Cmt2TracingError` with source info, but this is insufficient:

```python
def __str__(self):
    return f"""
CMT2 Tracing Error at {self.source_info.file}:{self.source_info.line}:
{self.args[0]}

Python Source:
{self.source_info.get_source_lines()}
    """
```

**Issues:**
1. Stack traces through traced code are often meaningless
2. The mapping between Python operations and generated IR is non-trivial
3. Runtime errors in simulation don't map back to Python source

**Recommendation:**

1. **Maintain a source map** from IR operations to Python source locations
2. **Provide visual debugging** (the proposal mentions `traced.visualize()`—expand this)
3. **Consider integration with Python's `sys.settrace`** for better debugging

```python
# Enhanced error with IR context
try:
    result = traced.run()
except Cmt2SimulationError as e:
    e.print_context()  # Shows Python source + IR operation + waveforms
```

#### ⚠️ **WARNING: Integration with Python Ecosystem**

```python
import cmt2
from cmt2 import jit, Circuit, UInt, Bits
```

**Issue:** The import structure suggests CMT2 is a standalone library. Consider:
- NumPy compatibility for array operations?
- Dataclass integration for structured types?
- Protocol/ABC for extensibility?

**Recommendation:** Define clear integration points:

```python
from cmt2.ext.numpy import as_signal  # Convert numpy arrays to signals
from cmt2.ext.dataclasses import hardware_dataclass  # Structured hardware types
```

---

## 3. Specific Recommendations

### 3.1 Rename and Refine the Core API

**Current:**
```python
@cmt2.jit(mode="lazy")
@cmt2.jit(mode="eager")
```

**Recommended:**
```python
@cmt2.define      # Elaborate a circuit definition (formerly mode="lazy")
@cmt2.simulate    # Compile and run simulation (formerly mode="eager")
@cmt2.jit         # Reserved for future: JIT-compile simulation kernels
```

**Rationale:** Clear separation of concerns, better type inference, more intuitive naming.

### 3.2 Adopt Structured Type Annotations

**Current:**
```python
def counter_module(max_count: int = 100):
    count = Reg(UInt(32), init=0)
```

**Recommended:**
```python
from cmt2.types import uint, circuit

@cmt2.define
def counter_module(max_count: uint[32]) -> circuit:
    count = Reg(uint[32], init=0)
    ...
```

**Rationale:** Accurate hardware semantics, better IDE support, self-documenting.

### 3.3 Implement Explicit Hardware Control Flow

**Recommended additions:**

```python
from cmt2.control import when, switch, for_loop, while_loop

@cmt2.define
def with_control_flow(data: uint[8]) -> circuit:
    circuit = Circuit("Example")
    
    @circuit.rule("process")
    def process():
        # Hardware conditionals (not Python if!)
        with when(data > 10):
            ...
        
        # Hardware switch
        with switch(data):
            with case(0):
                ...
            with case(1):
                ...
            with default():
                ...
        
        # Hardware loops (must have bounded iterations)
        for i in for_loop(0, 8):  # Unrolls to hardware
            ...
    
    return circuit
```

**Rationale:** Eliminates confusion about Python vs. hardware control flow.

### 3.4 Add Type Promotion and Width Inference Rules

**Recommended:**

```python
# cmt2/types/promotion.py

class TypePromotion:
    """Explicit type promotion rules for hardware operations."""
    
    # Arithmetic operations widen to prevent overflow
    ADD_RESULT_WIDTH = lambda w1, w2: max(w1, w2) + 1
    MUL_RESULT_WIDTH = lambda w1, w2: w1 + w2
    
    # Comparisons always return 1-bit
    CMP_RESULT_WIDTH = 1
    
    # Signedness propagation
    @staticmethod
    def result_signed(op: str, *operand_signed: bool) -> bool:
        # Unsigned arithmetic unless explicitly signed
        if op in ("add", "sub", "mul"):
            return any(operand_signed)  # Signed if any operand signed
        return False
```

**Rationale:** Predictable, documentable, testable behavior.

### 3.5 Implement Source Maps for Better Errors

**Recommended:**

```python
class SourceMap:
    """Maps between Python source and generated IR."""
    
    def add_mapping(self, 
        python_loc: SourceLocation,  # File, line, col
        ir_op: Operation,
        kind: Literal["trace", "lower", "optimize"]
    ):
        ...
    
    def get_python_loc(self, ir_op: Operation) -> SourceLocation:
        ...
    
    def get_ir_ops(self, python_loc: SourceLocation) -> list[Operation]:
        ...
```

**Rationale:** Essential for debugging and error reporting.

### 3.6 Consider Alternative: Embedded DSL with AST Transform

Given the control flow issues with tracing, consider offering an AST-based alternative:

```python
from cmt2 import ast_transform

@ast_transform
def design(data: uint[8]) -> circuit:
    # This function's AST is transformed, not traced
    # Python control flow becomes hardware control flow
    
    if data > 10:  # This IS a hardware conditional!
        ...
    
    for i in range(8):  # This IS a hardware loop!
        ...
    
    return circuit
```

**Tradeoffs:**
- ✅ Python control flow becomes hardware control flow
- ✅ Better error messages
- ❌ Can't use dynamic Python features
- ❌ More complex implementation

---

## 4. Questions for Other Experts

### For Architecture Expert:

1. **Memory Model**: How should the JIT handle memories (SRAMs, register files) with parameterized sizes? Is there a standard pattern for memory inference?

2. **Clock/Reset Domain Crossing**: How should the JIT API handle multiple clock domains and CDC? Is this in scope?

3. **Interface Types**: The proposal shows simple signals. How should the JIT handle complex interface types (AXI, handshakes, credit-based flow control)?

4. **Timing Constraints**: Should the JIT capture timing constraints (setup/hold, clock frequency) in the IR or as metadata?

### For Compiler Expert:

1. **MLIR Integration**: The proposal shows direct MLIR generation. Should we instead generate Python bindings to the existing CMT2 C++ API?

2. **Optimization Pipeline**: What passes should run between "Lowered" and "Compiled" stages? Are there CMT2-specific optimizations?

3. **Cross-Compilation**: How should the JIT handle targeting different backends (simulation vs. synthesis) with different optimization requirements?

4. **Debugging Format**: Should we generate debug info compatible with standard tools (DWARF, VCD waveforms with source mapping)?

### For Product/UX Expert:

1. **Learning Curve**: The staged API is powerful but complex. Is there a simpler "getting started" mode?

2. **Migration Path**: How painful will migration from existing PyCMT2 be? Can we provide automated tools?

3. **Error Messages**: What would ideal error messages look like for common mistakes (width mismatch, control flow confusion)?

---

## 5. Approval Status

**CONDITIONAL APPROVAL**

The CMT2 JIT Proposal is a solid foundation with good architectural decisions informed by successful prior art (JAX, Triton). However, several critical PL issues must be addressed before implementation:

### Required Changes (blocking):

1. **Rename `@cmt2.jit`** to reflect the actual semantic (circuit elaboration, not JIT compilation)
2. **Document Python vs. Hardware control flow** distinction explicitly with examples
3. **Define type promotion and width inference rules** formally
4. **Improve type annotations** for better IDE/type checker support

### Strongly Recommended (non-blocking):

5. Add explicit hardware control flow constructs (`when`, `switch`, etc.)
6. Implement source maps for error reporting
7. Provide `Annotated`-based static argument specification
8. Consider AST-transform alternative for users needing hardware control flow

### Follow-up Reviews Needed:

- Review revised API naming with architecture expert
- Validate type system design with compiler expert
- User testing of error messages and debugging experience

---

## References

- JAX JIT Design: https://jax.readthedocs.io/en/latest/jax-101/02-jitting.html
- Triton Language Reference: https://triton-lang.org/main/programming-guide/chapter-1/introduction.html
- TileLang Documentation: https://github.com/tile-ai/tilelang
- PyShEx (Python Shape Expressions): https://github.com/RDFLib/PyShEx
- TorchDynamo Design: https://pytorch.org/tutorials/intermediate/torchdynamo_tutorial.html
