#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Simulation workspace generation for PyCMT2 designs.

This module provides utilities for generating RTL simulation workspaces
with Verilator or other simulators.

Example:
    from pycmt2 import Circuit
    from pycmt2.simulation import SimulationWorkspace

    circuit = Circuit("Counter")
    # ... build circuit ...

    ws = SimulationWorkspace(circuit, "./sim")
    ws.generate_placeholder()

    # Or with custom testbench:
    from pycmt2.testbench import Testbench

    tb = Testbench(circuit)
    with tb.sequence("basic") as seq:
        seq.reset(5)
        seq.wait(10)
        seq.expect("count", 10)

    ws.generate_with_testbench(tb)
"""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .circuit import Circuit
    from .testbench import Testbench


class SimulationWorkspace:
    """Generate simulation workspace for CMT2 designs.

    SimulationWorkspace creates a directory structure suitable for
    RTL simulation with Verilator or other simulators.

    Directory structure:
        sim_workspace/
        ├── rtl/
        │   ├── <module>.sv       # Generated Verilog
        │   └── <module>_pkg.sv   # Package definitions (if any)
        ├── tb/
        │   ├── testbench.cpp     # C++ testbench (Verilator)
        │   └── testbench.py      # cocotb testbench (optional)
        ├── build/
        │   └── .gitkeep
        ├── waves/
        │   └── .gitkeep
        ├── Makefile              # Top-level Makefile
        └── README.md             # Instructions
    """

    def __init__(self, circuit: Circuit, output_dir: str | Path):
        """Create a simulation workspace generator.

        Args:
            circuit: The CMT2 circuit to simulate.
            output_dir: Directory to generate workspace in.
        """
        self.circuit = circuit
        self.output_dir = Path(output_dir)
        self._top_module = self._get_top_module_name()
        self._external_rtl: dict[str, str] = {}  # filename -> content

    def add_external_rtl(self, filename: str, content: str) -> "SimulationWorkspace":
        """Add external RTL file to the workspace.

        Use this to provide implementations for external modules
        (e.g., registers, FIFOs, memories).

        Args:
            filename: Name of the file (e.g., "Reg32.sv").
            content: Verilog/SystemVerilog content.

        Returns:
            self for chaining.
        """
        self._external_rtl[filename] = content
        return self

    def _get_top_module_name(self) -> str:
        """Get the top-level module name from the circuit."""
        if self.circuit._modules:
            return next(iter(self.circuit._modules.keys()))
        return self.circuit.name

    def _add_stl_rtl(self):
        """Add STL RTL files from the registry."""
        from .stl import get_stl_rtl_files
        for filename, content in get_stl_rtl_files().items():
            self.add_external_rtl(filename, content)

    def generate_placeholder(self):
        """Generate workspace with placeholder testbench.

        Creates a simulation workspace with a template testbench
        that users can customize.
        """
        self._add_stl_rtl()
        self._create_directories()
        self._generate_rtl()
        self._generate_placeholder_testbench()
        self._generate_makefile()
        self._generate_readme()

        print(f"Simulation workspace generated at {self.output_dir}")
        print(f"Edit tb/testbench.cpp to add your test logic")
        print(f"Run 'make' to build and 'make run' to simulate")

    def generate_with_testbench(self, testbench: Testbench):
        """Generate workspace with testbench from DSL.

        Args:
            testbench: A Testbench object describing the test sequences.
        """
        self._add_stl_rtl()
        self._create_directories()
        self._generate_rtl()
        self._generate_testbench_from_dsl(testbench)
        self._generate_makefile()
        self._generate_readme()

        print(f"Simulation workspace generated at {self.output_dir}")
        print(f"Run 'make' to build and 'make run' to simulate")

    def build(self) -> bool:
        """Build the simulation executable.

        Returns:
            True if build succeeded, False otherwise.
        """
        import subprocess
        result = subprocess.run(
            ["make", "-C", str(self.output_dir)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"Build failed:\n{result.stdout}\n{result.stderr}")
            return False
        return True

    def run(self) -> tuple[bool, str]:
        """Run the simulation.

        Returns:
            Tuple of (success, output).
        """
        import subprocess
        result = subprocess.run(
            ["make", "-C", str(self.output_dir), "run"],
            capture_output=True,
            text=True,
        )
        output = result.stdout
        if result.stderr:
            output += "\n" + result.stderr
        return result.returncode == 0, output

    def build_and_run(self) -> tuple[bool, str]:
        """Build and run the simulation.

        Returns:
            Tuple of (success, output).
        """
        if not self.build():
            return False, "Build failed"
        return self.run()

    def _create_directories(self):
        """Create the workspace directory structure."""
        dirs = [
            self.output_dir / "rtl",
            self.output_dir / "tb",
            self.output_dir / "build",
            self.output_dir / "waves",
        ]
        for d in dirs:
            d.mkdir(parents=True, exist_ok=True)

        # Create .gitkeep files
        (self.output_dir / "build" / ".gitkeep").touch()
        (self.output_dir / "waves" / ".gitkeep").touch()

    def _generate_rtl(self):
        """Generate Verilog RTL files."""
        try:
            verilog = self.circuit.to_verilog()
            rtl_file = self.output_dir / "rtl" / f"{self._top_module}.sv"
            rtl_file.write_text(verilog)
        except Exception as e:
            # If Verilog generation fails, write placeholder
            rtl_file = self.output_dir / "rtl" / f"{self._top_module}.sv"
            rtl_file.write_text(f"""\
