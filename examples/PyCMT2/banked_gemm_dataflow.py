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
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/banked_gemm_dataflow.py
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Memory, clear_stl_registry
from circt.pycmt2.testbench import Testbench


def create_banked_gemm_circuit() -> Circuit:
    clear_stl_registry()

    circuit = Circuit("BankedGemmTileSim")

    mem_a = Memory.create_1r1w_async(circuit, data_width=32, addr_width=1, depth=2)
    mem_b = Memory.create_1r1w_async(circuit, data_width=32, addr_width=1, depth=2)
    mem_c = Memory.create_1r1w_async(circuit, data_width=32, addr_width=2, depth=4)

    with circuit.module("BankedGemmTile") as m:
        clk = m.clock()
        rst = m.reset()

        # 4 banks for each matrix
        a = [m.instance(mem_a, f"a_b{i}", clk=clk, rst=rst) for i in range(4)]
        b = [m.instance(mem_b, f"b_b{i}", clk=clk, rst=rst) for i in range(4)]
        c = [m.instance(mem_c, f"c_b{i}", clk=clk, rst=rst) for i in range(4)]

        # ---------------------------------------------------------------------
        # Host-facing write methods for A and B banks (preload).
        # ---------------------------------------------------------------------
        for i in range(4):
            with m.method(f"a_write_b{i}", args=[("addr", UInt(1)), ("data", UInt(32))]) as meth:
                with meth.guard() as g:
                    g.always()
                with meth.body() as body:
                    body.call(a[i], "write", body.arg("data"), body.arg("addr"))

            with m.method(f"b_write_b{i}", args=[("addr", UInt(1)), ("data", UInt(32))]) as meth:
                with meth.guard() as g:
                    g.always()
                with meth.body() as body:
                    body.call(b[i], "write", body.arg("data"), body.arg("addr"))

        # ---------------------------------------------------------------------
        # Host-facing value methods to observe C bank contents for tile (0,0).
        # Tile (0,0) maps to bank_addr=0 for all 4 banks under the mapping above.
        # ---------------------------------------------------------------------
        for i in range(4):
            with m.value(f"get_c_b{i}", returns=[UInt(32)]) as val:
                with val.guard() as g:
                    g.always()
                with val.body() as body:
                    data = body.call(c[i], "read", body.const(0, 2))
                    body.returns(data)

        # ---------------------------------------------------------------------
        # Dataflow: load -> compute (unrolled) -> store
        # ---------------------------------------------------------------------
        from circt.pycmt2.types import SyncToken

        with m.dataflow(
            "gemm",
            args=[],
            returns=[UInt(32), UInt(32), UInt(32), UInt(32)],
        ) as df:
            # Source token: "go" pulse every cycle (streaming pipeline).
            with df.task("go", timing=(0, 1), tokens_out=[SyncToken(UInt(1))]) as task:
                tok_go = task.create_token(task.const(1, 1), UInt(1))
                task.yield_tokens(tok_go)

            # Load tile (rows 0..1, cols 0..1) for A (M=4,K=2) and B (K=2,N=4).
            # Using 4-bank layouts so all 4 elements per operand can be read in parallel.
            with df.task(
                "load",
                tokens_in=[tok_go],
                timing=(1, 2),
                tokens_out=[SyncToken(UInt(32))] * 8,
            ) as task:
                # A tile elements (bank_addr=0 for rows 0..1):
                a00 = task.call(a[0], "read", task.const(0, 1))
                a01 = task.call(a[1], "read", task.const(0, 1))
                a10 = task.call(a[2], "read", task.const(0, 1))
                a11 = task.call(a[3], "read", task.const(0, 1))

                # B tile elements (bank_addr=0 for cols 0..1):
                b00 = task.call(b[0], "read", task.const(0, 1))
                b01 = task.call(b[1], "read", task.const(0, 1))
                b10 = task.call(b[2], "read", task.const(0, 1))
                b11 = task.call(b[3], "read", task.const(0, 1))

                tok_a00 = task.create_token(a00, UInt(32))
                tok_a01 = task.create_token(a01, UInt(32))
                tok_a10 = task.create_token(a10, UInt(32))
                tok_a11 = task.create_token(a11, UInt(32))
                tok_b00 = task.create_token(b00, UInt(32))
                tok_b01 = task.create_token(b01, UInt(32))
                tok_b10 = task.create_token(b10, UInt(32))
                tok_b11 = task.create_token(b11, UInt(32))
                task.yield_tokens(
                    tok_a00,
                    tok_a01,
                    tok_a10,
                    tok_a11,
                    tok_b00,
                    tok_b01,
                    tok_b10,
                    tok_b11,
                )

            # Compute unrolled K=2 for a 2x2 output tile:
            # C00 = A00*B00 + A01*B10
            # C01 = A00*B01 + A01*B11
            # C10 = A10*B00 + A11*B10
            # C11 = A10*B01 + A11*B11
            with df.task(
                "compute",
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
                tokens_out=[SyncToken(UInt(32))] * 4,
            ) as task:
                va00 = task.token_data(tok_a00)
                va01 = task.token_data(tok_a01)
                va10 = task.token_data(tok_a10)
                va11 = task.token_data(tok_a11)
                vb00 = task.token_data(tok_b00)
                vb01 = task.token_data(tok_b01)
                vb10 = task.token_data(tok_b10)
                vb11 = task.token_data(tok_b11)

                p00 = task.mul(va00, vb00)
                p01 = task.mul(va01, vb10)
                s00 = task.add(p00, p01)

                p10 = task.mul(va00, vb01)
                p11 = task.mul(va01, vb11)
                s01 = task.add(p10, p11)

                p20 = task.mul(va10, vb00)
                p21 = task.mul(va11, vb10)
                s10 = task.add(p20, p21)

                p30 = task.mul(va10, vb01)
                p31 = task.mul(va11, vb11)
                s11 = task.add(p30, p31)

                c00 = task.bits(s00, 31, 0)
                c01 = task.bits(s01, 31, 0)
                c10 = task.bits(s10, 31, 0)
                c11 = task.bits(s11, 31, 0)

                tok_c00 = task.create_token(c00, UInt(32))
                tok_c01 = task.create_token(c01, UInt(32))
                tok_c10 = task.create_token(c10, UInt(32))
                tok_c11 = task.create_token(c11, UInt(32))
                task.yield_tokens(tok_c00, tok_c01, tok_c10, tok_c11)

            # Store to C banks (tile (0,0) => bank_addr=0 for all 4 banks) and return outputs.
            with df.task(
                "store",
                tokens_in=[tok_go, tok_c00, tok_c01, tok_c10, tok_c11],
                timing=(3, 4),
            ) as task:
                out00 = task.token_data(tok_c00)
                out01 = task.token_data(tok_c01)
                out10 = task.token_data(tok_c10)
                out11 = task.token_data(tok_c11)

                task.call(c[0], "write", out00, task.const(0, 2))
                task.call(c[1], "write", out01, task.const(0, 2))
                task.call(c[2], "write", out10, task.const(0, 2))
                task.call(c[3], "write", out11, task.const(0, 2))

                task.return_values(out00, out01, out10, out11)

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
