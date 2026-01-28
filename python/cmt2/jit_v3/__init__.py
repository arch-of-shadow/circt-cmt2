#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT v3: Clean API with clear guard/body separation and minimal boilerplate.

JIT v3 provides a Pythonic API for hardware design with:
- Clear guard/body separation using @r.guard and @r.body
- No `def _:` boilerplate
- Auto-inferred names from function definitions
- Attribute-based method access (no strings!)

Example:
    import cmt2.jit_v3 as jit
    from circt.pycmt2 import Circuit
    from circt.pycmt2.stl import Reg
    
    @jit.elaborate
    def counter(width: int = 32):
        circuit = Circuit("Counter")
        
        with jit.module(circuit, "Counter") as m:
            clk = m.clock()
            rst = m.reset()
            count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
            
            @jit.rule(m)  # Name: "increment"
            def increment(r):
                @r.guard
                r.always()
                
                @r.body
                count.next = count.read + 1
        
        return circuit
"""

from __future__ import annotations

from ._decorators import elaborate, simulate, rule, method, value
from ._module import module, ModuleContext
from ._method_ref import SignalRef, MethodRef, wrap_instance
from ._context import RuleContext, MethodContext, ValueContext

__all__ = [
    # Decorators
    "elaborate",
    "simulate",
    "rule",
    "method", 
    "value",
    # Module
    "module",
    "ModuleContext",
    # Method references
    "SignalRef",
    "MethodRef",
    "wrap_instance",
    # Contexts
    "RuleContext",
    "MethodContext",
    "ValueContext",
]
