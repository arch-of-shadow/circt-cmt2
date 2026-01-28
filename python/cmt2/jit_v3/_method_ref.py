#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Method reference system for zero-boilerplate method calls.

This module provides MethodRef and SignalRef classes that enable
attribute-based method access like `count.read` instead of 
`b.call(count, "read")`.

Example:
    count = m.instance(Reg.create(circuit, 32), "count", clk=clk, rst=rst)
    
    # Old way (string-based):
    b.call(count, "read")
    b.call(count, "write", value)
    
    # New way (attribute-based):
    count.read          # MethodRef for read
    count.write(value)  # Direct write call
    count.next = value  # Shortcut syntax
"""

from __future__ import annotations

from typing import Any, Callable


class MethodRef:
    """Reference to a method on a hardware instance.
    
    Enables attribute-style method access without strings.
    
    Example:
        # count is a Reg instance
        count.read  # Returns MethodRef for "read" method
        count.write  # Returns MethodRef for "write" method
        
        # Call the method
        value = count.read()  # Calls read method
        count.write(value)    # Calls write method
        
        # Shortcut for write
        count.next = value    # Same as count.write(value)
    """
    
    def __init__(self, instance: Any, method_name: str):
        """Initialize method reference.
        
        Args:
            instance: The hardware instance (e.g., Reg, FIFO)
            method_name: Name of the method (e.g., "read", "write")
        """
        self._instance = instance
        self._method_name = method_name
    
    def __call__(self, *args, **kwargs) -> Any:
        """Call the method with arguments.
        
        This is invoked when the MethodRef is called like a function.
        The actual call is deferred to the current builder context.
        
        Args:
            *args: Positional arguments for the method
            **kwargs: Keyword arguments for the method
            
        Returns:
            Method call result
        """
        # Get current builder from context
        builder = _get_current_builder()
        if builder is None:
            raise RuntimeError(
                f"Cannot call method '{self._method_name}' outside of "
                "guard/body context. Use within @jit.rule decorated function."
            )
        
        return builder.call(self._instance, self._method_name, *args, **kwargs)
    
    def __repr__(self) -> str:
        return f"MethodRef({self._instance.name}.{self._method_name})"


class SignalRef:
    """Reference to a hardware signal with method access.
    
    Wraps hardware instances (Reg, FIFO, etc.) to provide attribute-based
    method access and shortcut syntax.
    
    Example:
        count = SignalRef(reg_instance)
        
        # Method access
        val = count.read()      # Calls reg.read()
        count.write(val + 1)    # Calls reg.write(val + 1)
        
        # Shortcut syntax
        count.next = val + 1    # Same as count.write(val + 1)
    """
    
    def __init__(self, instance: Any):
        """Initialize signal reference.
        
        Args:
            instance: The underlying hardware instance
        """
        self._instance = instance
        self._method_cache: dict[str, MethodRef] = {}
    
    def __getattr__(self, name: str) -> Any:
        """Get attribute - returns MethodRef for method names.
        
        Args:
            name: Attribute name
            
        Returns:
            MethodRef if it's a known method, else the actual attribute
        """
        # Check if it's a method we should expose
        if name in self._get_method_names():
            if name not in self._method_cache:
                self._method_cache[name] = MethodRef(self._instance, name)
            return self._method_cache[name]
        
        # Otherwise return the actual attribute
        return getattr(self._instance, name)
    
    def __setattr__(self, name: str, value: Any) -> None:
        """Set attribute - enables `signal.next = value` syntax.
        
        Args:
            name: Attribute name
            value: Value to set
        """
        if name in ("_instance", "_method_cache"):
            # Private attributes
            super().__setattr__(name, value)
        elif name == "next":
            # Shortcut: count.next = value means count.write(value)
            self.write(value)
        else:
            # Regular attribute set
            setattr(self._instance, name, value)
    
    def _get_method_names(self) -> set[str]:
        """Get available method names for this instance type.
        
        Returns:
            Set of method names
        """
        # Common methods for STL components
        common_methods = {"read", "write", "enq", "deq", "notFull", "notEmpty", "isFull", "isEmpty", "count"}
        
        # Try to get instance-specific methods
        instance_methods = set()
        if hasattr(self._instance, "_methods"):
            instance_methods = set(self._instance._methods.keys())
        
        return common_methods | instance_methods
    
    def __repr__(self) -> str:
        return f"SignalRef({self._instance})"


class BuilderProxy:
    """Proxy that adds method reference capabilities to builders.
    
    Wraps guard/body builders to support attribute-based method calls.
    """
    
    def __init__(self, builder: Any):
        """Initialize proxy.
        
        Args:
            builder: The underlying PyCMT2 builder (guard or body)
        """
        self._builder = builder
    
    def __getattr__(self, name: str) -> Any:
        """Get attribute from underlying builder."""
        return getattr(self._builder, name)
    
    def call(self, instance: Any, method: str, *args) -> Any:
        """Call method on instance.
        
        Args:
            instance: Hardware instance
            method: Method name
            *args: Method arguments
            
        Returns:
            Call result
        """
        # If instance is a SignalRef, unwrap it
        if isinstance(instance, SignalRef):
            instance = instance._instance
        
        return self._builder.call(instance, method, *args)


# Thread-local storage for current builder
_current_builder: Any = None


def _get_current_builder() -> Any:
    """Get the current builder from context.
    
    Returns:
        Current builder or None if not in a rule context
    """
    return _current_builder


def _set_current_builder(builder: Any) -> None:
    """Set the current builder.
    
    Args:
        builder: Builder to set, or None to clear
    """
    global _current_builder
    _current_builder = builder


class BuilderContext:
    """Context manager for builder proxy.
    
    Usage:
        with BuilderContext(builder) as proxy:
            # Now method calls work
            count.read()
    """
    
    def __init__(self, builder: Any):
        self._builder = builder
        self._proxy = BuilderProxy(builder)
    
    def __enter__(self) -> BuilderProxy:
        _set_current_builder(self._builder)
        return self._proxy
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        _set_current_builder(None)
        return False


def wrap_instance(instance: Any) -> SignalRef:
    """Wrap a hardware instance with SignalRef for method access.
    
    Args:
        instance: Hardware instance (Reg, FIFO, etc.)
        
    Returns:
        SignalRef wrapping the instance
    """
    return SignalRef(instance)
