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

import os
from pathlib import Path
from typing import TYPE_CHECKING
import re
import shutil
import subprocess

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

    def __init__(
        self,
        circuit: Circuit,
        output_dir: str | Path,
        top_module: str | None = None,
        debug_ports: bool = False,
        trace: bool | None = None,
        *,
        use_circt_opt: bool | None = None,
        circt_opt: str | None = None,
    ):
        """Create a simulation workspace generator.

        Args:
            circuit: The CMT2 circuit to simulate.
            output_dir: Directory to generate workspace in.
            top_module: Optional explicit top module name. If not specified,
                uses the last defined module in the circuit.
            debug_ports: If True, enables debug firing ports in generated Verilog.
                These ports expose rule fire signals for debugging and testbench
                assertions. Each rule gets a `dbg_<rule_name>_firing` output port.
                Default is False.
            trace: If True, enables VCD tracing (`--trace`) and emits a waveform
                config in `waves/`. If False, disables VCD tracing for faster
                Verilator builds. If None (default), respects environment
                variable `PYCMT2_TRACE` (default: enabled).
            use_circt_opt: If True, run the lowering pipeline via an external
                `circt-opt` binary instead of the in-process Python pass
                bindings. This is useful when Python bindings are unavailable
                or out of date relative to the compiler.
                If None (default), respects environment variable
                `PYCMT2_USE_CIRCT_OPT` (default: disabled).
            circt_opt: Optional explicit path to `circt-opt`. If not provided,
                uses `PYCMT2_CIRCT_OPT`, then searches `PATH`, then falls back
                to `./build/bin/circt-opt` when present.
        """
        self.circuit = circuit
        self.output_dir = Path(output_dir)
        self._top_module = top_module if top_module else self._get_top_module_name()
        self._external_rtl: dict[str, str] = {}  # filename -> content
        self._debug_ports = debug_ports
        self._trace = (
            trace
            if trace is not None
            else os.environ.get("PYCMT2_TRACE", "1").lower() not in ("0", "false", "no", "off")
        )
        self._use_circt_opt = (
            use_circt_opt
            if use_circt_opt is not None
            else os.environ.get("PYCMT2_USE_CIRCT_OPT", "0").lower() in ("1", "true", "yes", "on")
        )
        self._circt_opt = self._resolve_circt_opt(circt_opt)

    @staticmethod
    def _resolve_circt_opt(explicit: str | None) -> str | None:
        if explicit:
            return explicit

        env = os.environ.get("PYCMT2_CIRCT_OPT")
        if env:
            return env

        found = shutil.which("circt-opt")
        if found:
            return found

        local = Path.cwd() / "build" / "bin" / "circt-opt"
        if local.exists():
            return str(local)

        return None

    def _emit_verilog_via_circt_opt(self) -> str:
        """Lower and export Verilog via an external `circt-opt` binary."""
        if not self._circt_opt:
            raise RuntimeError(
                "PYCMT2_USE_CIRCT_OPT is enabled but no `circt-opt` was found. "
                "Set `PYCMT2_CIRCT_OPT` or pass `circt_opt=...`."
            )

        rtl_dir = self.output_dir / "rtl"
        rtl_dir.mkdir(parents=True, exist_ok=True)

        mlir_in = rtl_dir / f"{self._top_module}.input.mlir"
        mlir_lowered = rtl_dir / f"{self._top_module}.lowered.mlir"

        mlir_in.write_text(self.circuit.emit_mlir())

        cmt2_passes = [
            "cmt2-compile-invoke",
            "cmt2-dataflow-lowering",
            "cmt2-token-lowering",
            "cmt2-token-rtl-gen",
            "cmt2-tdcc",
            "cmt2-proc-stmt-to-action",
            "cmt2-proc-to-gaa",
        ]
        if self._debug_ports:
            cmt2_passes.append("cmt2-add-rule-firing-port")

        # Match the in-process pipeline in `Circuit.emit_verilog()` but run it
        # out-of-process so it does not depend on Python bindings.
        pipeline = (
            "builtin.module("
            f"cmt2.circuit({','.join(cmt2_passes)}),"
            "lower-cmt2-to-firrtl,"
            "firrtl.circuit(firrtl-infer-resets,firrtl-lower-types),"
            "any(any(firrtl-expand-whens)),"
            "lower-firrtl-to-hw,"
            "lower-seq-to-sv"
            ")"
        )

        lower_cmd = [
            self._circt_opt,
            str(mlir_in),
            f"--pass-pipeline={pipeline}",
            "-o",
            str(mlir_lowered),
        ]
        lower = subprocess.run(
            lower_cmd,
            capture_output=True,
            text=True,
            errors="replace",
        )
        if lower.returncode != 0:
            raise RuntimeError(
                "circt-opt lowering failed.\n"
                f"Command: {' '.join(lower_cmd)}\n"
                f"stdout:\n{lower.stdout}\n"
                f"stderr:\n{lower.stderr}"
            )

        # `circt-opt` always prints the final IR. `--export-verilog` additionally
        # prints Verilog to stdout. Discard the IR output to keep the captured
        # stdout as pure Verilog.
        export_cmd = [self._circt_opt, str(mlir_lowered), "--export-verilog", "-o", os.devnull]
        export = subprocess.run(
            export_cmd,
            capture_output=True,
            text=True,
            errors="replace",
        )
        if export.returncode != 0:
            raise RuntimeError(
                "circt-opt Verilog export failed.\n"
                f"Command: {' '.join(export_cmd)}\n"
                f"stdout:\n{export.stdout}\n"
                f"stderr:\n{export.stderr}"
            )
        return export.stdout

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

    def add_external_rtl_file(
        self, path: str | Path, *, dest_name: str | None = None
    ) -> "SimulationWorkspace":
        """Add an external RTL file to the workspace by path.

        This is a convenience wrapper over `add_external_rtl()` that reads the
        file content and stages it into `rtl/<dest_name>`.

        Args:
            path: Path to an existing RTL file (SystemVerilog/Verilog).
            dest_name: Optional destination filename under `rtl/`. Defaults to
                `Path(path).name`.

        Returns:
            self for chaining.
        """
        path = Path(path)
        filename = dest_name if dest_name is not None else path.name
        self._external_rtl[filename] = path.read_text()
        return self

    def _add_external_rtl_files_from_circuit(self):
        """Stage user-provided external RTL files declared on external modules."""
        for ext in getattr(self.circuit, "_external_modules", {}).values():
            for rtl in getattr(ext, "_rtl_files", []) or []:
                try:
                    self.add_external_rtl_file(rtl)
                except Exception as e:
                    ext_name = getattr(ext, "_name", None) or getattr(
                        ext, "_firrtl_module_name", None
                    )
                    raise RuntimeError(
                        f"Failed to stage external RTL file {rtl!s}"
                        + (f" for external module {ext_name!r}" if ext_name else "")
                    ) from e

    def _get_top_module_name(self) -> str:
        """Get the top-level module name from the circuit.

        Returns the last defined module, which is typically the top-level.
        For circuits with STL modules, the first modules are library modules
        and the last is the user's top-level module.
        """
        if self.circuit._modules:
            # Return the last module (typically the top-level)
            return list(self.circuit._modules.keys())[-1]
        return self.circuit.name

    def _add_stl_rtl(self):
        """Add STL RTL files from ModuleLibrary.

        Converts cached FIRRTL modules from ModuleLibrary to Verilog
        and adds them to the workspace.
        """
        from .module_library import get_module_library

        library = get_module_library()

        needed_module_names: set[str] = set()

        # 1) Modules explicitly requested by the circuit (extern bindings).
        # These are the "true hardware" extern modules backed by ModuleLibrary.
        for ext in getattr(self.circuit, "_external_modules", {}).values():
            firrtl_name = getattr(ext, "_firrtl_module_name", None) or getattr(ext, "_name", None)
            if firrtl_name:
                needed_module_names.add(firrtl_name)

        # 2) Modules referenced by emitted Verilog but not defined in it.
        # This is primarily for auto-generated Proc FSM regs (and similar),
        # which are created during lowering and may not be present in
        # circuit._external_modules.
        verilog = getattr(self, "_last_emitted_verilog", None)
        defined_modules: set[str] = set()
        referenced_modules: set[str] = set()
        if isinstance(verilog, str) and verilog:
            defined_modules = self._extract_defined_modules(verilog)
            referenced_modules = self._extract_instantiated_modules(verilog)
            needed_module_names.update(referenced_modules - defined_modules)

        # Build only what we know how to build from ModuleLibrary.
        for module_name in sorted(needed_module_names):
            build_spec = self._module_library_build_spec(module_name)
            if build_spec is None:
                continue
            library_name, params = build_spec
            library.build_module(library_name, params)

        verilog_modules = library.get_verilog_for_modules()

        # Only stage files that (a) were requested and (b) have generated RTL.
        for module_name in sorted(needed_module_names):
            verilog_content = verilog_modules.get(module_name)
            if not verilog_content:
                continue

            # If the emitted SV already defines this module, don't write a
            # duplicate file into the workspace.
            if module_name in defined_modules:
                continue

            filename = f"{module_name}.sv"
            if filename not in self._external_rtl:
                self.add_external_rtl(filename, verilog_content)

    def _add_user_external_module_rtl(self) -> None:
        """Stage user-provided RTL for custom external modules.

        External modules can optionally register one or more RTL files via
        `ExternalModuleBuilder.rtl_path(...)`. Those files are copied into the
        workspace so Verilator can elaborate the design.
        """
        from pathlib import Path

        for ext in getattr(self.circuit, "_external_modules", {}).values():
            rtl_files = getattr(ext, "_rtl_files", None)
            if not isinstance(rtl_files, dict) or not rtl_files:
                continue
            for filename, path in rtl_files.items():
                if filename in self._external_rtl:
                    continue
                p = Path(path)
                try:
                    content = p.read_text()
                except Exception as e:
                    raise RuntimeError(
                        f"Failed to read external RTL file for extern '{getattr(ext, 'name', '<ext>')}': {p}"
                    ) from e
                self.add_external_rtl(filename, content)

    @staticmethod
    def _extract_defined_modules(verilog: str) -> set[str]:
        return set(
            re.findall("(?m)^\\s*module\\s+([A-Za-z_][A-Za-z0-9_$]*)\\b", verilog)
        )

    @staticmethod
    def _extract_instantiated_modules(verilog: str) -> set[str]:
        # Heuristic: matches "<Type> <inst> (" at the start of a line, excluding
        # "module <Type> (...)" definitions.
        matches = re.findall(
            "(?m)^\\s*(?!module\\b)([A-Za-z_][A-Za-z0-9_$]*)\\s+[A-Za-z_][A-Za-z0-9_$]*\\s*\\(",
            verilog,
        )
        return set(matches)

    @staticmethod
    def _module_library_build_spec(module_name: str) -> tuple[str, dict[str, int]] | None:
        # Reg_width${width}_init${init}
        m = re.fullmatch("Reg_width(\\d+)_init(\\d+)", module_name)
        if m:
            return ("FIRRTLReg", {"width": int(m.group(1)), "init": int(m.group(2))})

        # Wire_w${width}
        m = re.fullmatch("Wire_w(\\d+)", module_name)
        if m:
            return ("Wire", {"width": int(m.group(1))})

        # Mem1r1w1c_w${data_width}_a${addr_width}_d${depth}
        m = re.fullmatch("Mem1r1w1c_w(\\d+)_a(\\d+)_d(\\d+)", module_name)
        if m:
            return (
                "Mem1r1w1c",
                {"data_width": int(m.group(1)), "addr_width": int(m.group(2)), "depth": int(m.group(3))},
            )

        # Mem1r1w0c_w${data_width}_a${addr_width}_d${depth}
        m = re.fullmatch("Mem1r1w0c_w(\\d+)_a(\\d+)_d(\\d+)", module_name)
        if m:
            return (
                "Mem1r1w0c",
                {"data_width": int(m.group(1)), "addr_width": int(m.group(2)), "depth": int(m.group(3))},
            )

        return None

    def generate_placeholder(self):
        """Generate workspace with placeholder testbench.

        Creates a simulation workspace with a template testbench
        that users can customize.
        """
        self._create_directories()
        self._generate_rtl()
        self._generate_placeholder_testbench()
        self._generate_makefile()
        self._generate_readme()
        if self._trace:
            self.generate_waveform_config()

        print(f"Simulation workspace generated at {self.output_dir}")
        print(f"Edit tb/testbench.cpp to add your test logic")
        print(f"Run 'make' to build and 'make run' to simulate")

    def generate_with_testbench(self, testbench: Testbench):
        """Generate workspace with testbench from DSL.

        Args:
            testbench: A Testbench object describing the test sequences.
        """
        self._create_directories()
        self._generate_rtl()
        self._generate_testbench_from_dsl(testbench)
        self._generate_makefile()
        self._generate_readme()
        if self._trace:
            self.generate_waveform_config()

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
            errors="replace",
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
            errors="replace",
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

    def generate_waveform_config(self, output_file: str | None = None) -> str:
        """Generate GTKWave save file with signal annotations.

        Creates a .gtkw file that groups signals by CMT2 construct type:
        - Clock and Reset signals
        - Rule firing signals (if debug_ports enabled)
        - Register values
        - Instance signals

        Args:
            output_file: Optional path to write the .gtkw file.
                        If not specified, writes to waves/<top>.gtkw.

        Returns:
            The path to the generated .gtkw file.
        """
        top = self._top_module
        if output_file is None:
            output_file = str(self.output_dir / "waves" / f"{top}.gtkw")

        # Collect signals from the circuit
        clock_signals = []
        reset_signals = []
        rule_signals = []
        register_signals = []
        other_signals = []

        # Analyze circuit structure for signal groups
        if hasattr(self.circuit, '_modules') and self.circuit._modules:
            for mod_name, mod in self.circuit._modules.items():
                prefix = f"TOP.{top}"

                # Clock and reset
                if hasattr(mod, '_clock') and mod._clock:
                    clock_signals.append(f"{prefix}.clk")
                if hasattr(mod, '_reset') and mod._reset:
                    reset_signals.append(f"{prefix}.rst")

                # Rules (debug firing signals)
                if hasattr(mod, '_rules'):
                    for rule_name in mod._rules:
                        if self._debug_ports:
                            rule_signals.append(f"{prefix}.dbg_{rule_name}_firing")

                # Instances (likely registers)
                if hasattr(mod, '_instances'):
                    for inst_name, inst in mod._instances.items():
                        register_signals.append(f"{prefix}.{inst_name}_read_data")

        # Generate GTKWave save file
        gtkw_content = f"""[*]
[*] GTKWave Signal Configuration for {top}
[*] Generated by PyCMT2
[*]
[dumpfile] "{self.output_dir}/waves/{top}.vcd"
[dumpfile_size] 0
[savefile] "{output_file}"
[timestart] 0
[size] 1920 1080
[pos] -1 -1
*-19.000000 50000 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1
[treeopen] TOP.
[treeopen] TOP.{top}.
[sst_width] 233
[signals_width] 300
[sst_expanded] 1
[sst_vpaned_height] 300
"""

        # Add signal groups
        if clock_signals or reset_signals:
            gtkw_content += "@28\n"  # Group header
            gtkw_content += f"-Clock/Reset\n"
            for sig in clock_signals + reset_signals:
                gtkw_content += f"+{{{sig}}}\n"
                gtkw_content += f"{sig}\n"

        if rule_signals:
            gtkw_content += "@28\n"
            gtkw_content += f"-Rule Firing\n"
            for sig in rule_signals:
                gtkw_content += f"+{{{sig}}}\n"
                gtkw_content += f"{sig}\n"

        if register_signals:
            gtkw_content += "@22\n"  # Hexadecimal display
            gtkw_content += f"-Registers\n"
            for sig in register_signals:
                gtkw_content += f"+{{{sig}}}\n"
                gtkw_content += f"{sig}\n"

        gtkw_content += "@22\n"
        gtkw_content += f"-Other\n"
        gtkw_content += f"+{{TOP.{top}.*}}\n"

        gtkw_content += "[pattern_trace] 1\n[pattern_trace] 0\n"

        # Write the file
        Path(output_file).parent.mkdir(parents=True, exist_ok=True)
        Path(output_file).write_text(gtkw_content)

        print(f"GTKWave config written to {output_file}")
        print(f"Open with: gtkwave {self.output_dir}/waves/{top}.vcd -a {output_file}")

        return output_file

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
            if self._use_circt_opt:
                verilog = self._emit_verilog_via_circt_opt()
            else:
                verilog = self.circuit.emit_verilog(debug_ports=self._debug_ports)
            self._last_emitted_verilog = verilog
            self._add_stl_rtl()
            self._add_user_external_module_rtl()
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

        trace_include = '#include "verilated_vcd_c.h"\n' if self._trace else ""
        trace_setup = ""
        trace_step = ""
        trace_close = ""
        if self._trace:
            trace_setup = f"""
    Verilated::traceEverOn(true);

    // Create VCD trace
    auto tfp = std::make_unique<VerilatedVcdC>();
    dut->trace(tfp.get(), 99);
    tfp->open("waves/{top}.vcd");
"""
            trace_step = """
        tfp->dump(cycle * 10);
"""
            trace_close = """
    // Finalize
    tfp->close();
"""

        tb_content = f'''\
// Testbench for {top}
// Generated by PyCMT2 SimulationWorkspace
//
// Edit this file to add your test logic.

#include "V{top}.h"
#include "verilated.h"
{trace_include}

#include <iostream>
#include <memory>

// Simulation parameters
constexpr int MAX_CYCLES = 1000;
constexpr int RESET_CYCLES = 5;

int main(int argc, char** argv) {{
    // Initialize Verilator
    Verilated::commandArgs(argc, argv);

    // Create DUT instance
    auto dut = std::make_unique<V{top}>();
{trace_setup}

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
{trace_step}

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

{trace_close}

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

        trace_include = '#include "verilated_vcd_c.h"\n' if self._trace else ""
        trace_setup = ""
        trace_close = ""
        if self._trace:
            trace_setup = f"""
    Verilated::traceEverOn(true);

    auto tfp_owner = std::make_unique<VerilatedVcdC>();
    tfp = tfp_owner.get();
    dut->trace(tfp, 99);
    tfp->open("waves/{top}.vcd");
"""
            trace_close = """
    tfp->close();
    tfp = nullptr;
"""

        tb_content = f'''\
