#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Method reference system for zero-boilerplate method calls.

This module provides MethodRef and SignalRef classes that enable
attribute-based method access like `count.read` instead of 
`b.call(count, "read")`.

Example:
    count = m.instance(Reg.create(circuit, 32), clk=clk, rst=rst)
    value = count.read         # property-like read (no parentheses)
    count.write(value)         # callable method
    count.next = value         # shortcut for write
"""

from __future__ import annotations

import contextvars
from typing import Any


class MethodRef:
    """Reference to a method on a hardware instance.
    
    Enables attribute-style method access without strings.
    
    Example:
        # count is a Reg instance
        count.read  # Returns a signal (property-like read)
        count.write  # Returns MethodRef for "write" method
        
        # Call the method
        value = count.read     # Reads current value
        count.write(value)     # Calls write method
        
        # Shortcut for write
        count.next = value    # Same as count.write(value)
    """
    
    def __init__(self, instance: Any, method_name: str, *, _py_ref: Any | None = None):
        """Initialize method reference.
        
        Args:
            instance: The hardware instance (e.g., Reg, FIFO)
            method_name: Name of the method (e.g., "read", "write")
        """
        self._instance = instance
        self._method_name = method_name
        self._py_ref = _py_ref

    @property
    def name(self) -> str:
        if self._py_ref is not None:
            return getattr(self._py_ref, "name", self._method_name)
        return self._method_name

    @property
    def builder(self) -> Any:
        """Expose PyCMT2 method/value builder when available.

        This is required for APIs like `proc.invoke()` which query return types
        from `method.builder`.
        """
        if self._py_ref is None:
            return None
        return getattr(self._py_ref, "builder", None)
    
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
        builder = _get_current_builder()
        if builder is None:
            raise RuntimeError(
                f"Cannot call method '{self._method_name}' outside of "
                "guard/body context. Use within `with r.guard:` / `with r.body:`."
            )
        
        # Call by name to reuse PyCMT2's signature lookup on the instance module.
        #
        # (Passing the raw PyCMT2 MethodRef here currently tickles a PyCMT2
        # internal `_arg_types` shape mismatch in RegionBuilder.call().)
        return builder.call(self._instance, self._method_name, *args, **kwargs)
    
    def __repr__(self) -> str:
        inst_name = getattr(self._instance, "name", None)
        if inst_name is None:
            inst_name = getattr(self._instance, "_name", None)
        if inst_name is None:
            inst_name = type(self._instance).__name__
        return f"MethodRef({inst_name}.{self.name})"


class SignalRef:
    """Reference to a hardware signal with method access.
    
    Wraps hardware instances (Reg, FIFO, etc.) to provide attribute-based
    method access and shortcut syntax.
    
    Example:
        count = SignalRef(reg_instance)
        
        # Method access
        val = count.read        # Property-like read (no parentheses)
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

    @property
    def instance(self) -> Any:
        """Return the underlying PyCMT2 Instance.

        This is useful in advanced contexts where you need to pass the raw
        instance into low-level PyCMT2 APIs (e.g. condition-region builders
        that call `RegionBuilder.call()` directly).
        """
        return self._instance

    @property
    def _ext_module(self) -> Any:  # noqa: N802 (match PyCMT2 naming)
        """Expose module information for PyCMT2 builder lookups.

        PyCMT2's RegionBuilder.call() treats non-`Instance` targets as "external"
        and consults `target._ext_module` to recover method/value signatures.

        For many CMT2 instances, the signature source lives on `instance._module`
        (a ModuleBuilder). Exposing that here keeps `b.call(signal_ref, "...")`
        working without forcing users to unwrap the instance.
        """
        ext = getattr(self._instance, "_ext_module", None)
        if ext is not None:
            return ext
        return getattr(self._instance, "_module", None)

    def __getattr__(self, name: str) -> Any:
        """Get attribute - returns MethodRef for method names.
        
        Args:
            name: Attribute name
            
        Returns:
            MethodRef if it's a known method, else the actual attribute
        """
        if name in _PROPERTY_METHODS and not self._method_has_args(name):
            builder = _get_current_builder()
            if builder is None:
                raise RuntimeError(
                    f"Cannot read '{name}' outside of guard/body context. "
                    "Use within `with r.guard:` / `with r.body:`."
                )
            return builder.call(self._instance, name)

        # Treat module-defined values as property-like reads.
        inst_module = getattr(self._instance, "_module", None)
        if inst_module is not None and hasattr(inst_module, "_values") and name in inst_module._values:
            builder = _get_current_builder()
            if builder is None:
                raise RuntimeError(
                    f"Cannot read '{name}' outside of guard/body context. "
                    "Use within `with r.guard:` / `with r.body:`."
                )
            return builder.call(self._instance, name)

        # Treat external-module values with no args as property-like reads.
        ext = getattr(self._instance, "_ext_module", None)
        if ext is not None and getattr(ext, "get_value_arg_types", None) is not None:
            pending_values = getattr(ext, "_pending_values", None)
            has_value = isinstance(pending_values, list) and any(
                getattr(v, "get", None) is not None and v.get("name") == name for v in pending_values
            )
            if has_value:
                v_args = ext.get_value_arg_types(name)
                if len(v_args) == 0:
                    builder = _get_current_builder()
                    if builder is None:
                        raise RuntimeError(
                            f"Cannot read '{name}' outside of guard/body context. "
                            "Use within `with r.guard:` / `with r.body:`."
                        )
                    return builder.call(self._instance, name)

                # Value exists and takes args: treat as callable.
                if name not in self._method_cache:
                    self._method_cache[name] = MethodRef(self._instance, name, _py_ref=None)
                return self._method_cache[name]

        # Treat external-module methods as callables, even if not in the common set.
        if ext is not None and getattr(ext, "get_method_arg_types", None) is not None:
            pending_methods = getattr(ext, "_pending_methods", None)
            has_method = isinstance(pending_methods, list) and any(
                getattr(m, "get", None) is not None and m.get("name") == name for m in pending_methods
            )
            if has_method:
                if name not in self._method_cache:
                    self._method_cache[name] = MethodRef(self._instance, name, _py_ref=None)
                return self._method_cache[name]

        if name in self._get_method_names():
            if name in self._method_cache:
                return self._method_cache[name]
            try:
                py_ref = getattr(self._instance, name)
            except AttributeError:
                py_ref = None
            self._method_cache[name] = MethodRef(self._instance, name, _py_ref=py_ref)
            return self._method_cache[name]
        
        # Otherwise return the actual attribute
        return getattr(self._instance, name)

    def _method_has_args(self, name: str) -> bool:
        """Best-effort check: does `name` require arguments?"""
        ext = getattr(self._instance, "_ext_module", None)
        if ext is not None:
            try:
                if getattr(ext, "get_value_arg_types", None) is not None:
                    if ext.get_value_arg_types(name):
                        return True
                if getattr(ext, "get_method_arg_types", None) is not None:
                    if ext.get_method_arg_types(name):
                        return True
            except Exception:
                pass

        inst_module = getattr(self._instance, "_module", None)
        if inst_module is not None:
            try:
                if hasattr(inst_module, "_methods") and name in inst_module._methods:
                    meth_builder = inst_module._methods[name]
                    arg_types = getattr(meth_builder, "_arg_types", [])
                    return bool(arg_types)
            except Exception:
                pass

        return False
    
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
        common_methods = set(_COMMON_METHODS)
        
        # Try to get instance-specific methods
        instance_methods = set()
        if hasattr(self._instance, "_methods"):
            instance_methods |= set(self._instance._methods.keys())

        # Instances created by PyCMT2 store signatures on `instance._module`.
        inst_module = getattr(self._instance, "_module", None)
        if inst_module is not None:
            if hasattr(inst_module, "_methods"):
                instance_methods |= set(inst_module._methods.keys())
            if hasattr(inst_module, "_values"):
                instance_methods |= set(inst_module._values.keys())

        return common_methods | instance_methods
    
    def __repr__(self) -> str:
        return f"SignalRef({self._instance})"


