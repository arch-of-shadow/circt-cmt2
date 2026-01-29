#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Circuit builder for PyCMT2 EDSL."""

from __future__ import annotations

import sys
import dis
from contextlib import contextmanager
from pathlib import Path
from typing import TYPE_CHECKING, Iterator

if TYPE_CHECKING:
    from .module import ModuleBuilder


def _get_assignment_target(depth: int = 2) -> str | None:
    """Use bytecode introspection to find the variable name being assigned to.

    This enables JIT naming where:
        my_circuit = Circuit()  # Name is inferred as "my_circuit"

    Args:
        depth: Stack frame depth to inspect.

    Returns:
        The variable name if found, None otherwise.
    """
    try:
        frame = sys._getframe(depth)
        code = frame.f_code
        instructions = list(dis.get_instructions(code))

        for i, instr in enumerate(instructions):
            if instr.offset >= frame.f_lasti:
                for j in range(i, min(i + 5, len(instructions))):
                    next_instr = instructions[j]
                    if next_instr.opname in ("STORE_NAME", "STORE_FAST", "STORE_GLOBAL"):
                        return next_instr.argval
    except Exception:
        pass
    return None


class Context:
    """MLIR context wrapper for PyCMT2."""

    def __init__(self):
        from circt.ir import Context as MlirContext, Location
        from circt.dialects import cmt2 as cmt2_dialect

        self._mlir_ctx = MlirContext()
        # Register all CIRCT dialects (including FIRRTL)
        import circt
        circt.register_dialects(self._mlir_ctx)

        from .location import get_default_mlir_location

        self._loc = get_default_mlir_location(self._mlir_ctx)

    @property
    def mlir_context(self):
        return self._mlir_ctx

    @property
    def location(self):
        return self._loc


