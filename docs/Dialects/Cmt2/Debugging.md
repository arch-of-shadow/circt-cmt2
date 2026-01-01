# CMT2 Debugging Guide

This guide covers the debugging tools available for CMT2 designs, from Python-level simulation to cycle-accurate hardware debugging.

## Overview

CMT2 provides three levels of debugging support:

| Level | Tool | Use Case |
|-------|------|----------|
| Python | `pycmt2.Interpreter` | Quick prototyping, unit testing |
| MLIR | `cmt2-dbg` | GAA-level debugging with REPL |
| RTL | `SimulationWorkspace` | Verilator/cocotb simulation |

## 1. Python Interpreter

The Python interpreter (`pycmt2.Interpreter`) provides cycle-accurate simulation directly from Python, ideal for rapid prototyping and testing.

### Basic Usage

```python
from circt.pycmt2 import Circuit, UInt

# Create your circuit
circuit = Circuit("Counter")
with circuit.module("Counter") as m:
    # ... define your circuit ...

# Create interpreter
interp = circuit.interpreter()

# Reset and run
interp.reset()
for _ in range(10):
    results = interp.step()
    print(f"Cycle {interp.cycle}: {interp.get_register('counter')}")
```

### Callback-Based Simulation

The interpreter parses rules and precedence from MLIR but relies on user-provided callbacks for guard evaluation and rule body execution:

```python
interp = circuit.interpreter()

# Register guard callback (returns True if rule should be enabled)
def reset_guard(interp):
    return interp.get_register("counter") == 10

interp.register_guard("reset_rule", reset_guard)

# Register body callback (executes when rule fires)
def increment_body(interp):
    val = interp.get_register("counter")
    interp.set_register("counter", val + 1)

interp.register_body("increment", increment_body)

# Now step() will execute callbacks correctly
interp.step()
```

### Breakpoints

```python
# Break when a rule fires
bp_id = interp.add_breakpoint_on_rule("reset_rule")

# Break at a specific cycle
bp_id = interp.add_breakpoint_at_cycle(100)

# Break on a condition
def counter_is_5(interp):
    return interp.get_register("counter") == 5

bp_id = interp.add_breakpoint_on_condition(counter_is_5)

# Run until breakpoint
bp = interp.run(max_cycles=1000)
if bp:
    print(f"Breakpoint hit at cycle {interp.cycle}")

# Remove breakpoint
interp.remove_breakpoint(bp_id)
interp.clear_breakpoints()
```

### Tracing

```python
interp.enable_tracing(True)

for _ in range(10):
    results = interp.step()
    fired = [r.name for r in results[0] if r.fired]
    enabled = [r.name for r in results[0] if r.guard_enabled]
    print(f"Cycle {interp.cycle}: enabled={enabled}, fired={fired}")

# Print trace history
for trace in interp.traces[-5:]:
    interp.print_trace(trace)
```

### State Inspection

```python
# Get register value
value = interp.get_register("counter")

# Set register value (for testing)
interp.set_register("counter", 42)

# Print full state
interp.print_state()

# Print rule status
interp.print_rules()
```

## 2. CMT2 Debugger (cmt2-dbg)

The `cmt2-dbg` tool provides a GDB-like REPL for debugging CMT2 circuits at the GAA level.

### Running the Debugger

```bash
# Interactive mode
cmt2-dbg circuit.mlir

# With script
cmt2-dbg circuit.mlir < commands.txt
```

### Commands

| Command | Description |
|---------|-------------|
| `step [N]` / `s [N]` | Execute N cycles (default 1) |
| `run [N]` / `r [N]` | Run until breakpoint or N cycles |
| `reset` | Reset circuit to initial state |
| `state` | Show all register values |
| `rules` | Show rule status (enabled/disabled) |
| `regs` | List all registers |
| `reg <name>` | Show register value |
| `set <name> <val>` | Set register value |
| `fire <rule>` | Force-fire a rule |
| `break <rule>` / `b <rule>` | Set breakpoint on rule |
| `bpc <cycle>` | Set breakpoint at cycle |
| `bpw <reg>` | Set watchpoint on register write |
| `list` | List all breakpoints |
| `delete <id>` / `d <id>` | Delete breakpoint |
| `trace on/off` | Enable/disable tracing |
| `history [N]` | Show last N trace entries |
| `help` / `h` | Show help |
| `quit` / `q` | Exit debugger |

### Example Session

