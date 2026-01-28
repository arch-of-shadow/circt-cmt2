#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Counter Example - CMT2 JIT End-to-End Demo.

This example demonstrates the CMT2 JIT workflow:
1. Define a parameterized counter using @elaborate decorator
2. Elaborate the circuit with specific parameters
3. Lower to target representation
4. Compile and generate output

Example:
    python counter_example.py
    python counter_example.py --help
"""

from __future__ import annotations

import argparse
import sys
from typing import Annotated

# Add python directory to path for development
sys.path.insert(0, '../../python')

import cmt2
from cmt2 import elaborate, simulate, static


@elaborate
def counter_design(
    width: Annotated[int, static] = 8,
    max_count: Annotated[int, static] = 100,
):
    """A parameterized counter design.
    
    Args:
        width: Bit width of the counter
        max_count: Maximum count value before wrapping
        
    Returns:
        An elaborated circuit
    """
    # Note: This is a simplified example showing the JIT API.
    # Full implementation would use the Circuit builder API.
    
    print(f"Elaborating counter with width={width}, max_count={max_count}")
    
    # Return a placeholder for the example
    # In real usage, this would return a Circuit object
    return {"name": "Counter", "width": width, "max_count": max_count}


@simulate
def test_counter(
    width: Annotated[int, static] = 8,
    cycles: int = 100,
):
    """Test the counter design.
    
    Args:
        width: Bit width of the counter
        cycles: Number of simulation cycles
        
    Returns:
        Simulation results
    """
    print(f"Testing counter with width={width} for {cycles} cycles")
    
    # Elaborate the design
    circuit = counter_design(width)
    
    # Run simulation (placeholder)
    return {
        "circuit": circuit,
        "cycles": cycles,
        "status": "success"
    }


def demo_basic_elaboration():
    """Demonstrate basic elaboration with caching."""
    print("=" * 60)
    print("Demo: Basic Elaboration with Caching")
    print("=" * 60)
    
    # First call - cache miss
    print("\n1. First call (cache miss):")
    circuit1 = counter_design(width=8, max_count=100)
    print(f"   Result: {circuit1}")
    print(f"   Cache key: {counter_design.cache_key}")
    
    # Second call with same args - cache hit
    print("\n2. Second call with same args (cache hit):")
    circuit2 = counter_design(width=8, max_count=100)
    print(f"   Result: {circuit2}")
    print(f"   Cache key: {counter_design.cache_key}")
    
    # Third call with different args - cache miss
    print("\n3. Third call with different args (cache miss):")
    circuit3 = counter_design(width=16, max_count=1000)
    print(f"   Result: {circuit3}")
    print(f"   Cache key: {counter_design.cache_key}")


def demo_staged_compilation():
    """Demonstrate staged compilation workflow."""
    print("\n" + "=" * 60)
    print("Demo: Staged Compilation")
    print("=" * 60)
    
    # Stage 1: Elaboration
    print("\n1. Elaboration Stage:")
    print("   @elaborate decorator captures the design function")
    print("   Static arguments (width, max_count) are part of cache key")
    
    # Stage 2: Lowering
    print("\n2. Lowering Stage:")
    print("   ElaboratedCircuit.lower(target='verilog')")
    print("   -> LoweredCircuit with MLIR in target dialect")
    
    # Stage 3: Compilation
    print("\n3. Compilation Stage:")
    print("   LoweredCircuit.compile()")
    print("   -> CompiledCircuit ready for output")
    
    # Stage 4: Code generation
    print("\n4. Code Generation:")
    print("   CompiledCircuit.codegen(format='verilog')")
    print("   CompiledCircuit.write('output.sv')")


def demo_simulation():
    """Demonstrate simulation workflow."""
    print("\n" + "=" * 60)
    print("Demo: Simulation Workflow")
    print("=" * 60)
    
    # Run simulation
    print("\n1. Running simulation:")
    result = test_counter(width=8, cycles=100)
    print(f"   Result: {result}")
    
    print("\n2. @simulate decorator:")
    print("   - Runs elaboration and simulation immediately")
    print("   - Returns simulation results directly")
    print("   - Does not cache the circuit")


def demo_static_arguments():
    """Demonstrate static argument handling."""
    print("\n" + "=" * 60)
    print("Demo: Static Arguments")
    print("=" * 60)
    
    print("\n1. Using Annotated type hint (recommended):")
    print("   def design(width: Annotated[int, static])")
    
    print("\n2. Using static_argnums:")
    print("   @elaborate(static_argnums=0)")
    print("   def design(width: int)")
    
    print("\n3. Using static_argnames:")
    print("   @elaborate(static_argnames=['width'])")
    print("   def design(width: int)")
    
    print("\nStatic arguments:")
    print("   - Become part of the cache key")
    print("   - Must be hashable")
    print("   - Trigger recompilation when changed")
    print("   - Examples: bit widths, depths, feature flags")


def demo_control_flow():
    """Demonstrate hardware control flow constructs."""
    print("\n" + "=" * 60)
    print("Demo: Hardware Control Flow")
    print("=" * 60)
    
    print("\n1. Conditional execution (hardware mux):")
    print("   with cmt2.when(condition):")
    print("       reg.next = value1")
    print("   with cmt2.otherwise():")
    print("       reg.next = value2")
    
    print("\n2. Multi-way branching:")
    print("   with cmt2.switch(selector):")
    print("       with cmt2.case(0):")
    print("           ...")
    print("       with cmt2.case(1):")
    print("           ...")
    
    print("\n3. Trace-time unrolling:")
    print("   for i in cmt2.unroll(range(4)):")
    print("       # Creates 4 parallel instances")


def main():
    """Run all demos."""
    parser = argparse.ArgumentParser(description='CMT2 JIT Counter Example')
    parser.add_argument(
        '--demo',
        choices=['all', 'basic', 'staged', 'sim', 'static', 'control'],
        default='all',
        help='Which demo to run'
    )
    args = parser.parse_args()
    
    demos = {
        'basic': demo_basic_elaboration,
        'staged': demo_staged_compilation,
        'sim': demo_simulation,
        'static': demo_static_arguments,
        'control': demo_control_flow,
    }
    
    if args.demo == 'all':
        for demo_func in demos.values():
            demo_func()
    else:
        demos[args.demo]()
    
    print("\n" + "=" * 60)
    print("Demo Complete!")
    print("=" * 60)
    print("\nKey Takeaways:")
    print("  - @elaborate for staged compilation with caching")
    print("  - @simulate for immediate execution")
    print("  - Static arguments control specialization")
    print("  - Hardware control flow constructs (when/switch/unroll)")


if __name__ == "__main__":
    main()
