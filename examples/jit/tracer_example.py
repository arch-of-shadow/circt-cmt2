#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Example demonstrating CMT2 JIT Tracer usage.

This example shows how to use the SignalTracer and Cmt2Trace classes
to trace Python operations and generate MLIR.

Example:
    # Without CIRCT bindings (demonstrates API)
    python tracer_example.py --dry-run
    
    # With CIRCT bindings (generates actual MLIR)
    python tracer_example.py
"""

import sys
import argparse

# Add python directory to path
sys.path.insert(0, '../../python')

from cmt2.types import UInt, SInt, Bits
from cmt2.jit import Cmt2Trace, SignalTracer, OperationType, get_primitive_registry


def example_basic_arithmetic():
    """Example: Basic arithmetic operations."""
    print("=" * 60)
    print("Example: Basic Arithmetic")
    print("=" * 60)
    
    try:
        from cmt2.ir import MLIRBuilder
        from circt.ir import InsertionPoint, Location
        from circt.dialects import hw
        
        builder = MLIRBuilder()
        
        with builder.context():
            loc = Location.unknown()
            # Create a module to provide insertion point
            with InsertionPoint(builder._module.body):
                # Create a test module
                mod = hw.HWModuleOp(
                    name="test_arithmetic", 
                    body_builder=lambda m: None,
                    loc=loc
                )
                
                with InsertionPoint(mod.body.blocks[0]):
                    with Cmt2Trace(builder) as trace:
                        # Create inputs
                        a = trace.input(UInt(32), "a")
                        b = trace.input(UInt(32), "b")
                        
                        # Basic arithmetic - these operations are intercepted
                        sum_val = a + b        # Emits comb.add
                        diff = a - b           # Emits comb.sub
                        prod = a * b           # Emits comb.mul
                        
                        # With constants
                        incremented = a + 1    # Constant 1 is created automatically
                        
                        # Create outputs
                        trace.output(sum_val, "sum")
                        trace.output(diff, "diff")
                        trace.output(prod, "product")
            
            # Emit MLIR
            mlir_text = builder.emit_mlir()
            print("Generated MLIR:")
            print(mlir_text)
            
    except ImportError as e:
        print(f"Note: CIRCT bindings not available ({e})")
        print("This example demonstrates the API usage:")
        print()
        print("  with Cmt2Trace(builder) as trace:")
        print("      a = trace.input(UInt(32), 'a')")
        print("      b = trace.input(UInt(32), 'b')")
        print("      sum_val = a + b  # Intercepted!")
        print("      trace.output(sum_val, 'sum')")


def example_bitwise_operations():
    """Example: Bitwise operations."""
    print("\n" + "=" * 60)
    print("Example: Bitwise Operations")
    print("=" * 60)
    
    try:
        from cmt2.ir import MLIRBuilder
        
        builder = MLIRBuilder()
        
        with builder.context():
            with Cmt2Trace(builder) as trace:
                # Create inputs
                a = trace.input(UInt(16), "a")
                b = trace.input(UInt(16), "b")
                mask = trace.input(UInt(16), "mask")
                
                # Bitwise operations
                masked = a & mask          # AND
                combined = a | b           # OR
                toggled = a ^ mask         # XOR
                inverted = ~a              # NOT
                
                # Shifts
                shifted_left = a << 4      # Left shift
                shifted_right = a >> 2     # Right shift (unsigned)
                
                # Create outputs
                trace.output(masked, "masked")
                trace.output(combined, "combined")
                trace.output(toggled, "toggled")
                
            mlir_text = builder.emit_mlir()
            print("Generated MLIR:")
            print(mlir_text)
            
    except ImportError as e:
        print(f"Note: CIRCT bindings not available ({e})")
        print("This example demonstrates the API usage:")
        print()
        print("  a = trace.input(UInt(16), 'a')")
        print("  mask = trace.input(UInt(16), 'mask')")
        print("  masked = a & mask   # Bitwise AND")
        print("  inverted = ~a       # Bitwise NOT")


def example_comparison_operations():
    """Example: Comparison operations."""
    print("\n" + "=" * 60)
    print("Example: Comparison Operations")
    print("=" * 60)
    
    try:
        from cmt2.ir import MLIRBuilder
        
        builder = MLIRBuilder()
        
        with builder.context():
            with Cmt2Trace(builder) as trace:
                # Create inputs
                a = trace.input(UInt(32), "a")
                b = trace.input(UInt(32), "b")
                limit = trace.input(UInt(32), "limit")
                
                # Comparisons return Bits(1)
                is_equal = a == b          # Equality
                is_not_equal = a != b      # Inequality
                is_less = a < limit        # Less than
                is_greater = a > b         # Greater than
                is_less_eq = a <= limit    # Less than or equal
                is_greater_eq = a >= b     # Greater than or equal
                
                # Boolean combinations
                in_range = (a >= 0) & (a < limit)
                
                # Create outputs
                trace.output(is_equal, "is_equal")
                trace.output(in_range, "in_range")
                
            mlir_text = builder.emit_mlir()
            print("Generated MLIR:")
            print(mlir_text)
            
    except ImportError as e:
        print(f"Note: CIRCT bindings not available ({e})")
        print("This example demonstrates the API usage:")
        print()
        print("  a = trace.input(UInt(32), 'a')")
        print("  b = trace.input(UInt(32), 'b')")
        print("  is_equal = a == b    # Returns Bits(1)")
        print("  in_range = (a >= 0) & (a < limit)")


def example_signed_operations():
    """Example: Signed integer operations."""
    print("\n" + "=" * 60)
    print("Example: Signed Integer Operations")
    print("=" * 60)
    
    try:
        from cmt2.ir import MLIRBuilder
        
        builder = MLIRBuilder()
        
        with builder.context():
            with Cmt2Trace(builder) as trace:
                # Create signed inputs
                a = trace.input(SInt(16), "a")
                b = trace.input(SInt(16), "b")
                
                # Signed arithmetic (signed operations are used)
                sum_signed = a + b
                diff_signed = a - b
                
                # Signed division
                quotient = a / b           # Uses comb.divs
                remainder = a % b          # Uses comb.mods
                
                # Signed comparisons
                is_negative = a < 0
                
                # Create outputs
                trace.output(sum_signed, "sum")
                trace.output(is_negative, "is_negative")
                
            mlir_text = builder.emit_mlir()
            print("Generated MLIR:")
            print(mlir_text)
            
    except ImportError as e:
        print(f"Note: CIRCT bindings not available ({e})")
        print("This example demonstrates the API usage:")
        print()
        print("  a = trace.input(SInt(16), 'a')")
        print("  b = trace.input(SInt(16), 'b')")
        print("  quotient = a / b     # Signed division (comb.divs)")
        print("  is_negative = a < 0  # Signed comparison")


def example_bit_slicing():
    """Example: Bit slicing and extraction."""
    print("\n" + "=" * 60)
    print("Example: Bit Slicing and Extraction")
    print("=" * 60)
    
    try:
        from cmt2.ir import MLIRBuilder
        
        builder = MLIRBuilder()
        
        with builder.context():
            with Cmt2Trace(builder) as trace:
                # Create input
                data = trace.input(UInt(32), "data")
                
                # Bit extraction
                lsb = data[0]              # Single bit
                byte0 = data[0:8]          # Slice (bits 0-7)
                byte1 = data[8:16]         # Slice (bits 8-15)
                
                # Concatenation
                swapped = byte1.concat(byte0)
                
                # Create outputs
                trace.output(lsb, "lsb")
                trace.output(swapped, "swapped")
                
            mlir_text = builder.emit_mlir()
            print("Generated MLIR:")
            print(mlir_text)
            
    except ImportError as e:
        print(f"Note: CIRCT bindings not available ({e})")
        print("This example demonstrates the API usage:")
        print()
        print("  data = trace.input(UInt(32), 'data')")
        print("  lsb = data[0]           # Extract bit 0")
        print("  byte0 = data[0:8]       # Extract bits 0-7")
        print("  swapped = byte1.concat(byte0)")


def example_type_promotion():
    """Example: Type promotion in operations."""
    print("\n" + "=" * 60)
    print("Example: Type Promotion")
    print("=" * 60)
    
    # Demonstrate type promotion rules
    from cmt2._type_promotion import promote_types, Operation as TypeOp
    
    # UInt + UInt -> UInt with +1 width
    t1 = UInt(8)
    t2 = UInt(16)
    result = t1 + t2
    print(f"  {t1} + {t2} = {result}")
    
    # SInt + UInt -> SInt with +1 width
    s1 = SInt(8)
    result = s1 + t2
    print(f"  {s1} + {t2} = {result}")
    
    # Multiplication: w1 + w2
    result = t1 * t2
    print(f"  {t1} * {t2} = {result}")
    
    # Comparison always returns Bits(1)
    result = promote_types(t1, t2, TypeOp.COMPARE)
    print(f"  compare({t1}, {t2}) = {result}")


def example_complex_expression():
    """Example: Complex expression."""
    print("\n" + "=" * 60)
    print("Example: Complex Expression")
    print("=" * 60)
    
    try:
        from cmt2.ir import MLIRBuilder
        
        builder = MLIRBuilder()
        
        with builder.context():
            with Cmt2Trace(builder) as trace:
                # ALU-like expression
                a = trace.input(UInt(32), "a")
                b = trace.input(UInt(32), "b")
                op_select = trace.input(UInt(2), "op_select")
                
                # Multiple operations
                add_result = a + b
                sub_result = a - b
                and_result = a & b
                xor_result = a ^ b
                
                # Select result based on op_select
                # (This would typically use a mux tree)
                # For demonstration, just output one
                trace.output(add_result, "result")
                
            mlir_text = builder.emit_mlir()
            print("Generated MLIR:")
            print(mlir_text)
            
    except ImportError as e:
        print(f"Note: CIRCT bindings not available ({e})")
        print("This example demonstrates complex expression tracing")


def main():
    """Run all examples."""
    parser = argparse.ArgumentParser(description='CMT2 Tracer Examples')
    parser.add_argument('--example', type=str, default='all',
                       choices=['all', 'arithmetic', 'bitwise', 'comparison', 
                               'signed', 'slicing', 'promotion', 'complex'],
                       help='Which example to run')
    args = parser.parse_args()
    
    examples = {
        'arithmetic': example_basic_arithmetic,
        'bitwise': example_bitwise_operations,
        'comparison': example_comparison_operations,
        'signed': example_signed_operations,
        'slicing': example_bit_slicing,
        'promotion': example_type_promotion,
        'complex': example_complex_expression,
    }
    
    if args.example == 'all':
        for name, example_fn in examples.items():
            example_fn()
        print("\n" + "=" * 60)
        print("All examples completed!")
        print("=" * 60)
    else:
        examples[args.example]()


if __name__ == "__main__":
    main()