class Circuit:
    """Top-level circuit container.

    Circuit is the entry point for building CMT2 designs. It manages
    modules, external modules, and interfaces.

    Example:
        circuit = Circuit("MyDesign")

        with circuit.module("Counter") as mod:
            clk = mod.clock()
            rst = mod.reset()
            # ... define rules, methods, etc.

        print(circuit.emit_mlir())
    """

    def __init__(self, name: str | None = None):
        """Create a new circuit.

        Args:
            name: Optional circuit name. If not provided, the name is
                  inferred from the variable assignment using JIT introspection.
        """
        self._name = name
        self._ctx = Context()
        self._modules: dict[str, ModuleBuilder] = {}
        self._external_modules: dict[str, object] = {}
        self._interfaces: dict[str, object] = {}
        self._op = None

        # Resolve name and create circuit op
        resolved_name = self._resolve_name()
        self._create_circuit_op(resolved_name)

    def _resolve_name(self) -> str:
        """Resolve the circuit name, using JIT if not provided."""
        if self._name is not None:
            return self._name

        # Try JIT naming
        jit_name = _get_assignment_target(depth=3)
        if jit_name:
            self._name = jit_name
            return jit_name

        # Fallback
        self._name = "Circuit"
        return self._name

    def _create_circuit_op(self, name: str):
        """Create the MLIR circuit operation."""
        from circt.ir import InsertionPoint, Module as MlirModule, Block
        from circt.dialects import cmt2

        # Create a module to hold the circuit
        self._mlir_module = MlirModule.create(self._ctx.location)

        with InsertionPoint(self._mlir_module.body):
            self._op = cmt2.CircuitOp(loc=self._ctx.location)

        # Add an entry block to the circuit's body region
        self._op.regions[0].blocks.append()

    def __enter__(self):
        """Enter context manager."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Exit context manager."""
        return False

    @property
    def name(self) -> str:
        """Get the circuit name."""
        return self._name or "Circuit"

    @contextmanager
    def module(self, name: str | None = None) -> Iterator[ModuleBuilder]:
        """Create a module within this circuit.

        Args:
            name: Optional module name. If not provided, inferred from
                  the context manager variable.

        Yields:
            A ModuleBuilder for defining the module contents.

        Example:
            with circuit.module("Counter") as counter:
                clk = counter.clock()
                # ...
        """
        from .module import ModuleBuilder

        builder = ModuleBuilder(self, name)
        yield builder
        builder._finalize()
        self._modules[builder.name] = builder

    @contextmanager
    def external_module(
        self,
        name: str,
        *,
        rtl: str | Path | None = None,
        rtl_files: list[str | Path] | None = None,
    ) -> Iterator["ExternalModuleBuilder"]:
        """Create an external module binding.

        External modules define the CMT2 interface to FIRRTL modules,
        including clock/reset ports, value methods, and action methods.

        Args:
            name: The external module name.
            rtl: Optional path to a Verilog/SystemVerilog file implementing the
                external module. When provided, SimulationWorkspace will stage
                it into the generated workspace automatically.
            rtl_files: Optional list of additional RTL file paths to stage.

        Yields:
            An ExternalModuleBuilder for defining bindings.

        Example:
            with circuit.external_module("Reg32") as reg:
                reg.clock("clk")
                reg.reset("rst")
                reg.value("read", ready_name="read_ready", returns=[("data", UInt(32))])
                reg.method(
                    "write",
                    enable_name="write_enable",
                    ready_name="write_ready",
                    args=[("data", UInt(32))],
                )
                reg.sequence_before("read", "write")
        """
        from .external_module import ExternalModuleBuilder

        builder = ExternalModuleBuilder(self, name, rtl=rtl, rtl_files=rtl_files)
        yield builder
        builder._finalize()
        self._external_modules[name] = builder

    @contextmanager
    def interface(self, name: str | None = None) -> Iterator[object]:
        """Define an interface.

        Args:
            name: Optional interface name.

        Yields:
            An InterfaceBuilder for defining the interface.
        """
        from .interface import InterfaceBuilder

        builder = InterfaceBuilder(self, name)
        yield builder
        builder._finalize()
        self._interfaces[builder.name] = builder

    def include_library_module(
        self, library_name: str, params: dict[str, int] | None = None
    ) -> str | None:
        """Include a FIRRTL module from the ModuleLibrary.

        This builds the FIRRTL module from Chisel sources (if needed) and
        includes it in the circuit. The module can then be referenced by
        cmt2.module.extern.firrtl bindings.

        Args:
            library_name: Name of the module in the library (e.g., "FIRRTLReg")
            params: Parameters for the module (e.g., {"width": 32, "init": 0})

        Returns:
            The actual FIRRTL module name (e.g., "Reg_width32_init0"),
            or None if the module couldn't be built/included.

        Example:
            # Include a 32-bit register
            circuit.include_library_module("FIRRTLReg", {"width": 32, "init": 0})
        """
        from .module_library import get_module_library

        if params is None:
            params = {}

        library = get_module_library()
        return library.include_module_in_circuit(
            library_name, params, self._mlir_module
        )

    def emit_mlir(self) -> str:
        """Emit the circuit as MLIR text.

        Returns:
            The MLIR representation of the circuit.
        """
        return str(self._mlir_module)

    def emit_firrtl(self) -> str:
        """Run CMT2-to-FIRRTL conversion and emit FIRRTL MLIR.

        This runs the CMT2 pass pipeline to lower the design to FIRRTL.

        Returns:
            The FIRRTL MLIR representation of the circuit.
        """
        from circt.passmanager import PassManager
        import copy

        # Clone the module to preserve original
        cloned = self._clone_module()

        # Run CMT2 to FIRRTL pipeline.
        #
        # Note: `cmt2-compile-static` is required to legalize call-site timing
        # attributes (`arg_timing` / `result_timing`) inside `cmt2.proc.static_step`
        # before procedural lowering clones calls out of the step body.
        pm = PassManager.parse(
            "builtin.module("
            "cmt2.circuit("
            "cmt2-compile-invoke,"
            "cmt2-tdcc,"
            "cmt2-static-inference,"
            "cmt2-static-promotion,"
            "cmt2-timing-inference,"
            "cmt2-timing-validation,"
            "cmt2-static-fsm-allocation,"
            "cmt2-compile-static,"
            "cmt2-proc-stmt-to-action,"
            "cmt2-proc-to-gaa"
            "),"
            "lower-cmt2-to-firrtl"
            ")",
            context=self._ctx.mlir_context,
        )
        pm.run(cloned.operation)

        return str(cloned)

    def emit_verilog(self, output_dir: str | None = None, debug_ports: bool = False) -> str:
        """Run full compilation pipeline and emit Verilog.

        This runs the complete pipeline: CMT2 -> FIRRTL -> HW -> Verilog.

        Args:
            output_dir: Optional directory to write Verilog files.
                       If None, returns Verilog as a string.
            debug_ports: If True, adds debug firing ports for each rule.
                        These ports expose the rule fire signals for debugging
                        and testbench assertions. Default is False.

        Returns:
            The Verilog representation of the circuit.
        """
        from circt.passmanager import PassManager
        import io

        # Clone the module to preserve original
        cloned = self._clone_module()

        # Build the CMT2 pass pipeline
        # The debug pass must run after cmt2-proc-to-gaa (rules exist) but before lowering
        passes = [
            "cmt2-compile-invoke",
            "cmt2-dataflow-lowering",
            "cmt2-token-lowering",
            "cmt2-token-rtl-gen",
            "cmt2-tdcc",
            "cmt2-static-inference",
            "cmt2-static-promotion",
            "cmt2-timing-inference",
            "cmt2-timing-validation",
            "cmt2-static-fsm-allocation",
            "cmt2-compile-static",
            "cmt2-proc-stmt-to-action",
            "cmt2-proc-to-gaa",
        ]
        if debug_ports:
            passes.append("cmt2-add-rule-firing-port")

        cmt2_passes = ",".join(passes)

        # Run full pipeline: CMT2 -> FIRRTL -> lower-to-hw -> export-verilog
        try:
            # First run CMT2 to FIRRTL
            # CMT2 passes operate on cmt2.circuit, conversion operates on builtin.module
            # Pipeline includes dataflow passes for token-based pipelines
            cmt2_pm = PassManager.parse(
                f"builtin.module("
                f"cmt2.circuit({cmt2_passes}),"
                f"lower-cmt2-to-firrtl"
                f")",
                context=self._ctx.mlir_context
            )
            cmt2_pm.run(cloned.operation)

            # Then run FIRRTL passes to prepare for lowering
            # These passes are needed to handle high-level FIRRTL constructs
            firrtl_prep_pm = PassManager.parse(
                "builtin.module("
                "firrtl.circuit("
                "firrtl-infer-resets,"
                "firrtl-lower-types"
                "),"
                "any(any(firrtl-expand-whens))"
                ")",
                context=self._ctx.mlir_context
            )
            firrtl_prep_pm.run(cloned.operation)

            # Then run FIRRTL to HW/SV conversion
            firrtl_pm = PassManager.parse(
                "builtin.module("
                "lower-firrtl-to-hw,"
                "lower-seq-to-sv"
                ")",
                context=self._ctx.mlir_context
            )
            firrtl_pm.run(cloned.operation)

        except Exception as e:
            # If passes fail, return error message as comment
            return f"// Verilog generation failed: {e}\n// Run passes manually for debugging."

        # Export to Verilog
        output = io.StringIO()
        from circt import export_verilog
        export_verilog(cloned, output)
        result = output.getvalue()

        if output_dir:
            import os
            os.makedirs(output_dir, exist_ok=True)
            filepath = os.path.join(output_dir, f"{self.name}.sv")
            with open(filepath, 'w') as f:
                f.write(result)

        return result

    def to_verilog(self, debug_ports: bool = False) -> str:
        """Run the compilation pipeline and emit Verilog.

        Deprecated: Use emit_verilog() instead.

        Args:
            debug_ports: If True, adds debug firing ports for rules.

        Returns:
            The Verilog representation of the circuit.
        """
        return self.emit_verilog(debug_ports=debug_ports)

    def debug_pipeline(
        self,
        output_dir: str,
        passes: list[str] | None = None,
        stop_on_error: bool = True,
        debug_ports: bool = False,
    ) -> dict[str, str]:
        """Run passes and dump IR after each pass for debugging.

        This is useful for debugging pass pipeline issues. It runs each pass
        individually and saves the IR to files in the output directory.

        Args:
            output_dir: Directory to write IR dumps.
            passes: List of pass strings to run. If None, uses the default
                   CMT2 compilation pipeline.
            stop_on_error: If True, stops at first failing pass. Otherwise
                          continues and logs errors.
            debug_ports: If True, includes cmt2-add-rule-firing-port in the
                        default pass list.

        Returns:
            Dict mapping pass name to the IR after that pass (or error message).

        Example:
            results = circuit.debug_pipeline("./debug_ir", passes=[
                "cmt2-compile-invoke",
                "cmt2-tdcc",
                "cmt2-proc-stmt-to-action",
                "cmt2-proc-to-gaa",
                "cmt2-add-rule-firing-port",
                "lower-cmt2-to-firrtl",
            ])
        """
        import os
        from circt.passmanager import PassManager

        os.makedirs(output_dir, exist_ok=True)

        # Default passes
        if passes is None:
            passes = [
                # CMT2 passes
                "cmt2-compile-invoke",
                # Dataflow passes (for token-based pipelines)
                "cmt2-dataflow-lowering",
                "cmt2-token-lowering",
                "cmt2-token-rtl-gen",
                # Procedural control passes
                "cmt2-tdcc",
                "cmt2-static-inference",
                "cmt2-static-promotion",
                "cmt2-timing-inference",
                "cmt2-timing-validation",
                "cmt2-static-fsm-allocation",
                "cmt2-compile-static",
                "cmt2-proc-stmt-to-action",
                "cmt2-proc-to-gaa",
            ]
            # Optionally add debug ports pass
            if debug_ports:
                passes.append("cmt2-add-rule-firing-port")
            passes.extend([
                # Lowering to FIRRTL
                "lower-cmt2-to-firrtl",
                # FIRRTL passes
                "firrtl-infer-resets",
                "firrtl-lower-types",
                # HW passes
                "lower-firrtl-to-hw",
                "lower-seq-to-sv",
            ])

        results = {}
        cloned = self._clone_module()

        # Dump initial IR
        initial_ir = str(cloned)
        results["00_initial"] = initial_ir
        with open(os.path.join(output_dir, "00_initial.mlir"), "w") as f:
            f.write(initial_ir)

        for idx, pass_str in enumerate(passes, start=1):
            pass_name = f"{idx:02d}_{pass_str.replace('-', '_')}"

            # Build the pass pipeline string
            # CMT2 passes need cmt2.circuit nesting
            if pass_str.startswith("cmt2-"):
                pipeline = f"builtin.module(cmt2.circuit({pass_str}))"
            elif pass_str.startswith("firrtl-"):
                pipeline = f"builtin.module(firrtl.circuit({pass_str}))"
            elif pass_str == "lower-cmt2-to-firrtl":
                pipeline = f"builtin.module({pass_str})"
            elif pass_str == "lower-firrtl-to-hw" or pass_str == "lower-seq-to-sv":
                pipeline = f"builtin.module({pass_str})"
            else:
                # Generic pass
                pipeline = f"builtin.module({pass_str})"

            try:
                pm = PassManager.parse(pipeline, context=self._ctx.mlir_context)
                pm.run(cloned.operation)

                ir_after = str(cloned)
                results[pass_name] = ir_after

                with open(os.path.join(output_dir, f"{pass_name}.mlir"), "w") as f:
                    f.write(ir_after)

            except Exception as e:
                error_msg = f"// Pass failed: {pass_str}\n// Error: {e}"
                results[pass_name] = error_msg

                with open(os.path.join(output_dir, f"{pass_name}_ERROR.mlir"), "w") as f:
                    f.write(error_msg)
                    f.write("\n\n// IR before failure:\n")
                    f.write(str(cloned))

                if stop_on_error:
                    break

        return results

    def _clone_module(self):
        """Clone the MLIR module for pass running.

        Uses Operation.clone() to create a deep copy without serialization,
        avoiding MLIR print/parse round-trip issues (e.g., boolean constants).
        """
        from circt.ir import Module as MlirModule, InsertionPoint

        # Create a new empty module
        new_module = MlirModule.create(self._ctx.location)

        # Clone all operations from the original module's body
        with InsertionPoint(new_module.body):
            for op in self._mlir_module.body:
                op.operation.clone()

        return new_module

    def __repr__(self) -> str:
        return f"Circuit({self.name!r})"
