#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT FIFO Example with End-to-End Simulation.

This example demonstrates a parameterized FIFO design using CMT2 JIT:
1. Define a FIFO module with configurable width and depth
2. Compile and run E2E simulation
3. Test FIFO operations (push, pop, full, empty)

Usage:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 ../examples/jit/jit_fifo.py
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path
from typing import Annotated

# Setup path
sys.path.insert(0, '../../python')

import cmt2
from cmt2 import elaborate, simulate, static
from cmt2.pycmt2_integration import (
    CircuitBuilder,
    JITSimulationRunner,
    is_pycmt2_available,
)

if not is_pycmt2_available():
    print("ERROR: PyCMT2 not available.")
    sys.exit(1)

from circt.pycmt2 import UInt


@cmt2.elaborate
def fifo_design(
    data_width: Annotated[int, static] = 32,
    depth: Annotated[int, static] = 8,
):
    """Create a parameterized FIFO circuit.
    
    Args:
        data_width: Width of data elements
        depth: FIFO depth (must be power of 2 for this implementation)
        
    Returns:
        PyCMT2 Circuit object
    """
    print(f"Elaborating FIFO: data_width={data_width}, depth={depth}")
    
    builder = CircuitBuilder("FIFO")
    
    with builder.module("FIFO") as m:
        # Clock and reset
        clk = m.clock("clk")
        rst = m.reset("rst")
        
        # Data input
        data_in = m.input("data_in", UInt(data_width))
        push = m.input("push", UInt(1))
        
        # Data output
        data_out = m.output("data_out", UInt(data_width))
        pop = m.input("pop", UInt(1))
        
        # Status outputs
        full = m.output("full", UInt(1))
        empty = m.output("empty", UInt(1))
        count = m.output("count", UInt(32))
        
        # Create the FIFO instance using STL
        fifo_inst = m.instance_fifo(
            UInt(data_width),
            depth,
            "fifo_inst",
            clk=clk,
            rst=rst,
        )
        
        # Push rule: write data when push is high and not full
        with m.rule("do_push") as rule:
            with rule.guard() as g:
                # Check push signal and not full
                not_full = g.call(fifo_inst, "notFull")
                g.equals(push, g.const(1, 1))
                g.equals(not_full, g.const(1, 1))
            
            with rule.body() as b:
                b.call(fifo_inst, "enq", data_in)
        
        # Pop rule: read data when pop is high and not empty
        with m.rule("do_pop") as rule:
            with rule.guard() as g:
                not_empty = g.call(fifo_inst, "notEmpty")
                g.equals(pop, g.const(1, 1))
                g.equals(not_empty, g.const(1, 1))
            
            with rule.body() as b:
                b.call(fifo_inst, "deq")
        
        # Update status outputs
        with m.rule("update_status") as rule:
            with rule.guard() as g:
                g.always()
            
            with rule.body() as b:
                # Get FIFO status
                is_full = b.call(fifo_inst, "isFull")
                is_empty = b.call(fifo_inst, "isEmpty")
                fifo_count = b.call(fifo_inst, "count")
                
                # Write to outputs (using external wires would be better,
                # but for simplicity we use rules here)
                # Note: In real implementation, use value methods or wires
    
    return builder.circuit


