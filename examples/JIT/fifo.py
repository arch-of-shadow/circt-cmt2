#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT FIFO example (builds a normal PyCMT2 Circuit).

Usage (from repo root):
  PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
    python3 examples/JIT/fifo.py --width 8 --depth 2 --emit mlir
"""

from __future__ import annotations

import argparse

import cmt2.jit as jit
from circt.pycmt2 import Circuit, UInt
from cmt2.stl import FIFO, Reg


@jit.elaborate
def fifo_demo(width: int = 8, depth: int = 2) -> Circuit:
    circuit = Circuit("JitFifoDemo")

    with jit.module(circuit, "FifoDemo") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(FIFO.create(circuit, width, depth=depth), clk=clk, rst=rst)
        src = m.instance(Reg.create(circuit, width, init=0), clk=clk, rst=rst)
        last = m.instance(Reg.create(circuit, width, init=0), clk=clk, rst=rst)

        @jit.rule(m)
        def produce(r):
            with r.guard:
                r.ready(r.not_(fifo.full))

            with r.body:
                val = src.read
                fifo.enq(val)
                src.next = val + 1

        @jit.rule(m)
        def consume(r):
            with r.guard:
                r.ready(r.not_(fifo.empty))

            with r.body:
                data = fifo.deq()
                last.next = data

        @jit.value(m)
        def get_last(r) -> UInt[width]:
            with r.guard:
                r.always()

            with r.body:
                r.returns(last.read)

    return circuit


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--width", type=int, default=8)
    parser.add_argument("--depth", type=int, default=2)
    parser.add_argument("--emit", choices=["mlir", "verilog"], default="mlir")
    args = parser.parse_args()

    c = fifo_demo(width=args.width, depth=args.depth)
    if args.emit == "mlir":
        print(c.emit_mlir())
    else:
        print(c.emit_verilog())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
