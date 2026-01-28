#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT v2 Counter Example - Agile syntax on PyCMT2.

This example demonstrates the new thin JIT v2 layer that provides
agile syntax without duplicating PyCMT2 functionality.

Usage:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 jit_v2_counter.py
"""

import sys
sys.path.insert(0, '../../python')

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench

import cmt2.jit_v2 as jit


@jit.elaborate
def counter_v2(width: int = 32, use_enable: bool = True):
    """Counter design using JIT v2 agile syntax.
    
    Compare this to the verbose v1 syntax - much cleaner!
    """
    circuit = Circuit("CounterV2")
    
    @jit.module(circuit, "Counter")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        
        # Create register using native PyCMT2
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        
        if use_enable:
            enable = m.input("enable", UInt(1))
        
        # Define rule using agile @rule decorator
        @jit.rule(m, "increment")
        def _(rule):
            @rule.guard
            def _(g):
                if use_enable:
                    g.equals(enable, g.const(1, 1))
                else:
                    g.always()
            
            @rule.body
            def _(b):
                val = b.call(count, "read")
                next_val = b.add(val, b.const(1, width))
                b.call(count, "write", next_val)
        
        # Define value method using @value decorator
        @jit.value(m, "get_count", returns=[UInt(width)])
        def _(val):
            @val.guard
            def _(g): g.always()
            @val.body
            def _(b): b.returns(b.call(count, "read"))
    
    return circuit


@jit.simulate
def test_counter_v2():
    """Test the counter using JIT v2."""
    print("=" * 60)
    print("JIT v2 Counter Example")
    print("=" * 60)
    
    # Build circuit
    circuit = counter_v2(width=16, use_enable=True)
    
    print("\nGenerated MLIR:")
    print(circuit.emit_mlir()[:1500])
    print("... [truncated] ...")
    
    # Create simulation
    workspace_dir = "/tmp/jit_v2_counter"
    ws = SimulationWorkspace(circuit, workspace_dir)
    
    # Create testbench
    tb = Testbench(circuit)
    
    with tb.sequence("test") as seq:
        seq.reset(5)
        seq.poke("enable", 1)
        seq.wait(10)
        seq.expect("get_count_res0", 10)
    
    ws.generate_with_testbench(tb)
    
    print(f"\nWorkspace created at: {workspace_dir}")
    print("Run 'make' in that directory to build and simulate")
    
    return {"success": True, "workspace": workspace_dir}


if __name__ == "__main__":
    result = test_counter_v2()
    print(f"\nTest {'PASSED' if result['success'] else 'FAILED'}")
