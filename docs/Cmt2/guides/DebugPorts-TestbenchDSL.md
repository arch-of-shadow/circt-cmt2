# Debug Ports and Testbench DSL Guide

This guide covers two features for debugging and testing CMT2 designs:

1. **Debug Firing Ports**: Output ports that indicate when each rule fires
2. **Testbench DSL**: Python DSL for defining simulation testbenches

---

## 1. Debug Firing Ports

### Overview

The `cmt2-add-rule-firing-port` pass adds output ports to modules that expose rule firing signals. These are useful for:

**Note:** PyCMT2 integration is now complete! Use `debug_ports=True` in `emit_verilog()` and `SimulationWorkspace`. See Section 6 for details.

- Debugging rule guard logic
- Verifying scheduling behavior
- Waveform analysis
- Testbench assertions

### Usage

Run the pass before FIRRTL conversion:

```bash
circt-opt input.mlir \
    --cmt2-add-rule-firing-port \
    --cmt2-to-firrtl
```

### Pass Options

| Option | Default | Description |
|--------|---------|-------------|
| `--prefix` | `"dbg_"` | Prefix for generated port names |

### Generated Ports

For each rule `@my_rule`, the pass generates:

- **Attribute on rule**: `{debug.firing_port = "dbg_my_rule_firing"}`
- **Module attribute**: `{debug.firing_ports = ["dbg_my_rule_firing", ...]}`
- **Output port**: `dbg_my_rule_firing : !firrtl.uint<1>`

### Example

**Input MLIR:**
```mlir
cmt2.module @Counter {
  cmt2.rule @increment { ... } { ... }
  cmt2.rule @reset { ... } { ... }
}
```

**After pass:**
```mlir
cmt2.module @Counter {debug.firing_ports = ["dbg_increment_firing", "dbg_reset_firing"]} {
  cmt2.rule @increment { ... } { ... } {debug.firing_port = "dbg_increment_firing"}
  cmt2.rule @reset { ... } { ... } {debug.firing_port = "dbg_reset_firing"}
}
```

**Generated Verilog:**
```verilog
module Counter(
  input  wire clk,
  input  wire rst,
  output wire dbg_increment_firing,
  output wire dbg_reset_firing
);
  // dbg_increment_firing = fire signal of @increment rule
  // dbg_reset_firing = fire signal of @reset rule
endmodule
```

### Using in Testbenches

```cpp
// C++ (Verilator)
void observe_rules() {
    if (dut->dbg_increment_firing) {
        std::cout << "increment fired at cycle " << cycle << std::endl;
    }
    if (dut->dbg_reset_firing) {
        std::cout << "reset fired at cycle " << cycle << std::endl;
    }
}
```

```python
# Python (cocotb)
@cocotb.test()
async def test_rule_firing(dut):
    await RisingEdge(dut.clk)
    if dut.dbg_increment_firing.value:
        print("increment fired")
```

---

## 2. Testbench DSL

### Overview

The Testbench DSL provides a Pythonic way to define test sequences that can be compiled to:

- **C++ testbenches** for Verilator
- **Python testbenches** for cocotb

### Basic Usage

```python
from circt.pycmt2 import Circuit
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.simulation import SimulationWorkspace

# Create circuit
circuit = Circuit("MyDesign")
# ... build circuit ...

# Create testbench
tb = Testbench(circuit)

# Define test sequences
with tb.sequence("basic_test") as seq:
    seq.reset(5)
    seq.wait(10)
    seq.expect("output", 42)

# Generate code
cpp_code = tb.generate_cpp()
cocotb_code = tb.generate_cocotb()
```

### Available Operations

| Operation | Description | Example |
|-----------|-------------|---------|
| `reset(cycles)` | Assert reset | `seq.reset(5)` |
| `wait(cycles)` | Wait N cycles | `seq.wait(10)` |
| `drive(port, value)` | Set input | `seq.drive("input_a", 42)` |
| `expect(port, value)` | Assert output | `seq.expect("result", 84)` |
| `call_method(inst, meth, *args)` | Call method | `seq.call_method("reg", "write", 100)` |
| `wait_ready(inst, meth)` | Wait for ready | `seq.wait_ready("alu", "compute")` |
| `wait_condition(cond)` | Wait for condition | `seq.wait_condition("dut->done")` |
| `print(msg, *signals)` | Print message | `seq.print("Value:", "count")` |
| `comment(text)` | Add comment | `seq.comment("Phase 1")` |
| `record_cycle(label)` | Record cycle | `seq.record_cycle("start")` |
| `print_cycle_diff(a, b, msg)` | Print elapsed | `seq.print_cycle_diff("start", "end", "Latency")` |

