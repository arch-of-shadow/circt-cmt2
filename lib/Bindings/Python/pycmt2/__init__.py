#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
PyCMT2: High-level Python EDSL for the CMT2 dialect.

This module provides a Pythonic API for building CMT2 hardware designs
using context managers, strong typing, and object references.

Example:
    from pycmt2 import Circuit, UInt

    circuit = Circuit("Counter")

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

    print(circuit.emit_mlir())
"""

from .types import (
    Cmt2Type,
    UInt,
    SInt,
    ClockType,
    ResetType,
    AsyncResetType,
    Bundle,
    Vector,
    Clock,
    Reset,
    AsyncReset,
    Bool,
)

from .signals import Signal

from .circuit import Circuit

from .module import ModuleBuilder

from .refs import MethodRef, ValueRef, GroupRef, Instance

__all__ = [
    # Types
    "Cmt2Type",
    "UInt",
    "SInt",
    "ClockType",
    "ResetType",
    "AsyncResetType",
    "Bundle",
    "Vector",
    "Clock",
    "Reset",
    "AsyncReset",
    "Bool",
    # Signals
    "Signal",
    # Builders
    "Circuit",
    "ModuleBuilder",
    # References
    "MethodRef",
    "ValueRef",
    "GroupRef",
    "Instance",
]
