#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 JIT v3 - Zero-boilerplate API for hardware design.

JIT v3 provides a clean, Pythonic API for CMT2 hardware design with:
- Auto-inferred names from function definitions
- Attribute-based method calls (no strings!)
- Minimal boilerplate
- Full PyCMT2 integration

Example:
    import cmt2.jit_v3 as jit
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import Reg
    
    @jit.elaborate
    def counter(width: int = 32):
        circuit = Circuit("Counter")
        
        with jit.module(circuit, "Counter") as m:
            clk = m.clock()
            rst = m.reset()
            count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
            
            # Rule name inferred from function: "increment"
            @jit.rule(m)
            def increment(guard, body):
                guard.always()
                count.next = count.read + 1  # Clean attribute access!
        
        return circuit
"""

from __future__ import annotations

# Core decorators
from ._ast_decorators import (
    elaborate,
    simulate,
    rule,
    method,
    value,
)

# Module context
from ._module import (
    module,
    ModuleContext,
)

# Method reference system
from ._method_ref import (
    MethodRef,
    SignalRef,
    wrap_instance,
)

__all__ = [
    # Core decorators
    "elaborate",
    "simulate",
    "rule",
    "method",
    "value",
    # Module
    "module",
    "ModuleContext",
    # Method references
    "MethodRef",
    "SignalRef",
    "wrap_instance",
]

__version__ = "3.0.0"
