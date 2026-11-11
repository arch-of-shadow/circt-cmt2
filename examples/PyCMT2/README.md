# PyCMT2 Examples

This directory contains examples demonstrating the PyCMT2 Python EDSL for CMT2 hardware design.

## Prerequisites

Build CIRCT with Python bindings enabled:

```bash
cd circt-cmt2/build
ninja
```

## Running Examples

```bash
cd circt-cmt2/build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/<name>.py
```

## Examples by Category

### Basic Examples

| File | Description |
|------|-------------|
| `counter.py` | Simple counter - module creation, rules, values |
| `gcd.py` | GCD algorithm with Verilator simulation |
| `alu.py` | Multi-cycle ALU with Verilator simulation |

### Procedural Control

| File | Description |
|------|-------------|
| `proc.py` | Complete procedural control - steps, seq/par, if/while |
| `timing.py` | Static timing annotations for steps |
| `static_proc.py` | **End-to-end** static proc with pipelined accumulator |

### Hardware Generators

| File | Description |
|------|-------------|
| `systolic.py` | **End-to-end** systolic array generator for matrix multiply |
| `banked_gemm_dataflow.py` | **End-to-end** banked-memory tiled GEMM (load/compute/store dataflow tasks) |

### Simulation & Testing

| File | Description |
|------|-------------|
| `simulation_workspace.py` | **Start here** - Verilator workspace generation |
| `interpret.py` | cmt2-dbg interpreter script tests |
| `proc_testbench.py` | Testbench DSL for verification |
| `pipeline_fifo_testbench.py` | FIFO pipeline with full testbench |

### Debugging & Diagnostics

| File | Description |
|------|-------------|
| `diagnostics.py` | Source location tracing through compilation |

## Quick Start Guide

### 1. Basic Design (counter.py)

```python
from circt.pycmt2 import Circuit, UInt

circuit = Circuit("Counter")
with circuit.module("Counter") as m:
    clk, rst = m.clock(), m.reset()

    with m.rule("increment") as rule:
        with rule.guard() as g:
            g.always()
        with rule.body() as b:
            pass  # increment logic

print(circuit.emit_mlir())
```

### 2. Simulation Workspace (simulation_workspace.py)

```python
from circt.pycmt2 import Circuit
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry

clear_stl_registry()
circuit = Circuit("MyDesign")
# ... define circuit ...

ws = SimulationWorkspace(circuit, "./my_sim_workspace")
ws.generate_placeholder()

# Then: cd my_sim_workspace && make && make run
```

## Generated Workspaces

Examples that generate simulation workspaces will create directories with `_workspace` suffix.
These are gitignored and should not be committed:

```
*_workspace/     # All workspace directories
sim_*/           # All sim_* directories
```

## API Reference

See `docs/Cmt2/` for full documentation:

- `docs/Cmt2/guides/QuickStart.md` - first runnable example
- `docs/Cmt2/features/Concepts.md` - core semantics
- `docs/Cmt2/features/STL.md` - standard library components
- `docs/Cmt2/guides/Debugging.md` - debugger + simulation workspace

## Debugging

### Interactive Debugger

```bash
build/bin/cmt2-dbg test/Dialect/Cmt2/gcd.mlir
```

Commands: `step`, `run`, `break @rule`, `print @reg`, `trace`, `quit`

### Verbose Compilation

```bash
build/bin/circt-opt input.mlir --mlir-print-ir-after-all
```
