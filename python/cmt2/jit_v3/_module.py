#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Module context manager for JIT v3.

Provides a clean context manager for building modules with automatic
instance wrapping for attribute-based method access.

Example:
    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        count = m.instance(Reg.create(circuit, width), "count", clk=clk)
"""

from __future__ import annotations

from typing import Any

from ._method_ref import wrap_instance


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
    
    def clock(self) -> Any:
        """Create a clock input."""
        return self._module_builder.clock()
    
    def reset(self) -> Any:
        """Create a reset input."""
        return self._module_builder.reset()
    
    def input(self, name: str, type_: Any) -> Any:
        """Create an input port."""
        return self._module_builder.input(name, type_)
    
    def output(self, name: str, type_: Any) -> Any:
        """Create an output port."""
        return self._module_builder.output(name, type_)
    
    def instance(self, module_def: Any, name: str, **connections: Any) -> Any:
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
        inst = self._module_builder.instance(module_def, name, **connections)
        return wrap_instance(inst)
    
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