class InterfaceRef:
    """Reference wrapper for an `InterfaceDecl` (interface instance in a module).

    - Interface values are treated as properties: `reader.getData`
    - Interface methods are callable: `writer.store(x)`
    """

    def __init__(self, decl: Any):
        self._decl = decl
        self._method_cache: dict[str, MethodRef] = {}

    @property
    def decl(self) -> Any:
        """Return the underlying PyCMT2 `InterfaceDecl`.

        Use this when you need to pass the decl into low-level PyCMT2 APIs such
        as instance interface bindings or the testbench interface helpers.
        """
        return self._decl

    @property
    def name(self) -> str:
        return getattr(self._decl, "name", getattr(self._decl, "_name", "<iface>"))

    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            raise AttributeError(name)

        iface = getattr(self._decl, "interface", None)
        if iface is not None and hasattr(iface, "get_function"):
            func = iface.get_function(name)
            if func is not None:
                # Interface values are property-like (no parentheses).
                if name in getattr(iface, "_values", {}):
                    builder = _get_current_builder()
                    if builder is None:
                        raise RuntimeError(
                            f"Cannot read '{name}' outside of guard/body context. "
                            "Use within `with r.guard:` / `with r.body:`."
                        )
                    return builder.call(self._decl, name)

                # Interface methods are callable.
                if name in getattr(iface, "_methods", {}):
                    if name not in self._method_cache:
                        self._method_cache[name] = MethodRef(self._decl, name)
                    return self._method_cache[name]

        return getattr(self._decl, name)

    def __repr__(self) -> str:
        return f"InterfaceRef({self.name})"


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
    
    def call(self, instance: Any, method: Any, *args, **kwargs) -> Any:
        """Call method on instance.

        Args:
            instance: Hardware instance
            method: Method name
            *args: Method arguments
            **kwargs: Forwarded to underlying builder (e.g. arg_timing/result_timing)
            
        Returns:
            Call result
        """
        # If instance is a SignalRef, unwrap it
        if isinstance(instance, SignalRef):
            instance = instance._instance
        
        return self._builder.call(instance, method, *args, **kwargs)