### Complete Example

```python
from circt.pycmt2.testbench import Testbench

tb = Testbench(circuit)

# Sequence 1: Basic functionality
with tb.sequence("basic") as seq:
    seq.comment("Initialize and test basic operation")
    seq.reset(5)

    # Start operation
    seq.drive("start", 1)
    seq.drive("input_value", 100)
    seq.wait(1)
    seq.drive("start", 0)

    # Wait for completion
    seq.wait_condition("dut->done == 1", timeout=50)

    # Verify result
    seq.expect("result", 200, "Result should be 2x input")

# Sequence 2: Timing verification with debug ports
with tb.sequence("timing") as seq:
    seq.reset(5)

    seq.record_cycle("op_start")
    seq.drive("start", 1)
    seq.wait(1)
    seq.drive("start", 0)

    # Monitor debug ports
    for i in range(10):
        seq.wait(1)
        seq.print(f"Cycle {i}", "dbg_compute_firing")

    seq.wait_condition("dut->done == 1", timeout=20)
    seq.record_cycle("op_end")

    seq.print_cycle_diff("op_start", "op_end", "Operation latency")

# Sequence 3: Edge cases
with tb.sequence("edge_cases") as seq:
    seq.comment("Test boundary conditions")
    seq.reset(5)

    # Test with zero input
    seq.drive("input_value", 0)
    seq.drive("start", 1)
    seq.wait(1)
    seq.drive("start", 0)
    seq.wait_condition("dut->done == 1", timeout=10)
    seq.expect("result", 0, "Zero input should give zero output")

# Generate testbench code
print("=== C++ Testbench ===")
print(tb.generate_cpp())

print("=== cocotb Testbench ===")
print(tb.generate_cocotb())
```

### Integration with SimulationWorkspace

```python
from circt.pycmt2.simulation import SimulationWorkspace

# Create workspace
ws = SimulationWorkspace(circuit, "./sim_workspace")

# Generate with testbench
ws.generate_with_testbench(tb)

# Build and run
ws.build()
result = ws.run()

if result.passed:
    print("All tests passed!")
else:
    print(f"Tests failed: {result.failures}")
```

### Generated Code Examples

**C++ Output:**
```cpp
void run_basic() {
    std::cout << "Running sequence: basic" << std::endl;
    // Initialize and test basic operation
    reset(5);
    dut->start = 1;
    dut->input_value = 100;
    wait_cycles(1);
    dut->start = 0;
    {
        int timeout = 50;
        while (!(dut->done == 1) && timeout-- > 0) tick();
        if (timeout <= 0) {
            std::cerr << "TIMEOUT waiting for: dut->done == 1" << std::endl;
            check_passed = false;
        }
    }
    expect("result", dut->result, 200);
}

void run_all_sequences() {
    run_basic();
    run_timing();
    run_edge_cases();
}
```

**cocotb Output:**
```python
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ClockCycles

async def reset_dut(dut, cycles):
    dut.rst.value = 1
    await ClockCycles(dut.clk, cycles)
    dut.rst.value = 0

@cocotb.test()
async def test_basic(dut):
    """Test sequence: basic"""
    clock = Clock(dut.clk, 10, units='ns')
    cocotb.start_soon(clock.start())

    # Initialize and test basic operation
    await reset_dut(dut, 5)
    dut.start.value = 1
    dut.input_value.value = 100
    await ClockCycles(dut.clk, 1)
    dut.start.value = 0
    for _ in range(50):
        if dut.done.value == 1:
            break
        await RisingEdge(dut.clk)
    else:
        assert False, "TIMEOUT waiting for: dut.done.value == 1"
    assert dut.result.value == 200, "Result should be 2x input"
```

---

## 3. Best Practices

### Debug Ports

1. **Use meaningful prefixes**: `--prefix=dbg_` (default) or custom like `--prefix=mon_`
2. **Strip in production**: Debug ports add area; remove for synthesis
3. **Combine with waveforms**: Use VCD dumps to visualize rule firing

### Testbench DSL

