#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Counter Example with External Register Module using PyCMT2 EDSL

This example demonstrates a complete counter using:
- External module bindings for a 32-bit register
- A rule that increments the counter each cycle
- A value method to read the current count
- End-to-end compilation to Verilog with simulation support

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/counter_reg_example.py
"""

from circt.pycmt2 import Circuit, UInt


def main():
    print("=" * 60)
    print("PyCMT2 Counter with Register Example")
    print("=" * 60)

    circuit = Circuit("CounterReg")

    # Define external register module
    with circuit.external_module("Reg32") as reg_mod:
        reg_mod.clock("clk")
        reg_mod.reset("rst")
        reg_mod.value("read", returns=[UInt(32)])
        reg_mod.method("write", args=[("data", UInt(32))])
        reg_mod.sequence_before("read", "write")

    with circuit.module("Counter") as m:
        # Ports
        clk = m.clock()
        rst = m.reset()
        enable = m.input("enable", UInt(1))

        # State: counter register
        count_reg = m.instance(reg_mod, "count", clk=clk, rst=rst)

        # Method: increment counter (called when enabled)
        with m.method("increment", args=[]) as meth:
            with meth.guard() as g:
                # Always ready to increment
                g.always()
            with meth.body() as body:
                # Read current value
                current = body.call(count_reg, "read")
                # Compute next value: current + 1
                one = body.const(1, 32)
                next_val = body.add(current, one)
                # Write back
                body.call(count_reg, "write", next_val)

        # Value method: read current count
        with m.value("read_count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(count_reg, "read")
                body.returns(result)

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
    print("Counter example completed!")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
