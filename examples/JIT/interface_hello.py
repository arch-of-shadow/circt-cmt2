#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Interface feature E2E example (JIT stacked on PyCMT2).

This example validates the Cmt2 interface stack end-to-end:
  - Circuit-level interfaces (`cmt2.interface`)
  - Module interface declarations (`cmt2.interface.decl`)
  - Module interface definitions (`cmt2.interface.def`)
  - Instance interface bindings (`cmt2.instance ... with [...]`)
  - Calls through interface decls (lowered to FIRRTL ports)

It is a Python/JIT reimplementation of the key interface patterns in
`examples/ECMT2/hello_v3.cpp`.

Usage:
  PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
    python3 examples/JIT/interface_hello.py
"""

from __future__ import annotations

import shutil
from pathlib import Path

import cmt2.jit as jit
from cmt2.stl import Reg, clear_stl_registry

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def getData() -> UInt[32]:
    ...


def store(data: UInt[32]) -> None:
    ...


@jit.elaborate
def build_circuit() -> Circuit:
    clear_stl_registry()

    c = Circuit("InterfaceHello")

    # Circuit-level interface definitions (typed signatures).
    with c.interface("Reader") as i:
        i.value_sig(getData)

    with c.interface("Writer") as i:
        i.method_sig(store)

    reg32 = Reg.create(c, 32, init=0)

    # Child module: uses an interface decl to read data, plus a local reg.
    with jit.module(c, "child") as m:
        clk = m.clock()
        rst = m.reset()

        r = m.instance(reg32, "r", clk=clk, rst=rst)
        reader = m.interface_decl("reader", "Reader")

        @jit.method(m)
        def setMethod(ctx, v: UInt[32]) -> UInt[32]:
            with ctx.guard:
                ctx.always()

            with ctx.body:
                reader_data = reader.getData
                current = r.read

                sum1 = ctx.truncate(reader_data + v, 32)
                sum2 = ctx.truncate(current + sum1, 32)

                r.next = sum2
                ctx.returns(sum2)

    # Top module: defines an interface def mapping x.read -> Reader.getData,
    # binds it into the child instance, and calls a Writer interface from a rule.
    with jit.module(c, "top") as m:
        clk = m.clock()
        rst = m.reset()

        x = m.instance(reg32, "x", clk=clk, rst=rst)
        writer = m.interface_decl("writer", "Writer")

        read_x = m.interface_def("readX", "Reader")
        read_x.bind(x, x._instance.read, getData)

        child_mod = c._modules["child"]
        child_reader = child_mod._interface_decls["reader"]
        child = m.instance(
            child_mod,
            "c",
            clk=clk,
            rst=rst,
            interface_bindings={read_x: child_reader},
        )

        @jit.method(m)
        def callChild(ctx, v: UInt[32]) -> UInt[32]:
            with ctx.guard:
                ctx.always()

            with ctx.body:
                res = child.setMethod(v)
                ctx.returns(res)

        @jit.rule(m)
        def incr(ctx):
            with ctx.guard:
                ctx.always()

            with ctx.body:
                one = ctx.const(1, 32)
                new_val = ctx.truncate(x.read + one, 32)
                x.next = new_val
                writer.store(new_val)

    return c


def main() -> None:
    circuit = build_circuit()

    out_dir = Path(__file__).parent / "sim_interface_hello"
    if out_dir.exists():
        shutil.rmtree(out_dir)

    ws = SimulationWorkspace(circuit, out_dir, debug_ports=False)
    tb = Testbench(circuit)
    writer = tb.interface_decl("writer")

    with tb.sequence("basic") as seq:
        # Establish deterministic defaults *before* reset ticks.
        seq.drive("writer_store_ready", 0)
        seq.drive("callChild_enable", 0)
        seq.drive("callChild_v", 0)

        seq.reset(5)
        seq.wait(1)

        # callChild(v=10) with x=0 and child.r=0 => returns 10, updates child.r=10
        seq.drive("callChild_v", 10)
        seq.drive("callChild_enable", 1)
        seq.eval()
        seq.expect("callChild_res0", 10)
        seq.wait(1)
        seq.drive("callChild_enable", 0)

        # Enable `incr` by making Writer ready; check three outgoing interface calls.
        seq.call_interface(writer, store, 1, ready=1)
        seq.call_interface(writer, store, 2, ready=1)
        seq.call_interface(writer, store, 3, ready=1)

        # Disable again and callChild(v=1) with x=3 and child.r=10 => 10 + (3 + 1) = 14
        seq.drive("writer_store_ready", 0)
        seq.wait(1)
        seq.drive("callChild_v", 1)
        seq.drive("callChild_enable", 1)
        seq.eval()
        seq.expect("callChild_res0", 14)
        seq.wait(1)
        seq.drive("callChild_enable", 0)

    ws.generate_with_testbench(tb)
    ok, out = ws.build_and_run()
    if not ok:
        raise RuntimeError(out)

    print("E2E Simulation PASSED!")


if __name__ == "__main__":
    main()
