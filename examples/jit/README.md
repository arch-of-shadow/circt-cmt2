# CMT2 JIT Examples

This directory contains examples demonstrating the CMT2 Python JIT (Just-In-Time) compilation system with full PyCMT2 integration for end-to-end hardware design and simulation.

## Prerequisites

To run these examples, you need:

1. **CIRCT Python bindings** - Build CIRCT with Python bindings enabled:
   ```bash
   cmake -DCIRCT_BINDINGS_PYTHON_ENABLED=ON ...
   ```

2. **PYTHONPATH** set to include the CIRCT bindings and CMT2 JIT:
   ```bash
   export PYTHONPATH=\
     /path/to/circt/build/tools/circt/python_packages/circt_core:\
     /path/to/circt-cmt2/python
   ```

## Examples

### 1. JIT Counter (`jit_counter.py`)

A parameterized counter design with:
- Configurable bit width
- Optional enable signal
- End-to-end simulation with testbench

```bash
python jit_counter.py --demo sim --width 16 --cycles 100
```

**Features demonstrated:**
- `@elaborate` decorator with static arguments
- `CircuitBuilder` for circuit construction
- `JITSimulationRunner` for E2E simulation
- Testbench creation with sequences

### 2. JIT FIFO (`jit_fifo.py`)

A parameterized FIFO design with:
- Configurable data width and depth
- Push/pop operations
- Full/empty status
- End-to-end simulation

```bash
python jit_fifo.py --demo sim --width 32 --depth 8
```

**Features demonstrated:**
- STL FIFO integration
- Multiple test sequences
- Parameter sweep demonstration

### 3. JIT GEMM (`jit_gemm.py`)

A matrix multiplication (GEMM) accelerator with:
- Configurable matrix dimensions (M, N, K)
- MAC (Multiply-Accumulate) units
- Memory interfaces
- End-to-end simulation

```bash
python jit_gemm.py --demo sim --dims 2,2,2 --width 16
```

**Features demonstrated:**
- Complex hardware design
- State machine implementation
- Multiple register arrays
- MLIR generation for different sizes

## Common Usage Patterns

### Basic Elaboration

```python
import cmt2
from cmt2 import elaborate
from cmt2.pycmt2_integration import CircuitBuilder
from typing import Annotated

@elaborate
def my_design(width: Annotated[int, cmt2.static] = 32):
    builder = CircuitBuilder("MyDesign")
    
    with builder.module("Top") as m:
        clk = m.clock()
        rst = m.reset()
        
        # Create registers, FIFOs, etc.
        reg = m.instance_reg(width, "my_reg", clk=clk, rst=rst)
        
        # Define rules
        with m.rule("my_rule") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                val = b.call(reg, "read")
                b.call(reg, "write", val + 1)
    
    return builder.circuit

# Elaborate
circuit = my_design(width=16)
print(circuit.emit_verilog())
```

### Simulation with Testbench

```python
from cmt2 import simulate
from cmt2.pycmt2_integration import JITSimulationRunner

@simulate
def test_my_design():
    circuit = my_design(width=16)
    
    # Create simulation runner
    runner = JITSimulationRunner(circuit, "./sim")
    
    # Create testbench
    tb = runner.create_testbench()
    
    with tb.sequence("test") as seq:
        seq.reset(5)
        seq.wait(10)
        seq.expect("output_port", expected_value)
    
    # Run simulation
    result = runner.run(testbench=tb)
    return result

# Execute
result = test_my_design()
print(f"Success: {result['success']}")
```

### Staged Compilation

```python
from cmt2.jit import ElaboratedCircuit, LoweredCircuit, CompiledCircuit

# Stage 1: Elaboration
circuit = my_design(width=32)
elaborated = ElaboratedCircuit(circuit, "MyDesign", {"width": 32})

# Stage 2: Lowering
lowered = elaborated.lower(target="verilog")
print(lowered.codegen(format="mlir"))

# Stage 3: Compilation
compiled = lowered.compile()
compiled.write("output.sv")
```

## Running Examples

### With CIRCT Build Directory

From the CIRCT build directory:

```bash
cd /path/to/circt/build
PYTHONPATH=tools/circt/python_packages/circt_core:../circt-cmt2/python \
  python3 ../circt-cmt2/examples/jit/jit_counter.py
```

### With Installed CIRCT

If CIRCT is installed to your Python environment:

```bash
cd /path/to/circt-cmt2/examples/jit
PYTHONPATH=../../python python3 jit_counter.py
```

## Example Output

### JIT Counter Example

```
============================================================
Testing Counter: width=32, cycles=50
============================================================

Workspace: /tmp/jit_counter_abc123

Running simulation...
[Simulation output from Verilator]

Simulation result: SUCCESS
Workspace: /tmp/jit_counter_abc123
```

### Generated Verilog

The examples can emit SystemVerilog:

```bash
python jit_counter.py --demo staged
```

Output:
```systemverilog
module Counter(
  input clk,
  input rst,
  input enable,
  output [31:0] get_count_res0
);
  // ... generated hardware ...
endmodule
```

## Debugging

### Enable Debug Ports

```python
runner = JITSimulationRunner(
    circuit=circuit,
    workspace_dir="./sim",
    debug_ports=True,  # Exposes rule firing signals
)
```

### View Waveforms

After simulation:

```bash
cd /tmp/jit_counter_*/waves
gtkwave waves.vcd
```

## Troubleshooting

### ImportError: PyCMT2 not available

**Cause:** CIRCT Python bindings not in PYTHONPATH

**Solution:**
```bash
export PYTHONPATH=/path/to/circt/build/tools/circt/python_packages/circt_core:$PYTHONPATH
```

### Simulation fails to build

**Cause:** Verilator not installed or not in PATH

**Solution:**
```bash
# Ubuntu/Debian
sudo apt-get install verilator

# macOS
brew install verilator
```

### ImportError: No module named 'circt'

**Cause:** CIRCT Python bindings not built

**Solution:**
```bash
cd /path/to/circt/build
cmake -DCIRCT_BINDINGS_PYTHON_ENABLED=ON ..
ninja
```

## Further Reading

- [CMT2 JIT Proposal](../../docs/Cmt2/future/CMT2-JIT-Proposal.md)
- [Implementation Plan](../../docs/Cmt2/future/Implementation-Plan.md)
- [PyCMT2 Documentation](../../docs/Cmt2/reference/PyCMT2-API.md)

## Contributing

To add new examples:

1. Create a new file `jit_<name>.py`
2. Follow the pattern of existing examples
3. Use `@elaborate` for circuit definitions
4. Use `@simulate` for test functions
5. Add documentation and comments

## License

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
