#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Module context manager for Cmt2 JIT.

Provides a clean context manager for building modules with automatic
instance wrapping for attribute-based method access.

Example:
    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        count = m.instance(Reg.create(circuit, width), clk=clk)
"""

from __future__ import annotations

from typing import Any

from ._method_ref import BuilderContext, wrap_instance, wrap_interface


def _infer_assignment_name(*, depth: int) -> str | None:
    try:
        from circt.pycmt2.circuit import _get_assignment_target
    except Exception:
        _get_assignment_target = None

    if _get_assignment_target is None:
        return None
    return _get_assignment_target(depth=depth)


class _ProcRuleAdapter:
    """Adapter that makes `proc_rule` guard syntax match JIT style.

    PyCMT2 proc rules use `with rule.guard() as g:`. The JIT E2E suite
    standardizes on `with rule.guard as g:` (property-style), so we wrap the
    proc rule builder to expose a `.guard` context manager property while
    delegating everything else.
    """

    def __init__(self, rule: Any):
        self._rule = rule

    @property
    def guard(self):
        return _BuilderContextCM(self._rule.guard())

    def control(self):
        return _BuilderContextCM(self._rule.control())

    def __getattr__(self, name: str) -> Any:
        return getattr(self._rule, name)


class _BuilderContextCM:
    """Wrap a PyCMT2 builder context manager with JIT builder context."""

    def __init__(self, cm: Any):
        self._cm = cm
        self._builder_ctx: BuilderContext | None = None

    def __enter__(self) -> Any:
        builder = self._cm.__enter__()
        self._builder_ctx = BuilderContext(builder)
        self._builder_ctx.__enter__()
        return builder

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self._builder_ctx is not None:
            try:
                self._builder_ctx.__exit__(exc_type, exc_val, exc_tb)
            finally:
                self._builder_ctx = None
        return self._cm.__exit__(exc_type, exc_val, exc_tb)


class _NamedBuilderContextCM:
    """Like `_BuilderContextCM`, but infers the symbol name on `__enter__`.

    This avoids relying on PyCMT2's internal naming JIT (which assumes direct
    use of PyCMT2 builders, without the extra wrapper stack frames).
    """

    def __init__(self, cm_factory: Any, *, alias: str | None = None):
        self._cm_factory = cm_factory
        self._alias = alias
        self._cm: Any | None = None
        self._builder_ctx: BuilderContext | None = None

    def __enter__(self) -> Any:
        name = self._alias or _infer_assignment_name(depth=3)
        self._cm = self._cm_factory(name)
        builder = self._cm.__enter__()
        self._builder_ctx = BuilderContext(builder)
        self._builder_ctx.__enter__()
        return builder

    def __exit__(self, exc_type, exc_val, exc_tb):
        assert self._cm is not None
        if self._builder_ctx is not None:
            try:
                self._builder_ctx.__exit__(exc_type, exc_val, exc_tb)
            finally:
                self._builder_ctx = None
        return self._cm.__exit__(exc_type, exc_val, exc_tb)


class _ProcRuleCM:
    def __init__(self, module_builder: Any, name: str | None, *args: Any, **kwargs: Any):
        self._module_builder = module_builder
        self._name = name
        self._args = args
        self._kwargs = kwargs
        self._cm = None

    def __enter__(self) -> _ProcRuleAdapter:
        name = self._name or _infer_assignment_name(depth=3)
        self._cm = self._module_builder.proc_rule(name, *self._args, **self._kwargs)
        rule = self._cm.__enter__()
        return _ProcRuleAdapter(rule)

    def __exit__(self, exc_type, exc_val, exc_tb):
        assert self._cm is not None
        return self._cm.__exit__(exc_type, exc_val, exc_tb)


class ModuleContext:
    """Context manager for building a module.
    
    Wraps PyCMT2's module builder and automatically wraps instances
    with SignalRef for attribute-based method access.
    
    Example:
        with ModuleContext(circuit, "Counter") as m:
            clk = m.clock()
            count = m.instance(Reg.create(circuit, width), "count", clk=clk)
            # count is automatically wrapped - can use count.read, count.write()
    """
    
    def __init__(self, circuit: Any, name: str):
        self._circuit = circuit
        self._name = name
        self._module_builder: Any = None
    
    def __enter__(self) -> "ModuleContext":
        self._cm = self._circuit.module(self._name)
        self._module_builder = self._cm.__enter__()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        return self._cm.__exit__(exc_type, exc_val, exc_tb)
    
    def clock(self, name: str = "clk") -> Any:
        """Create a clock input port."""
        return self._module_builder.clock(name)
    
    def reset(self, name: str = "rst") -> Any:
        """Create a reset input port."""
        return self._module_builder.reset(name)
    
    def input(self, name: str, type_: Any) -> Any:
        """Create an input port."""
        return self._module_builder.input(name, type_)
    
    def output(self, name: str, type_: Any) -> Any:
        """Create an output port."""
        return self._module_builder.output(name, type_)
    
    def instance(
        self,
        module_def: Any,
        name: str | None = None,
        interface_bindings: dict[str, Any] | None = None,
        *,
        alias: str | None = None,
        **connections: Any,
    ) -> Any:
        """Create a module instance with automatic SignalRef wrapping.
        
        The returned instance is wrapped with SignalRef to enable
        attribute-based method access like:
            count.read, count.write(value), count.next = value
        
        Args:
            module_def: Module definition to instantiate
            name: Legacy positional name (avoid; use inference/alias).
            **connections: Port connections
            
        Returns:
            Wrapped instance with SignalRef
        """
        if name is not None:
            raise TypeError(
                "JIT instance naming is inferred from the assignment target. "
                "Use `alias=...` only when you need an explicit name."
            )

        # One extra frame vs PyCMT2: user -> ModuleContext.instance -> _infer_assignment_name.
        inst_name = alias or _infer_assignment_name(depth=3)
        if not inst_name:
            raise TypeError("Cannot infer instance name; use `alias=...`")

        inst = self._module_builder.instance(
            module_def, inst_name, interface_bindings=interface_bindings, **connections
        )
        return wrap_instance(inst)

    def interface_decl(self, interface: Any, *, alias: str | None = None) -> Any:
        """Declare an interface instance in this module."""
        if isinstance(interface, str):
            raise TypeError("JIT interface decls must reference an InterfaceBuilder, not a string name")

        decl_name = alias or _infer_assignment_name(depth=3)
        if not decl_name:
            raise TypeError("Cannot infer interface decl name; use `alias=...`")

        decl = self._module_builder.interface_decl(decl_name, interface)
        return wrap_interface(decl)

    def interface_def(self, interface: Any, *, alias: str | None = None) -> Any:
        """Define an interface instance in this module."""
        if isinstance(interface, str):
            raise TypeError("JIT interface defs must reference an InterfaceBuilder, not a string name")

        def_name = alias or _infer_assignment_name(depth=3)
        if not def_name:
            raise TypeError("Cannot infer interface def name; use `alias=...`")

        return self._module_builder.interface_def(def_name, interface)

    def proc_rule(self, name: str | None = None, *args: Any, alias: str | None = None, **kwargs: Any) -> _ProcRuleCM:
        """Define a procedural rule with JIT-style guard syntax."""
        if name is not None:
            raise TypeError(
                "JIT proc_rule naming is inferred from the `as <name>` target. "
                "Use `alias=...` only when you need an explicit name."
            )
        return _ProcRuleCM(self._module_builder, alias, *args, **kwargs)

    def step(self, name: str | None = None, *args: Any, alias: str | None = None, **kwargs: Any) -> Any:
        """Define a procedural step with JIT builder context (no strings)."""
        if name is not None:
            raise TypeError(
                "JIT step naming is inferred from the `as <name>` target. "
                "Use `alias=...` only when you need an explicit name."
            )
        return _NamedBuilderContextCM(lambda n: self._module_builder.step(n, *args, **kwargs), alias=alias)

    def static_step(
        self,
        latency: int,
        name: str | None = None,
        *args: Any,
        interval: int | None = None,
        alias: str | None = None,
        **kwargs: Any,
    ) -> Any:
        """Define a static-latency step with JIT builder context (no strings)."""
        if name is not None:
            raise TypeError(
                "JIT static_step naming is inferred from the `as <name>` target. "
                "Use `alias=...` only when you need an explicit name."
            )
        return _NamedBuilderContextCM(
            lambda n: self._module_builder.static_step(latency, n, *args, interval=interval, **kwargs),
            alias=alias,
        )

    def __getattr__(self, name: str) -> Any:
        """Delegate unknown attributes to the underlying PyCMT2 ModuleBuilder."""
        if self._module_builder is None:
            raise AttributeError(name)
        return getattr(self._module_builder, name)

    @property
    def builder(self) -> Any:
        """Access the underlying PyCMT2 module builder."""
        return self._module_builder

    @property
    def module_def(self) -> Any:
        """Return a module definition handle suitable for `m.instance(...)`.

        For in-circuit modules created via `with jit.module(circuit, "...")`,
        PyCMT2 expects the corresponding `ModuleBuilder` object when
        instantiating. Exposing it here avoids reaching into `circuit._modules`.
        """
        if self._module_builder is None:
            raise RuntimeError("ModuleContext.module_def is only available inside/after module elaboration")
        return self._module_builder


def module(circuit: Any, name: str) -> ModuleContext:
    """Create a module context manager.
    
    This is a factory function that creates a ModuleContext.
    
    Example:
        with jit.module(circuit, "Counter") as m:
            clk = m.clock()
            count = m.instance(Reg.create(circuit, width), "count", clk=clk)
    
    Args:
        circuit: PyCMT2 Circuit object
        name: Module name
        
    Returns:
        ModuleContext instance
    """
    return ModuleContext(circuit, name)
