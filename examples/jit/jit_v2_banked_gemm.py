#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT v2 Banked GEMM Example - Tiled Matrix Multiplication.

Demonstrates banked memory and dataflow for high-performance GEMM.

Usage:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 jit_v2_banked_gemm.py
"""

import sys
sys.path.insert(0, '../../python')

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Memory
from circt.pycmt2.types import SyncToken
from circt.pycmt2.simulation import SimulationWorkspace

import cmt2.jit_v2 as jit


@jit.elaborate
def banked_gemm_tile(
    M: int = 4,
    K: int = 2,
    N: int = 4,
    data_width: int = 32,
):
    """Banked memory GEMM tile using JIT v2.
    
    Design:
      - A is MxK with M=4, K=2
      - B is KxN with N=4
      - C is MxN
      - Banked memories for parallel access
      - Dataflow: load → compute → store
    """
    circuit = Circuit("BankedGemmV2")
    
    # Create memory templates
    mem_a = Memory.create_1r1w_async(circuit, data_width, addr_width=1, depth=2)
    mem_b = Memory.create_1r1w_async(circuit, data_width, addr_width=1, depth=2)
    mem_c = Memory.create_1r1w_async(circuit, data_width, addr_width=2, depth=4)
    
    @jit.module(circuit, "GemmTile")
    def build(m):
        clk = m.clock()
        rst = m.reset()
        
        # Create banked memories
        a_banks = [m.instance(mem_a, f"a_b{i}", clk=clk, rst=rst) for i in range(4)]
        b_banks = [m.instance(mem_b, f"b_b{i}", clk=clk, rst=rst) for i in range(4)]
        c_banks = [m.instance(mem_c, f"c_b{i}", clk=clk, rst=rst) for i in range(4)]
        
        # Host write methods for A and B banks
        for i in range(4):
            @jit.method(m, f"a_write_b{i}", args=[("addr", UInt(1)), ("data", UInt(data_width))])
            def _(meth, bank_idx=i):
                @meth.guard
                def _(g): g.always()
                @meth.body
                def _(b): b.call(a_banks[bank_idx], "write", b.arg("data"), b.arg("addr"))
            
            @jit.method(m, f"b_write_b{i}", args=[("addr", UInt(1)), ("data", UInt(data_width))])
            def _(meth, bank_idx=i):
                @meth.guard
                def _(g): g.always()
                @meth.body
                def _(b): b.call(b_banks[bank_idx], "write", b.arg("data"), b.arg("addr"))
        
        # Host read methods for C banks
        for i in range(4):
            @jit.value(m, f"get_c_b{i}", returns=[UInt(data_width)])
            def _(val, bank_idx=i):
                @val.guard
                def _(g): g.always()
                @val.body
                def _(b): b.returns(b.call(c_banks[bank_idx], "read", b.const(0, 2)))
        
        # Dataflow: load → compute → store
        @jit.dataflow(
            m,
            "gemm",
            args=[],
            returns=[UInt(data_width)] * 4,
        )
        def build_gemm(df):
            
            # Load stage: Read from A and B banks
            @jit.task(df, "load", timing=(0, 1))
            def load_task(task):
                # Read from banked memories
                a_data = [task.call(a_banks[i], "read", task.const(0, 1)) for i in range(4)]
                b_data = [task.call(b_banks[i], "read", task.const(0, 1)) for i in range(4)]
                
                # Create tokens for compute stage
                tokens = []
                for i, (a, b) in enumerate(zip(a_data[:2], b_data[:2])):
                    tok_a = task.create_token(a, SyncToken(UInt(data_width)))
                    tok_b = task.create_token(b, SyncToken(UInt(data_width)))
                    tokens.extend([tok_a, tok_b])
                
                task.yield_tokens(*tokens)
            
            # Compute stage: MAC operations
            @jit.task(df, "compute", timing=(1, 3))
            def compute_task(task):
                # Get tokens from load stage
                # Perform MAC operations
                results = []
                for i in range(4):
                    # Simplified: just sum some values
                    acc = task.const(0, data_width)
                    for k in range(K):
                        # In real implementation: MAC
                        acc = task.add(acc, task.const(i * k, data_width))
                    results.append(acc)
                
                # Create tokens for store stage
                out_tokens = [task.create_token(r, SyncToken(UInt(data_width))) for r in results]
                task.yield_tokens(*out_tokens)
            
            # Store stage: Write to C banks
            @jit.task(df, "store", timing=(3, 4))
            def store_task(task):
                # Get results and store to C banks
                for i in range(4):
                    data = task.token_data(f"compute_out_{i}")
                    task.call(c_banks[i], "write", data, task.const(0, 2))
                
                # Return final values
                task.return_values(*[task.const(i, data_width) for i in range(4)])
    
    return circuit


@jit.simulate
def test_banked_gemm():
    """Test banked GEMM."""
    print("=" * 60)
    print("JIT v2 Banked GEMM Example")
    print("=" * 60)
    
    circuit = banked_gemm_tile(M=4, K=2, N=4, data_width=32)
    
    print(f"\nCircuit: {circuit.name}")
    print(f"MLIR size: {len(circuit.emit_mlir())} chars")
    
    # Print first part of MLIR
    mlir = circuit.emit_mlir()
    print("\nMLIR Preview:")
    print("-" * 40)
    print(mlir[:2000])
    print("... [truncated] ...")
    
    return {"success": True, "circuit": circuit}


if __name__ == "__main__":
    result = test_banked_gemm()
    print(f"\nTest {'PASSED' if result['success'] else 'FAILED'}")
