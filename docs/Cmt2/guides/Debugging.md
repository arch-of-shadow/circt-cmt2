# CMT2 Debugging Guide

This guide covers the debugging tools available for CMT2 designs, from GAA-level debugging to cycle-accurate RTL simulation.

## Overview

CMT2 provides two primary levels of debugging support:

| Level | Tool | Use Case |
|-------|------|----------|
| MLIR | `cmt2-dbg` | GAA-level debugging with REPL |
| RTL | `SimulationWorkspace` | Verilator/cocotb simulation |

## Interpretation vs RTL Simulation (What Runs Where)

There are two distinct “execution” stories:

1. **GAA interpretation (`cmt2-dbg`)**
   - Executes **Cmt2 rules/methods/values** at the IR level.
   - Updates architectural state (regs, method-visible state) cycle-by-cycle.
   - For **external modules**, semantics are only available if there is a
     corresponding interpreter implementation registered for that external
     module. Otherwise, the module is effectively a black box at the debugger
     level.

2. **RTL simulation (`SimulationWorkspace`)**
   - Runs the **generated RTL** (SystemVerilog) using Verilator.
   - This is the source of truth for “what the hardware does”, including
     *all* timing behavior and any external RTL blocks included in the build.

If you care about fidelity to real hardware (especially for ModuleLibrary-backed
blocks like FIFO/memory variants), prefer **RTL simulation**.

## 1. CMT2 Debugger (cmt2-dbg)

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

For a runnable end-to-end example of scripted debugging, see:

- `examples/PyCMT2/interpret.py` (drives `cmt2-dbg` to validate debugger behavior)

## 2. RTL Simulation Workspace

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
│   └── <extern>.sv           # Optional: ModuleLibrary-backed extern RTL (only if needed)
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

### External RTL (ModuleLibrary)

PyCMT2 STL primitives such as `Reg`, `Wire`, and `Memory` bind to FIRRTL modules
from the CMT2 ModuleLibrary. Depending on how the circuit is constructed and
compiled, the emitted Verilog may already include these module definitions (fully
self-contained).

When the emitted Verilog references a ModuleLibrary-backed module that is *not*
defined in the emitted SV, `SimulationWorkspace` will:
- Build the exact referenced module variants (e.g., `Reg_width32_init0`)
- Write only the missing module RTL into `rtl/`
- Avoid writing duplicate module definitions if the emitted SV already defines it

### Why “prebuilt” regs show up in simulation

Proc lowering and other transforms can introduce internal state (e.g., FSM state,
pipeline bookkeeping) that is materialized using ModuleLibrary-backed primitives
(notably `Reg_width<N>_init<M>` variants). If those module definitions are not
inlined into the emitted SV, the top-level SV will reference them as externs.

`SimulationWorkspace` makes the **workspace** self-contained by adding the missing
ModuleLibrary RTL. This does not require the *single emitted SV file* to contain
every definition; it requires the *simulation build* to have all referenced
modules available.

## 3. Source Location Tracing

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

## 4. Debugging Workflow

Recommended debugging workflow:

1. **cmt2-dbg**: Use for GAA-level debugging
   - Step through cycles
   - Set breakpoints on rules
   - Trace execution history

2. **RTL Simulation**: For final verification
   - Cycle-accurate behavior
   - Timing verification
   - Waveform analysis

## 5. Common Issues

### Rule Not Firing

Check with `cmt2-dbg`:
```
(cmt2-dbg) rules
=== Rules at cycle 5 ===
  my_rule: disabled (guard = false)
```

Or force-fire a rule for debugging:
```
(cmt2-dbg) fire my_rule
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

Use `cmt2-dbg` to inspect register state over cycles:
```
(cmt2-dbg) reg count
(cmt2-dbg) step 1
(cmt2-dbg) reg count
```

## See Also

- [PyCMT2-Guide.md](PyCMT2-Guide.md) - PyCMT2 user guide
- [ECMT2-Guide.md](ECMT2-Guide.md) - ECMT2 C++ user guide
- [Interpreter.md](../features/Interpreter.md) - `cmt2-dbg` coverage and extensibility