_CURRENT_BUILDER: contextvars.ContextVar[BuilderProxy | None] = contextvars.ContextVar(
    "cmt2_jit_current_builder", default=None
)


def _get_current_builder() -> Any:
    """Get the current builder from context.
    
    Returns:
        Current builder or None if not in a rule context
    """
    return _CURRENT_BUILDER.get()


def _push_current_builder(builder: Any) -> contextvars.Token[BuilderProxy | None]:
    """Push a builder into the active JIT context."""
    return _CURRENT_BUILDER.set(BuilderProxy(builder))


class BuilderContext:
    """Context manager for builder proxy.
    
    Usage:
        with BuilderContext(builder) as proxy:
            # Now method calls work
            count.read()
    """
    
    def __init__(self, builder: Any):
        self._builder = builder

    def __enter__(self) -> BuilderProxy:
        self._token = _push_current_builder(self._builder)
        return _get_current_builder()

    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        _CURRENT_BUILDER.reset(self._token)
        return False


def wrap_instance(instance: Any) -> SignalRef:
    """Wrap a hardware instance with SignalRef for method access.
    
    Args:
        instance: Hardware instance (Reg, FIFO, etc.)
        
    Returns:
        SignalRef wrapping the instance
    """
    return SignalRef(instance)


def wrap_interface(decl: Any) -> InterfaceRef:
    return InterfaceRef(decl)


_COMMON_METHODS: frozenset[str] = frozenset(
    {
        "read",
        "write",
        "enq",
        "deq",
        "full",
        "empty",
        "notFull",
        "notEmpty",
        "isFull",
        "isEmpty",
        "count",
    }
)

# Methods that are treated as read-only "properties" (no parentheses) in JIT.
_PROPERTY_METHODS: frozenset[str] = frozenset(
    {
        "read",
        "full",
        "empty",
        "notFull",
        "notEmpty",
        "isFull",
        "isEmpty",
        "count",
    }
)
