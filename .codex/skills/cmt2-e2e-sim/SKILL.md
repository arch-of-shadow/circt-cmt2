---
name: cmt2-e2e-sim
description: End-to-end CMT2 simulation using PyCMT2 ecosystem. Use this skill for compiling CMT2 designs to Verilog, creating simulation workspaces with Verilator, running simulations, using the Python interpreter, or debugging with cmt2-dbg. Supports PyCMT2 Python DSL and MLIR inputs.
allowed-tools: Bash(ninja:*), Bash(build/bin/*:*), Bash(verilator:*), Bash(make:*), Bash(mkdir:*), Bash(cat:*), Bash(ls:*), Bash(python*:*), Bash(PYTHONPATH=*:*)
---

# CMT2 End-to-End Simulation Skill

This skill provides comprehensive simulation and debugging support for CMT2 designs using the PyCMT2 ecosystem.

## Overview

The PyCMT2 ecosystem provides multiple simulation and debugging options:

```
                    ┌─────────────────────────────────────────────────────┐
                    │                 PyCMT2 Ecosystem                     │
                    ├─────────────────────────────────────────────────────┤
PyCMT2 (.py) ───┐   │  1. SimulationWorkspace  → Verilator C++ Simulation │
                ├─→ │  2. Python Interpreter   → Pure Python Simulation   │
CMT2 MLIR (.mlir)   │  3. cmt2-dbg CLI         → Interactive GAA Debugger │
                    └─────────────────────────────────────────────────────┘
```

## Quick Start

### Option 1: PyCMT2 SimulationWorkspace (Recommended)

Create a complete Verilator-based simulation workspace:

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 \
    ../examples/PyCMT2/simulation_workspace_example.py
```

This generates:
- `rtl/` - Generated SystemVerilog
- `tb/` - Verilator C++ testbench
- `Makefile` - Build automation
- `waves/` - VCD output directory

### Option 2: Python Interpreter

For rapid prototyping without RTL generation:

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 \
    ../examples/PyCMT2/interpreter_example.py
```

### Option 3: cmt2-dbg Interactive Debugger

For GDB-like debugging at GAA semantic level:

```bash
build/bin/cmt2-dbg test/Dialect/Cmt2/gcd.mlir
```

Commands: `step`, `run`, `break @rulename`, `print @reg`, `trace`, `quit`

## Detailed Workflows

### PyCMT2 SimulationWorkspace API

```python
from circt.pycmt2 import Circuit
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry

# Create circuit
clear_stl_registry()
circuit = Circuit("MyDesign")

with circuit.module("Counter") as m:
    clk, rst = m.clock(), m.reset()
    count = m.instance(Reg.create(circuit, 32), "count", clk=clk, rst=rst)

    with m.rule("increment") as rule:
        with rule.guard() as g:
            g.always()
        with rule.body() as body:
            val = body.call(count, "read")
            body.call(count, "write", body.add(val, body.const(1, 32)))

# Generate simulation workspace
ws = SimulationWorkspace(circuit, "./sim_workspace")
ws.generate_placeholder()

# Then:
# cd sim_workspace && make && make run
```

### Python Interpreter API

```python
from circt.pycmt2 import Circuit
from circt.pycmt2.interpreter import Interpreter

circuit = Circuit("Counter")
# ... define circuit ...

interp = circuit.interpreter()

# Register callbacks
def increment_guard(interp):
    return True  # Always enabled

def increment_body(interp):
    val = interp.get_register("count")
    interp.set_register("count", val + 1)

interp.register_guard("increment", increment_guard)
interp.register_body("increment", increment_body)

# Run simulation
for _ in range(10):
    results = interp.step()
    print(f"Cycle {interp.cycle}: count={interp.get_register('count')}")
```

### Testbench DSL

```python
from circt.pycmt2.testbench import Testbench

tb = Testbench(circuit)

with tb.sequence("basic_test") as seq:
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

## IMPORTANT: Comprehensive E2E Testing Requirements

**When adding E2E simulation to any example, you MUST test ALL features demonstrated by that example.** Do NOT create simplified versions that only test a subset of functionality.

### Mandatory Testing Principles

1. **Full Feature Coverage**: Every feature, operation, and control flow construct in the example must be exercised by the testbench.

2. **No Simplified Subsets**: Never say "I'll focus on one or two simpler features" or "I'll test a subset". Test ALL features.

3. **Correctness Verification**: Each feature must have assertions (`seq.expect()`) that verify correct behavior with known expected values.

4. **For Comprehensive Examples**: If an example demonstrates multiple features (e.g., proc.py showing seq, par, if, while, static steps), the E2E simulation MUST test EACH of those features separately.

### Example: What NOT to do

```python
# WRONG: Only tests one simple proc rule when the example has 6 different rules
def create_simulatable_proc_circuit():
    # Only creates a simple triple increment...
    # MISSING: seq_test, par_test, if_test, while_test, mixed_test, nested_test
```

### Example: What TO do

```python
# CORRECT: Test all features demonstrated in the example
def create_simulatable_proc_circuit():
    # Create ALL proc rules from the comprehensive example:
    # - seq_test: sequential step execution
    # - par_test: parallel step execution
    # - if_test: conditional control
    # - while_test: loop control
    # - mixed_test: static + dynamic steps combined
    # - nested_test: nested seq/par/if combinations

def create_proc_testbench(circuit):
    # Test each feature with specific expected outcomes:
    # - Test 1: Verify sequential execution order
    # - Test 2: Verify parallel execution completes together
    # - Test 3: Verify conditional branch taken correctly
    # - Test 4: Verify while loop iteration count
    # - Test 5: Verify static timing is respected
    # - Test 6: Verify nested control combinations
```

### Checklist Before Completing E2E Simulation

- [ ] Read the ENTIRE example file to understand ALL features it demonstrates
- [ ] Create simulation circuits that exercise EVERY feature
- [ ] Write test sequences with `seq.expect()` for EACH feature
- [ ] Verify expected values are mathematically correct
- [ ] Run simulation and confirm ALL tests pass
- [ ] Do NOT claim "done" until comprehensive coverage is achieved

## PyCMT2 Examples (examples/PyCMT2/)

The best way to learn is through the working examples. Run them with:

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/<example>.py
```

### Simulation Examples

| File | Description | Run Command |
|------|-------------|-------------|
| `simulation_workspace_example.py` | **Start here**: Verilator workspace generation | `python3 ../examples/PyCMT2/simulation_workspace_example.py` |
| `interpreter_example.py` | Pure Python simulation with callbacks | `python3 ../examples/PyCMT2/interpreter_example.py` |
| `proc_testbench_example.py` | Testbench DSL for Verilator | `python3 ../examples/PyCMT2/proc_testbench_example.py` |
| `pipeline_fifo_testbench_example.py` | FIFO-based pipeline with testbench | `python3 ../examples/PyCMT2/pipeline_fifo_testbench_example.py` |

### Design Examples

| File | Description |
|------|-------------|
| `counter_example.py` | Simple counter with register |
| `counter_reg_example.py` | Counter using STL Reg module |
| `gcd_example.py` | GCD algorithm with procedural control |
| `proc_example.py` | Procedural rule basics |
| `proc_comprehensive_example.py` | Full procedural control patterns |
| `proc_conflict_example.py` | Rule conflict handling |
| `timing_example.py` | Static timing annotations |

### Testing Examples

| File | Description |
|------|-------------|
| `test_gcd_simulation.py` | GCD simulation verification |
| `test_proc_simulation.py` | Procedural simulation tests |
| `diagnostics_example.py` | Source location tracing |

### MLIR Test Files (test/Dialect/Cmt2/)

| File | Description |
|------|-------------|
| `gcd.mlir` | GCD algorithm in raw MLIR |
| `hybrid-static-dynamic.mlir` | Hybrid static/dynamic control |
| `systolic-compute-kernel.mlir` | Systolic array pattern |

## Key CMT2 Passes

| Pass | Description |
|------|-------------|
| `--cmt2-inline-private-funcs` | Inline private functions |
| `--cmt2-static-inference` | Infer static timing |
| `--cmt2-compile-static` | Compile static steps to FSM |
| `--cmt2-tdcc` | Top-down compile control |
| `--cmt2-proc-to-gaa` | Convert proc to GAA rules |
| `--lower-cmt2-to-firrtl` | Lower to FIRRTL |

## End-to-End Compilation (Manual)

For manual compilation without PyCMT2:

```bash
# CMT2 MLIR → FIRRTL → SystemVerilog
build/bin/circt-opt input.mlir \
    --cmt2-inline-private-funcs \
    --cmt2-static-inference \
    --cmt2-compile-static \
    --lower-cmt2-to-firrtl | \
build/bin/firtool --format=mlir -o output.sv
```

## Shell Scripts (Legacy)

For quick MLIR-only compilation (less recommended):

```bash
# Compile MLIR to SystemVerilog
.claude/skills/cmt2-e2e-sim/scripts/cmt2-to-sv.sh test.mlir output/

# Create iverilog-based simulation workspace
.claude/skills/cmt2-e2e-sim/scripts/create-sim-workspace.sh test.mlir sim/

# Run simulation
.claude/skills/cmt2-e2e-sim/scripts/run-sim.sh sim/
```

## Debugging with cmt2-dbg

```
$ build/bin/cmt2-dbg counter.mlir

CMT2 Debugger
(cmt2-dbg) print @count
@count = 0
(cmt2-dbg) step
Cycle 1: firing @increment
(cmt2-dbg) print @count
@count = 1
(cmt2-dbg) break @increment
Breakpoint set
(cmt2-dbg) run
Paused at @increment
(cmt2-dbg) quit
```

## Troubleshooting

### Build Issues
```bash
ninja -C build
```

### Import Errors
```bash
# Ensure correct PYTHONPATH
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 -c "from circt.pycmt2 import Circuit"
```

### Pass Errors
```bash
# Debug with verbose IR output
build/bin/circt-opt input.mlir --mlir-print-ir-after-all
```

See also:
- [COMPILATION.md](COMPILATION.md) - Detailed pass reference
- [SIMULATION.md](SIMULATION.md) - Simulation setup
- [TESTBENCH.md](TESTBENCH.md) - Writing testbenches
- [docs/Dialects/Cmt2/CMT2-Improvements-Plan.md](../../../../docs/Dialects/Cmt2/CMT2-Improvements-Plan.md) - Full ecosystem documentation