```
$ cmt2-dbg counter.mlir

CMT2 GAA Debugger
Type 'help' for available commands

(cmt2-dbg) state
=== State at cycle 0 ===
  counter = 0

(cmt2-dbg) rules
=== Rules at cycle 0 ===
  increment: enabled (priority 0)
  reset_at_10: disabled

(cmt2-dbg) step 5
Cycle 1: fired [increment]
Cycle 2: fired [increment]
Cycle 3: fired [increment]
Cycle 4: fired [increment]
Cycle 5: fired [increment]

(cmt2-dbg) reg counter
counter = 5

(cmt2-dbg) break reset_at_10
Breakpoint 1 set on rule @reset_at_10

(cmt2-dbg) run
Cycle 6-10: fired [increment]
Breakpoint 1 hit: rule @reset_at_10 fired

(cmt2-dbg) state
=== State at cycle 11 ===
  counter = 0

(cmt2-dbg) quit
```

### Script Mode

Create a script file for automated testing:

```bash
# test_script.txt
reset
trace on
step 5
state
step 10
state
quit
```

Run with:
```bash
cmt2-dbg circuit.mlir < test_script.txt
```

## 3. RTL Simulation Workspace

For full RTL simulation with Verilator or cocotb.

### Placeholder Workspace

Generate a simulation environment with a template testbench:

```python
from circt.pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, "./sim_workspace")
ws.generate_placeholder()
```

This creates:
```
sim_workspace/
├── rtl/
│   ├── Counter.sv           # Your design
│   └── FIRRTLReg_32.sv       # STL modules
├── tb/
│   └── testbench.cpp         # Template testbench
├── build/
└── Makefile
```

Edit `tb/testbench.cpp` and build:
```bash
cd sim_workspace
make
./build/VCounter
```

### Testbench DSL

Define testbenches in Python:

```python
from circt.pycmt2.testbench import Testbench

tb = Testbench(circuit)

with tb.sequence("basic_test") as seq:
    seq.reset(5)
    seq.wait(10)
    seq.expect("count_res0", 10)
    seq.comment("Counter should be 10")

with tb.sequence("stress_test") as seq:
    seq.reset(5)
    for i in range(100):
        seq.wait(1)
        seq.call_method("counter", "read")
        seq.expect("count_res0", i)

# Generate workspace with testbench
from circt.pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, "./sim_workspace")
ws.generate_with_testbench(tb)
```

### Cocotb Testbenches

Generate Python-based cocotb testbenches:

```python
ws.generate_with_testbench(tb, backend="cocotb")
```

## 4. Source Location Tracing

Python source locations are preserved through the compilation pipeline and appear in generated Verilog:

```verilog
module Counter(
  input         clk,
                rst,
  output [31:0] count_res0  // counter.py:42
);
```

### Enabling Location Tracking

Locations are captured automatically when using PyCMT2 builders. For custom diagnostics:

```python
from circt.pycmt2.location import get_python_location
from circt.pycmt2.diagnostics import Diagnostic, DiagnosticLevel

loc = get_python_location()
diag = Diagnostic(
    level=DiagnosticLevel.WARNING,
    message="Potential conflict detected",
    location=loc,
)
print(diag.format())
```

### Diagnostic Handler

Capture diagnostics during compilation:

```python
from circt.pycmt2.diagnostics import DiagnosticHandler

with DiagnosticHandler() as handler:
    verilog = circuit.emit_verilog()

    if handler.has_errors():
        print("Compilation failed!")
        for diag in handler.get_diagnostics():
            print(diag.format())
```

## 5. Debugging Workflow

Recommended debugging workflow:

1. **Python Interpreter**: Start with the Python interpreter for quick validation
   - Test guard logic
   - Verify rule firing order
   - Check precedence conflicts

2. **cmt2-dbg**: Use for GAA-level debugging
   - Step through cycles
   - Set breakpoints on rules
   - Trace execution history

3. **RTL Simulation**: For final verification
   - Cycle-accurate behavior
   - Timing verification
   - Waveform analysis

## 6. Common Issues

### Rule Not Firing

Check with `cmt2-dbg`:
```
(cmt2-dbg) rules
=== Rules at cycle 5 ===
  my_rule: disabled (guard = false)
```

Or with Python interpreter:
```python
interp.register_guard("my_rule", lambda i: True)  # Override to always enable
```

### Precedence Conflicts

Rules with conflicts are resolved by precedence. Check with:
```
(cmt2-dbg) rules
  rule_a: enabled (priority 0)
  rule_b: enabled (priority 1)  # Lower priority, blocked by rule_a
```

Define precedence in PyCMT2:
```python
mod.precedence(rule_a.ref(), rule_b.ref())  # rule_a > rule_b
```

### Register Values Not Updating

In Python interpreter, register updates require body callbacks:
```python
interp.register_body("increment", lambda i: i.set_register("count", i.get_register("count") + 1))
```

## See Also

- [PyCMT2-Design.md](./PyCmt2-Design.md) - PyCMT2 API reference
- [ecmt2-EDSL.md](./ecmt2-EDSL.md) - ECMT2 C++ API
- [CMT2-Implementation-Tracker.md](./CMT2-Implementation-Tracker.md) - Implementation status
