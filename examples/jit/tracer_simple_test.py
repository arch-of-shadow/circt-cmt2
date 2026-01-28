#!/usr/bin/env python3
"""Simple test of CMT2 tracer with CIRCT bindings."""

import sys
sys.path.insert(0, '../../python')
sys.path.insert(0, '../../build/tools/circt/python_packages/circt_core')

import circt
from cmt2.ir import MLIRBuilder
from cmt2.types import UInt, SInt, Bits
from cmt2.jit import Cmt2Trace, SignalTracer

from circt.ir import InsertionPoint, Location, IntegerType, IntegerAttr, Module
from circt.dialects import hw, comb

print("Testing CMT2 Tracer with CIRCT bindings...")
print("=" * 60)

# Use the CIRCT bindings directly (similar to integration tests)
with circt.ir.Context() as ctx, Location.unknown():
    circt.register_dialects(ctx)
    
    i32 = IntegerType.get_signless(32)
    i1 = IntegerType.get_signless(1)
    
    m = Module.create()
    
    def build(module):
        # Create constants as inputs
        const_a = hw.ConstantOp(IntegerAttr.get(i32, 42))
        const_b = hw.ConstantOp(IntegerAttr.get(i32, 17))
        
        # Create trace with a mock builder
        class MockBuilder:
            def __init__(self, ctx):
                self._ctx = ctx
                self._module = m
        
        builder = MockBuilder(ctx)
        
        with Cmt2Trace(builder) as trace:
            # Wrap the constants in SignalTracers
            a = SignalTracer(const_a.result, UInt(32), trace)
            b = SignalTracer(const_b.result, UInt(32), trace)
            
            # Perform operations - these should emit MLIR
            print("Performing a + b...")
            sum_val = a + b
            print(f"  Result type: {sum_val.dtype}")
            
            print("Performing a - b...")
            diff = a - b
            print(f"  Result type: {diff.dtype}")
            
            print("Performing a * b...")
            prod = a * b
            print(f"  Result type: {prod.dtype}")
            
            print("Performing a & b...")
            bitwise_and = a & b
            print(f"  Result type: {bitwise_and.dtype}")
            
            print("Performing a == b...")
            is_eq = a == b
            print(f"  Result type: {is_eq.dtype}")
            
            print("Performing a < b...")
            is_lt = a < b
            print(f"  Result type: {is_lt.dtype}")
            
            print("Performing ~a...")
            inverted = ~a
            print(f"  Result type: {inverted.dtype}")
            
            print("Performing a >> 2...")
            shifted = a >> 2
            print(f"  Result type: {shifted.dtype}")
            
            print("Performing a << 1...")
            shifted_left = a << 1
            print(f"  Result type: {shifted_left.dtype}")
    
    with InsertionPoint(m.body):
        hw.HWModuleOp(name="test_tracer", body_builder=build)

print("\nGenerated MLIR:")
print("=" * 60)
print(m)
print("=" * 60)
print("Test completed successfully!")
