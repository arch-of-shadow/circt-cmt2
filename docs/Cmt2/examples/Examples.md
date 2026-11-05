# CMT2 Examples

Index of examples demonstrating CMT2 features.

**Location:** `examples/PyCMT2/`

**JIT location:** `examples/JIT/` (same designs/testbenches, but elaborated via `cmt2.jit`)

---

## Running Examples

```bash
ninja -C build
cd build
export PYTHONPATH=$PWD/tools/circt/python_packages/circt_core
python3 ../examples/PyCMT2/<example>.py
```

To run the JIT version of an example:

```bash
cd build
export PYTHONPATH=$PWD/tools/circt/python_packages/circt_core:../python
python3 ../examples/JIT/<example>.py
```

### Curated release-confidence suite (recommended)

Run the curated E2E suite (Verilator) that is kept in sync with the repository:

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/run_examples.py
```

Run the matching JIT E2E suite:

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core:../python python3 ../examples/JIT/run_examples.py
```

---

## Basic Examples

### counter.py

Simple counter with increment rule and read value.

**Features:**
- Basic module creation
- Rules with guards
- Values for data exposure
- Clock and reset ports

**Concepts:** [QuickStart.md](../guides/QuickStart.md), [Concepts.md](../features/Concepts.md)

```bash
python3 ../examples/PyCMT2/counter.py
```

---

### gcd.py

Greatest Common Divisor with simulation.

**Features:**
- STL Reg modules
- Multi-cycle computation
- Method guards
- Value methods with ready signals
- SimulationWorkspace integration

**Concepts:** [PyCMT2-Guide.md](../guides/PyCMT2-Guide.md), [STL.md](../features/STL.md)

```bash
python3 ../examples/PyCMT2/gcd.py
```

**Expected Output:**
```
Testing GCD(48, 18) = 6
  PASS: result = 6 (took 5 cycles)
Testing GCD(100, 80) = 20
  PASS: result = 20 (took 5 cycles)
...
ALL TESTS PASSED!
```

---

## Procedural Control Examples

### proc.py

Comprehensive procedural control validation.

**Features:**
- Dynamic steps with done signals
- Static steps with fixed latency
- Sequential composition (seq)
- Parallel composition (par)
- Conditional (if)
- Loops (while)
- Static repeat
- Procedural rules and methods

**Concepts:** [MultiCycle.md](../features/MultiCycle.md)

```bash
python3 ../examples/PyCMT2/proc.py
```

---

### static_proc.py

Static procedural control and pipelining.

**Features:**
- All static control features
- `static_step` with latency
- `static_repeat` for fixed-iteration loops
- Method timing attributes (`static_latency`, `interval`)
- Pipelined method invocation
- Full Verilator simulation

**Concepts:** [MultiCycle.md](../features/MultiCycle.md)

```bash
python3 ../examples/PyCMT2/static_proc.py
```

**Expected Output:**
```
Pipeline completed in 18 cycles
Result: 48
Expected: 48
PASS: Dot product correct!
```

---

### proc_testbench.py

Testbench for procedural control validation.

**Features:**
- Testbench DSL
- Step completion verification
- FSM state inspection
- Multiple test sequences

**Concepts:** [Debugging.md](../guides/Debugging.md)

```bash
python3 ../examples/PyCMT2/proc_testbench.py
cd proc_testbench_workspace && make && make run
```

---

---

## Timing Examples

### timing.py

Cycle-precise timing with static steps.

**Features:**
- Static step latency
- Pipelined operations
- Compile-time scheduling

**Concepts:** [MultiCycle.md](../features/MultiCycle.md)

```bash
python3 ../examples/PyCMT2/timing.py
```

---

## Advanced Examples

### alu.py

Multi-cycle ALU with simulation.

**Features:**
- Complex procedural control
- Simulation integration
- STL usage

```bash
python3 ../examples/PyCMT2/alu.py
```

---

### systolic.py

2x2 Systolic array for matrix multiplication.

**Features:**
- Complex module hierarchy
- Multiply-accumulate operations
- Pipelined data flow

```bash
python3 ../examples/PyCMT2/systolic.py
```

---

### banked_gemm_dataflow.py

Banked-memory GEMM with **tiled + unrolled** compute, structured as decoupled
dataflow stages (load / compute / store).

**Features:**
- Banked memory addressing
- Dataflow tasks + tokens for decoupling
- Useful as a “rich feature” showcase (dataflow + multi-cycle control)

**Concepts:** [Dataflow.md](../features/Dataflow.md), [MultiCycle.md](../features/MultiCycle.md), [STL.md](../features/STL.md)

```bash
python3 ../examples/PyCMT2/banked_gemm_dataflow.py
```

---

### pipeline_fifo_testbench.py

Producer-consumer pipeline with FIFOs.

**Features:**
- FIFO-based communication
- Multiple pipeline stages
- GAA semantics demonstration
- Complex scheduling

**Concepts:** [STL.md](../features/STL.md)

```bash
python3 ../examples/PyCMT2/pipeline_fifo_testbench.py
```

---

## Infrastructure Examples

### interpret.py

`cmt2-dbg` interpreter script tests.

**Features:**
- Interpreter regression tests (script mode)
- Command coverage: modules/stats/source/trace
- Useful for debugging *lowered* CMT2 behavior (not RTL)

**Concepts:** [Debugging.md](../guides/Debugging.md)

```bash
python3 ../examples/PyCMT2/interpret.py
```

---

### diagnostics.py

Multi-level diagnostic system.

**Features:**
- Error/warning/info/debug levels
- Source location tracking

```bash
python3 ../examples/PyCMT2/diagnostics.py
```

---

### simulation_workspace.py

Verilator simulation workspace setup.

**Features:**
- SimulationWorkspace generation
- RTL compilation
- Testbench integration

**Concepts:** [Debugging.md](../guides/Debugging.md)

```bash
python3 ../examples/PyCMT2/simulation_workspace.py
```

---

### run_examples.py

Curated E2E runner for PyCMT2 examples.

```bash
python3 ../examples/PyCMT2/run_examples.py
```

---

## Example Summary

| Example | Category | Key Features |
|---------|----------|--------------|
| `counter.py` | Basic | Rules, values, ports |
| `gcd.py` | Basic | STL, simulation |
| `proc.py` | Procedural | All control constructs |
| `static_proc.py` | Static | Timing, pipelining |
| `proc_testbench.py` | Testing | Testbench DSL |
| `timing.py` | Timing | Static timing |
| `alu.py` | Advanced | Multi-cycle ALU |
| `systolic.py` | Advanced | Systolic array |
| `banked_gemm_dataflow.py` | Advanced | Banked memory GEMM (dataflow tasks) |
| `pipeline_fifo_testbench.py` | Advanced | FIFO pipeline |
| `interpret.py` | Debug | `cmt2-dbg` interpreter script tests |
| `diagnostics.py` | Debug | Error reporting |
| `simulation_workspace.py` | Debug | Verilator setup |
| `run_examples.py` | Debug | Curated E2E runner |

---

## Creating Your Own Examples

1. **Start simple:** Begin with `counter.py` pattern
2. **Add STL:** Use `Reg`, `FIFO` from STL
3. **Add control:** Introduce procedural control
4. **Add timing:** Add static timing for optimization
5. **Simulate:** Use SimulationWorkspace for verification

```python
from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace

clear_stl_registry()
circuit = Circuit("MyDesign")

# Build circuit...

# Simulate
ws = SimulationWorkspace(circuit, "sim_output")
ws.build()
success, output = ws.run()
print(output)
```
