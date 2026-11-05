#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Cmt2 JIT: Clean API with clear guard/body separation and minimal boilerplate.

This JIT provides a Pythonic API for hardware design with:
- Clear guard/body separation using `with r.guard:` and `with r.body:`
- No `def _:` boilerplate
- Auto-inferred names from function definitions
- Attribute-based method access (no strings!)

Example:
    import cmt2.jit as jit
    from circt.pycmt2 import Circuit
    from circt.pycmt2.stl import Reg
    
    @jit.elaborate
    def counter(width: int = 32):
        circuit = Circuit("Counter")
        
        with jit.module(circuit, "Counter") as m:
            clk = m.clock()
            rst = m.reset()
            count = m.instance(Reg.create(circuit, width), clk=clk, rst=rst)
            
            @jit.rule(m)  # Name: "increment"
            def increment(r):
                with r.guard:
                    r.always()

                with r.body:
                    count.next = count.read + 1
        
        return circuit
"""

from __future__ import annotations

from ._decorators import Elaborated, elaborate, handles, method, rule, value
from ._dataflow import dataflow, DataflowContext
from ._external_module import external_module
from ._module import module, ModuleContext
from ._method_ref import SignalRef, MethodRef, InterfaceRef, wrap_instance, wrap_interface
from ._context import RuleContext, MethodContext, ValueContext

__all__ = [
    # Decorators
    "elaborate",
    "Elaborated",
    "handles",
    "rule",
    "method", 
    "value",
    "dataflow",
    # External modules
    "external_module",
    # Module
    "module",
    "ModuleContext",
    # Dataflow
    "DataflowContext",
    # Method references
    "SignalRef",
    "MethodRef",
    "InterfaceRef",
    "wrap_instance",
    "wrap_interface",
    # Contexts
    "RuleContext",
    "MethodContext",
    "ValueContext",
]

# Optional: STL re-export (requires PyCMT2/CIRCT bindings).
try:  # pragma: no cover
    from . import stl as stl

    __all__.append("stl")
except ImportError:
    pass
