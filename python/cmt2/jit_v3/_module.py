#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Module context manager for clean syntax.

Provides a context manager for module definition that enables
automatic wrapping of instances with SignalRef.
"""

from __future__ import annotations

from typing import Any, Iterator
from contextlib import contextmanager

from ._method_ref import wrap_instance


class ModuleContext:
    """Context for building a module with automatic SignalRef wrapping.
    
    This wraps a PyCMT2 ModuleBuilder and automatically wraps instances
    with SignalRef when created.
    
    Example:
        with jit.module(circuit, "Counter") as m:
            count = m.instance(Reg.create(circuit, 32), "count", ...)
            # count is automatically wrapped as SignalRef
            count.next = count.read + 1  # Clean syntax!
    """
    
    def __init__(self, circuit: Any, name: str):
        """Initialize module context.
        
        Args:
            circuit: PyCMT2 Circuit
            name: Module name
        """
        self._circuit = circuit
        self._name = name
        self._module_builder = None
    
    def __enter__(self) -> "ModuleContext":
        """Enter the module context.
        
        Returns:
            Self for attribute access
        """
        self._context = self._circuit.module(self._name)
        self._module_builder = self._context.__enter__()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        """Exit the module context."""
        return self._context.__exit__(exc_type, exc_val, exc_tb)
    
    def __getattr__(self, name: str) -> Any:
        """Get attribute from underlying module builder."""
        return getattr(self._module_builder, name)
    
    def instance(self, module: Any, name: str, **kwargs) -> Any:
        """Create an instance and wrap it with SignalRef.
        
        Args:
            module: Module to instantiate
            name: Instance name
            **kwargs: Additional arguments (clk, rst, etc.)
            
        Returns:
            SignalRef wrapping the instance
        """
        raw_instance = self._module_builder.instance(module, name, **kwargs)
        return wrap_instance(raw_instance)
    
    def clock(self, name: str = "clk") -> Any:
        """Create a clock port."""
        return self._module_builder.clock(name)
    
    def reset(self, name: str = "rst") -> Any:
        """Create a reset port."""
        return self._module_builder.reset(name)
    
    def input(self, name: str, dtype: Any) -> Any:
        """Create an input port."""
        return self._module_builder.input(name, dtype)
    
    def output(self, name: str, dtype: Any) -> Any:
        """Create an output port."""
        return self._module_builder.output(name, dtype)


@contextmanager
def module(circuit: Any, name: str) -> Iterator[ModuleContext]:
    """Context manager for module definition.
    
    Example:
        with jit.module(circuit, "Counter") as m:
            clk = m.clock()
            rst = m.reset()
            count = m.instance(Reg.create(circuit, 32), "count", clk=clk, rst=rst)
    
    Args:
        circuit: PyCMT2 Circuit
        name: Module name
        
    Yields:
        ModuleContext for building the module
    """
    ctx = ModuleContext(circuit, name)
    try:
        yield ctx.__enter__()
    finally:
        ctx.__exit__(None, None, None)
