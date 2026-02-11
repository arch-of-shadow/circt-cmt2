#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Value-with-arguments example (JIT stacked on PyCMT2).

This example validates the JIT surface area for value arguments:
- `@jit.value` supports typed arguments and exposes them as `r.<arg>` inside guard/body.
- Calling a value-with-args uses function-call syntax: `inst.add1(x)`.
- Interface values may take arguments and are callable: `reader.lookup(i)`.

It is intentionally MLIR-only (no simulation) to keep the test fast.

Usage:
  cd circt-cmt2/build
  PYTHONPATH=tools/circt/python_packages/circt_core:../python \
    python3 ../examples/JIT/value_args.py
"""

from __future__ import annotations

import cmt2.jit as jit
from circt.pycmt2 import Circuit, UInt


def lookup(idx: UInt[4]) -> UInt[8]:
    ...


@jit.elaborate
def build() -> Circuit:
    c = Circuit("JitValueArgs")

    with c.interface() as Reader:
        Reader.value_sig(lookup)

    with jit.module(c, "Child") as child:
        @jit.value(child)
        def add1(r, x: UInt[8]) -> UInt[8]:
            with r.guard:
                r.always()
            with r.body:
                r.returns(x + 1)

    with jit.module(c, "Top") as m:
        clk = m.clock()
        rst = m.reset()
        _ = (clk, rst)

        child_inst = m.instance(child.module_def)
        reader = m.interface_decl(Reader)

        @jit.rule(m)
        def drive(r):
            with r.guard:
                r.always()
            with r.body:
                x = r.const(7, 8)
                idx = r.const(3, 4)
                _ = child_inst.add1(x)
                _ = reader.lookup(idx)

    return c


def main() -> int:
    c = build()
    mlir = c.emit_mlir()
    assert "cmt2.value @add1 (" in mlir, "expected module value @add1 to have arguments"
    assert "cmt2.value @lookup (" in mlir, "expected interface value @lookup to have arguments"
    assert "cmt2.call @child" in mlir and "@add1" in mlir, "expected call to value-with-args @add1"
    assert "cmt2.call @reader @lookup" in mlir, "expected call to interface value-with-args @lookup"

    print("PASSED: All tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
