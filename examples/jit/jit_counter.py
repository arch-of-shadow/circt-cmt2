#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT Counter Example with End-to-End Simulation.

This example demonstrates using the CMT2 JIT with PyCMT2 for a complete
hardware design flow:
1. Define a parameterized counter using @elaborate
2. Compile to MLIR/FIRRTL/Verilog
3. Run E2E simulation with Verilator

Usage:
    # From build directory with CIRCT compiled:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 ../examples/jit/jit_counter.py

    # Or from this directory with proper PYTHONPATH set:
    python jit_counter.py
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path
from typing import Annotated

# Setup path for development
sys.path.insert(0, '../../python')

import cmt2
from cmt2 import elaborate, simulate, static
from cmt2.pycmt2_integration import (
    CircuitBuilder,
    JITSimulationRunner,
    is_pycmt2_available,
)

# Check PyCMT2 availability
if not is_pycmt2_available():
    print("ERROR: PyCMT2 not available. Please set PYTHONPATH to include CIRCT bindings.")
    print("Example: PYTHONPATH=build/tools/circt/python_packages/circt_core python jit_counter.py")
    sys.exit(1)

# Import PyCMT2 types
from circt.pycmt2 import UInt


@cmt2.elaborate
def counter_design(
    width: Annotated[int, static] = 32,
    use_enable: Annotated[bool, static] = True,
):
    """Create a parameterized counter circuit.
    
    Args:
        width: Bit width of the counter
        use_enable: Whether to include an enable signal
        
    Returns:
        PyCMT2 Circuit object
    """
    print(f"Elaborating counter: width={width}, use_enable={use_enable}")
    
    builder = CircuitBuilder("Counter")
    
    with builder.module("Counter") as m:
        # Create ports
        clk = m.clock("clk")
        rst = m.reset("rst")
        
        if use_enable:
            enable = m.input("enable", UInt(1))
        
        # Create counter register
        count = m.instance_reg(width, "count", clk=clk, rst=rst, init=0)
        
        # Increment rule
        with m.rule("increment") as rule:
            with rule.guard() as g:
                if use_enable:
                    # Guard: only increment when enabled
                    g.equals(enable, g.const(1, 1))
                else:
                    g.always()
            
            with rule.body() as b:
                # Read current value
                val = b.call(count, "read")
                # Increment
                one = b.const(1, width)
                next_val = b.add(val, one)
                # Write back
                b.call(count, "write", next_val)
        
        # Value method to read the counter
        with m.value("get_count", returns=[UInt(width)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                cnt = b.call(count, "read")
                b.returns(cnt)
    
    return builder.circuit


@cmt2.simulate
def test_counter(
    width: Annotated[int, static] = 32,
    cycles: int = 50,
    workspace_dir: str | None = None,
):
    """Test the counter with E2E simulation.
    
    Args:
        width: Counter bit width
        cycles: Number of simulation cycles
        workspace_dir: Optional workspace directory
        
    Returns:
        Simulation results
    """
    print(f"\n{'='*60}")
    print(f"Testing Counter: width={width}, cycles={cycles}")
    print(f"{'='*60}\n")
    
    # Elaborate the design
    circuit = counter_design(width=width, use_enable=True)
    
    # Create workspace
    if workspace_dir is None:
        workspace_dir = tempfile.mkdtemp(prefix="jit_counter_")
    
    print(f"Simulation workspace: {workspace_dir}")
    
    # Create simulation runner
    runner = JITSimulationRunner(
        circuit=circuit,
        workspace_dir=workspace_dir,
        debug_ports=True,
    )
    
    # Create testbench
    tb = runner.create_testbench(auto_debug_ports=True)
    
    # Test sequence 1: Reset behavior
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Counter should be 0 after reset")
        seq.reset(5)
        seq.expect("get_count_res0", 0, "Counter should be 0 after reset")
        seq.print("Reset test passed!")
    
    # Test sequence 2: Enable controlled counting
    with tb.sequence("test_enable") as seq:
        seq.comment("Test: Counter should only increment when enabled")
        seq.reset(5)
        
        # Enable counting
        seq.poke("enable", 1)
        seq.wait(5)
        seq.expect("get_count_res0", 5, "Counter should be 5 after 5 cycles")
        
        # Disable counting
        seq.poke("enable", 0)
        seq.wait(5)
        seq.expect("get_count_res0", 5, "Counter should still be 5 when disabled")
        
        # Re-enable
        seq.poke("enable", 1)
        seq.wait(5)
        seq.expect("get_count_res0", 10, "Counter should be 10 after 5 more cycles")
        seq.print("Enable test passed!")
    
    # Test sequence 3: Longer run
    with tb.sequence("test_long_run") as seq:
        seq.comment(f"Test: Counter over {cycles} cycles")
        seq.reset(5)
        seq.poke("enable", 1)
        seq.wait(cycles)
        seq.expect("get_count_res0", cycles, f"Counter should be {cycles}")
        seq.print("Long run test passed!")
    
    # Run simulation
    print("\nRunning simulation...")
    result = runner.run(testbench=tb, waves=True)
    
    return result


def demo_staged_compilation():
    """Demonstrate staged compilation workflow."""
    print(f"\n{'='*60}")
    print("Demo: Staged Compilation")
    print(f"{'='*60}\n")
    
    # Stage 1: Elaboration
    print("Stage 1: Elaboration")
    circuit = counter_design(width=16, use_enable=True)
    print(f"  Circuit: {circuit.name}")
    
    # Stage 2: Emit MLIR
    print("\nStage 2: MLIR Generation")
    mlir = circuit.emit_mlir()
    print(f"  MLIR length: {len(mlir)} characters")
    print(f"  First 500 chars:\n{mlir[:500]}...")
    
    # Stage 3: Emit FIRRTL
    print("\nStage 3: FIRRTL Generation")
    firrtl = circuit.emit_firrtl()
    print(f"  FIRRTL length: {len(firrtl)} characters")
    print(f"  First 500 chars:\n{firrtl[:500]}...")
    
    # Stage 4: Emit Verilog
    print("\nStage 4: Verilog Generation")
    verilog = circuit.emit_verilog()
    print(f"  Verilog length: {len(verilog)} characters")
    print(f"  First 500 chars:\n{verilog[:500]}...")


def demo_parameterization():
    """Demonstrate parameterization with caching."""
    print(f"\n{'='*60}")
    print("Demo: Parameterization with JIT Caching")
    print(f"{'='*60}\n")
    
    # Create different variants
    configs = [
        (8, True),
        (16, True),
        (32, False),
        (16, True),  # Should hit cache
    ]
    
    for width, enable in configs:
        print(f"Creating counter: width={width}, enable={enable}")
        circuit = counter_design(width=width, use_enable=enable)
        print(f"  Cache key: {counter_design.cache_key}")
        print(f"  Circuit name: {circuit.name}")
        print()


def main():
    parser = argparse.ArgumentParser(
        description="JIT Counter Example with E2E Simulation"
    )
    parser.add_argument(
        "--demo",
        choices=["sim", "staged", "params", "all"],
        default="all",
        help="Which demo to run"
    )
    parser.add_argument(
        "--width",
        type=int,
        default=32,
        help="Counter bit width"
    )
    parser.add_argument(
        "--cycles",
        type=int,
        default=50,
        help="Simulation cycles"
    )
    parser.add_argument(
        "--workspace",
        type=str,
        default=None,
        help="Simulation workspace directory"
    )
    args = parser.parse_args()
    
    if args.demo in ("sim", "all"):
        result = test_counter(
            width=args.width,
            cycles=args.cycles,
            workspace_dir=args.workspace,
        )
        print(f"\nSimulation result: {'SUCCESS' if result['success'] else 'FAILED'}")
        if not result['success'] and 'error' in result:
            print(f"Error: {result['error']}")
    
    if args.demo in ("staged", "all"):
        demo_staged_compilation()
    
    if args.demo in ("params", "all"):
        demo_parameterization()
    
    print(f"\n{'='*60}")
    print("JIT Counter Example Complete!")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
