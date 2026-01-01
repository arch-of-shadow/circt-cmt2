#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""External module builder for PyCMT2 EDSL.

This module provides builders for creating CMT2 external modules that bind
to FIRRTL modules. External modules are used for standard library components
like registers, FIFOs, and memories.

Example:
    from pycmt2 import Circuit, UInt

    circuit = Circuit("MyDesign")

    # Define an external register module
    with circuit.external_module("Reg32") as reg:
        reg.clock("clk")
        reg.reset("rst")
        reg.value("read", returns=[UInt(32)])
        reg.method("write", args=[("data", UInt(32))])
        reg.sequence_before("read", "write")

    # Use it in a module
    with circuit.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(reg, "count", clk=clk, rst=rst)
        # ...
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Iterator

from .types import Cmt2Type, UInt, ClockType, ResetType
from .location import get_python_location
from .refs import Instance, MethodRef, ValueRef

if TYPE_CHECKING:
    from .circuit import Circuit


class ExternalModuleBuilder:
    """Builder for CMT2 external modules.

    External modules define the interface to FIRRTL modules, including:
    - Clock and reset ports
    - Value methods (read-only)
    - Action methods (may have side effects)
    - Scheduling constraints (sequence before, conflict, conflict-free)
    """

    def __init__(self, circuit: Circuit, name: str):
        self._circuit = circuit
        self._name = name
        self._op = None

        # Port tracking
        self._clock_port: str | None = None
        self._reset_port: str | None = None
        self._args: list[tuple[str, Cmt2Type]] = []

        # Pending method bindings (added at finalize time)
        self._pending_values: list[tuple[str, list, list]] = []  # (name, args, returns)
        self._pending_methods: list[tuple[str, list, list]] = []  # (name, args, returns)

        # Scheduling constraints
        self._sequence_before: list[tuple[str, str]] = []
        self._conflict: list[tuple[str, str]] = []
        self._conflict_free: list[tuple[str, str]] = []

        # Capture location
        self._python_loc = get_python_location(depth=3)

    @property
    def name(self) -> str:
        """Get the external module name."""
        return self._name

    def clock(self, name: str = "clk") -> ExternalModuleBuilder:
        """Declare a clock port.

        Args:
            name: Port name.

        Returns:
            self for chaining.
        """
        self._clock_port = name
        self._args.append((name, ClockType()))
        return self

    def reset(self, name: str = "rst") -> ExternalModuleBuilder:
        """Declare a reset port.

        Args:
            name: Port name.

        Returns:
            self for chaining.
        """
        self._reset_port = name
        self._args.append((name, ResetType()))
        return self

    def value(
        self,
        name: str,
        args: list[tuple[str, Cmt2Type]] | None = None,
        returns: list[Cmt2Type] | None = None,
    ) -> ExternalModuleBuilder:
        """Declare a value method binding.

        Value methods are read-only and return data.

        Args:
            name: Method name.
            args: Method arguments as (name, type) pairs.
            returns: Return types.

        Returns:
            self for chaining.
        """
        self._pending_values.append((name, args or [], returns or []))
        return self

    def method(
        self,
        name: str,
        args: list[tuple[str, Cmt2Type]] | None = None,
        returns: list[Cmt2Type] | None = None,
    ) -> ExternalModuleBuilder:
        """Declare an action method binding.

        Action methods may have side effects.

        Args:
            name: Method name.
            args: Method arguments as (name, type) pairs.
            returns: Return types.

        Returns:
            self for chaining.
        """
        self._pending_methods.append((name, args or [], returns or []))
        return self

    def sequence_before(self, before: str, after: str) -> ExternalModuleBuilder:
        """Declare that method 'before' must sequence before 'after'.

        Args:
            before: Method that must execute first.
            after: Method that must execute after.

        Returns:
            self for chaining.
        """
        self._sequence_before.append((before, after))
        return self

    def conflict(self, method1: str, method2: str) -> ExternalModuleBuilder:
        """Declare that two methods conflict (cannot fire together).

        Args:
            method1: First conflicting method.
            method2: Second conflicting method.

        Returns:
            self for chaining.
        """
        self._conflict.append((method1, method2))
        return self

    def conflict_free(self, method1: str, method2: str) -> ExternalModuleBuilder:
        """Declare that two methods are conflict-free.

        Args:
            method1: First method.
            method2: Second method.

        Returns:
            self for chaining.
        """
        self._conflict_free.append((method1, method2))
        return self

    def _finalize(self):
        """Finalize the external module by creating the MLIR operations."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FlatSymbolRefAttr
        from circt.dialects import cmt2

        ctx = self._circuit._ctx.mlir_context
        mlir_loc = self._python_loc.to_mlir_location(ctx)

        # Build argument names from declared ports
        arg_names = [StringAttr.get(name, context=ctx) for name, _ in self._args]

        # Build argument types for the block
        arg_types = [ty.to_firrtl_type(ctx) for _, ty in self._args]

        with InsertionPoint(self._circuit._op.body):
            self._op = cmt2.ExtModuleFirrtlOp(
                sym_name=StringAttr.get(self._name, context=ctx),
                ext_module_name=FlatSymbolRefAttr.get(self._name, context=ctx),
                argNames=ArrayAttr.get(arg_names, context=ctx),
                loc=mlir_loc,
            )
            # Create body block with arguments for clock/reset
            from circt.ir import Location
            with Location.unknown(ctx):
                block = Block.create_at_start(self._op.body, arg_types)

        # Add bind.bare operations for clock/reset arguments
        self._add_bind_bare_operations(block)

        # Add binding operations for values and methods
        self._add_bind_operations()

        # Add scheduling constraint attributes
        self._add_scheduling_constraints()

    def _add_bind_bare_operations(self, block):
        """Add bind.bare operations for clock/reset ports."""
        from circt.ir import InsertionPoint, FlatSymbolRefAttr
        from circt.dialects import cmt2

        mlir_ctx = self._circuit._ctx.mlir_context
        mlir_loc = self._python_loc.to_mlir_location(mlir_ctx)

        # Create bind.bare for each argument (clock, reset, etc.)
        with InsertionPoint.at_block_begin(block):
            for i, (port_name, _) in enumerate(self._args):
                block_arg = block.arguments[i]
                port_ref = FlatSymbolRefAttr.get(port_name, context=mlir_ctx)
                cmt2.BindBareOp(
                    signal=block_arg,
                    port=port_ref,
                    loc=mlir_loc,
                )

    def _add_bind_operations(self):
        """Add bind.value and bind.method operations."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FunctionType, TypeAttr
        from circt.dialects import cmt2

        mlir_ctx = self._circuit._ctx.mlir_context
        mlir_loc = self._python_loc.to_mlir_location(mlir_ctx)

        # Add value bindings
        for name, args, returns in self._pending_values:
            with InsertionPoint(self._op.body.blocks[0]):
                arg_types = [ty.to_firrtl_type(mlir_ctx) for _, ty in args]
                ret_types = [ty.to_firrtl_type(mlir_ctx) for ty in returns]
                func_type = FunctionType.get(arg_types, ret_types)

                arg_name_attrs = [StringAttr.get(n, context=mlir_ctx) for n, _ in args]
                res_name_attrs = [StringAttr.get(f"res{i}", context=mlir_ctx) for i in range(len(returns))]

                cmt2.BindValueOp(
                    sym_name=StringAttr.get(name, context=mlir_ctx),
                    function_type=TypeAttr.get(func_type),
                    argNames=ArrayAttr.get(arg_name_attrs, context=mlir_ctx),
                    bodyResNames=ArrayAttr.get(res_name_attrs, context=mlir_ctx),
                    loc=mlir_loc,
                )

        # Add method bindings
        for name, args, returns in self._pending_methods:
            with InsertionPoint(self._op.body.blocks[0]):
                arg_types = [ty.to_firrtl_type(mlir_ctx) for _, ty in args]
                ret_types = [ty.to_firrtl_type(mlir_ctx) for ty in returns]
                func_type = FunctionType.get(arg_types, ret_types)

                arg_name_attrs = [StringAttr.get(n, context=mlir_ctx) for n, _ in args]
                res_name_attrs = [StringAttr.get(f"res{i}", context=mlir_ctx) for i in range(len(returns))]

                cmt2.BindMethodOp(
                    sym_name=StringAttr.get(name, context=mlir_ctx),
                    function_type=TypeAttr.get(func_type),
                    argNames=ArrayAttr.get(arg_name_attrs, context=mlir_ctx),
                    bodyResNames=ArrayAttr.get(res_name_attrs, context=mlir_ctx),
                    loc=mlir_loc,
                )

    def _add_scheduling_constraints(self):
        """Add scheduling constraint attributes to the op."""
        from circt.ir import ArrayAttr, StringAttr

        mlir_ctx = self._circuit._ctx.mlir_context

        if self._sequence_before:
            pairs = []
            for before, after in self._sequence_before:
                pairs.append(ArrayAttr.get([
                    StringAttr.get(before, context=mlir_ctx),
                    StringAttr.get(after, context=mlir_ctx),
                ], context=mlir_ctx))
            self._op.attributes["sequenceBefore"] = ArrayAttr.get(pairs, context=mlir_ctx)

        if self._conflict:
            pairs = []
            for m1, m2 in self._conflict:
                pairs.append(ArrayAttr.get([
                    StringAttr.get(m1, context=mlir_ctx),
                    StringAttr.get(m2, context=mlir_ctx),
                ], context=mlir_ctx))
            self._op.attributes["conflict"] = ArrayAttr.get(pairs, context=mlir_ctx)

        if self._conflict_free:
            pairs = []
            for m1, m2 in self._conflict_free:
                pairs.append(ArrayAttr.get([
                    StringAttr.get(m1, context=mlir_ctx),
                    StringAttr.get(m2, context=mlir_ctx),
                ], context=mlir_ctx))
            self._op.attributes["conflictFree"] = ArrayAttr.get(pairs, context=mlir_ctx)

    def get_value_return_types(self, name: str) -> list[Cmt2Type]:
        """Get the return types for a value method."""
        for vname, args, returns in self._pending_values:
            if vname == name:
                return returns
        return []

    def get_method_return_types(self, name: str) -> list[Cmt2Type]:
        """Get the return types for a method."""
        for mname, args, returns in self._pending_methods:
            if mname == name:
                return returns
        return []

    def get_value_arg_types(self, name: str) -> list[Cmt2Type]:
        """Get the argument types for a value method."""
        for vname, args, returns in self._pending_values:
            if vname == name:
                return [ty for _, ty in args]
        return []

    def get_method_arg_types(self, name: str) -> list[Cmt2Type]:
        """Get the argument types for a method."""
        for mname, args, returns in self._pending_methods:
            if mname == name:
                return [ty for _, ty in args]
        return []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

    def __repr__(self) -> str:
        return f"ExternalModuleBuilder({self.name!r})"


class ExternalModuleInstance:
    """Instance of an external module.

    Provides access to the bound methods and values.
    """

    def __init__(
        self,
        name: str,
        ext_module: ExternalModuleBuilder,
        inst_op,
    ):
        self.name = name
        self._ext_module = ext_module
        self._op = inst_op

    def method_ref(self, method_name: str) -> MethodRef:
        """Get a reference to a method on this instance."""
        return MethodRef(self, method_name)

    def value_ref(self, value_name: str) -> ValueRef:
        """Get a reference to a value on this instance."""
        return ValueRef(self, value_name)

    @property
    def read(self) -> str:
        """Convenience for standard 'read' value method."""
        return "read"

    @property
    def write(self) -> str:
        """Convenience for standard 'write' action method."""
        return "write"

    def __repr__(self) -> str:
        return f"ExternalModuleInstance({self.name!r}, {self._ext_module.name!r})"
