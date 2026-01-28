#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT v3 Counter Example - Zero-Boilerplate API.

This demonstrates the new clean API with:
- Auto-inferred rule names
- Attribute-based method calls (no strings!)
- Minimal boilerplate

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
    """Counter using JIT v3 zero-boilerplate API.
    
    Compare to v1 and v2 - this is much cleaner!
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
        def increment(guard, body):
            # Guard region
            if use_enable:
                guard.equals(enable, body.const(1, 1))
            else:
                guard.always()
            
            # Body region - CLEAN ATTRIBUTE ACCESS!
            # Instead of: b.call(count, "read") and b.call(count, "write", value)
            count.next = count.read + 1  # count.next automatically calls write()
        
        # Another rule - name inferred as "decrement"
        @jit.rule(m)
        def decrement(guard, body):
            guard.equals(body.call(count, "isZero"), body.const(0, 1))  # Can still use explicit
            count.next = count.read - 1
        
        # Value method - name inferred as "get_count"
        @jit.value(m, returns=[UInt(width)])
        def get_count(guard, body):
            guard.always()
            body.returns(count.read)  # Attribute access!
    
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
        
        # Enqueue rule
        @jit.rule(m)
        def do_enqueue(guard, body):
            # Use fifo.notFull() - attribute access!
            guard.equals(fifo.notFull, body.const(1, 1))
            data_in = m.input("data_in", UInt(data_width))
            fifo.enq(data_in)  # Direct method call!
        
        # Dequeue rule
        @jit.rule(m)
        def do_dequeue(guard, body):
            guard.equals(fifo.notEmpty, body.const(1, 1))
            fifo.deq()  # Direct method call - no strings!
        
        # Status method
        @jit.value(m, name="get_count", returns=[UInt(32)])  # Explicit name override
        def status(guard, body):
            guard.always()
            body.returns(fifo.count)  # Attribute access to count method
    
    return circuit


def main():
    """Test JIT v3 examples."""
    print("=" * 60)
    print("JIT v3 Zero-Boilerplate Examples")
    print("=" * 60)
    
    # Test counter
    print("\n1. Counter Example")
    print("-" * 40)
    circuit = counter_v3(width=16, use_enable=True)
    print(f"Circuit: {circuit.name}")
    mlir = circuit.emit_mlir()
    print(f"MLIR size: {len(mlir)} chars")
    print("\nMLIR Preview:")
    print(mlir[:1500])
    
    # Test FIFO
    print("\n\n2. FIFO Example")
    print("-" * 40)
    circuit = fifo_v3(data_width=32, depth=8)
    print(f"Circuit: {circuit.name}")
    print(f"MLIR size: {len(circuit.emit_mlir())} chars")
    
    print("\n" + "=" * 60)
    print("JIT v3 Examples Complete!")
    print("=" * 60)
    print("\nKey improvements:")
    print("  ✓ No 'def _' boilerplate")
    print("  ✓ No string method names")
    print("  ✓ Auto-inferred rule names")
    print("  ✓ Attribute access: count.read, count.write(), count.next =")
    print("  ✓ Direct method calls: fifo.enq(), fifo.deq()")


if __name__ == "__main__":
    main()
