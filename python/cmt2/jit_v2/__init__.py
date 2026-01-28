#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 JIT v2 - Thin layer on top of PyCMT2.

This module provides agile syntax sugar on top of PyCMT2 without
duplicating functionality. All MLIR construction goes through PyCMT2.

Example:
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import Reg
    import cmt2.jit_v2 as jit
    
    @jit.elaborate
    def counter(width: int = 32):
        circuit = Circuit("Counter")
        
        @jit.module(circuit, "Counter")
        def build(m):
            clk = m.clock()
            rst = m.reset()
            count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
            
            @jit.rule(m, "increment")
            def _(r):
                @r.guard
                def _(g): g.always()
                @r.body
                def _(b): b.call(count, "write", b.call(count, "read") + 1)
        
        return circuit
"""

from __future__ import annotations

# Import decorators
from ._decorators import (
    elaborate,
    simulate,
    module,
    rule,
    method,
    value,
)

# Import dataflow decorators
from ._dataflow import (
    dataflow,
    task,
    forkjoin,
)

# Import utility functions
from ._utils import (
    static,
    enable_operator_overloading,
)

__all__ = [
    # Core decorators
    "elaborate",
    "simulate",
    "module",
    "rule",
    "method",
    "value",
    # Dataflow decorators
    "dataflow",
    "task",
    "forkjoin",
    # Utilities
    "static",
    "enable_operator_overloading",
]

__version__ = "2.0.0"
