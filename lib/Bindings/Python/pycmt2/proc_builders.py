#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Procedural control builders for PyCMT2 EDSL."""

from __future__ import annotations

from contextlib import contextmanager
from typing import TYPE_CHECKING, Iterator

from .types import Cmt2Type, UInt, Bool
from .signals import Signal
from .builders import RegionBuilder
from .function_builders import GuardBuilder, BodyBuilder
from .refs import StepRef, MethodRef, ValueRef, RuleRef
from .location import get_python_location, PythonLocation

if TYPE_CHECKING:
    from .module import ModuleBuilder


class StepBuilder(RegionBuilder):
    """Builder for procedural steps.

    Groups are execution units with a go-done interface. They bundle
    operations that execute atomically when activated.

    Example:
        with mod.step("load") as load:
            val = load.call(input_reg, input_reg.read)
            load.call(reg_a, reg_a.write, val)
            load.done(load.const(1, 1))
    """

    def __init__(self, module: ModuleBuilder, name: str | None):
        self._module = module
        self._name = name
        self._done_set = False
        self._op = None

        # Capture Python source location for debugging
        # depth=3 to skip: __init__ -> step() -> contextmanager wrapper -> user code
        self._python_loc = get_python_location(depth=3)

        # Create the step op
        self._create_group_op()

        # Initialize RegionBuilder with Python source location
        body_block = self._op.body_block
        mlir_loc = self._python_loc.to_mlir_location(module._circuit._ctx.mlir_context)
        super().__init__(
            body_block,
            mlir_loc,
            module._circuit._ctx,
        )

    def _create_group_op(self):
        """Create the MLIR step operation."""
        from circt.ir import InsertionPoint, StringAttr, Block
        from circt.dialects import cmt2

        # Use Python source location for better error messages
        mlir_loc = self._python_loc.to_mlir_location(
            self._module._circuit._ctx.mlir_context
        )

        with InsertionPoint(self._module._op.body):
            self._op = cmt2.ProcStepOp(
                sym_name=StringAttr.get(self.name),
                loc=mlir_loc,
            )
            # Create body block
            body_block = Block.create_at_start(self._op.body)

    @property
    def name(self) -> str:
        """Get the step name."""
        if self._name is None:
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"group_{id(self):x}"
        return self._name

    def done(self, condition: Signal) -> None:
        """Signal that the step is done.

        Args:
            condition: Boolean condition indicating completion.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2

        if not isinstance(condition.type, UInt) or condition.type.width != 1:
            raise TypeError("Done condition must be Bool (UInt<1>)")

        with InsertionPoint(self._block):
            cmt2.ProcStepDoneOp(condition.value, loc=self._loc)

        self._done_set = True

    def ref(self) -> StepRef:
        """Get a reference to this step for control flow."""
        return StepRef(self, self.name)

    def _finalize(self):
        """Finalize step construction."""
        if not self._done_set:
            raise ValueError(f"Step '{self.name}' must call done()")


class StaticStepBuilder(RegionBuilder):
    """Builder for static latency steps.

    Static steps have a known fixed latency, so they don't need
    runtime done signals.

    Example:
        with mod.static_step(4, "multiply") as mult:
            # Operations with fixed 4-cycle latency
            mult.call(multiplier, multiplier.start, a, b)

        # Pipelined static step (8 cycles latency, II=2)
        with mod.static_step(8, "pipeline", interval=2) as step:
            step.call(pipe, "process", data)
    """

    def __init__(
        self,
        module: ModuleBuilder,
        name: str | None,
        latency: int,
        interval: int | None = None,
    ):
        self._module = module
        self._name = name
        self._latency = latency
        self._interval = interval
        self._op = None

        # Capture Python source location for debugging
        # depth=3 to skip: __init__ -> static_step() -> contextmanager wrapper -> user code
        self._python_loc = get_python_location(depth=3)

        # Create the static step op
        self._create_static_step_op()

        # Initialize RegionBuilder with Python source location
        body_block = self._op.body_block
        mlir_loc = self._python_loc.to_mlir_location(module._circuit._ctx.mlir_context)
        super().__init__(
            body_block,
            mlir_loc,
            module._circuit._ctx,
        )

    def _create_static_step_op(self):
        """Create the MLIR static step operation."""
        from circt.ir import InsertionPoint, StringAttr, Block, IntegerAttr, IntegerType
        from circt.dialects import cmt2

        # Use Python source location for better error messages
        mlir_loc = self._python_loc.to_mlir_location(
            self._module._circuit._ctx.mlir_context
        )

        # Build interval attribute if provided
        interval_attr = None
        if self._interval is not None:
            interval_attr = cmt2.IntervalAttr.get(
                self._module._circuit._ctx.mlir_context, self._interval
            )

        with InsertionPoint(self._module._op.body):
            self._op = cmt2.ProcStaticStepOp(
                sym_name=StringAttr.get(self.name),
                latency=IntegerAttr.get(IntegerType.get_signless(64), self._latency),
                interval=interval_attr,
                loc=mlir_loc,
            )
            # Create body block
            body_block = Block.create_at_start(self._op.body)

    @property
    def name(self) -> str:
        """Get the step name."""
        if self._name is None:
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"static_step_{id(self):x}"
        return self._name

    def ref(self) -> StepRef:
        """Get a reference to this step for control flow."""
        return StepRef(self, self.name)

    def _finalize(self):
        """Finalize static step construction."""
        pass


class ControlBuilder(RegionBuilder):
    """Builder for procedural control flow.

    ControlBuilder provides methods for sequential, parallel, conditional,
    and loop control structures. It extends RegionBuilder to provide access
    to expression-building methods like call(), lt(), add(), etc.

    Example:
        with proc_rule.control() as ctrl:
            with ctrl.seq():
                ctrl.enable(load.ref())
                # While loop with condition function
                def loop_cond(b):
                    cnt = b.call(counter, "read")
                    return b.lt(cnt, b.const(10, 32))
                with ctrl.while_(loop_cond) as loop:
                    loop.enable(step.ref())
                ctrl.enable(store.ref())
    """

    def __init__(self, block, loc, ctx):
        super().__init__(block, loc, ctx)

    @contextmanager
    def seq(self) -> Iterator[ControlBuilder]:
        """Sequential composition - execute children one after another.

        Yields:
            A nested ControlBuilder for sequential operations.
        """
        from circt.ir import InsertionPoint, Block
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            seq_op = cmt2.ProcSeqOp(loc=self._loc)
            seq_block = Block.create_at_start(seq_op.body)

        nested = ControlBuilder(seq_block, self._loc, self._ctx)
        yield nested

    @contextmanager
    def par(self) -> Iterator[ControlBuilder]:
        """Parallel composition - execute children concurrently.

        Yields:
            A nested ControlBuilder for parallel operations.
        """
        from circt.ir import InsertionPoint, Block
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            par_op = cmt2.ProcParOp(loc=self._loc)
            par_block = Block.create_at_start(par_op.body)

        nested = ControlBuilder(par_block, self._loc, self._ctx)
        yield nested

    @contextmanager
    def if_(self, condition: Signal) -> Iterator[IfControlBuilder]:
        """Conditional control flow.

        Args:
            condition: Boolean condition.

        Yields:
            An IfControlBuilder for then/else branches.
        """
        builder = IfControlBuilder(self, condition)
        yield builder
        builder._finalize()

    @contextmanager
    def while_(self, condition_fn) -> Iterator[ControlBuilder]:
        """While loop control flow with condition region.

        The condition is computed in a dedicated region that supports
        cmt2.call operations for reading from instances.

        Args:
            condition_fn: A callable that takes a RegionBuilder and returns
                          a Signal representing the loop condition.
                          Example: lambda b: b.lt(b.call(counter, "read"), b.const(10, 32))

        Yields:
            A nested ControlBuilder for the loop body.

        Example:
            # Define condition function
            def loop_cond(b):
                cnt = b.call(counter, "read")
                return b.lt(cnt, b.const(10, 32))

            # Use in while loop
            with ctrl.while_(loop_cond) as loop:
                loop.enable(step.ref())

            # Or with lambda
            with ctrl.while_(lambda b: b.lt(b.call(cnt_reg, "read"), b.const(10, 32))) as loop:
                loop.enable(step.ref())
        """
        from circt.ir import InsertionPoint, Block
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            # Create the while op with two regions
            while_op = cmt2.ProcWhileOp(loc=self._loc)

            # Create condition region block
            cond_block = Block.create_at_start(while_op.condRegion)
            # Create body region block
            body_block = Block.create_at_start(while_op.body)

        # Build condition in the condition region
        cond_builder = RegionBuilder(cond_block, self._loc, self._ctx)
        with InsertionPoint(cond_block):
            condition = condition_fn(cond_builder)
            # Add terminator to yield the condition
            cmt2.ProcWhileCondYieldOp(condition.value, loc=self._loc)

        # Yield body builder for the loop body
        nested = ControlBuilder(body_block, self._loc, self._ctx)
        yield nested

        # Add terminator to the body region
        with InsertionPoint(body_block):
            cmt2.ProcYieldOp(loc=self._loc)

    @contextmanager
    def static_repeat(
        self, count: int, body_latency: int | None = None
    ) -> Iterator[ControlBuilder]:
        """Static fixed-iteration loop with known total latency.

        Unlike dynamic while loops, static_repeat:
        - Has no loop counter register at runtime
        - No loop condition evaluation
        - FSM advances automatically based on static timing
        - Total latency = count * body_latency

        Args:
            count: Number of iterations (must be > 0).
            body_latency: Optional explicit body latency. If not specified,
                          inferred from body contents.

        Yields:
            A nested ControlBuilder for the loop body.

        Example:
            with ctrl.static_repeat(4) as loop:
                loop.enable(step_3cycle.ref())  # total = 4 * 3 = 12 cycles
        """
        from circt.ir import InsertionPoint, Block, IntegerAttr, IntegerType
        from circt.dialects import cmt2

        if count <= 0:
            raise ValueError("static_repeat count must be greater than 0")

        with InsertionPoint(self._block):
            count_attr = IntegerAttr.get(IntegerType.get_signless(64), count)
            latency_attr = None
            if body_latency is not None:
                latency_attr = IntegerAttr.get(
                    IntegerType.get_signless(64), body_latency
                )
            repeat_op = cmt2.ProcStaticRepeatOp(
                count=count_attr, body_latency=latency_attr, loc=self._loc
            )
            body_block = Block.create_at_start(repeat_op.body)

        nested = ControlBuilder(body_block, self._loc, self._ctx)
        yield nested

    @contextmanager
    def static_if(
        self,
        condition: Signal,
        then_latency: int | None = None,
        else_latency: int | None = None,
    ) -> Iterator[StaticIfControlBuilder]:
        """Static conditional with known branch latencies.

        Unlike dynamic if:
        - Both branches have deterministic execution time
        - No done signal checking at runtime
        - FSM advances based on static timing
        - Total latency = max(then_latency, else_latency)
        - Shorter branch is padded to match longer branch

        Args:
            condition: Boolean condition for branch selection.
            then_latency: Optional explicit then-branch latency.
            else_latency: Optional explicit else-branch latency.
                          Must specify both or neither.

        Yields:
            A StaticIfControlBuilder for then/else branches.

        Example:
            with ctrl.static_if(cond) as sif:
                with sif.then_() as then_ctrl:
                    then_ctrl.enable(branch_a.ref())  # 5 cycles
                with sif.else_() as else_ctrl:
                    else_ctrl.enable(branch_b.ref())  # 3 cycles
            # total latency = max(5, 3) = 5 cycles
        """
        if (then_latency is None) != (else_latency is None):
            raise ValueError(
                "static_if: must specify both then_latency and else_latency, or neither"
            )

        builder = StaticIfControlBuilder(
            self, condition, then_latency, else_latency
        )
        yield builder
        builder._finalize()

    def enable(self, group: StepRef) -> None:
        """Enable a step.

        Args:
            group: Reference to the step to enable.
        """
        from circt.ir import InsertionPoint, FlatSymbolRefAttr
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            cmt2.ProcEnableOp(
                stepName=FlatSymbolRefAttr.get(group.name),
                loc=self._loc,
            )

    def invoke(
        self,
        instance: object,
        method: MethodRef,
        *args: Signal,
    ) -> tuple[Signal, ...] | Signal | None:
        """Invoke a method within procedural control.

        Args:
            instance: The instance to invoke on.
            method: The method to invoke.
            *args: Arguments to pass.

        Returns:
            Return values as Signals.
        """
        from circt.ir import InsertionPoint, FlatSymbolRefAttr
        from circt.dialects import cmt2

        with InsertionPoint(self._block):
            # Get result types from method builder if available
            result_types = []
            if method.builder is not None and hasattr(method.builder, "_return_types"):
                result_types = [
                    ty.to_firrtl_type(self._ctx.mlir_context)
                    for ty in method.builder._return_types
                ]

            input_values = [arg.value for arg in args]

            invoke_op = cmt2.ProcInvokeOp(
                result_types,
                FlatSymbolRefAttr.get(instance.name),
                FlatSymbolRefAttr.get(method.name),
                input_values,
                loc=self._loc,
            )

            # Wrap results
            from .builders import RegionBuilder
            dummy_builder = RegionBuilder(self._block, self._loc, self._ctx)

            if len(invoke_op.results) == 0:
                return None
            elif len(invoke_op.results) == 1:
                ty = (
                    method.builder._return_types[0]
                    if method.builder and hasattr(method.builder, "_return_types")
                    else UInt(32)
                )
                return Signal(invoke_op.results[0], ty, dummy_builder)
            else:
                signals = []
                return_types = (
                    method.builder._return_types
                    if method.builder and hasattr(method.builder, "_return_types")
                    else [UInt(32)] * len(invoke_op.results)
                )
                for i, result in enumerate(invoke_op.results):
                    signals.append(Signal(result, return_types[i], dummy_builder))
                return tuple(signals)


class IfControlBuilder:
    """Builder for conditional control flow with then/else branches."""

    def __init__(self, parent: ControlBuilder, condition: Signal):
        self._parent = parent
        self._condition = condition
        self._then_builder: ControlBuilder | None = None
        self._else_builder: ControlBuilder | None = None
        self._op = None

    @contextmanager
    def then_(self) -> Iterator[ControlBuilder]:
        """The 'then' branch."""
        from circt.ir import InsertionPoint, Block
        from circt.dialects import cmt2

        with InsertionPoint(self._parent._block):
            self._op = cmt2.ProcIfOp(self._condition.value, loc=self._parent._loc)
            then_block = Block.create_at_start(self._op.thenRegion)

        self._then_builder = ControlBuilder(then_block, self._parent._loc, self._parent._ctx)
        yield self._then_builder

    @contextmanager
    def else_(self) -> Iterator[ControlBuilder]:
        """The 'else' branch (optional)."""
        from circt.ir import Block

        if self._op is None:
            raise RuntimeError("Must call then_() before else_()")

        else_block = Block.create_at_start(self._op.elseRegion)
        self._else_builder = ControlBuilder(else_block, self._parent._loc, self._parent._ctx)
        yield self._else_builder

    def _finalize(self):
        """Finalize the if operation."""
        pass


class StaticIfControlBuilder:
    """Builder for static conditional control flow with known branch latencies.

    Static if has deterministic execution time based on the maximum of
    both branch latencies. The shorter branch is implicitly padded.
    """

    def __init__(
        self,
        parent: ControlBuilder,
        condition: Signal,
        then_latency: int | None,
        else_latency: int | None,
    ):
        self._parent = parent
        self._condition = condition
        self._then_latency = then_latency
        self._else_latency = else_latency
        self._then_builder: ControlBuilder | None = None
        self._else_builder: ControlBuilder | None = None
        self._op = None

    @contextmanager
    def then_(self) -> Iterator[ControlBuilder]:
        """The 'then' branch of the static if."""
        from circt.ir import InsertionPoint, Block, IntegerAttr, IntegerType
        from circt.dialects import cmt2

        with InsertionPoint(self._parent._block):
            then_latency_attr = None
            else_latency_attr = None
            if self._then_latency is not None:
                then_latency_attr = IntegerAttr.get(
                    IntegerType.get_signless(64), self._then_latency
                )
            if self._else_latency is not None:
                else_latency_attr = IntegerAttr.get(
                    IntegerType.get_signless(64), self._else_latency
                )

            self._op = cmt2.ProcStaticIfOp(
                self._condition.value,
                then_latency=then_latency_attr,
                else_latency=else_latency_attr,
                loc=self._parent._loc,
            )
            then_block = Block.create_at_start(self._op.thenRegion)

        self._then_builder = ControlBuilder(
            then_block, self._parent._loc, self._parent._ctx
        )
        yield self._then_builder

    @contextmanager
    def else_(self) -> Iterator[ControlBuilder]:
        """The 'else' branch of the static if (optional)."""
        from circt.ir import Block

        if self._op is None:
            raise RuntimeError("Must call then_() before else_()")

        else_block = Block.create_at_start(self._op.elseRegion)
        self._else_builder = ControlBuilder(
            else_block, self._parent._loc, self._parent._ctx
        )
        yield self._else_builder

    def _finalize(self):
        """Finalize the static if operation."""
        pass


class ProcRuleBuilder:
    """Builder for procedural rules.

    Procedural rules extend regular rules with multi-cycle control flow.
    They have a guard region and a control region.

    Example:
        with mod.proc_rule("compute") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                with ctrl.seq():
                    ctrl.enable(load.ref())
                    # While with condition function that can call instances
                    with ctrl.while_(lambda b: b.lt(b.call(cnt, "read"), b.const(10, 32))) as loop:
                        loop.enable(step.ref())
                    ctrl.enable(store.ref())
    """

    def __init__(self, module: ModuleBuilder, name: str | None):
        self._module = module
        self._name = name
        self._guard_builder: GuardBuilder | None = None
        self._control_builder: ControlBuilder | None = None
        self._op = None

        # Capture Python source location for debugging
        # depth=3 to skip: __init__ -> proc_rule() -> contextmanager wrapper -> user code
        self._python_loc = get_python_location(depth=3)

        # Create the proc rule op
        self._create_proc_rule_op()

    def _create_proc_rule_op(self):
        """Create the MLIR procedural rule operation."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FunctionType, TypeAttr
        from circt.dialects import cmt2

        # Use Python source location for better error messages
        mlir_loc = self._python_loc.to_mlir_location(
            self._module._circuit._ctx.mlir_context
        )

        with InsertionPoint(self._module._op.body):
            func_type = FunctionType.get([], [])

            self._op = cmt2.ProcRuleOp(
                sym_name=StringAttr.get(self.name),
                function_type=TypeAttr.get(func_type),
                argNames=ArrayAttr.get([]),
                loc=mlir_loc,
            )

            # Create guard and control blocks
            guard_block = Block.create_at_start(self._op.guard)
            control_block = Block.create_at_start(self._op.control)

    @property
    def name(self) -> str:
        """Get the rule name."""
        if self._name is None:
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"proc_rule_{id(self):x}"
        return self._name

    @contextmanager
    def guard(self) -> Iterator[GuardBuilder]:
        """Enter the guard region."""
        guard_block = self._op.guard_block
        mlir_loc = self._python_loc.to_mlir_location(
            self._module._circuit._ctx.mlir_context
        )
        self._guard_builder = GuardBuilder(
            self,
            guard_block,
            mlir_loc,
            self._module._circuit._ctx,
        )
        with self._guard_builder as g:
            yield g

    @contextmanager
    def control(self) -> Iterator[ControlBuilder]:
        """Enter the control region."""
        control_block = self._op.control_block
        mlir_loc = self._python_loc.to_mlir_location(
            self._module._circuit._ctx.mlir_context
        )
        self._control_builder = ControlBuilder(
            control_block,
            mlir_loc,
            self._module._circuit._ctx,
        )
        yield self._control_builder
        # Add control end terminator
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2
        with InsertionPoint(control_block):
            cmt2.ProcControlEndOp(loc=self._module._circuit._ctx.location)

    def ref(self) -> RuleRef:
        """Get a reference to this proc rule for scheduling directives.

        Returns:
            A RuleRef for use with precedence() and other scheduling directives.
        """
        return RuleRef(self, self.name)

    def _finalize(self):
        """Finalize procedural rule construction."""
        if self._guard_builder is None:
            raise ValueError(f"ProcRule '{self.name}' must have a guard region")
        if self._control_builder is None:
            raise ValueError(f"ProcRule '{self.name}' must have a control region")


