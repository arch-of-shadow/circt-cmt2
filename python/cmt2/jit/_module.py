#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Module context manager for Cmt2 JIT.

Provides a clean context manager for building modules with automatic
instance wrapping for attribute-based method access.

Example:
    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk)
"""

from __future__ import annotations

from typing import Any

from ._method_ref import wrap_instance, wrap_interface


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
        return self._rule.guard()

    def __getattr__(self, name: str) -> Any:
        return getattr(self._rule, name)


class _ProcRuleCM:
    def __init__(self, module_builder: Any, name: str, *args: Any, **kwargs: Any):
        self._module_builder = module_builder
        self._name = name
        self._args = args
        self._kwargs = kwargs
        self._cm = None

    def __enter__(self) -> _ProcRuleAdapter:
        self._cm = self._module_builder.proc_rule(self._name, *self._args, **self._kwargs)
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
        name: str,
        interface_bindings: dict[str, Any] | None = None,
        **connections: Any,
    ) -> Any:
        """Create a module instance with automatic SignalRef wrapping.
        
        The returned instance is wrapped with SignalRef to enable
        attribute-based method access like:
            count.read, count.write(value), count.next = value
        
        Args:
            module_def: Module definition to instantiate
            name: Instance name
            **connections: Port connections
            
        Returns:
            Wrapped instance with SignalRef
        """
        inst = self._module_builder.instance(
            module_def, name, interface_bindings=interface_bindings, **connections
        )
        return wrap_instance(inst)

    def interface_decl(self, name: str, interface: Any) -> Any:
        """Declare an interface instance in this module."""
        decl = self._module_builder.interface_decl(name, interface)
        return wrap_interface(decl)

    def interface_def(self, name: str, interface: Any) -> Any:
        """Define an interface instance in this module."""
        return self._module_builder.interface_def(name, interface)

    def proc_rule(self, name: str, *args: Any, **kwargs: Any) -> _ProcRuleCM:
        """Define a procedural rule with JIT-style guard syntax."""
        return _ProcRuleCM(self._module_builder, name, *args, **kwargs)

    def __getattr__(self, name: str) -> Any:
        """Delegate unknown attributes to the underlying PyCMT2 ModuleBuilder."""
        if self._module_builder is None:
            raise AttributeError(name)
        return getattr(self._module_builder, name)

    @property
    def builder(self) -> Any:
        """Access the underlying PyCMT2 module builder."""
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
