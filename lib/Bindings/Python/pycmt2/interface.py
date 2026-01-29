#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Interface builders for PyCMT2.

PyCMT2 interfaces are defined at circuit scope using `cmt2.interface` and can be
declared/defined inside modules using `cmt2.interface.decl` / `cmt2.interface.def`.

This module implements:
- `InterfaceBuilder`: creates a `cmt2.interface` and defines its methods/values.
- `InterfaceDefBuilder`: builds a `cmt2.interface.def` mapping interface functions
  to underlying instance functions.
"""

from __future__ import annotations

from contextlib import contextmanager
from typing import TYPE_CHECKING, Any, Iterator

from .function_builders import MethodBuilder, ValueBuilder

if TYPE_CHECKING:
    from .circuit import Circuit
    from .types import Cmt2Type


class InterfaceBuilder:
    """Builder for a circuit-level interface.

    Example:
        with circuit.interface("Reader") as i:
            with i.value("getData", returns=[UInt(32)]) as v:
                with v.guard() as g: g.always()
                with v.body() as b: ...
    """

    def __init__(self, circuit: "Circuit", name: str | None = None):
        self._circuit = circuit
        self._name = name
        self._op = None
        self._methods: dict[str, MethodBuilder] = {}
        self._values: dict[str, ValueBuilder] = {}

        self._create_interface_op()

    @property
    def name(self) -> str:
        if self._name is None:
            from .circuit import _get_assignment_target

            jit_name = _get_assignment_target(depth=4)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"Interface_{id(self):x}"
        return self._name

    def _create_interface_op(self) -> None:
        from circt.ir import InsertionPoint, StringAttr
        from circt.dialects import cmt2

        with InsertionPoint(self._circuit._op.body):
            self._op = cmt2.InterfaceOp(
                sym_name=StringAttr.get(self.name),
                loc=self._circuit._ctx.location,
            )

        # Add an entry block to the interface body region.
        self._op.regions[0].blocks.append()

    @contextmanager
    def method(
        self,
        name: str | None = None,
        args: list[tuple[str, "Cmt2Type"]] | None = None,
        returns: list["Cmt2Type"] | None = None,
    ) -> Iterator[MethodBuilder]:
        builder = MethodBuilder(self, name, args or [], returns or [])
        yield builder
        builder._finalize()
        self._methods[builder.name] = builder

    @contextmanager
    def value(
        self, name: str | None = None, returns: list["Cmt2Type"] | None = None
    ) -> Iterator[ValueBuilder]:
        builder = ValueBuilder(self, name, returns or [])
        yield builder
        builder._finalize()
        self._values[builder.name] = builder

    def get_function(self, name: str) -> MethodBuilder | ValueBuilder | None:
        return self._methods.get(name) or self._values.get(name)

    def _finalize(self) -> None:
        # Nothing to do yet; methods/values are finalized as they are created.
        return None


class InterfaceDefBuilder:
    """Helper for building a `cmt2.interface.def` mapping list."""

    def __init__(self, module_builder: Any, name: str, op: Any, interface: InterfaceBuilder):
        self._module_builder = module_builder
        self._name = name
        self._op = op
        self._interface = interface
        self._mappings: list[tuple[str, str, str]] = []

    @property
    def name(self) -> str:
        return self._name

    @property
    def interface(self) -> InterfaceBuilder:
        return self._interface

    def bind(self, target: Any, target_func: Any, interface_func: str) -> "InterfaceDefBuilder":
        """Bind an interface function to a target instance/decl function.

        Args:
            target: `Instance`, `ExternalInstance`, `InterfaceDecl`, or string symbol.
            target_func: `MethodRef`/`ValueRef` or string name on the target.
            interface_func: Name of the interface method/value.
        """
        # Unwrap common wrappers (e.g. JIT SignalRef/InterfaceRef) without
        # taking a dependency on those packages.
        if not hasattr(target, "name") and hasattr(target, "_instance"):
            target = getattr(target, "_instance")

        target_name = getattr(target, "name", None) or getattr(target, "_name", None) or target
        if not isinstance(target_name, str):
            raise TypeError(f"Expected target name to be str-like, got {type(target).__name__}")

        func_name = getattr(target_func, "name", None) or target_func
        if not isinstance(func_name, str):
            raise TypeError(
                f"Expected target_func to be MethodRef/ValueRef or str, got {type(target_func).__name__}"
            )
        if interface_func not in self._interface._methods and interface_func not in self._interface._values:
            raise KeyError(
                f"Interface '{self._interface.name}' has no function '{interface_func}'"
            )

        self._mappings.append((target_name, func_name, interface_func))
        self._flush()
        return self

    def _flush(self) -> None:
        from circt.ir import ArrayAttr, FlatSymbolRefAttr

        ctx = self._module_builder._circuit._ctx.mlir_context
        entries = []
        for target_name, func_name, iface_name in self._mappings:
            entries.append(
                ArrayAttr.get(
                    [
                        FlatSymbolRefAttr.get(target_name, context=ctx),
                        FlatSymbolRefAttr.get(func_name, context=ctx),
                        FlatSymbolRefAttr.get(iface_name, context=ctx),
                    ],
                    context=ctx,
                )
            )
        self._op.attributes["methods"] = ArrayAttr.get(entries, context=ctx)
