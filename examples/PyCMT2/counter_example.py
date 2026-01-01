#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Counter Example using PyCMT2 EDSL

This example demonstrates a simple counter module using the PyCMT2 embedded DSL.
It shows:
- Module creation with clock/reset ports
- Rule definitions with guards and bodies
- Value methods for reading state
- End-to-end compilation to MLIR, FIRRTL, and Verilog

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/counter_example.py
"""

from circt.pycmt2 import Circuit, UInt


def main():
    print("=" * 60)
    print("PyCMT2 Counter Example")
    print("=" * 60)

    # Create circuit
    circuit = Circuit("Counter")

    with circuit.module("Counter") as m:
        # Ports
        clk = m.clock()
        rst = m.reset()
        enable = m.input("enable", UInt(1))

        # Increment rule - always fires (demonstrates rule structure)
        # Note: Using g.always() because CMT2 rules are IsolatedFromAbove
        # and can't directly access module ports. A real counter would
        # need to use a register interface method call in the guard.
        with m.rule("increment") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                # Would increment register here
                pass

        # Read value method - always ready
        with m.value("read", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                b.returns(b.const(0, 32))

    # Emit CMT2 MLIR
    print("\n=== CMT2 MLIR ===\n")
    print(circuit.emit_mlir())

    # Try to emit FIRRTL (may fail if passes not available)
    print("\n=== FIRRTL (if available) ===\n")
    try:
        print(circuit.emit_firrtl())
    except Exception as e:
        print(f"FIRRTL emission not available: {e}")

    # Try to emit Verilog
    print("\n=== Verilog (if available) ===\n")
    try:
        verilog = circuit.emit_verilog()
        if "Verilog generation failed" in verilog:
            # This is an error message
            print("Verilog emission failed.")
            print(verilog)
        else:
            print(verilog)
    except Exception as e:
        print(f"Verilog emission not available: {e}")

    print("\n" + "=" * 60)
    print("Counter example completed!")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
