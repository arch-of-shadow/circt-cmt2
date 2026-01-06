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

        # FIRRTL module name (defaults to same as CMT2 module name)
        self._firrtl_module_name: str | None = None

        # Pending method bindings (added at finalize time)
        self._pending_values: list[dict] = []  # {name, ready_name, args, returns}
        self._pending_methods: list[dict] = []  # {name, enable_name, ready_name, args, returns}

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

    def set_firrtl_module_name(self, name: str) -> ExternalModuleBuilder:
        """Set the FIRRTL module name for this external module.

        By default, the FIRRTL module name matches the CMT2 module name.
        Use this to bind to a differently-named FIRRTL module (e.g., from
        ModuleLibrary where module names include parameters).

        Args:
            name: The FIRRTL module name (e.g., "Reg_width32_init0").

        Returns:
            self for chaining.
        """
        self._firrtl_module_name = name
        return self

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
        ready_name: str | None = None,
        args: list[tuple[str, Cmt2Type]] | None = None,
        returns: list[tuple[str, Cmt2Type]] | None = None,
        static_latency: int | None = None,
    ) -> ExternalModuleBuilder:
        """Declare a value method binding.

        Value methods are read-only and return data.

        Args:
            name: Value method name (used as symbol).
            ready_name: Port name for the ready output signal.
            args: Method arguments as (name, type) pairs.
            returns: Return values as (name, type) pairs.
            static_latency: Optional fixed latency in cycles for static scheduling.

        Returns:
            self for chaining.

        Example:
            fifo.value("full", ready_name="full_ready", returns=[("full_data", UInt(1))])
            # With static latency for pipelined access
            mem.value("read", static_latency=2, returns=[("data", UInt(32))])
        """
        self._pending_values.append({
            "name": name,
            "ready_name": ready_name,
            "args": args or [],
            "returns": returns or [],
            "static_latency": static_latency,
        })
        return self

    def method(
        self,
        name: str,
        enable_name: str | None = None,
        ready_name: str | None = None,
        args: list[tuple[str, Cmt2Type]] | None = None,
        returns: list[tuple[str, Cmt2Type]] | None = None,
        static_latency: int | None = None,
        interval: int | None = None,
    ) -> ExternalModuleBuilder:
        """Declare an action method binding.

        Action methods may have side effects.

        Args:
            name: Method name (used as symbol).
            enable_name: Port name for the enable input signal.
            ready_name: Port name for the ready output signal.
            args: Method arguments as (name, type) pairs.
            returns: Return values as (name, type) pairs.
            static_latency: Optional fixed latency in cycles for static scheduling.
            interval: Optional initiation interval for pipelined methods.
                      If provided, the method can accept new calls every
                      `interval` cycles. Must be <= static_latency.

        Returns:
            self for chaining.

        Example:
            fifo.method("enq", enable_name="enq_enable", ready_name="enq_ready",
                        args=[("enq_data", UInt(32))])

            # Pipelined method (8-cycle latency, can start new call every 2 cycles)
            mult.method("multiply", static_latency=8, interval=2,
                       args=[("a", UInt(32)), ("b", UInt(32))],
                       returns=[("result", UInt(64))])
        """
        self._pending_methods.append({
            "name": name,
            "enable_name": enable_name,
            "ready_name": ready_name,
            "args": args or [],
            "returns": returns or [],
            "static_latency": static_latency,
            "interval": interval,
        })
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

        # Use custom FIRRTL module name if set, otherwise default to CMT2 module name
        firrtl_name = self._firrtl_module_name if self._firrtl_module_name else self._name

        with InsertionPoint(self._circuit._op.body):
            self._op = cmt2.ExtModuleFirrtlOp(
                sym_name=StringAttr.get(self._name, context=ctx),
                ext_module_name=FlatSymbolRefAttr.get(firrtl_name, context=ctx),
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
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FunctionType, TypeAttr, IntegerAttr, IntegerType
        from circt.dialects import cmt2

        mlir_ctx = self._circuit._ctx.mlir_context
        mlir_loc = self._python_loc.to_mlir_location(mlir_ctx)

        # Add value bindings
        for val_info in self._pending_values:
            name = val_info["name"]
            ready_name = val_info["ready_name"]
            args = val_info["args"]
            returns = val_info["returns"]
            static_latency = val_info.get("static_latency")

            with InsertionPoint(self._op.body.blocks[0]):
                arg_types = [ty.to_firrtl_type(mlir_ctx) for _, ty in args]
                ret_types = [ty.to_firrtl_type(mlir_ctx) for _, ty in returns]
                func_type = FunctionType.get(arg_types, ret_types)

                arg_name_attrs = [StringAttr.get(n, context=mlir_ctx) for n, _ in args]
                res_name_attrs = [StringAttr.get(n, context=mlir_ctx) for n, _ in returns]

                # Build static_latency attribute if provided
                static_latency_attr = None
                if static_latency is not None:
                    static_latency_attr = IntegerAttr.get(
                        IntegerType.get_signless(64), static_latency
                    )

                op = cmt2.BindValueOp(
                    sym_name=StringAttr.get(name, context=mlir_ctx),
                    function_type=TypeAttr.get(func_type),
                    readyName=StringAttr.get(ready_name, context=mlir_ctx) if ready_name else None,
                    argNames=ArrayAttr.get(arg_name_attrs, context=mlir_ctx),
                    bodyResNames=ArrayAttr.get(res_name_attrs, context=mlir_ctx),
                    loc=mlir_loc,
                )
                # Add static_latency as attribute if provided
                if static_latency_attr is not None:
                    op.attributes["static_latency"] = static_latency_attr

        # Add method bindings
        for meth_info in self._pending_methods:
            name = meth_info["name"]
            enable_name = meth_info["enable_name"]
            ready_name = meth_info["ready_name"]
            args = meth_info["args"]
            returns = meth_info["returns"]
            static_latency = meth_info.get("static_latency")
            interval = meth_info.get("interval")

            with InsertionPoint(self._op.body.blocks[0]):
                arg_types = [ty.to_firrtl_type(mlir_ctx) for _, ty in args]
                ret_types = [ty.to_firrtl_type(mlir_ctx) for _, ty in returns]
                func_type = FunctionType.get(arg_types, ret_types)

                arg_name_attrs = [StringAttr.get(n, context=mlir_ctx) for n, _ in args]
                res_name_attrs = [StringAttr.get(n, context=mlir_ctx) for n, _ in returns]

                # Build timing attributes if provided
                static_latency_attr = None
                if static_latency is not None:
                    static_latency_attr = IntegerAttr.get(
                        IntegerType.get_signless(64), static_latency
                    )

                interval_attr = None
                if interval is not None:
                    interval_attr = cmt2.IntervalAttr.get(mlir_ctx, interval)

                op = cmt2.BindMethodOp(
                    sym_name=StringAttr.get(name, context=mlir_ctx),
                    function_type=TypeAttr.get(func_type),
                    enableName=StringAttr.get(enable_name, context=mlir_ctx) if enable_name else None,
                    readyName=StringAttr.get(ready_name, context=mlir_ctx) if ready_name else None,
                    argNames=ArrayAttr.get(arg_name_attrs, context=mlir_ctx),
                    bodyResNames=ArrayAttr.get(res_name_attrs, context=mlir_ctx),
                    static_latency=static_latency_attr,
                    interval=interval_attr,
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
        for val_info in self._pending_values:
            if val_info["name"] == name:
                return [ty for _, ty in val_info["returns"]]
        return []

    def get_method_return_types(self, name: str) -> list[Cmt2Type]:
        """Get the return types for a method."""
        for meth_info in self._pending_methods:
            if meth_info["name"] == name:
                return [ty for _, ty in meth_info["returns"]]
        return []

    def get_value_arg_types(self, name: str) -> list[Cmt2Type]:
        """Get the argument types for a value method."""
        for val_info in self._pending_values:
            if val_info["name"] == name:
                return [ty for _, ty in val_info["args"]]
        return []

    def get_method_arg_types(self, name: str) -> list[Cmt2Type]:
        """Get the argument types for a method."""
        for meth_info in self._pending_methods:
            if meth_info["name"] == name:
                return [ty for _, ty in meth_info["args"]]
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
