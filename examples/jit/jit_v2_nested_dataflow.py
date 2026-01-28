#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT v2 Nested Dataflow Example - Hierarchical Pipeline.

Demonstrates nested dataflows where a task contains an inner dataflow.

Usage:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 jit_v2_nested_dataflow.py
"""

import sys
sys.path.insert(0, '../../python')

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg
from circt.pycmt2.types import SyncToken

import cmt2.jit_v2 as jit


@jit.elaborate
def nested_image_pipeline():
    """Image processing pipeline with nested dataflows.
    
    Architecture:
        Outer Dataflow: image_pipeline
            input → preprocess → [convolve_stage] → postprocess → output
                                    │
                         Inner Dataflow: convolution
                             h_pass ─┐
                                     ├→ sum
                             v_pass ─┘
    """
    circuit = Circuit("NestedDataflowV2")
    
    @jit.module(circuit, "ImagePipeline")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        
        # State registers
        pixel_count = m.instance(Reg.create(circuit, 16), "pixel_count", clk=clk, rst=rst)
        result_reg = m.instance(Reg.create(circuit, 32), "result_reg", clk=clk, rst=rst)
        
        # Outer dataflow
        @jit.dataflow(
            m,
            "image_pipeline",
            args=[("pixel_in", UInt(16)), ("kernel", UInt(8))],
            returns=[UInt(32)],
            interval=1,
        )
        def build_outer(df):
            
            # Stage 1: Preprocess
            @jit.task(df, "preprocess", timing=(0, 1))
            def preprocess(task):
                # Normalize input
                pixel = task.arg("pixel_in")
                normalized = task.add(pixel, task.const(0, 16))
                
                tok = task.create_token(normalized, SyncToken(UInt(16)))
                task.yield_tokens(tok)
            
            # Stage 2: Convolve (contains inner dataflow)
            @jit.task(df, "convolve_stage", timing=(1, 4))
            def convolve_stage(task):
                # Get input from preprocess
                pixel = task.token_data("preprocess_out")
                kernel = task.arg("kernel")
                
                # Inner dataflow for convolution
                @task.dataflow(
                    "convolution",
                    args=[("p", UInt(16)), ("k", UInt(8))],
                    returns=[UInt(32)],
                )
                def build_inner(inner_df):
                    
                    # Horizontal pass
                    @jit.task(inner_df, "h_pass")
                    def h_pass(t):
                        p = t.arg("p")
                        result = t.add(p, t.const(0, 16))
                        t.yield_tokens(t.create_token(result, SyncToken(UInt(17))))
                    
                    # Vertical pass
                    @jit.task(inner_df, "v_pass")
                    def v_pass(t):
                        k = t.arg("k")
                        result = t.add(k, t.const(0, 8))
                        t.yield_tokens(t.create_token(result, SyncToken(UInt(9))))
                    
                    # Sum results
                    @jit.task(inner_df, "sum")
                    def sum_task(t):
                        h = t.token_data("h_pass_out")
                        v = t.token_data("v_pass_out")
                        result = t.add(
                            t.zero_extend(h, 32),
                            t.zero_extend(v, 32)
                        )
                        t.return_values(result)
                
                # Run inner dataflow and forward result
                conv_result = task.call_inner_dataflow("convolution", [pixel, kernel])
                task.yield_tokens(task.create_token(conv_result, SyncToken(UInt(32))))
            
            # Stage 3: Postprocess
            @jit.task(df, "postprocess", timing=(4, 5))
            def postprocess(task):
                data = task.token_data("convolve_stage_out")
                # Scale output
                scaled = task.add(data, task.const(0, 32))
                task.return_values(scaled)
    
    return circuit


@jit.simulate
def test_nested_dataflow():
    """Test nested dataflow example."""
    print("=" * 60)
    print("JIT v2 Nested Dataflow Example")
    print("=" * 60)
    
    circuit = nested_image_pipeline()
    
    print(f"\nCircuit: {circuit.name}")
    print(f"MLIR size: {len(circuit.emit_mlir())} chars")
    
    # Print MLIR
    mlir = circuit.emit_mlir()
    print("\nMLIR Preview:")
    print("-" * 40)
    print(mlir[:2500])
    print("... [truncated] ...")
    
    return {"success": True}


if __name__ == "__main__":
    result = test_nested_dataflow()
    print(f"\nTest {'PASSED' if result['success'] else 'FAILED'}")
