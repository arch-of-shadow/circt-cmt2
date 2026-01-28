#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT v2 Dataflow Example - Fork-Join Pipeline.

Demonstrates dataflow support in JIT v2 with agile syntax.

Usage:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 jit_v2_dataflow.py
"""

import sys
sys.path.insert(0, '../../python')

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.types import SyncToken
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench

import cmt2.jit_v2 as jit


@jit.elaborate
def forkjoin_pipeline(data_width: int = 32):
    """Fork-join pipeline using JIT v2 dataflow decorators.
    
    Architecture:
        Input → [Branch A] ──┐
                            ├──→ Sum → Output
               [Branch B] ──┘
    """
    circuit = Circuit("ForkJoinV2")
    
    @jit.module(circuit, "ParallelCompute")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        
        # Create fork-join pipeline using @forkjoin decorator
        @jit.forkjoin(m, "parallel", UInt(data_width), UInt(data_width + 1))
        def build_parallel(fjp):
            
            @fjp.source
            def source(task, data):
                """Pass through input data."""
                return data
            
            @fjp.branch("add_one")
            def add_one(task, data):
                """Add 1 to data."""
                return task.add(data, task.const(1, data_width))
            
            @fjp.branch("add_two")
            def add_two(task, data):
                """Add 2 to data."""
                return task.add(data, task.const(2, data_width))
            
            @fjp.sink
            def sink(task, results):
                """Sum results from both branches."""
                a, b = results
                return task.add(a, b)
    
    return circuit


@jit.elaborate
def explicit_dataflow(data_width: int = 32):
    """Explicit dataflow using @dataflow and @task decorators.
    
    Architecture:
        Input → Stage 1 → Stage 2 → Output
    """
    circuit = Circuit("DataflowV2")
    
    @jit.module(circuit, "Pipeline")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        
        # Create dataflow using @dataflow decorator
        @jit.dataflow(
            m,
            "pipeline",
            args=[("input", UInt(data_width))],
            returns=[UInt(data_width + 2)],
            interval=1,
        )
        def build_pipeline(df):
            
            # Stage 1: Add 1
            @jit.task(df, "stage1", timing=(0, 1))
            def stage1(task):
                tok_in = task.create_token(df.input, UInt(data_width))
                task.yield_tokens(tok_in)
                
                # Get data from token
                data = task.token_data(tok_in)
                result = task.add(data, task.const(1, data_width))
                
                # Create output token
                tok_out = task.create_token(result, UInt(data_width + 1))
                task.yield_tokens(tok_out)
            
            # Stage 2: Add 2
            @jit.task(df, "stage2", tokens_in=["stage1_output"], timing=(1, 2))
            def stage2(task):
                # Process from stage 1
                data = task.token_data("stage1_output")
                result = task.add(data, task.const(2, data_width + 1))
                task.return_values(result)
    
    return circuit


@jit.simulate
def test_dataflow_v2():
    """Test dataflow examples."""
    print("=" * 60)
    print("JIT v2 Dataflow Examples")
    print("=" * 60)
    
    # Test fork-join
    print("\n1. Fork-Join Pipeline")
    print("-" * 40)
    circuit1 = forkjoin_pipeline(data_width=32)
    print(f"Circuit: {circuit1.name}")
    print(f"MLIR size: {len(circuit1.emit_mlir())} chars")
    
    # Test explicit dataflow
    print("\n2. Explicit Dataflow Pipeline")
    print("-" * 40)
    circuit2 = explicit_dataflow(data_width=16)
    print(f"Circuit: {circuit2.name}")
    print(f"MLIR size: {len(circuit2.emit_mlir())} chars")
    
    print("\n" + "=" * 60)
    print("Dataflow examples generated successfully!")
    print("=" * 60)
    
    return {"success": True}


if __name__ == "__main__":
    result = test_dataflow_v2()
    print(f"\nTest {'PASSED' if result['success'] else 'FAILED'}")
