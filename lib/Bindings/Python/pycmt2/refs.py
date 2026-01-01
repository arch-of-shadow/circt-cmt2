#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Reference types for PyCMT2 EDSL.

These types provide object-oriented references to methods, values, and groups,
avoiding the use of string-based indexing which is error-prone.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .builders import MethodBuilder, ValueBuilder, StepBuilder
    from .module import ModuleBuilder
    from .signals import Signal
    from .types import Cmt2Type


class MethodRef:
    """Reference to a method for scheduling and calls.

    MethodRef provides a type-safe way to reference methods without using
    string names. It can be used for:
    - Calling methods on instances
    - Specifying scheduling directives (sequenceBefore, conflict, etc.)
    """

    __slots__ = ("_builder", "_instance", "_name")

    def __init__(
        self,
        builder: MethodBuilder | None = None,
        instance: Instance | None = None,
        name: str | None = None,
    ):
        self._builder = builder
        self._instance = instance  # None means @this (current module)
        self._name = name or (builder.name if builder else "")

    @property
    def name(self) -> str:
        """Get the method name."""
        return self._name

    @property
    def instance(self) -> Instance | None:
        """Get the instance this method belongs to, or None for @this."""
        return self._instance

    @property
    def builder(self) -> MethodBuilder | None:
        """Get the method builder, if available."""
        return self._builder

    def __repr__(self) -> str:
        if self._instance:
            return f"MethodRef(@{self._instance.name}::{self.name})"
        return f"MethodRef(@this::{self.name})"


class ValueRef:
    """Reference to a value method for scheduling and calls.

    Similar to MethodRef, but for value methods (read-only, no side effects).
    """

    __slots__ = ("_builder", "_instance", "_name")

    def __init__(
        self,
        builder: ValueBuilder | None = None,
        instance: Instance | None = None,
        name: str | None = None,
    ):
        self._builder = builder
        self._instance = instance
        self._name = name or (builder.name if builder else "")

    @property
    def name(self) -> str:
        """Get the value method name."""
        return self._name

    @property
    def instance(self) -> Instance | None:
        """Get the instance this value belongs to, or None for @this."""
        return self._instance

    @property
    def builder(self) -> ValueBuilder | None:
        """Get the value builder, if available."""
        return self._builder

    def __repr__(self) -> str:
        if self._instance:
            return f"ValueRef(@{self._instance.name}::{self.name})"
        return f"ValueRef(@this::{self.name})"


class StepRef:
    """Reference to a procedural step for control flow.

    StepRef provides a type-safe way to reference steps in procedural
    control flow (e.g., cmt2.proc.enable).
    """

    __slots__ = ("_builder", "_name")

    def __init__(
        self,
        builder: StepBuilder | None = None,
        name: str | None = None,
    ):
        self._builder = builder
        self._name = name or (builder.name if builder else "")

    @property
    def name(self) -> str:
        """Get the step name."""
        return self._name

    @property
    def builder(self) -> StepBuilder | None:
        """Get the step builder, if available."""
        return self._builder

    def __repr__(self) -> str:
        return f"StepRef(@{self.name})"


class RuleRef:
    """Reference to a rule for scheduling directives.

    RuleRef provides a type-safe way to reference rules (both regular rules
    and proc.rules) for scheduling directives like precedence.
    """

    __slots__ = ("_builder", "_name")

    def __init__(
        self,
        builder=None,  # RuleBuilder or ProcRuleBuilder
        name: str | None = None,
    ):
        self._builder = builder
        self._name = name or (builder.name if builder else "")

    @property
    def name(self) -> str:
        """Get the rule name."""
        return self._name

    @property
    def builder(self):
        """Get the rule builder, if available."""
        return self._builder

    def __repr__(self) -> str:
        return f"RuleRef(@{self.name})"


class Instance:
    """An instance of a module with typed method/value access.

    Instance provides attribute-style access to methods and values defined
    on the instantiated module.
    """

    __slots__ = ("_name", "_module", "_port_signals", "_op")

    def __init__(
        self,
        name: str,
        module: ModuleBuilder,
        port_signals: dict[str, Signal] | None = None,
        op=None,
    ):
        self._name = name
        self._module = module
        self._port_signals = port_signals or {}
        self._op = op

    @property
    def name(self) -> str:
        """Get the instance name."""
        return self._name

    @property
    def module(self) -> ModuleBuilder:
        """Get the module this is an instance of."""
        return self._module

    def method(self, name: str) -> MethodRef:
        """Get a reference to a method on this instance.

        Args:
            name: The method name.

        Returns:
            A MethodRef for calling or scheduling.

        Raises:
            KeyError: If the method doesn't exist on the module.
        """
        if name not in self._module._methods:
            raise KeyError(f"Module '{self._module.name}' has no method '{name}'")
        return MethodRef(self._module._methods[name], self, name)

    def value(self, name: str) -> ValueRef:
        """Get a reference to a value on this instance.

        Args:
            name: The value method name.

        Returns:
            A ValueRef for calling or scheduling.

        Raises:
            KeyError: If the value doesn't exist on the module.
        """
        if name not in self._module._values:
            raise KeyError(f"Module '{self._module.name}' has no value '{name}'")
        return ValueRef(self._module._values[name], self, name)

    def __getattr__(self, name: str) -> MethodRef | ValueRef:
        """Attribute-style access to methods and values.

        Allows `inst.read` instead of `inst.method("read")` or `inst.value("read")`.
        """
        if name.startswith("_"):
            raise AttributeError(name)
        if name in self._module._methods:
            return MethodRef(self._module._methods[name], self, name)
        if name in self._module._values:
            return ValueRef(self._module._values[name], self, name)
        raise AttributeError(
            f"'{self._module.name}' has no method or value '{name}'"
        )

    def __repr__(self) -> str:
        return f"Instance({self._name}: {self._module.name})"


class ExternalInstance:
    """An instance of an external module.

    Similar to Instance, but for external FIRRTL modules bound to CMT2.
    """

    __slots__ = ("_name", "_module_name", "_methods", "_values", "_op")

    def __init__(
        self,
        name: str,
        module_name: str,
        methods: dict[str, MethodRef],
        values: dict[str, ValueRef],
        op=None,
    ):
        self._name = name
        self._module_name = module_name
        self._methods = methods
        self._values = values
        self._op = op

    @property
    def name(self) -> str:
        """Get the instance name."""
        return self._name

    def method(self, name: str) -> MethodRef:
        """Get a reference to a method on this instance."""
        if name not in self._methods:
            raise KeyError(f"External module '{self._module_name}' has no method '{name}'")
        ref = self._methods[name]
        return MethodRef(ref.builder, self, name)

    def value(self, name: str) -> ValueRef:
        """Get a reference to a value on this instance."""
        if name not in self._values:
            raise KeyError(f"External module '{self._module_name}' has no value '{name}'")
        ref = self._values[name]
        return ValueRef(ref.builder, self, name)

    def __getattr__(self, name: str) -> MethodRef | ValueRef:
        """Attribute-style access to methods and values."""
        if name.startswith("_"):
            raise AttributeError(name)
        if name in self._methods:
            ref = self._methods[name]
            return MethodRef(ref.builder, self, name)
        if name in self._values:
            ref = self._values[name]
            return ValueRef(ref.builder, self, name)
        raise AttributeError(
            f"'{self._module_name}' has no method or value '{name}'"
        )

    def __repr__(self) -> str:
        return f"ExternalInstance({self._name}: {self._module_name})"