1. **Name sequences descriptively**: `"basic_add"`, `"overflow_check"`, `"timing_verify"`
2. **Use comments**: Document test intent with `seq.comment()`
3. **Set appropriate timeouts**: Prevent infinite loops with `wait_condition(..., timeout=N)`
4. **Record timing**: Use `record_cycle` + `print_cycle_diff` for latency checks
5. **Test edge cases**: Include boundary conditions in test sequences

---

## 4. Example Files

- **Example with both features**: `examples/PyCMT2/debug_testbench_example.py`
- **Comprehensive testbench**: `examples/PyCMT2/proc_testbench.py`
- **Testbench DSL module**: `lib/Bindings/Python/pycmt2/testbench.py`
- **AddRuleFiringPort pass**: `lib/Dialect/Cmt2/Transforms/AddRuleFiringPort.cpp`

---

## 5. Troubleshooting

### Debug ports not in Verilog

**Problem**: Debug ports not appearing in generated Verilog.

**Solution**: Ensure `--cmt2-add-rule-firing-port` runs BEFORE `--cmt2-to-firrtl`:

```bash
circt-opt input.mlir \
    --cmt2-add-rule-firing-port \  # Must come first
    --cmt2-to-firrtl
```

### Testbench compilation errors

**Problem**: Generated C++ testbench doesn't compile.

**Solution**: Ensure Verilator headers are available and DUT class name matches:

```cpp
#include "VMyModule.h"  // Must match top module name
VMyModule* dut;
```

### cocotb test hangs

**Problem**: Python testbench hangs waiting for condition.

**Solution**: Add reasonable timeouts and check signal names:

```python
# Bad - no timeout
while not dut.done.value:
    await RisingEdge(dut.clk)

# Good - with timeout
for _ in range(1000):
    if dut.done.value:
        break
    await RisingEdge(dut.clk)
else:
    assert False, "Timeout"
```

---

## 6. PyCMT2 Integration (Implemented)

The following APIs are now available for easy debug port usage:

### emit_verilog() with debug ports

```python
# Emit Verilog with debug firing ports
verilog = circuit.emit_verilog(debug_ports=True)
# Automatically runs cmt2-add-rule-firing-port pass
```

### SimulationWorkspace with debug ports

```python
# Create simulation workspace with debug ports enabled
ws = SimulationWorkspace(circuit, "./sim", debug_ports=True)
ws.generate()  # Generated Verilog will include debug ports
ws.build()
ws.run()
```

### Testbench DSL with debug port operations

```python
# Create testbench with auto_debug_ports enabled
tb = Testbench(circuit, auto_debug_ports=True)

# Get all rule names from the circuit
rule_names = tb.get_rule_names()

with tb.sequence("test_rules") as seq:
    seq.reset(5)
    seq.wait(1)

    # Check if a specific rule fired
    seq.expect_rule_fired("increment", fired=True)

    # Print status of a single rule
    seq.print_rule_status("increment")

    # Print status of all rules
    seq.print_all_rule_status(rule_names)

# Auto-generate a debug sequence that prints rule status each cycle
tb.add_debug_print_sequence(name="auto_debug", cycles=10)
```

### Debug Port Operations

| Operation | Description | Example |
|-----------|-------------|---------|
| `expect_rule_fired(rule, fired)` | Assert rule firing status | `seq.expect_rule_fired("inc", True)` |
| `print_rule_status(rule)` | Print single rule status | `seq.print_rule_status("inc")` |
| `print_all_rule_status(rules)` | Print all rule statuses | `seq.print_all_rule_status(["r1", "r2"])` |
| `get_rule_names()` | Get all rule names from circuit | `rules = tb.get_rule_names()` |
| `add_debug_print_sequence(n, cycles)` | Auto-generate debug sequence | `tb.add_debug_print_sequence("dbg", 10)` |

### Working Examples

The following examples demonstrate debug port usage with testbench DSL integration:

- `examples/PyCMT2/debug_testbench_example.py` - Full debug port DSL demo
- `examples/PyCMT2/comprehensive_example.py` - Comprehensive example with debug_ports=True
- `examples/PyCMT2/gcd.py` - GCD with debug ports
- `examples/PyCMT2/proc_testbench.py` - Procedural testbench with debug ports

For related topics, see `docs/Cmt2/features/Lowering.md` and `docs/Cmt2/guides/Debugging.md`.