class ProcMethodBuilder:
    """Builder for procedural methods.

    Procedural methods extend regular methods with multi-cycle control flow.
    Unlike atomic methods, procedural methods CAN have timing attributes
    (static_latency, interval) because they execute over multiple cycles.

    Example (dynamic timing):
        with mod.proc_method("multiply", args=[("a", UInt(32)), ("b", UInt(32))],
                            returns=[UInt(64)]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.control() as ctrl:
                with ctrl.seq():
                    ctrl.enable(start.ref())
                    ctrl.enable(wait.ref())

    Example (static timing):
        with mod.proc_method("fast_mult", args=[("a", UInt(32)), ("b", UInt(32))],
                            returns=[UInt(64)], static_latency=4, interval=2) as meth:
            # Method takes 4 cycles total, new calls can start every 2 cycles (pipelined)
            with meth.guard() as g:
                g.always()
            with meth.control() as ctrl:
                with ctrl.seq():
                    ctrl.static_step(4, "compute")
    """

    def __init__(
        self,
        module: ModuleBuilder,
        name: str | None,
        args: list[tuple[str, Cmt2Type]],
        returns: list[Cmt2Type],
        static_latency: int | None = None,
        interval: int | None = None,
    ):
        self._module = module
        self._name = name
        self._arg_types = args
        self._return_types = returns
        self._static_latency = static_latency
        self._interval = interval
        self._guard_builder: GuardBuilder | None = None
        self._control_builder: ControlBuilder | None = None
        self._op = None
        self._arg_signals: list[tuple[str, Signal]] = []

        # Create the proc method op
        self._create_proc_method_op()

    def _create_proc_method_op(self):
        """Create the MLIR procedural method operation."""
        from circt.ir import InsertionPoint, StringAttr, ArrayAttr, Block, FunctionType, TypeAttr, IntegerAttr, IntegerType
        from circt.dialects import cmt2

        ctx = self._module._circuit._ctx

        with InsertionPoint(self._module._op.body):
            # Build function type
            arg_mlir_types = [ty.to_firrtl_type(ctx.mlir_context) for _, ty in self._arg_types]
            ret_mlir_types = [ty.to_firrtl_type(ctx.mlir_context) for ty in self._return_types]
            func_type = FunctionType.get(arg_mlir_types, ret_mlir_types)

            arg_names = [StringAttr.get(name) for name, _ in self._arg_types]
            body_res_names = [StringAttr.get(f"res{i}") for i in range(len(self._return_types))]

            self._op = cmt2.ProcMethodOp(
                sym_name=StringAttr.get(self.name),
                function_type=TypeAttr.get(func_type),
                argNames=ArrayAttr.get(arg_names),
                bodyResNames=ArrayAttr.get(body_res_names),
                loc=ctx.location,
            )

            # Set timing attributes after op creation
            if self._static_latency is not None:
                self._op.attributes["static_latency"] = IntegerAttr.get(
                    IntegerType.get_signless(64), self._static_latency
                )
            if self._interval is not None:
                # Use cmt2.IntervalAttr for the interval attribute
                self._op.attributes["interval"] = cmt2.IntervalAttr.get(
                    ctx.mlir_context, self._interval
                )

            # Create guard and control blocks with arguments
            arg_locs = [ctx.location] * len(arg_mlir_types)
            guard_block = Block.create_at_start(self._op.guard, arg_mlir_types, arg_locs)
            control_block = Block.create_at_start(self._op.control, arg_mlir_types, arg_locs)

            # Create signals for arguments
            from .builders import RegionBuilder
            dummy_builder = RegionBuilder(control_block, ctx.location, ctx)
            for i, (arg_name, arg_ty) in enumerate(self._arg_types):
                sig = Signal(control_block.arguments[i], arg_ty, dummy_builder)
                self._arg_signals.append((arg_name, sig))

    @property
    def name(self) -> str:
        """Get the method name."""
        if self._name is None:
            from .circuit import _get_assignment_target
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"proc_method_{id(self):x}"
        return self._name

    @contextmanager
    def guard(self) -> Iterator[GuardBuilder]:
        """Enter the guard region."""
        guard_block = self._op.guard_block
        self._guard_builder = GuardBuilder(
            self,
            guard_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
        )
        with self._guard_builder as g:
            # Expose arguments
            for arg_name, sig in self._arg_signals:
                setattr(g, arg_name, sig)
            yield g

    @contextmanager
    def control(self) -> Iterator[ControlBuilder]:
        """Enter the control region."""
        control_block = self._op.control_block
        self._control_builder = ControlBuilder(
            control_block,
            self._module._circuit._ctx.location,
            self._module._circuit._ctx,
        )
        yield self._control_builder
        # Add control end terminator
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2
        with InsertionPoint(control_block):
            cmt2.ProcControlEndOp(loc=self._module._circuit._ctx.location)

    @property
    def is_static(self) -> bool:
        """Returns True if this method has static timing (known latency)."""
        return self._static_latency is not None

    @property
    def latency(self) -> int | None:
        """Get the static latency, or None if dynamic."""
        return self._static_latency

    @property
    def interval(self) -> int | None:
        """Get the initiation interval, or None if not pipelined."""
        return self._interval

    @property
    def is_pipelined(self) -> bool:
        """Returns True if this method is pipelined (has interval)."""
        return self._interval is not None

    def ref(self) -> MethodRef:
        """Get a reference to this method for scheduling."""
        return MethodRef(self, None, self.name)

    def _finalize(self):
        """Finalize procedural method construction."""
        if self._guard_builder is None:
            raise ValueError(f"ProcMethod '{self.name}' must have a guard region")
        if self._control_builder is None:
            raise ValueError(f"ProcMethod '{self.name}' must have a control region")

        # TV2: Validate static_latency matches control flow
        if self._static_latency is not None:
            self._validate_static_latency()

    def _validate_static_latency(self):
        """Validate that declared static_latency matches the control flow.

        Walks the control region and computes the actual latency from
        seq/par/enable operations. Raises ValueError if mismatch.
        """
        computed = self._compute_region_latency(self._op.control)
        if computed is None:
            raise ValueError(
                f"ProcMethod '{self.name}' has static_latency={self._static_latency} "
                f"but control region contains dynamic constructs (dynamic steps, if, or while); "
                f"use only static_step, static_repeat, static_if, seq, and par for static methods"
            )
        if computed != self._static_latency:
            raise ValueError(
                f"ProcMethod '{self.name}' declared static_latency={self._static_latency} "
                f"but control flow computes to {computed} cycles"
            )

    def _compute_region_latency(self, region) -> int | None:
        """Compute latency of a control region by summing operations.

        Returns None if region contains dynamic constructs.
        """
        if len(region.blocks) == 0:
            return 0

        total = 0
        for op in region.blocks[0]:
            lat = self._compute_op_latency(op)
            if lat is None:
                return None
            total += lat
        return total

    def _compute_op_latency(self, op) -> int | None:
        """Compute latency of a single control operation.

        Returns None if dynamic (unknown latency).
        """
        from circt.dialects import cmt2

        op_name = op.operation.name

        # ProcEnableOp: Look up step latency
        if op_name == "cmt2.proc.enable":
            step_name = op.stepName.value
            return self._lookup_step_latency(step_name)

        # ProcSeqOp: Sum of children
        if op_name == "cmt2.proc.seq":
            return self._compute_region_latency(op.body)

        # ProcParOp: Max of children
        if op_name == "cmt2.proc.par":
            max_lat = 0
            for child_op in op.body.blocks[0]:
                child_lat = self._compute_op_latency(child_op)
                if child_lat is None:
                    return None
                max_lat = max(max_lat, child_lat)
            return max_lat

        # ProcStaticRepeatOp: count * body_latency
        if op_name == "cmt2.proc.static_repeat":
            count = op.count.value
            if "body_latency" in op.attributes:
                return count * op.body_latency.value
            body_lat = self._compute_region_latency(op.body)
            if body_lat is None:
                return None
            return count * body_lat

        # ProcStaticIfOp: max of branches
        if op_name == "cmt2.proc.static_if":
            then_lat = 0
            else_lat = 0
            if "then_latency" in op.attributes:
                then_lat = op.then_latency.value
            else:
                computed = self._compute_region_latency(op.thenRegion)
                if computed is None:
                    return None
                then_lat = computed
            if len(op.elseRegion.blocks) == 0:
                else_lat = 0
            elif "else_latency" in op.attributes:
                else_lat = op.else_latency.value
            else:
                computed = self._compute_region_latency(op.elseRegion)
                if computed is None:
                    return None
                else_lat = computed
            return max(then_lat, else_lat)

        # Dynamic constructs - can't compute
        if op_name in ("cmt2.proc.if", "cmt2.proc.while"):
            return None

        # Other ops (control_end, etc.) - zero latency
        return 0

    def _lookup_step_latency(self, step_name: str) -> int | None:
        """Look up a step's latency by name.

        Returns None if step is dynamic (ProcStepOp) or not found.
        """
        # Search for the step in the module's body block
        # self._module._op.body is already a Block (not a Region)
        module_body_block = self._module._op.body
        for op in module_body_block:
            op_name = op.operation.name
            if op_name == "cmt2.proc.static_step":
                if op.sym_name.value == step_name:
                    return op.latency.value
            elif op_name == "cmt2.proc.step":
                if op.sym_name.value == step_name:
                    # Dynamic step - latency unknown
                    return None
        # Step not found - might be external, can't compute
        return None
