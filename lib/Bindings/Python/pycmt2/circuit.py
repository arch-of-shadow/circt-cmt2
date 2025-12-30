#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Circuit builder for PyCMT2 EDSL."""

from __future__ import annotations

import sys
import dis
from contextlib import contextmanager
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

        self._loc = Location.unknown(self._mlir_ctx)

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
        self, firrtl_name: str, name: str | None = None
    ) -> Iterator[object]:
        """Create an external FIRRTL module binding.

        Args:
            firrtl_name: The name of the FIRRTL module to bind.
            name: Optional CMT2 module name.

        Yields:
            An ExternalModuleBuilder for defining bindings.
        """
        # TODO: Implement ExternalModuleBuilder
        raise NotImplementedError("External modules not yet implemented")

    @contextmanager
    def interface(self, name: str | None = None) -> Iterator[object]:
        """Define an interface.

        Args:
            name: Optional interface name.

        Yields:
            An InterfaceBuilder for defining the interface.
        """
        # TODO: Implement InterfaceBuilder
        raise NotImplementedError("Interfaces not yet implemented")

    def emit_mlir(self) -> str:
        """Emit the circuit as MLIR text.

        Returns:
            The MLIR representation of the circuit.
        """
        return str(self._mlir_module)

    def to_verilog(self) -> str:
        """Run the compilation pipeline and emit Verilog.

        Returns:
            The Verilog representation of the circuit.
        """
        from circt.passmanager import PassManager
        import io

        # Clone the module for pass running
        # pm = PassManager.parse(
        #     "builtin.module("
        #     "cmt2-compile-invoke,"
        #     "cmt2-to-firrtl"
        #     ")"
        # )
        # pm.run(self._mlir_module)

        # Export to verilog
        output = io.StringIO()
        from circt import export_verilog
        export_verilog(self._mlir_module, output)
        return output.getvalue()

    def __repr__(self) -> str:
        return f"Circuit({self.name!r})"
