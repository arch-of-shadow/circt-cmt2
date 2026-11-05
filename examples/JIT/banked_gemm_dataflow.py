#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Banked Memory GEMM (Tiled + Unrolled) using Dataflow Tasks

This example demonstrates a small tiled GEMM using *banked memories* and a
decoupled load/compute/store pipeline expressed as CMT2 dataflow tasks.

Design overview:
  - A is MxK with M=4, K=2; B is KxN with N=4; C is MxN (only tile (0,0) is computed)
  - Banked memories:
      A banks: 4 banks indexed by (row%2, col)       => bank = (row%2)*2 + col
      B banks: 4 banks indexed by (row, col%2)       => bank = row*2 + (col%2)
      C banks: 4 banks indexed by (row%2, col%2)     => bank = (row%2)*2 + (col%2)
  - Each bank is a Mem1r1w0c external module (ModuleLibrary-backed).
  - Dataflow tasks:
      source(start) -> load(A,B) -> compute(unrolled K=2) -> store(to C banks, and return outputs)

E2E simulation:
  - Writes one 2x2 tile of A (rows 0..1, cols 0..1) and B (rows 0..1, cols 0..1)
  - Pulses start
  - Checks the returned tile outputs and the stored C banks

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/banked_gemm_dataflow.py
"""

from __future__ import annotations

import cmt2.jit as jit

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Memory, clear_stl_registry
from circt.pycmt2.testbench import Testbench


@jit.elaborate
def create_banked_gemm_circuit() -> Circuit:
    clear_stl_registry()

    circuit = Circuit("BankedGemmTileSim")

    mem_a = Memory.create_1r1w_async(circuit, data_width=32, addr_width=1, depth=2)
    mem_b = Memory.create_1r1w_async(circuit, data_width=32, addr_width=1, depth=2)
    mem_c = Memory.create_1r1w_async(circuit, data_width=32, addr_width=2, depth=4)

    with jit.module(circuit, "BankedGemmTile") as m:
        clk = m.clock()
        rst = m.reset()

        # 4 banks for each matrix
        a = [m.instance(mem_a, clk=clk, rst=rst, alias=f"a_b{i}") for i in range(4)]
        b = [m.instance(mem_b, clk=clk, rst=rst, alias=f"b_b{i}") for i in range(4)]
        c = [m.instance(mem_c, clk=clk, rst=rst, alias=f"c_b{i}") for i in range(4)]

        # ---------------------------------------------------------------------
        # Host-facing write methods for A and B banks (preload).
        # ---------------------------------------------------------------------
        for i in range(4):
            def _define_bank_writes(a_bank, b_bank, idx: int):
                @jit.method(m, alias=f"a_write_b{idx}")
                def a_write(meth, addr: UInt[1], data: UInt[32]) -> None:
                    with meth.guard:
                        meth.always()
                    with meth.body:
                        a_bank.write(data, addr)

                @jit.method(m, alias=f"b_write_b{idx}")
                def b_write(meth, addr: UInt[1], data: UInt[32]) -> None:
                    with meth.guard:
                        meth.always()
                    with meth.body:
                        b_bank.write(data, addr)

            _define_bank_writes(a[i], b[i], i)

        # ---------------------------------------------------------------------
        # Host-facing value methods to observe C bank contents for tile (0,0).
        # Tile (0,0) maps to bank_addr=0 for all 4 banks under the mapping above.
        # ---------------------------------------------------------------------
        for i in range(4):
            def _define_bank_read(c_bank, idx: int):
                @jit.value(m, alias=f"get_c_b{idx}")
                def get_c(val) -> UInt[32]:
                    with val.guard:
                        val.always()
                    with val.body:
                        val.returns(c_bank.read(val.const(0, 2)))

            _define_bank_read(c[i], i)

        # ---------------------------------------------------------------------
        # Dataflow: load -> compute (unrolled) -> store
        # ---------------------------------------------------------------------
        from circt.pycmt2 import SyncToken

        @jit.dataflow(m)
        def gemm(df) -> tuple[UInt[32], UInt[32], UInt[32], UInt[32]]:
            @df.task(tokens_out=[SyncToken[UInt[1]]], timing=(0, 1))
            def go(task):
                return task.create_token(task.const(1, 1), UInt[1])

            (tok_go,) = go._cmt2_tokens

            @df.task(tokens_in=[tok_go], timing=(1, 2), tokens_out=[SyncToken[UInt[32]]] * 8)
            def load(task):
                addr0 = task.const(0, 1)
                a00 = a[0].read(addr0)
                a01 = a[1].read(addr0)
                a10 = a[2].read(addr0)
                a11 = a[3].read(addr0)
                b00 = b[0].read(addr0)
                b01 = b[1].read(addr0)
                b10 = b[2].read(addr0)
                b11 = b[3].read(addr0)
                return (
                    task.create_token(a00, UInt[32]),
                    task.create_token(a01, UInt[32]),
                    task.create_token(a10, UInt[32]),
                    task.create_token(a11, UInt[32]),
                    task.create_token(b00, UInt[32]),
                    task.create_token(b01, UInt[32]),
                    task.create_token(b10, UInt[32]),
                    task.create_token(b11, UInt[32]),
                )

            (
                tok_a00,
                tok_a01,
                tok_a10,
                tok_a11,
                tok_b00,
                tok_b01,
                tok_b10,
                tok_b11,
            ) = load._cmt2_tokens

            @df.task(
                tokens_in=[
                    tok_a00,
                    tok_a01,
                    tok_a10,
                    tok_a11,
                    tok_b00,
                    tok_b01,
                    tok_b10,
                    tok_b11,
                ],
                timing=(2, 3),
                tokens_out=[SyncToken[UInt[32]]] * 4,
            )
            def compute(task):
                va00 = task.token_data(tok_a00)
                va01 = task.token_data(tok_a01)
                va10 = task.token_data(tok_a10)
                va11 = task.token_data(tok_a11)
                vb00 = task.token_data(tok_b00)
                vb01 = task.token_data(tok_b01)
                vb10 = task.token_data(tok_b10)
                vb11 = task.token_data(tok_b11)

                c00 = task.bits(task.add(task.mul(va00, vb00), task.mul(va01, vb10)), 31, 0)
                c01 = task.bits(task.add(task.mul(va00, vb01), task.mul(va01, vb11)), 31, 0)
                c10 = task.bits(task.add(task.mul(va10, vb00), task.mul(va11, vb10)), 31, 0)
                c11 = task.bits(task.add(task.mul(va10, vb01), task.mul(va11, vb11)), 31, 0)

                return (
                    task.create_token(c00, UInt[32]),
                    task.create_token(c01, UInt[32]),
                    task.create_token(c10, UInt[32]),
                    task.create_token(c11, UInt[32]),
                )

            (tok_c00, tok_c01, tok_c10, tok_c11) = compute._cmt2_tokens

            @df.task(tokens_in=[tok_go, tok_c00, tok_c01, tok_c10, tok_c11], timing=(3, 4))
            def store(task) -> tuple[UInt[32], UInt[32], UInt[32], UInt[32]]:
                out00 = task.token_data(tok_c00)
                out01 = task.token_data(tok_c01)
                out10 = task.token_data(tok_c10)
                out11 = task.token_data(tok_c11)

                addr_c0 = task.const(0, 2)
                c[0].write(out00, addr_c0)
                c[1].write(out01, addr_c0)
                c[2].write(out10, addr_c0)
                c[3].write(out11, addr_c0)

                return (out00, out01, out10, out11)

    return circuit


def create_testbench(_: Circuit) -> Testbench:
    tb = Testbench(_, auto_debug_ports=False)

    # Input tile values (A: 2x2 at rows 0..1, cols 0..1; B: 2x2 at rows 0..1, cols 0..1)
    a00, a01, a10, a11 = 1, 2, 3, 4
    b00, b01, b10, b11 = 5, 6, 7, 8

    # Expected outputs:
    # c00 = 1*5 + 2*7 = 19
    # c01 = 1*6 + 2*8 = 22
    # c10 = 3*5 + 4*7 = 43
    # c11 = 3*6 + 4*8 = 50
    exp = (19, 22, 43, 50)

    PIPELINE_LATENCY = 4

    with tb.sequence("test_gemm_tile") as seq:
        seq.reset(5)

        # Preload A banks (bank_addr=0)
        seq.wait_condition("dut->a_write_b0_ready", timeout=10)
        seq.drive("a_write_b0_addr", 0)
        seq.drive("a_write_b0_data", a00)
        seq.drive("a_write_b0_enable", 1)
        seq.wait(1)
        seq.drive("a_write_b0_enable", 0)

        seq.wait_condition("dut->a_write_b1_ready", timeout=10)
        seq.drive("a_write_b1_addr", 0)
        seq.drive("a_write_b1_data", a01)
        seq.drive("a_write_b1_enable", 1)
        seq.wait(1)
        seq.drive("a_write_b1_enable", 0)

        seq.wait_condition("dut->a_write_b2_ready", timeout=10)
        seq.drive("a_write_b2_addr", 0)
        seq.drive("a_write_b2_data", a10)
        seq.drive("a_write_b2_enable", 1)
        seq.wait(1)
        seq.drive("a_write_b2_enable", 0)

        seq.wait_condition("dut->a_write_b3_ready", timeout=10)
        seq.drive("a_write_b3_addr", 0)
        seq.drive("a_write_b3_data", a11)
        seq.drive("a_write_b3_enable", 1)
        seq.wait(1)
        seq.drive("a_write_b3_enable", 0)

        # Preload B banks (bank_addr=0)
        seq.wait_condition("dut->b_write_b0_ready", timeout=10)
        seq.drive("b_write_b0_addr", 0)
        seq.drive("b_write_b0_data", b00)
        seq.drive("b_write_b0_enable", 1)
        seq.wait(1)
        seq.drive("b_write_b0_enable", 0)

        seq.wait_condition("dut->b_write_b1_ready", timeout=10)
        seq.drive("b_write_b1_addr", 0)
        seq.drive("b_write_b1_data", b01)
        seq.drive("b_write_b1_enable", 1)
        seq.wait(1)
        seq.drive("b_write_b1_enable", 0)

        seq.wait_condition("dut->b_write_b2_ready", timeout=10)
        seq.drive("b_write_b2_addr", 0)
        seq.drive("b_write_b2_data", b10)
        seq.drive("b_write_b2_enable", 1)
        seq.wait(1)
        seq.drive("b_write_b2_enable", 0)

        seq.wait_condition("dut->b_write_b3_ready", timeout=10)
        seq.drive("b_write_b3_addr", 0)
        seq.drive("b_write_b3_data", b11)
        seq.drive("b_write_b3_enable", 1)
        seq.wait(1)
        seq.drive("b_write_b3_enable", 0)

        # Wait through pipeline
        seq.wait(PIPELINE_LATENCY + 2)

        # Check returned outputs (final task is named "store")
        seq.expect("gemm_store_result_0", exp[0], "C00 mismatch")
        seq.expect("gemm_store_result_1", exp[1], "C01 mismatch")
        seq.expect("gemm_store_result_2", exp[2], "C10 mismatch")
        seq.expect("gemm_store_result_3", exp[3], "C11 mismatch")

        # Allow memory writes to commit, then check C banks
        seq.wait(2)
        seq.expect("get_c_b0_res0", exp[0], "C bank0 mismatch")
        seq.expect("get_c_b1_res0", exp[1], "C bank1 mismatch")
        seq.expect("get_c_b2_res0", exp[2], "C bank2 mismatch")
        seq.expect("get_c_b3_res0", exp[3], "C bank3 mismatch")

        seq.print("GEMM tile outputs:", "gemm_store_result_0")
        seq.print("PASSED: banked GEMM tile test")

    return tb


def main() -> int:
    print("=" * 70)
    print("Banked GEMM Tile (Dataflow Tasks + ModuleLibrary Memories)")
    print("=" * 70)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_banked_gemm"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    circuit = create_banked_gemm_circuit()
    tb = create_testbench(circuit)

    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)
    ws.generate_with_testbench(tb)

    if not ws.build():
        print("Build failed!")
        return 1

    success, output = ws.run()
    print(output)
    if not success:
        print("Simulation failed!")
        return 1

    print(f"Waveforms available at: {sim_dir / 'waves' / 'BankedGemmTile.vcd'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