// Placeholder - Verilog generation failed: {e}
// Run CMT2 passes manually:
//   circt-opt input.mlir -cmt2-compile-invoke -cmt2-to-firrtl | \\
//   firtool --format=mlir -o {self._top_module}.sv

module {self._top_module} (
    input logic clk,
    input logic rst
);
    // TODO: Generated RTL will go here
endmodule
""")

        # Write external RTL files
        for filename, content in self._external_rtl.items():
            rtl_file = self.output_dir / "rtl" / filename
            rtl_file.write_text(content)

    def _generate_placeholder_testbench(self):
        """Generate a placeholder C++ testbench for Verilator."""
        top = self._top_module
        tb_content = f'''\
// Testbench for {top}
// Generated by PyCMT2 SimulationWorkspace
//
// Edit this file to add your test logic.

#include "V{top}.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <iostream>
#include <memory>

// Simulation parameters
constexpr int MAX_CYCLES = 1000;
constexpr int RESET_CYCLES = 5;

int main(int argc, char** argv) {{
    // Initialize Verilator
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    // Create DUT instance
    auto dut = std::make_unique<V{top}>();

    // Create VCD trace
    auto tfp = std::make_unique<VerilatedVcdC>();
    dut->trace(tfp.get(), 99);
    tfp->open("waves/{top}.vcd");

    // Initialize signals
    dut->clk = 0;
    dut->rst = 1;

    // Simulation loop
    int cycle = 0;
    bool passed = true;

    std::cout << "Starting simulation of {top}..." << std::endl;

    while (cycle < MAX_CYCLES && !Verilated::gotFinish()) {{
        // Toggle clock
        dut->clk = !dut->clk;

        // Evaluate
        dut->eval();
        tfp->dump(cycle * 10);

        // Rising edge logic
        if (dut->clk) {{
            // Release reset after RESET_CYCLES
            if (cycle == RESET_CYCLES * 2) {{
                dut->rst = 0;
                std::cout << "Reset released at cycle " << cycle / 2 << std::endl;
            }}

            // ============================================
            // TODO: Add your test logic here
            // ============================================
            //
            // Example: Check an output value
            // if (cycle > RESET_CYCLES * 2 + 10) {{
            //     if (dut->some_output != expected_value) {{
            //         std::cerr << "FAIL: some_output mismatch at cycle "
            //                   << cycle / 2 << std::endl;
            //         passed = false;
            //     }}
            // }}
            //
            // Example: Drive an input
            // dut->some_input = (cycle / 2) % 16;
            //
            // ============================================
        }}

        cycle++;
    }}

    // Finalize
    tfp->close();

    if (passed) {{
        std::cout << "PASSED: Simulation completed successfully" << std::endl;
        return 0;
    }} else {{
        std::cerr << "FAILED: Test assertions failed" << std::endl;
        return 1;
    }}
}}
'''
        tb_file = self.output_dir / "tb" / "testbench.cpp"
        tb_file.write_text(tb_content)

    def _generate_testbench_from_dsl(self, testbench: Testbench):
        """Generate C++ testbench from DSL description."""
        top = self._top_module
        sequences_code = self._generate_sequence_code(testbench)

        tb_content = f'''\
// Testbench for {top}
// Generated by PyCMT2 Testbench DSL

#include "V{top}.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <iostream>
#include <memory>
#include <functional>
#include <vector>
#include <string>

// DUT instance (global for sequence access)
static V{top}* dut;
static int cycle;

// Helper functions
void tick() {{
    dut->clk = 0;
    dut->eval();
    dut->clk = 1;
    dut->eval();
    cycle++;
}}

void reset(int cycles) {{
    dut->rst = 1;
    for (int i = 0; i < cycles; i++) {{
        tick();
    }}
    dut->rst = 0;
}}

void wait_cycles(int cycles) {{
    for (int i = 0; i < cycles; i++) {{
        tick();
    }}
}}

bool check_passed = true;

void expect(const std::string& name, uint64_t actual, uint64_t expected) {{
    if (actual != expected) {{
        std::cerr << "FAIL at cycle " << cycle << ": " << name
                  << " = " << actual << ", expected " << expected << std::endl;
        check_passed = false;
    }}
}}

{sequences_code}

int main(int argc, char** argv) {{
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    dut = new V{top}();

    auto tfp = std::make_unique<VerilatedVcdC>();
    dut->trace(tfp.get(), 99);
    tfp->open("waves/{top}.vcd");

    dut->clk = 0;
    dut->rst = 0;
    cycle = 0;

    std::cout << "Running test sequences..." << std::endl;

    // Run all sequences
    run_all_sequences();

    tfp->close();
    delete dut;

    if (check_passed) {{
        std::cout << "PASSED: All tests passed" << std::endl;
        return 0;
    }} else {{
        std::cerr << "FAILED: Some tests failed" << std::endl;
        return 1;
    }}
}}
'''
        tb_file = self.output_dir / "tb" / "testbench.cpp"
        tb_file.write_text(tb_content)

    def _generate_sequence_code(self, testbench: Testbench) -> str:
        """Generate C++ code for test sequences."""
        from .testbench import (
            ResetOp, WaitOp, DriveOp, ExpectOp,
            CallMethodOp, WaitReadyOp
        )

        sequence_functions = []
        sequence_names = []

        for seq in testbench._sequences:
            sequence_names.append(seq.name)
            lines = [f'void run_{seq.name}() {{']
            lines.append(f'    std::cout << "Running sequence: {seq.name}" << std::endl;')

            for op in seq._ops:
                if isinstance(op, ResetOp):
                    lines.append(f'    reset({op.cycles});')
                elif isinstance(op, WaitOp):
                    lines.append(f'    wait_cycles({op.cycles});')
                elif isinstance(op, DriveOp):
                    lines.append(f'    dut->{op.port} = {op.value};')
                elif isinstance(op, ExpectOp):
                    lines.append(f'    expect("{op.port}", dut->{op.port}, {op.value});')
                elif isinstance(op, CallMethodOp):
                    # Generate method call with enable/ready handshake
                    lines.append(f'    // Call {op.instance}.{op.method}')
                    lines.append(f'    dut->{op.instance}_{op.method}_enable = 1;')
                    lines.append(f'    tick();')
                    lines.append(f'    dut->{op.instance}_{op.method}_enable = 0;')
                elif isinstance(op, WaitReadyOp):
                    lines.append(f'    // Wait for {op.instance}.{op.method} ready')
                    lines.append(f'    while (!dut->{op.instance}_{op.method}_ready) tick();')

            lines.append('}')
            sequence_functions.append('\n'.join(lines))

        # Generate run_all_sequences
        run_all = ['void run_all_sequences() {']
        for name in sequence_names:
            run_all.append(f'    run_{name}();')
        run_all.append('}')

        return '\n\n'.join(sequence_functions + ['\n'.join(run_all)])

    def _generate_makefile(self):
        """Generate Makefile for Verilator simulation."""
        top = self._top_module
        makefile_content = f'''\
# Makefile for {top} simulation
# Generated by PyCMT2 SimulationWorkspace

# Verilator configuration
VERILATOR ?= verilator
VERILATOR_FLAGS = --cc --exe --build -j 0 \\
    --trace --trace-structs \\
    -Wall -Wno-fatal \\
    --top-module {top}

# Directories
RTL_DIR = rtl
TB_DIR = tb
BUILD_DIR = build
WAVE_DIR = waves

# Source files
RTL_FILES = $(wildcard $(RTL_DIR)/*.sv)
TB_FILE = $(TB_DIR)/testbench.cpp

# Output
SIM_EXE = $(BUILD_DIR)/V{top}

# Default target
all: $(SIM_EXE)

# Build simulation executable
$(SIM_EXE): $(RTL_FILES) $(TB_FILE)
\t$(VERILATOR) $(VERILATOR_FLAGS) \\
\t\t-Mdir $(BUILD_DIR) \\
\t\t$(RTL_FILES) \\
\t\t$(TB_FILE)

# Run simulation
run: $(SIM_EXE)
\t@mkdir -p $(WAVE_DIR)
\t./$(SIM_EXE)

# View waveforms (requires GTKWave)
waves: run
\tgtkwave $(WAVE_DIR)/{top}.vcd &

# Clean build artifacts
clean:
\trm -rf $(BUILD_DIR)/*
\trm -rf $(WAVE_DIR)/*.vcd

# Clean everything
distclean: clean
\trm -rf $(BUILD_DIR)
\trm -rf $(WAVE_DIR)

.PHONY: all run waves clean distclean
'''
        makefile = self.output_dir / "Makefile"
        makefile.write_text(makefile_content)

    def _generate_readme(self):
        """Generate README with instructions."""
        top = self._top_module
        readme_content = f'''\
# {top} Simulation Workspace

Generated by PyCMT2 SimulationWorkspace.

## Directory Structure

```
.
├── rtl/            # Generated Verilog RTL
├── tb/             # Testbench files
├── build/          # Build artifacts
├── waves/          # Waveform files
├── Makefile        # Build automation
└── README.md       # This file
```

## Prerequisites

- Verilator (https://verilator.org/)
- GTKWave (optional, for waveform viewing)

### Install on Ubuntu/Debian

```bash
sudo apt-get install verilator gtkwave
```

### Install on macOS

```bash
brew install verilator gtkwave
```

## Usage

### Build the simulation

```bash
make
```

### Run the simulation

```bash
make run
```

### View waveforms

```bash
make waves
```

### Clean build artifacts

```bash
make clean
```

## Customizing the Testbench

Edit `tb/testbench.cpp` to add your test logic. Look for the
`TODO: Add your test logic here` section.

Example operations:
- Drive inputs: `dut->input_name = value;`
- Check outputs: `if (dut->output_name != expected) {{ ... }}`
- Advance time: Call `dut->eval()` and toggle clock

## Regenerating RTL

If you modify your CMT2 design, regenerate the RTL:

```python
from pycmt2.simulation import SimulationWorkspace

ws = SimulationWorkspace(circuit, ".")
ws._generate_rtl()
```

Or run the full pipeline manually:

```bash
circt-opt input.mlir \\
    -cmt2-compile-invoke \\
    -cmt2-to-firrtl | \\
firtool --format=mlir -o rtl/{top}.sv
```
'''
        readme = self.output_dir / "README.md"
        readme.write_text(readme_content)