// Testbench for {top}
// Generated by PyCMT2 Testbench DSL

#include "V{top}.h"
#include "verilated.h"
{trace_include}

#include <iostream>
#include <memory>
#include <functional>
#include <vector>
#include <string>

// DUT instance (global for sequence access)
static V{top}* dut;
static int cycle;
static uint64_t cycle_count;
static VerilatedVcdC* tfp;
static uint64_t sim_time;

// Helper functions
void tick() {{
    dut->clk = 0;
    dut->eval();
    if (tfp) tfp->dump(sim_time++);
    dut->clk = 1;
    dut->eval();
    if (tfp) tfp->dump(sim_time++);
    cycle++;
    cycle_count++;
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

    dut = new V{top}();
{trace_setup}

    dut->clk = 0;
    dut->rst = 0;
    cycle = 0;
    cycle_count = 0;
    sim_time = 0;

    std::cout << "Running test sequences..." << std::endl;

    // Run all sequences
    run_all_sequences();

{trace_close}
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
        """Generate C++ code for test sequences.

        Uses the to_cpp() method of each testbench operation to generate
        C++ code, ensuring all operation types are properly handled.
        """
        sequence_functions = []
        sequence_names = []

        for seq in testbench._sequences:
            sequence_names.append(seq.name)
            lines = [f'void run_{seq.name}() {{']
            lines.append(f'    std::cout << "Running sequence: {seq.name}" << std::endl;')

            # Use the operation's to_cpp() method for correct code generation
            for op in seq._ops:
                cpp_code = op.to_cpp()
                # Indent each line of the generated code
                for line in cpp_code.split('\n'):
                    lines.append(f'    {line}')

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
        verilator_output_split = os.environ.get("PYCMT2_VERILATOR_OUTPUT_SPLIT", "").strip().lower()
        split_n: int | None = None
        if verilator_output_split in ("", "0", "false", "no", "off"):
            split_n = None
        elif verilator_output_split in ("1", "true", "yes", "on"):
            split_n = 20000
        else:
            try:
                split_n = int(verilator_output_split)
            except ValueError:
                split_n = None

        split_flags = ""
        if split_n and split_n > 0:
            split_flags = (
                f"    --output-split {split_n} \\\n"
                f"    --output-split-cfuncs {split_n} \\\n"
            )

        fast_build = os.environ.get("PYCMT2_FAST_BUILD", "").strip().lower() not in (
            "",
            "0",
            "false",
            "no",
            "off",
        )
        fast_flags = "    -CFLAGS -O0 \\\n" if fast_build else ""

        trace_flags = (
            "    --trace --trace-structs $(VERILATOR_TRACE_UNDERSCORE_FLAG) \\\n"
            if self._trace
            else ""
        )
        extra_flags = f"{split_flags}{fast_flags}{trace_flags}"
        makefile_content = f'''\
# Makefile for {top} simulation
# Generated by PyCMT2 SimulationWorkspace

# Verilator configuration
VERILATOR ?= verilator
# NOTE: Verilator does not trace signals with a leading '_' by default.
# Many compiler-generated internal signals use '_' prefixes to avoid name
# collisions, so enable tracing underscores by default for debuggability.
VERILATOR_TRACE_UNDERSCORE ?= 1
VERILATOR_HAS_TRACE_UNDERSCORE := $(shell $(VERILATOR) --help 2>&1 | grep -q -- '--trace-underscore' && echo 1 || echo 0)
VERILATOR_TRACE_UNDERSCORE_FLAG :=
ifeq ($(VERILATOR_TRACE_UNDERSCORE),1)
ifeq ($(VERILATOR_HAS_TRACE_UNDERSCORE),1)
VERILATOR_TRACE_UNDERSCORE_FLAG := --trace-underscore
endif
endif

VERILATOR_FLAGS = --cc --exe --build -j 0 \\
{extra_flags}    -Wall -Wno-fatal \\
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