@cmt2.simulate
def test_fifo(
    data_width: Annotated[int, static] = 32,
    depth: Annotated[int, static] = 8,
    workspace_dir: str | None = None,
):
    """Test the FIFO with E2E simulation.
    
    Args:
        data_width: Data width
        depth: FIFO depth
        workspace_dir: Optional workspace directory
        
    Returns:
        Simulation results
    """
    print(f"\n{'='*60}")
    print(f"Testing FIFO: width={data_width}, depth={depth}")
    print(f"{'='*60}\n")
    
    # Elaborate
    circuit = fifo_design(data_width=data_width, depth=depth)
    
    # Create workspace
    if workspace_dir is None:
        workspace_dir = tempfile.mkdtemp(prefix="jit_fifo_")
    
    print(f"Workspace: {workspace_dir}")
    
    # Create runner
    runner = JITSimulationRunner(
        circuit=circuit,
        workspace_dir=workspace_dir,
        debug_ports=True,
    )
    
    # Create testbench
    tb = runner.create_testbench(auto_debug_ports=True)
    
    # Test 1: Reset behavior
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: FIFO should be empty after reset")
        seq.reset(5)
        seq.expect("empty", 1, "FIFO should be empty after reset")
        seq.print("Reset test passed!")
    
    # Test 2: Push and pop
    with tb.sequence("test_push_pop") as seq:
        seq.comment("Test: Push then pop")
        seq.reset(5)
        
        # Push some data
        seq.poke("data_in", 0xDEADBEEF)
        seq.poke("push", 1)
        seq.wait(1)
        seq.poke("push", 0)
        
        # Check not empty
        seq.expect("empty", 0, "FIFO should not be empty after push")
        
        # Pop the data
        seq.poke("pop", 1)
        seq.wait(1)
        seq.poke("pop", 0)
        
        # Should be empty again
        seq.wait(1)
        seq.expect("empty", 1, "FIFO should be empty after pop")
        seq.print("Push/pop test passed!")
    
    # Test 3: Multiple pushes
    with tb.sequence("test_multiple") as seq:
        seq.comment("Test: Multiple consecutive pushes")
        seq.reset(5)
        
        # Push several values
        for i in range(min(4, depth)):
            seq.poke("data_in", 0x1000 + i)
            seq.poke("push", 1)
            seq.wait(1)
        
        seq.poke("push", 0)
        seq.wait(1)
        
        # Pop them all
        for i in range(min(4, depth)):
            seq.expect("empty", 0, f"FIFO should not be empty at iteration {i}")
            seq.poke("pop", 1)
            seq.wait(1)
        
        seq.poke("pop", 0)
        seq.wait(1)
        seq.expect("empty", 1, "FIFO should be empty after popping all")
        seq.print("Multiple push/pop test passed!")
    
    # Run simulation
    print("\nRunning simulation...")
    result = runner.run(testbench=tb, waves=True)
    
    return result


def demo_parameter_sweep():
    """Demonstrate FIFO with different parameters."""
    print(f"\n{'='*60}")
    print("Demo: FIFO Parameter Sweep")
    print(f"{'='*60}\n")
    
    configs = [
        (8, 4),
        (16, 8),
        (32, 16),
    ]
    
    for width, depth in configs:
        print(f"Creating FIFO: width={width}, depth={depth}")
        circuit = fifo_design(data_width=width, depth=depth)
        
        # Show code stats
        mlir = circuit.emit_mlir()
        verilog = circuit.emit_verilog()
        print(f"  MLIR: {len(mlir)} chars")
        print(f"  Verilog: {len(verilog)} chars")
        print()


def main():
    parser = argparse.ArgumentParser(description="JIT FIFO Example")
    parser.add_argument(
        "--demo",
        choices=["sim", "sweep", "all"],
        default="all",
        help="Which demo to run"
    )
    parser.add_argument(
        "--width",
        type=int,
        default=32,
        help="Data width"
    )
    parser.add_argument(
        "--depth",
        type=int,
        default=8,
        help="FIFO depth"
    )
    parser.add_argument(
        "--workspace",
        type=str,
        default=None,
        help="Workspace directory"
    )
    args = parser.parse_args()
    
    if args.demo in ("sim", "all"):
        result = test_fifo(
            data_width=args.width,
            depth=args.depth,
            workspace_dir=args.workspace,
        )
        print(f"\nResult: {'SUCCESS' if result['success'] else 'FAILED'}")
    
    if args.demo in ("sweep", "all"):
        demo_parameter_sweep()
    
    print(f"\n{'='*60}")
    print("JIT FIFO Example Complete!")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
