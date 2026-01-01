#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
GCD Example using PyCMT2 EDSL

This example demonstrates a multi-cycle GCD computation using:
- External module bindings for registers
- Rules for combinational logic
- Method calls for register read/write
- Action methods for loading input values

The GCD algorithm:
    while b != 0:
        if a > b:
            a = a - b
        else:
            b = b - a
    return a

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/gcd_example.py
"""

from circt.pycmt2 import Circuit, UInt


def main():
    print("=" * 60)
    print("PyCMT2 GCD Example")
    print("=" * 60)

    circuit = Circuit("GCD")

    # Define external register module for 32-bit values
    with circuit.external_module("Reg32") as reg_mod:
        reg_mod.clock("clk")
        reg_mod.reset("rst")
        reg_mod.value("read", returns=[UInt(32)])
        reg_mod.method("write", args=[("data", UInt(32))])
        reg_mod.sequence_before("read", "write")

    with circuit.module("GCD") as m:
        # Ports - only clock and reset are module-level inputs
        # Inputs a/b are passed via the load method
        clk = m.clock()
        rst = m.reset()

        # State registers
        reg_a = m.instance(reg_mod, "reg_a", clk=clk, rst=rst)
        reg_b = m.instance(reg_mod, "reg_b", clk=clk, rst=rst)

        # Method: load - Load input values (called externally)
        # This is an action method that accepts a and b values
        with m.method("load", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
            with meth.guard() as g:
                # Always ready to load
                g.always()
            with meth.body() as body:
                # Get arguments
                a_in = body.arg("a")
                b_in = body.arg("b")
                # Write input values to registers
                body.call(reg_a, "write", a_in)
                body.call(reg_b, "write", b_in)

        # Rule: compute - One iteration of GCD algorithm
        # Fires when b != 0
        with m.rule("compute") as rule:
            with rule.guard() as g:
                # Read current b value
                b_val = g.call(reg_b, "read")
                # Guard: b != 0
                zero = g.const(0, 32)
                cond = g.neq(b_val, zero)
                g.returns(cond)
            with rule.body() as body:
                # Read current values
                a_val = body.call(reg_a, "read")
                b_val = body.call(reg_b, "read")

                # if a > b: a = a - b else: b = b - a
                a_gt_b = body.gt(a_val, b_val)
                new_a = body.sub(a_val, b_val)
                new_b = body.sub(b_val, a_val)

                # Conditional write using mux
                # When a > b: write new_a to reg_a, keep reg_b
                # When a <= b: keep reg_a, write new_b to reg_b
                final_a = body.mux(a_gt_b, new_a, a_val)
                final_b = body.mux(a_gt_b, b_val, new_b)

                body.call(reg_a, "write", final_a)
                body.call(reg_b, "write", final_b)

        # Value method: result - Read the GCD result (reg_a when b == 0)
        with m.value("result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                # Ready when b == 0
                b_val = g.call(reg_b, "read")
                zero = g.const(0, 32)
                ready = g.eq(b_val, zero)
                g.returns(ready)
            with val.body() as body:
                # Return reg_a
                result = body.call(reg_a, "read")
                body.returns(result)

    # Emit CMT2 MLIR
    print("\n=== CMT2 MLIR ===\n")
    print(circuit.emit_mlir())

    # Try FIRRTL
    print("\n=== FIRRTL (if available) ===\n")
    try:
        print(circuit.emit_firrtl())
    except Exception as e:
        print(f"FIRRTL emission not available: {e}")

    # Try Verilog
    print("\n=== Verilog (if available) ===\n")
    try:
        verilog = circuit.emit_verilog()
        if "Verilog generation failed" in verilog:
            print("Verilog emission failed.")
            print(verilog)
        else:
            print(verilog)
    except Exception as e:
        print(f"Verilog emission not available: {e}")

    print("\n" + "=" * 60)
    print("GCD example completed!")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
