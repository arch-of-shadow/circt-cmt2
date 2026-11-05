#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""JIT counter example (builds a normal PyCMT2 Circuit).

Usage (from repo root):
  PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
    python3 examples/JIT/counter.py --width 16 --emit mlir
"""

from __future__ import annotations

import argparse

import cmt2.jit as jit
from circt.pycmt2 import Circuit, UInt
from cmt2.stl import Reg


@jit.elaborate
def counter(width: int = 32) -> Circuit:
    circuit = Circuit("JitCounter")

    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()

        count = m.instance(Reg.create(circuit, width, init=0), clk=clk, rst=rst)

        @jit.rule(m)
        def increment(r):
            with r.guard:
                r.always()

            with r.body:
                count.next = count.read + 1

        @jit.value(m)
        def get_count(r) -> UInt[width]:
            with r.guard:
                r.always()

            with r.body:
                r.returns(count.read)

    return circuit


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--width", type=int, default=32)
    parser.add_argument("--emit", choices=["mlir", "verilog"], default="mlir")
    args = parser.parse_args()

    c = counter(width=args.width)
    if args.emit == "mlir":
        print(c.emit_mlir())
    else:
        print(c.emit_verilog())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
