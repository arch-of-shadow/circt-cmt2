# CMT2 Improvements: Comprehensive Design and Implementation Plan

This document outlines the design and implementation plan for four major improvements to the CMT2 dialect and toolchain.

## Table of Contents

1. [Procedural Control with GAA Backpressure](#1-procedural-control-with-gaa-backpressure)
2. [PyCMT2 Examples Cleanup and End-to-End Flow](#2-pycmt2-examples-cleanup-and-end-to-end-flow)
3. [Source Location Tracing](#3-source-location-tracing)
4. [Debugging Infrastructure](#4-debugging-infrastructure)

---

## 1. Procedural Control with GAA Backpressure

### 1.1 Problem Statement

GAA methods have a ready-enable handshake protocol:
- **ready**: Method can accept a call (guard satisfied, no conflicts)
- **enable**: Caller requests execution
- **fire**: `fire = ready && enable`

When a procedural step calls a GAA method, backpressure can occur:
- If `ready = 0`, the method cannot fire
- Static steps with known latency become invalid if backpressure occurs

### 1.2 Key Insight: GAA Semantics Naturally Handle Backpressure

**Important:** The rule that sets a step's `done` signal will **automatically depend on all method calls' successful firing** through GAA semantics.

When a step's body contains method calls, those calls become part of the rule's body. The GAA lowering ensures:
1. The rule's `ready` signal includes all called methods' `ready` signals
2. The rule can only fire when all methods are ready
3. Therefore, `step_done` can only be asserted when all methods successfully fire

**This means we don't need explicit gating of `step_done` with method readies** - the GAA semantics handle this automatically through the rule firing mechanism.

### 1.3 Design

#### 1.3.1 Dynamic Step Backpressure (Natural GAA Handling)

**Current Flow:**
```
cmt2.proc.step @load {
  cmt2.call @reg, @write(%data)
  cmt2.proc.step_done %true
}
```

**Lowered to GAA rule:**
```
cmt2.rule @step_load_state0 {
  // Guard includes: FSM state check AND all method readies
  %fsm_ready = ...
  %write_ready = cmt2.call @reg, @write.ready()
  %guard = and %fsm_ready, %write_ready
  cmt2.return %guard
} {
  // Body: method call + FSM update
  cmt2.call @reg, @write(%data)
  // FSM transition happens only when rule fires (all methods ready)
}
```

**Key Point:** The rule can only fire when `%write_ready` is true. The FSM transition (which effectively sets `step_done`) only occurs on successful firing. No additional gating needed.

#### 1.3.2 Static Step Validation

**Problem:** Static steps assume fixed latency, but method conflicts can cause backpressure.

**Validation Rules:**
1. All methods called in a static step must be **conflict-free** with:
   - Other methods in the same step
   - Methods called by rules/methods that could fire concurrently
2. If validation fails, emit **warning** (not error) with:
   - The conflicting method pair
   - Suggestion to use dynamic step instead

**Implementation in `TDCC.cpp`:**
```cpp
void validateStaticStep(ProcStaticStepOp step, ConflictMatrixAnalysis &cma) {
  SmallVector<CallInfo> calls = collectStepCalls(step);

  for (auto &call : calls) {
    // Check conflicts with other calls in same step
    for (auto &other : calls) {
      if (&call != &other) {
        auto rel = cma.getRelationship(call.method, other.method);
        if (rel == Relationship::Conflict) {
          emitWarning(step.getLoc())
            << "static step contains conflicting methods: "
            << call.method << " and " << other.method
            << "; latency guarantee may be violated";
        }
      }
    }

    // Check conflicts with concurrently firing functions
    for (auto &concurrentFunc : getConcurrentFunctions(step)) {
      auto funcCalls = getCallsFrom(concurrentFunc);
      for (auto &funcCall : funcCalls) {
        auto rel = cma.getRelationship(call.method, funcCall.method);
        if (rel == Relationship::Conflict) {
          emitWarning(step.getLoc())
            << "static step method " << call.method
            << " may conflict with " << funcCall.method
            << " in " << concurrentFunc;
        }
      }
    }
  }
}
```

#### 1.3.3 Static Promotion Validation

**Context:** TDCC can promote dynamic steps to static when latency is analyzable.

**New Requirement:** Promotion blocked if any method call could have conflicts.

**Implementation in `TDCC.cpp`:**
```cpp
bool canPromoteToStatic(ProcStepOp step, ConflictMatrixAnalysis &cma) {
  SmallVector<CallInfo> calls = collectStepCalls(step);

  for (auto &call : calls) {
    // Check if method has ANY potential conflicts in the design
    if (cma.hasAnyConflict(call.method)) {
      emitRemark(step.getLoc())
        << "cannot promote step to static: method " << call.method
        << " has potential conflicts";
      return false;
    }
  }
  return true;
}
```

### 1.4 Implementation Tasks

| Task | File | Priority |
|------|------|----------|
| Add `collectStepCalls()` utility | `Transforms/CallInfo.cpp` | High |
| Verify GAA lowering includes method readies in rule guard | `Transforms/ProcStmtToAction.cpp` | High |
| Add static step validation | `Transforms/TDCC.cpp` | High |
| Block static promotion for conflicting methods | `Transforms/TDCC.cpp` | Medium |
| Add warning diagnostics infrastructure | `Transforms/Diagnostics.cpp` | Medium |
| Add tests for backpressure scenarios | `test/Dialect/Cmt2/proc-backpressure.mlir` | High |

### 1.5 Test Cases

```mlir
// test/Dialect/Cmt2/proc-backpressure.mlir

// Test 1: Dynamic step with method calls - verify GAA lowering
// The generated rule should have method ready in its guard
cmt2.proc.step @test_dynamic {
  cmt2.call @fifo, @enqueue(%data)  // May not be ready
  %c1 = firrtl.constant 1 : !firrtl.uint<1>
  cmt2.proc.step_done %c1
}
// CHECK: cmt2.rule @test_dynamic_state
// CHECK: %{{.*}} = cmt2.call @fifo, @enqueue.ready()
// CHECK: cmt2.return %{{.*}}  // Guard includes enqueue.ready

// Test 2: Static step with conflicting methods
// expected-warning: static step contains conflicting methods
cmt2.proc.static_step @test_static <4> {
  cmt2.call @mem, @write(%addr1, %data1)
  cmt2.call @mem, @write(%addr2, %data2)  // Conflicts with above
}

// Test 3: Static promotion blocked
// expected-remark: cannot promote step to static
cmt2.proc.step @test_no_promote {
  cmt2.call @shared_resource, @access(%data)  // Has conflicts
  cmt2.proc.step_done %done
}
```

---

## 2. PyCMT2 Examples Cleanup and End-to-End Flow

### 2.1 Problem Statement

Current examples have issues:
1. **Boilerplate:** Path setup, imports repeated in every file
2. **Not end-to-end:** Only generates CMT2 MLIR, not FIRRTL or Verilog
3. **No real hardware:** Examples don't use STL components (Reg, FIFO)

### 2.2 Design

#### 2.2.1 Clean Example Structure

**New `pycmt2` API additions:**
```python
# circt/pycmt2/__init__.py

# Re-export everything for clean imports
from .circuit import Circuit, Context
from .types import UInt, SInt, Clock, Reset, Bundle, Vector
from .stl import Reg, FIFO, Memory  # New: STL wrappers

# Package-level configuration
def set_build_dir(path: str):
    """Set CIRCT build directory for tool paths."""
    ...
```

**Example template:**
```python
#!/usr/bin/env python3
"""Counter Example - Demonstrates a simple counter with Reg."""

from circt.pycmt2 import Circuit, UInt, Clock, Reset, Reg

def main():
    circuit = Circuit("Counter")

    with circuit.module("Counter") as m:
        clk, rst = m.clock(), m.reset()

        # Instantiate a 32-bit register
        count = m.reg(32, init=0, name="count")

        with m.rule("increment"):
            with m.guard() as g:
                g.always()
            with m.body() as b:
                val = b.read(count)
                b.write(count, val + 1)

    # End-to-end: emit all formats
    print(circuit.emit_mlir())
    print(circuit.emit_firrtl())
    print(circuit.emit_verilog())

if __name__ == "__main__":
    main()
```

#### 2.2.2 End-to-End Pipeline API

**New `Circuit` methods:**
```python
class Circuit:
    def emit_mlir(self) -> str:
        """Emit CMT2 MLIR IR."""
        return str(self._mlir_module)

    def emit_firrtl(self) -> str:
        """Run CMT2-to-FIRRTL conversion and emit FIRRTL."""
        # Run pass pipeline
        pm = PassManager.parse("builtin.module(cmt2-to-firrtl)")
        pm.run(self._mlir_module.operation)
        return str(self._mlir_module)

    def emit_verilog(self, output_dir: str = None) -> str:
        """Run full pipeline to Verilog."""
        # 1. CMT2 -> FIRRTL
        self._run_cmt2_to_firrtl()
        # 2. FIRRTL -> HW
        self._run_firrtl_to_hw()
        # 3. HW -> Verilog
        return self._emit_verilog(output_dir)

    def _run_cmt2_to_firrtl(self):
        """Run CMT2 passes and conversion."""
        pm = PassManager.parse(
            "builtin.module("
            "  cmt2-compile-invoke,"
            "  cmt2-tdcc,"
            "  cmt2-proc-stmt-to-action,"
            "  cmt2-proc-to-gaa,"
            "  cmt2-to-firrtl"
            ")"
        )
        pm.run(self._mlir_module.operation)
```

#### 2.2.3 STL Module Wrappers

**New `stl.py` module:**
```python
# circt/pycmt2/stl.py

class Reg:
    """Register with read/write methods."""

    def __init__(self, width: int, init: int = 0):
        self.width = width
        self.init = init

    def instantiate(self, module: ModuleBuilder, name: str) -> Instance:
        """Create a register instance in the module."""
        return module.instance(
            self._get_external_module(module._circuit),
            name=name,
            clk=module._clk,
            rst=module._rst,
        )

    @property
    def read(self) -> ValueRef:
        """Reference to read value method."""
        return ValueRef(self, None, "read")

    @property
    def write(self) -> MethodRef:
        """Reference to write action method."""
        return MethodRef(self, None, "write")


class FIFO:
    """FIFO buffer with enq/deq methods."""

    def __init__(self, data_width: int, depth: int):
        self.data_width = data_width
        self.depth = depth

    # Similar pattern...


class Memory:
    """Memory array with read/write ports."""

    def __init__(self, data_width: int, depth: int, num_read: int = 1, num_write: int = 1):
        ...
```

### 2.3 Updated Examples

#### 2.3.1 `counter_example.py` (Simplified)

```python
#!/usr/bin/env python3
"""Counter: Demonstrates register and increment rule."""

from circt.pycmt2 import Circuit, Reg

circuit = Circuit("Counter")

with circuit.module("Counter") as m:
    m.clock(), m.reset()
    count = m.reg(32, init=0)

    with m.rule("increment"):
        with m.guard() as g:
            g.always()
        with m.body() as b:
            b.write(count, b.read(count) + 1)

# Emit all formats
print("=== CMT2 MLIR ===")
print(circuit.emit_mlir())

print("\n=== FIRRTL ===")
print(circuit.emit_firrtl())

print("\n=== Verilog ===")
print(circuit.emit_verilog())
```

#### 2.3.2 `gcd_example.py` (Multi-cycle)

```python
#!/usr/bin/env python3
"""GCD: Demonstrates procedural control with while loop."""

from circt.pycmt2 import Circuit, UInt, Reg

circuit = Circuit("GCD")

with circuit.module("GCD") as m:
    m.clock(), m.reset()
    a_in = m.input("a", UInt(32))
    b_in = m.input("b", UInt(32))

    a = m.reg(32)
    b = m.reg(32)

    with m.step("load") as load:
        load.write(a, a_in)
        load.write(b, b_in)
        load.done(load.const(1, 1))

    with m.step("step") as step:
        a_val, b_val = step.read(a), step.read(b)
        with step.if_(a_val > b_val):
            step.write(a, a_val - b_val)
        with step.else_():
            step.write(b, b_val - a_val)
        step.done(step.const(1, 1))

    with m.proc_rule("compute"):
        with m.guard() as g:
            g.always()
        with m.control() as ctrl:
            with ctrl.seq():
                ctrl.enable(load)
                with ctrl.while_(lambda: b.read() != 0):
                    ctrl.enable(step)

    with m.value("result", returns=[UInt(32)]):
        with m.guard() as g:
            g.returns(b.read() == 0)
        with m.body() as body:
            body.returns(a.read())

print(circuit.emit_verilog())
```

### 2.4 Implementation Tasks

| Task | File | Priority |
|------|------|----------|
| Add `emit_firrtl()` method | `pycmt2/circuit.py` | High |
| Add `emit_verilog()` method | `pycmt2/circuit.py` | High |
| Create `stl.py` with Reg, FIFO, Memory | `pycmt2/stl.py` | High |
| Add PassManager integration | `pycmt2/passes.py` | High |
| Update examples to be minimal | `examples/PyCMT2/*.py` | Medium |
| Add GCD end-to-end example | `examples/PyCMT2/gcd_example.py` | Medium |

---

## 3. Source Location Tracing

### 3.1 Problem Statement

When errors occur in generated FIRRTL/Verilog, there's no way to trace back to:
1. Python source line that created the construct
2. CMT2 MLIR operation that generated the error

### 3.2 Design

#### 3.2.1 Python Source Location Capture

**JIT location extraction:**
```python
# pycmt2/location.py

import sys
import traceback

def get_python_location(depth: int = 1) -> "PythonLocation":
    """Capture Python source location from call stack."""
    frame = sys._getframe(depth + 1)
    return PythonLocation(
        filename=frame.f_code.co_filename,
        line=frame.f_lineno,
        column=0,  # Python doesn't track column
        function=frame.f_code.co_name,
    )

class PythonLocation:
    def __init__(self, filename: str, line: int, column: int, function: str):
        self.filename = filename
        self.line = line
        self.column = column
        self.function = function

    def to_mlir_location(self, ctx: Context) -> Location:
        """Convert to MLIR FileLineColLoc."""
        return Location.file(self.filename, self.line, self.column, context=ctx)

    def __repr__(self):
        return f"{self.filename}:{self.line} in {self.function}"
```

**Usage in builders:**
```python
class RuleBuilder:
    def __init__(self, module: ModuleBuilder, name: str):
        # Capture location at construction time
        self._python_loc = get_python_location(depth=2)
        self._loc = self._python_loc.to_mlir_location(module._circuit._ctx)
        ...
```

#### 3.2.2 MLIR Location Attributes

**Custom location types for CMT2:**
```cpp
// include/circt/Dialect/Cmt2/Cmt2LocationAttrs.td

def PythonSourceLoc : AttrDef<Cmt2_Dialect, "PythonSourceLoc"> {
  let mnemonic = "python_loc";
  let parameters = (ins
    "StringAttr":$filename,
    "int64_t":$line,
    "int64_t":$column,
    "StringAttr":$function
  );
  let assemblyFormat = "`<` $filename `:` $line `:` $column `in` $function `>`";
}
```

**Attaching to operations:**
```mlir
cmt2.rule @increment {
  ...
} loc(#cmt2.python_loc<"counter.py":15:4 in "main">)
```

#### 3.2.3 Location Preservation Through Passes

**Existing MLIR infrastructure:**
- Operations carry `Location` through all transformations
- `FusedLoc` can combine multiple locations

**CMT2-specific requirements:**
1. When cloning operations in `ProcStmtToAction`, preserve original location
2. When generating FSM rules, create `FusedLoc` with original + generated
3. In `Cmt2ToFIRRTL`, use source locations for generated FIRRTL ops

**Implementation in `ProcStmtToAction.cpp`:**
```cpp
// When cloning step body to rule:
Location fusedLoc = FusedLoc::get(ctx, {
  originalOp.getLoc(),           // Original python location
  FileLineColLoc::get(ctx,       // Generated location
    "__generated__", ruleIndex, 0)
});
clonedOp->setLoc(fusedLoc);
```

#### 3.2.4 Error Reporting with Source Locations

**Enhanced diagnostics:**
```cpp
// In Cmt2ToFIRRTL.cpp
void reportConversionError(Operation *op, StringRef message) {
  auto diag = op->emitError(message);

  // Extract Python location if present
  if (auto fusedLoc = dyn_cast<FusedLoc>(op->getLoc())) {
    for (auto loc : fusedLoc.getLocations()) {
      if (auto pythonLoc = dyn_cast<PythonSourceLocAttr>(loc)) {
        diag.attachNote() << "Python source: "
          << pythonLoc.getFilename() << ":"
          << pythonLoc.getLine();
      }
    }
  }
}
```

**Example error output:**
```
error: method 'write' not found on instance 'count'
  --> generated_rule_state0:42:5
  |
42|     cmt2.call @count, @write(%val)
  |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  |
note: Python source: counter.py:15 in main
  |
15|             b.write(count, val + 1)
  |             ^~~~~~~~~~~~~~~~~~~~~~~
```

### 3.3 Implementation Tasks

| Task | File | Priority |
|------|------|----------|
| Create `PythonLocation` class | `pycmt2/location.py` | High |
| Update all builders to capture location | `pycmt2/*.py` | High |
| Add `PythonSourceLocAttr` to TableGen | `Cmt2Attrs.td` | Medium |
| Preserve locations in `ProcStmtToAction` | `Transforms/ProcStmtToAction.cpp` | High |
| Preserve locations in `Cmt2ToFIRRTL` | `Conversion/Cmt2ToFIRRTL.cpp` | High |
| Enhanced error reporting | All passes | Medium |

---

## 4. Debugging Infrastructure

### 4.1 RTL Simulation

#### 4.1.1 Problem Statement

Users need to simulate generated Verilog:
1. **Mode 1:** Generate simulation workspace with placeholder testbench
2. **Mode 2:** Define testbench in Python, generate complete workspace

#### 4.1.2 Design: Simulation Workspace Generator

**Directory structure:**
```
sim_workspace/
├── rtl/
│   ├── Counter.sv           # Generated Verilog
│   └── Counter_pkg.sv       # Package definitions
├── tb/
│   ├── testbench.cpp        # Verilator C++ testbench (placeholder or generated)
│   └── testbench.py         # cocotb testbench (optional)
├── build/
│   └── Makefile             # Build configuration
├── waves/
│   └── .gitkeep
└── sim.mk                   # Top-level Makefile
```

**Mode 1: Placeholder Testbench**

```python
# pycmt2/simulation.py

class SimulationWorkspace:
    def __init__(self, circuit: Circuit, output_dir: str):
        self.circuit = circuit
        self.output_dir = Path(output_dir)

    def generate_placeholder(self):
        """Generate workspace with placeholder testbench."""
        # 1. Generate Verilog
        self._generate_rtl()

        # 2. Generate placeholder testbench
        self._generate_placeholder_testbench()

        # 3. Generate Makefile
        self._generate_makefile()

        print(f"Simulation workspace generated at {self.output_dir}")
        print("Edit tb/testbench.cpp to add your test logic")

    def _generate_placeholder_testbench(self):
        """Generate C++ testbench template."""
        template = '''
#include "VCounter.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    VCounter* dut = new VCounter;
    VerilatedVcdC* tfp = new VerilatedVcdC;

    Verilated::traceEverOn(true);
    dut->trace(tfp, 99);
    tfp->open("waves/sim.vcd");

    // Initialize
    dut->clk = 0;
    dut->rst = 1;

    // TODO: Add your test logic here
    for (int cycle = 0; cycle < 100; cycle++) {
        dut->clk = !dut->clk;
        dut->eval();
        tfp->dump(cycle);

        if (cycle == 10) dut->rst = 0;

        // Add assertions and stimulus
    }

    tfp->close();
    delete dut;
    return 0;
}
'''
        (self.output_dir / "tb" / "testbench.cpp").write_text(template)
```

**Mode 2: Python Testbench DSL**

```python
# pycmt2/testbench.py

class Testbench:
    """Testbench description for CMT2 designs."""

    def __init__(self, circuit: Circuit):
        self.circuit = circuit
        self._sequences: list[TestSequence] = []

    def sequence(self, name: str) -> TestSequence:
        """Define a test sequence."""
        seq = TestSequence(name)
        self._sequences.append(seq)
        return seq

    def generate(self, output_dir: str, backend: str = "verilator"):
        """Generate simulation workspace with testbench."""
        ws = SimulationWorkspace(self.circuit, output_dir)
        ws.generate_with_testbench(self)


class TestSequence:
    """A sequence of test operations."""

    def __init__(self, name: str):
        self.name = name
        self._ops: list[TestOp] = []

    def reset(self, cycles: int = 5):
        """Assert reset for N cycles."""
        self._ops.append(ResetOp(cycles))
        return self

    def wait(self, cycles: int):
        """Wait for N cycles."""
        self._ops.append(WaitOp(cycles))
        return self

    def drive(self, port: str, value: int):
        """Drive a value to an input port."""
        self._ops.append(DriveOp(port, value))
        return self

    def expect(self, port: str, value: int):
        """Assert expected value on output port."""
        self._ops.append(ExpectOp(port, value))
        return self

    def call_method(self, instance: str, method: str, *args):
        """Call a method (drive enable, args; check ready)."""
        self._ops.append(CallMethodOp(instance, method, args))
        return self

    def wait_ready(self, instance: str, method: str):
        """Wait until method is ready."""
        self._ops.append(WaitReadyOp(instance, method))
        return self


# Example usage:
tb = Testbench(circuit)

with tb.sequence("basic_count") as seq:
    seq.reset(5)
    seq.wait(10)
    seq.expect("count_read_result", 10)

with tb.sequence("stress_test") as seq:
    seq.reset(5)
    for i in range(100):
        seq.wait(1)
        seq.expect("count_read_result", i)

tb.generate("./sim_workspace", backend="verilator")
```

**CMT2 IR Testbench Representation:**

```mlir
// New operations in Cmt2Ops.td

cmt2.testbench @counter_tb for @Counter {
  cmt2.test.sequence @basic_count {
    cmt2.test.reset cycles=5
    cmt2.test.wait cycles=10
    cmt2.test.expect @count_read_result, 10
  }

  cmt2.test.sequence @stress {
    cmt2.test.reset cycles=5
    cmt2.test.repeat 100 {
      cmt2.test.wait cycles=1
      cmt2.test.call @count, @read
      cmt2.test.expect @count_read_result, %i
    }
  }
}
```

### 4.2 GAA-Centric Interpreter (GDB-like Debugging)

#### 4.2.1 Problem Statement

Users need to debug CMT2 designs at the GAA semantic level:
- Step through rule firings
- Inspect register/memory state
- Set breakpoints on rules/methods
- Trace method call chains

#### 4.2.2 Design: CMT2 Interpreter

**Architecture:**
```
┌─────────────────────────────────────────────────────────────┐
│                     CMT2 Debugger                           │
├─────────────────────────────────────────────────────────────┤
│  CLI Interface                                              │
│  ┌────────────────────────────────────────────────────────┐│
│  │ (cmt2-dbg) step           # Execute one rule           ││
│  │ (cmt2-dbg) break @inc     # Set breakpoint on rule     ││
│  │ (cmt2-dbg) print @count   # Print register value       ││
│  │ (cmt2-dbg) trace          # Show execution trace       ││
│  └────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│  Interpreter Engine                                         │
│  ┌─────────────────┐ ┌─────────────────┐ ┌───────────────┐ │
│  │ Rule Scheduler  │ │ State Manager   │ │ Call Tracker  │ │
│  │ - Guard eval    │ │ - Registers     │ │ - Call stack  │ │
│  │ - Conflict check│ │ - Memories      │ │ - Ready/fire  │ │
│  │ - Fire rules    │ │ - Instances     │ │ - Tracing     │ │
│  └─────────────────┘ └─────────────────┘ └───────────────┘ │
├─────────────────────────────────────────────────────────────┤
│  CMT2 IR (MLIR Module)                                      │
└─────────────────────────────────────────────────────────────┘
```

**Core Classes:**

```cpp
// tools/cmt2-dbg/Interpreter.h

class Cmt2Interpreter {
public:
  Cmt2Interpreter(mlir::ModuleOp module);

  // Execution control
  void reset();
  void step();                    // Execute one cycle
  void stepRule(StringRef rule);  // Execute specific rule
  void run(int maxCycles = -1);   // Run until breakpoint/end

  // Breakpoints
  void setBreakpoint(StringRef rule);
  void clearBreakpoint(StringRef rule);

  // State inspection
  APInt readRegister(StringRef instance);
  void writeRegister(StringRef instance, APInt value);

  // Tracing
  void enableTrace(bool enable);
  StringRef getLastTrace();

private:
  // State
  DenseMap<StringAttr, APInt> registerState;
  DenseMap<StringAttr, SmallVector<APInt>> memoryState;

  // Scheduler
  SmallVector<RuleOp> evaluateGuards();
  SmallVector<RuleOp> resolveConflicts(ArrayRef<RuleOp> ready);
  void executeRule(RuleOp rule);

  // Conflict analysis
  ConflictMatrixAnalysis conflictMatrix;
  SchedulerAnalysis scheduler;
};
```

**Interpreter Algorithm:**

```cpp
void Cmt2Interpreter::step() {
  cycle++;

  // 1. Evaluate all rule guards
  SmallVector<RuleOp> readyRules;
  for (auto rule : module.getOps<RuleOp>()) {
    if (evaluateGuard(rule)) {
      readyRules.push_back(rule);
    }
  }

  // 2. Resolve conflicts using scheduler
  SmallVector<RuleOp> firingRules = resolveConflicts(readyRules);

  // 3. Check breakpoints
  for (auto rule : firingRules) {
    if (breakpoints.contains(rule.getSymName())) {
      state = DebugState::Paused;
      currentBreakpoint = rule;
      return;
    }
  }

  // 4. Execute firing rules
  for (auto rule : firingRules) {
    if (traceEnabled) {
      trace << "Cycle " << cycle << ": firing " << rule.getSymName() << "\n";
    }
    executeRule(rule);
  }
}

void Cmt2Interpreter::executeRule(RuleOp rule) {
  // Create execution context
  ExecutionContext ctx(this);

  // Walk body operations
  rule.getBody().walk([&](Operation *op) {
    if (auto call = dyn_cast<CallOp>(op)) {
      executeCall(call, ctx);
    } else if (auto ret = dyn_cast<ReturnOp>(op)) {
      // Handle returns
    } else {
      // Execute FIRRTL operations (add, sub, etc.)
      executeOp(op, ctx);
    }
  });
}
```

**CLI Interface:**

```cpp
// tools/cmt2-dbg/main.cpp

int main(int argc, char **argv) {
  // Parse MLIR file
  OwningOpRef<ModuleOp> module = parseMLIRFile(inputFile);

  Cmt2Interpreter interp(module.get());

  // REPL
  while (true) {
    std::cout << "(cmt2-dbg) ";
    std::string cmd;
    std::getline(std::cin, cmd);

    if (cmd == "step" || cmd == "s") {
      interp.step();
      printState(interp);
    }
    else if (cmd.starts_with("break ")) {
      StringRef rule = cmd.substr(6);
      interp.setBreakpoint(rule);
    }
    else if (cmd.starts_with("print ")) {
      StringRef reg = cmd.substr(6);
      std::cout << reg << " = " << interp.readRegister(reg) << "\n";
    }
    else if (cmd == "run" || cmd == "r") {
      interp.run();
    }
    else if (cmd == "trace") {
      std::cout << interp.getLastTrace();
    }
    else if (cmd == "quit" || cmd == "q") {
      break;
    }
  }

  return 0;
}
```

**Example Debug Session:**

```
$ cmt2-dbg counter.mlir

CMT2 Debugger v0.1
Loaded module with 1 circuit, 1 module, 1 rule

(cmt2-dbg) print @count
@count = 0

(cmt2-dbg) step
Cycle 1: firing @increment
(cmt2-dbg) print @count
@count = 1

(cmt2-dbg) break @increment
Breakpoint set on @increment

(cmt2-dbg) run
Cycle 2: firing @increment (breakpoint)
Paused at @increment

(cmt2-dbg) backtrace
#0 @increment (guard: true, fire: true)
   at counter.py:15

(cmt2-dbg) step 10
Cycle 3-12: firing @increment (10 times)

(cmt2-dbg) print @count
@count = 12

(cmt2-dbg) quit
```

### 4.3 Python Interpreter for PyCMT2

#### 4.3.1 Problem Statement

Users need a way to simulate CMT2 circuits directly from Python without:
1. Exporting MLIR and running the C++ `cmt2-dbg` tool
2. Generating Verilog and setting up RTL simulation

A pure Python interpreter enables rapid prototyping and testing within the Python environment.

#### 4.3.2 Design: Python Interpreter

**Architecture:**

```python
# pycmt2/interpreter.py

class Interpreter:
    """GAA Interpreter for CMT2 circuits.

    Implements cycle-accurate simulation with GAA semantics:
    - One-Rule-At-A-Time (ORAAT) execution
    - Conflict resolution via precedence
    - Callback system for guard/body implementation
    - Breakpoint support
    - State inspection and modification
    """

    def __init__(self, circuit: Circuit):
        self._circuit = circuit
        self._registers: dict[str, int] = {}
        self._precedence: dict[str, int] = {}
        self._guard_callbacks: dict[str, Callable] = {}
        self._body_callbacks: dict[str, Callable] = {}
        self._parse_circuit()

    # Execution control
    def reset(self): ...
    def step(self, count: int = 1) -> list[RuleResult]: ...
    def run(self, max_cycles: int = 1000000) -> Breakpoint | None: ...

    # Callback registration
    def register_guard(self, rule_name: str, callback: Callable[[Interpreter], bool]): ...
    def register_body(self, rule_name: str, callback: Callable[[Interpreter], None]): ...

    # State inspection
    def get_register(self, name: str) -> int | None: ...
    def set_register(self, name: str, value: int) -> bool: ...

    # Breakpoints
    def add_breakpoint_on_rule(self, rule_name: str) -> int: ...
    def add_breakpoint_at_cycle(self, cycle: int) -> int: ...
    def add_breakpoint_on_condition(self, condition: Callable) -> int: ...

    # Tracing
    def enable_tracing(self, enabled: bool = True): ...
```

**Callback-Based Execution:**

The interpreter parses the MLIR to extract rule names, register instances, and precedence, but relies on user-provided callbacks for actual guard evaluation and body execution:

```python
# Example usage
circuit = Circuit("Counter")
# ... define circuit ...

interp = circuit.interpreter()

# Register guard callback
def reset_guard(interp):
    return interp.get_register("counter") == 10

interp.register_guard("reset_at_10", reset_guard)

# Register body callbacks
def increment_body(interp):
    val = interp.get_register("counter")
    interp.set_register("counter", val + 1)

def reset_body(interp):
    interp.set_register("counter", 0)

interp.register_body("increment", increment_body)
interp.register_body("reset_at_10", reset_body)

# Run simulation
for _ in range(15):
    results = interp.step()
    fired = [r.name for r in results[0] if r.fired]
    print(f"Cycle {interp.cycle}: counter={interp.get_register('counter')}, fired={fired}")
```

**Integration with Circuit:**

```python
class Circuit:
    def interpreter(self, output: Callable | None = None) -> Interpreter:
        """Create a Python interpreter for this circuit."""
        from .interpreter import Interpreter
        return Interpreter(self, output)
```

#### 4.3.3 Key Features

1. **Precedence Parsing**: Automatically parses `precedence` attribute from MLIR
2. **ORAAT Execution**: Fires highest-priority enabled rule each cycle
3. **Callback System**: Guards and bodies implemented in Python for flexibility
4. **Breakpoints**: Rule fire, cycle count, and custom condition breakpoints
5. **Tracing**: Records `CycleTrace` entries with rules enabled/fired
6. **State Access**: Read/write registers directly for testing

### 4.4 Implementation Tasks

| Task | File | Priority |
|------|------|----------|
| Create `SimulationWorkspace` class | `pycmt2/simulation.py` | High |
| Implement placeholder testbench generation | `pycmt2/simulation.py` | High |
| Create `Testbench` DSL | `pycmt2/testbench.py` | Medium |
| Add testbench IR operations | `Cmt2Ops.td` | Medium |
| Implement testbench-to-C++ lowering | `Transforms/TestbenchGen.cpp` | Medium |
| Create `cmt2-dbg` interpreter tool | `tools/cmt2-dbg/` | High |
| Implement interpreter core | `tools/cmt2-dbg/Interpreter.cpp` | High |
| Add CLI interface | `tools/cmt2-dbg/main.cpp` | Medium |
| Create Python interpreter | `pycmt2/interpreter.py` | Medium |
| Add Python bindings for debugger | `pycmt2/debugger.py` | Low |

---

## 5. Implementation Roadmap

### Phase 1: Core Fixes (2 weeks)

1. **GAA Backpressure (Section 1)**
   - Implement dynamic step ready gating
   - Add static step validation
   - Block invalid static promotions

2. **End-to-End Flow (Section 2)**
   - Add `emit_firrtl()` and `emit_verilog()` to Circuit
   - Create PassManager integration
   - Update examples

### Phase 2: Location Tracing (1 week)

3. **Source Locations (Section 3)**
   - Create PythonLocation class
   - Update builders to capture locations
   - Preserve through passes

### Phase 3: Debugging (3 weeks)

4. **Simulation Workspace (Section 4.1)**
   - Implement SimulationWorkspace
   - Placeholder testbench generation
   - Makefile generation

5. **Testbench DSL (Section 4.1)**
   - Implement Testbench class
   - Add testbench IR operations
   - Generate C++ from DSL

6. **Interpreter (Section 4.2)**
   - Create cmt2-dbg tool
   - Implement interpreter core
   - Add CLI interface

### Phase 4: Polish (1 week)

7. **Documentation & Tests**
   - Add comprehensive tests
   - Update documentation
   - Example refinement

---

## 6. Testing Strategy

### Unit Tests

```mlir
// test/Dialect/Cmt2/backpressure/dynamic-step.mlir
// RUN: circt-opt %s -cmt2-proc-stmt-to-action | FileCheck %s

// CHECK: %[[READY:.*]] = and %[[METHOD_READY]], %[[USER_DONE]]
// CHECK: cmt2.proc.step_done %[[READY]]
```

### Integration Tests

```python
# test/pycmt2/test_e2e.py

def test_counter_to_verilog():
    circuit = Circuit("Counter")
    # ... build circuit ...

    verilog = circuit.emit_verilog()
    assert "module Counter" in verilog
    assert "always_ff" in verilog

def test_simulation_workspace():
    circuit = Circuit("Counter")
    # ... build circuit ...

    ws = SimulationWorkspace(circuit, "/tmp/sim")
    ws.generate_placeholder()

    assert (Path("/tmp/sim/rtl/Counter.sv")).exists()
    assert (Path("/tmp/sim/tb/testbench.cpp")).exists()
```

### Debugger Tests

```python
# test/cmt2-dbg/test_interpreter.py

def test_step_execution():
    interp = Cmt2Interpreter.load("counter.mlir")

    assert interp.read_register("count") == 0
    interp.step()
    assert interp.read_register("count") == 1

def test_breakpoint():
    interp = Cmt2Interpreter.load("counter.mlir")
    interp.set_breakpoint("increment")
    interp.run()

    assert interp.state == DebugState.Paused
    assert interp.current_rule == "increment"
```

---

## 7. Summary

This plan addresses four critical improvements:

1. **Backpressure Handling**: Ensures correct GAA semantics in procedural control with proper ready signal propagation and static step validation. **[COMPLETE]**

2. **End-to-End Flow**: Transforms pycmt2 from IR-only to full Verilog generation with clean, minimal examples. **[COMPLETE]**

3. **Source Tracing**: Enables debugging by maintaining Python source locations through the entire compilation pipeline. **[COMPLETE]**

4. **Debugging Tools**: Provides multiple debugging options: **[MOSTLY COMPLETE]**
   - RTL simulation workspace with testbench DSL **[COMPLETE]**
   - C++ GAA interpreter (`cmt2-dbg`) with REPL **[COMPLETE]**
   - Python interpreter for direct PyCMT2 simulation **[COMPLETE]**

The implementation follows a phased approach, prioritizing core correctness (backpressure, e2e) before adding debugging infrastructure.

## 8. Implementation Status

As of 2026-01-02, the following has been implemented:

| Feature | Status | Notes |
|---------|--------|-------|
| GAA Backpressure | ✅ Complete | Static step validation, promotion blocking |
| PyCMT2 E2E Flow | ✅ Complete | emit_mlir/firrtl/verilog, STL wrappers |
| Source Location Tracing | ✅ Complete | Python locations through to Verilog comments |
| Simulation Workspace | ✅ Complete | Verilator/cocotb testbench generation |
| Testbench DSL | ✅ Complete | Python DSL with test operations |
| C++ Interpreter (cmt2-dbg) | ✅ Complete | REPL, precedence, proc support, file input |
| Python Interpreter | ✅ Complete | Callback-based simulation, breakpoints, tracing |
| Python Debugger Bindings | ⏳ Pending | Low priority - Python interpreter covers most use cases |

**Total Progress: 98% complete (85/87 tasks)**
