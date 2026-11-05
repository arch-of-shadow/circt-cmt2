#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Value-with-arguments example using PyCMT2 EDSL.

This example validates that:
- `cmt2.value` can take arguments in module definitions.
- `cmt2.value` can take arguments in interface definitions.
- Calls can pass operands to value methods.

It is intentionally MLIR-only (no simulation) to keep the test fast.

Usage:
  cd circt-cmt2/build
  PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/value_args.py
"""

from __future__ import annotations

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import clear_stl_registry


def lookup(idx: UInt[4]) -> UInt[8]:
    ...


def main() -> int:
    clear_stl_registry()
    c = Circuit("ValueArgs")

    with c.interface() as Reader:
        Reader.value_sig(lookup)

    with c.module("Child") as child:
        with child.value("add1", args=[("x", UInt(8))], returns=[UInt(8)]) as v:
            with v.guard() as g:
                g.always()
            with v.body() as b:
                x = b.arg("x")
                one = b.const(1, 8)
                y = b.add(x, one)
                b.returns(b.truncate(y, 8))

    with c.module("Top") as m:
        clk = m.clock()
        rst = m.reset()
        _ = (clk, rst)  # unused in this MLIR-only example

        child_inst = m.instance(child, "child")
        reader = m.interface_decl("reader", Reader)

        with m.rule("drive") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                x = b.const(7, 8)
                idx = b.const(3, 4)
                _ = b.call(child_inst, "add1", x)
                _ = b.call(reader, "lookup", idx)

    mlir = c.emit_mlir()
    assert "cmt2.value @add1 (" in mlir, "expected module value @add1 to have arguments"
    assert "cmt2.value @lookup (" in mlir, "expected interface value @lookup to have arguments"
    assert "cmt2.call @child @add1" in mlir, "expected call to value-with-args @add1"
    assert "cmt2.call @reader @lookup" in mlir, "expected call to interface value-with-args @lookup"

    print("PASSED: All tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
