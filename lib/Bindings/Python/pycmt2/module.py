#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Module builder for PyCMT2 EDSL."""

from __future__ import annotations

from contextlib import contextmanager
from typing import TYPE_CHECKING, Iterator

from .types import Cmt2Type, UInt, ClockType, ResetType, Clock, Reset
from .signals import Signal
from .refs import MethodRef, ValueRef, GroupRef, Instance

if TYPE_CHECKING:
    from .circuit import Circuit
    from .function_builders import RuleBuilder, MethodBuilder, ValueBuilder
    from .proc_builders import ProcRuleBuilder, ProcMethodBuilder, GroupBuilder


class ModuleBuilder:
    """Builder for CMT2 modules.

    ModuleBuilder provides methods for defining ports, instances, rules,
    methods, values, and procedural constructs within a module.

    Example:
        with circuit.module("Counter") as mod:
            clk = mod.clock()
            rst = mod.reset()

            count_reg = mod.instance(Reg(32, init=0))

            with mod.rule("increment") as rule:
                with rule.guard() as g:
                    g.always()
                with rule.body() as b:
                    val = b.call(count_reg, count_reg.read)
                    b.call(count_reg, count_reg.write, val + b.const(1, 32))
    """

    def __init__(self, circuit: Circuit, name: str | None = None):
        self._circuit = circuit
        self._name = name
        self._op = None

        # Port tracking
        self._args: list[tuple[str, Cmt2Type, Signal]] = []
        self._arg_names: list[str] = []

        # Internal definitions
        self._instances: dict[str, Instance] = {}
        self._rules: dict[str, RuleBuilder] = {}
        self._methods: dict[str, MethodBuilder] = {}
        self._values: dict[str, ValueBuilder] = {}
        self._proc_rules: dict[str, ProcRuleBuilder] = {}
        self._proc_methods: dict[str, ProcMethodBuilder] = {}
        self._groups: dict[str, GroupBuilder] = {}

        # Scheduling directives
        self._sequence_before: list[tuple[MethodRef | ValueRef, MethodRef | ValueRef]] = []
        self._conflict: list[tuple[MethodRef | ValueRef, MethodRef | ValueRef]] = []
        self._conflict_free: list[tuple[MethodRef | ValueRef, MethodRef | ValueRef]] = []

        # Create the module op
        self._create_module_op()

    def _create_module_op(self):
        """Create the MLIR module operation."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block
        from circt.dialects import cmt2

        # Get circuit body
        circuit_body = self._circuit._op.body

        with InsertionPoint(circuit_body):
            # Create module with empty args for now
            self._op = cmt2.ModuleOp(
                sym_name=StringAttr.get(self.name),
                argNames=ArrayAttr.get([]),
                loc=self._circuit._ctx.location,
            )

        # Add an entry block to the module's body region
        self._op.regions[0].blocks.append()

    @property
    def name(self) -> str:
        """Get the module name."""
        if self._name is None:
            # Try JIT naming
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=4)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"Module_{id(self):x}"
        return self._name

    # Port declarations

    def clock(self, name: str = "clk") -> Signal[ClockType]:
        """Add a clock input port.

        Args:
            name: Port name (default: "clk").

        Returns:
            A Signal representing the clock.
        """
        return self._add_port(name, Clock)

    def reset(self, name: str = "rst") -> Signal[ResetType]:
        """Add a reset input port.

        Args:
            name: Port name (default: "rst").

        Returns:
            A Signal representing the reset.
        """
        return self._add_port(name, Reset)

    def input(self, name: str, ty: Cmt2Type) -> Signal:
        """Add an input port.

        Args:
            name: Port name.
            ty: Port type.

        Returns:
            A Signal representing the input.
        """
        return self._add_port(name, ty)

    def _add_port(self, name: str, ty: Cmt2Type) -> Signal:
        """Internal method to add a port."""
        from circt.ir import StringAttr, ArrayAttr, Block

        ctx = self._circuit._ctx.mlir_context

        # Add block argument to module body
        body_block = self._op.body
        firrtl_ty = ty.to_firrtl_type(ctx)
        arg = body_block.add_argument(firrtl_ty, self._circuit._ctx.location)

        # Track the argument
        self._arg_names.append(name)

        # Update argNames attribute
        self._op.attributes["argNames"] = ArrayAttr.get(
            [StringAttr.get(n, context=ctx) for n in self._arg_names],
            context=ctx
        )

        # Create a Signal wrapper
        from .builders import RegionBuilder
        # Create a dummy builder for the signal
        dummy_builder = RegionBuilder(body_block, self._circuit._ctx.location, self._circuit._ctx)
        sig = Signal(arg, ty, dummy_builder)
        self._args.append((name, ty, sig))
        return sig

    # Instance creation

    def instance(
        self,
        module: ModuleBuilder,
        name: str | None = None,
        interface_bindings: dict | None = None,
        **port_connections,
    ) -> Instance:
        """Create an instance of another module.

        Args:
            module: The module to instantiate.
            name: Optional instance name (inferred if not provided).
            interface_bindings: Optional interface bindings.
            **port_connections: Port connections as keyword arguments.

        Returns:
            An Instance object for accessing methods/values.
        """
        from circt.ir import InsertionPoint, StringAttr, FlatSymbolRefAttr
        from circt.dialects import cmt2

        # Resolve instance name
        if name is None:
            name = f"inst_{len(self._instances)}"

        with InsertionPoint(self._op.body):
            # Collect port values
            port_values = [
                port_connections[arg_name].value
                for arg_name, _, _ in module._args
                if arg_name in port_connections
            ]
            port_types = [v.type for v in port_values]

            inst_op = cmt2.InstanceOp(
                sym_name=StringAttr.get(name),
                module_name=FlatSymbolRefAttr.get(module.name),
                args=port_values,
                loc=self._circuit._ctx.location,
            )

        inst = Instance(name, module, port_connections, inst_op)
        self._instances[name] = inst
        return inst

    # Function-like operations

    @contextmanager
    def rule(self, name: str | None = None) -> Iterator[RuleBuilder]:
        """Define a rule.

        Args:
            name: Optional rule name (inferred if not provided).

        Yields:
            A RuleBuilder for defining guard and body.

        Example:
            with mod.rule("increment") as rule:
                with rule.guard() as g:
                    g.always()
                with rule.body() as b:
                    # ... rule body
        """
        from .function_builders import RuleBuilder

        builder = RuleBuilder(self, name)
        yield builder
        builder._finalize()
        self._rules[builder.name] = builder

    @contextmanager
    def method(
        self,
        name: str | None = None,
        args: list[tuple[str, Cmt2Type]] | None = None,
        returns: list[Cmt2Type] | None = None,
    ) -> Iterator[MethodBuilder]:
        """Define an action method.

        Args:
            name: Optional method name.
            args: Method arguments as (name, type) pairs.
            returns: Return types.

        Yields:
            A MethodBuilder for defining guard and body.
        """
        from .function_builders import MethodBuilder

        builder = MethodBuilder(self, name, args or [], returns or [])
        yield builder
        builder._finalize()
        self._methods[builder.name] = builder

    @contextmanager
    def value(
        self,
        name: str | None = None,
        returns: list[Cmt2Type] | None = None,
    ) -> Iterator[ValueBuilder]:
        """Define a value method.

        Args:
            name: Optional value name.
            returns: Return types.

        Yields:
            A ValueBuilder for defining guard and body.
        """
        from .function_builders import ValueBuilder

        builder = ValueBuilder(self, name, returns or [])
        yield builder
        builder._finalize()
        self._values[builder.name] = builder

    # Procedural operations

    @contextmanager
    def proc_rule(self, name: str | None = None) -> Iterator[ProcRuleBuilder]:
        """Define a procedural rule with multi-cycle control.

        Args:
            name: Optional rule name.

        Yields:
            A ProcRuleBuilder for defining guard and control.
        """
        from .proc_builders import ProcRuleBuilder

        builder = ProcRuleBuilder(self, name)
        yield builder
        builder._finalize()
        self._proc_rules[builder.name] = builder

    @contextmanager
    def proc_method(
        self,
        name: str | None = None,
        args: list[tuple[str, Cmt2Type]] | None = None,
        returns: list[Cmt2Type] | None = None,
    ) -> Iterator[ProcMethodBuilder]:
        """Define a procedural method.

        Args:
            name: Optional method name.
            args: Method arguments.
            returns: Return types.

        Yields:
            A ProcMethodBuilder for defining guard and control.
        """
        from .proc_builders import ProcMethodBuilder

        builder = ProcMethodBuilder(self, name, args or [], returns or [])
        yield builder
        builder._finalize()
        self._proc_methods[builder.name] = builder

    @contextmanager
    def group(self, name: str | None = None) -> Iterator[GroupBuilder]:
        """Define a procedural group (go-done interface).

        Args:
            name: Optional group name.

        Yields:
            A GroupBuilder for defining group body.
        """
        from .proc_builders import GroupBuilder

        builder = GroupBuilder(self, name)
        yield builder
        builder._finalize()
        self._groups[builder.name] = builder

    @contextmanager
    def static_group(
        self, latency: int, name: str | None = None
    ) -> Iterator[GroupBuilder]:
        """Define a static latency group.

        Args:
            latency: Fixed latency in cycles.
            name: Optional group name.

        Yields:
            A StaticGroupBuilder for defining group body.
        """
        from .proc_builders import StaticGroupBuilder

        builder = StaticGroupBuilder(self, name, latency)
        yield builder
        builder._finalize()
        self._groups[builder.name] = builder

    # Scheduling directives

    def sequence_before(
        self, before: MethodRef | ValueRef, after: MethodRef | ValueRef
    ) -> ModuleBuilder:
        """Declare that 'before' must sequence before 'after'.

        Args:
            before: The method/value that executes first.
            after: The method/value that executes second.

        Returns:
            self for chaining.
        """
        self._sequence_before.append((before, after))
        return self

    def conflict(
        self, a: MethodRef | ValueRef, b: MethodRef | ValueRef
    ) -> ModuleBuilder:
        """Declare that 'a' and 'b' conflict.

        Args:
            a: First method/value.
            b: Second method/value.

        Returns:
            self for chaining.
        """
        self._conflict.append((a, b))
        return self

    def conflict_free(
        self, a: MethodRef | ValueRef, b: MethodRef | ValueRef
    ) -> ModuleBuilder:
        """Declare that 'a' and 'b' are conflict-free.

        Args:
            a: First method/value.
            b: Second method/value.

        Returns:
            self for chaining.
        """
        self._conflict_free.append((a, b))
        return self

    def _finalize(self):
        """Finalize module construction."""
        # Apply scheduling directives as attributes
        # TODO: Convert to MLIR attributes
        pass

    def __repr__(self) -> str:
        return f"ModuleBuilder({self.name!r})"
