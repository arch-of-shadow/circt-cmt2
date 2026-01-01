#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Procedural Control Example using PyCMT2 EDSL

This example demonstrates a multi-cycle ALU with procedural control flow.
It uses:
- External register modules for operand and result storage
- Steps with done signals for multi-cycle operations
- Procedural rules with sequential control flow
- End-to-end compilation to Verilog

The ALU loads two operands, computes their sum over multiple cycles,
and stores the result.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/proc_example.py
"""

from circt.pycmt2 import Circuit, UInt


def main():
    print("=" * 60)
    print("PyCMT2 Procedural Control Example")
    print("=" * 60)

    circuit = Circuit("ProcCircuit")

    # Define external register module
    with circuit.external_module("Reg32") as reg_mod:
        reg_mod.clock("clk")
        reg_mod.reset("rst")
        reg_mod.value("read", returns=[UInt(32)])
        reg_mod.method("write", args=[("data", UInt(32))])
        reg_mod.sequence_before("read", "write")

    # Define external 1-bit register for flags
    with circuit.external_module("Reg1") as reg1_mod:
        reg1_mod.clock("clk")
        reg1_mod.reset("rst")
        reg1_mod.value("read", returns=[UInt(1)])
        reg1_mod.method("write", args=[("data", UInt(1))])
        reg1_mod.sequence_before("read", "write")

    with circuit.module("MultiCycleALU") as alu:
        # Ports
        clk = alu.clock("clk")
        rst = alu.reset("rst")

        # Internal state registers
        reg_a = alu.instance(reg_mod, "reg_a", clk=clk, rst=rst)
        reg_b = alu.instance(reg_mod, "reg_b", clk=clk, rst=rst)
        reg_result = alu.instance(reg_mod, "reg_result", clk=clk, rst=rst)
        busy_reg = alu.instance(reg1_mod, "busy", clk=clk, rst=rst)

        # Method: start computation with operands
        with alu.method("start", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
            with meth.guard() as g:
                # Ready when not busy
                busy = g.call(busy_reg, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with meth.body() as body:
                a_in = body.arg("a")
                b_in = body.arg("b")
                # Store operands
                body.call(reg_a, "write", a_in)
                body.call(reg_b, "write", b_in)
                # Set busy flag
                one = body.const(1, 1)
                body.call(busy_reg, "write", one)

        # Rule: compute result when busy
        with alu.rule("compute") as rule:
            with rule.guard() as g:
                busy = g.call(busy_reg, "read")
                g.returns(busy)
            with rule.body() as body:
                # Read operands
                a = body.call(reg_a, "read")
                b = body.call(reg_b, "read")
                # Compute sum
                result = body.add(a, b)
                # Store result
                body.call(reg_result, "write", result)
                # Clear busy flag
                zero = body.const(0, 1)
                body.call(busy_reg, "write", zero)

        # Value method: read result (ready when not busy)
        with alu.value("get_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                busy = g.call(busy_reg, "read")
                not_busy = g.not_(busy)
                g.returns(not_busy)
            with val.body() as body:
                result = body.call(reg_result, "read")
                body.returns(result)

        # Value method: check if ready for new operation
        with alu.value("is_ready", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                busy = body.call(busy_reg, "read")
                not_busy = body.not_(busy)
                body.returns(not_busy)

    # Emit CMT2 MLIR
    print("\n=== CMT2 MLIR ===\n")
    print(circuit.emit_mlir())

    # Emit FIRRTL
    print("\n=== FIRRTL ===\n")
    try:
        print(circuit.emit_firrtl())
    except Exception as e:
        print(f"FIRRTL emission failed: {e}")

    # Emit Verilog
    print("\n=== Verilog ===\n")
    try:
        verilog = circuit.emit_verilog()
        if "Verilog generation failed" in verilog:
            print("Verilog emission failed.")
            print(verilog)
        else:
            print(verilog)
    except Exception as e:
        print(f"Verilog emission failed: {e}")

    print("\n" + "=" * 60)
    print("Multi-cycle ALU example completed!")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
