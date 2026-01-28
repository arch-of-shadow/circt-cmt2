#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT v3 Counter Example - Clean API with clear guard/body separation.

This demonstrates the improved v3 API:
- Clear guard/body separation with `with r.guard:` and `with r.body:`
- No `def _:` boilerplate
- Auto-inferred rule names
- Attribute-based method calls (no strings!)

Usage:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 jit_v3_counter.py
"""

import sys
sys.path.insert(0, '../../python')

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg

import cmt2.jit_v3 as jit


@jit.elaborate
def counter_v3(width: int = 32, use_enable: bool = True):
    """Counter using JIT v3 clean API.
    
    Clear guard/body separation without boilerplate!
    """
    circuit = Circuit("CounterV3")
    
    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()
        
        # Create register - automatically wrapped with SignalRef
        count = m.instance(Reg.create(circuit, width), "count", clk=clk, rst=rst)
        
        # Optional enable signal
        if use_enable:
            enable = m.input("enable", UInt(1))
        
        # Rule name inferred from function: "increment"
        @jit.rule(m)
        def increment(r):
            with r.guard:
                if use_enable:
                    r.equals(enable, r.const(1, 1))
                else:
                    r.always()
            
            with r.body:
                count.next = count.read + 1  # Clean attribute access!
        
        # Another rule - name inferred as "decrement"
        @jit.rule(m)
        def decrement(r):
            with r.guard:
                r.equals(count.read, r.const(0, width))  # Stop at 0
            
            with r.body:
                count.next = count.read - 1
        
        # Value method - name inferred as "get_count"
        @jit.value(m, returns=[UInt(width)])
        def get_count(r):
            with r.guard:
                r.always()
            
            with r.body:
                r.returns(count.read)  # Attribute access!
    
    return circuit


@jit.elaborate
def fifo_v3(data_width: int = 32, depth: int = 8):
    """FIFO using JIT v3 - demonstrates more clean syntax."""
    from circt.pycmt2.stl import FIFO
    
    circuit = Circuit("FIFOV3")
    
    with jit.module(circuit, "FIFO") as m:
        clk = m.clock()
        rst = m.reset()
        
        # Create FIFO
        fifo = m.instance(FIFO.create(circuit, UInt(data_width), depth), "fifo", clk=clk, rst=rst)
        
        # Inputs
        data_in = m.input("data_in", UInt(data_width))
        
        # Enqueue rule
        @jit.rule(m)
        def do_enqueue(r):
            with r.guard:
                r.equals(fifo.notFull, r.const(1, 1))  # Attribute access!
            
            with r.body:
                fifo.enq(data_in)  # Direct method call!
        
        # Dequeue rule
        @jit.rule(m)
        def do_dequeue(r):
            with r.guard:
                r.equals(fifo.notEmpty, r.const(1, 1))
            
            with r.body:
                fifo.deq()  # Direct method call - no strings!
        
        # Status method with explicit name override
        @jit.value(m, name="get_count", returns=[UInt(32)])
        def status(r):
            with r.guard:
                r.always()
            
            with r.body:
                r.returns(fifo.count)  # Attribute access to count
    
    return circuit


def main():
    """Test JIT v3 examples."""
    print("=" * 60)
    print("JIT v3 Clean API Examples")
    print("=" * 60)
    print("\nKey improvements:")
    print("  ✓ Clear guard/body separation with with r.guard / with r.body")
    print("  ✓ No 'def _:' boilerplate")
    print("  ✓ No string method names")
    print("  ✓ Auto-inferred rule names")
    print("  ✓ Attribute access: count.read, count.next =")
    
    # Test counter
    print("\n" + "-" * 40)
    print("1. Counter Example")
    print("-" * 40)
    circuit = counter_v3(width=16, use_enable=True)
    print(f"Circuit: {circuit.name}")
    mlir = circuit.emit_mlir()
    print(f"MLIR size: {len(mlir)} chars")
    print("\nMLIR Preview:")
    print(mlir[:1500])
    
    # Test FIFO
    print("\n" + "-" * 40)
    print("2. FIFO Example")
    print("-" * 40)
    circuit = fifo_v3(data_width=32, depth=8)
    print(f"Circuit: {circuit.name}")
    print(f"MLIR size: {len(circuit.emit_mlir())} chars")
    
    print("\n" + "=" * 60)
    print("JIT v3 Examples Complete!")
    print("=" * 60)


if __name__ == "__main__":
    main()
