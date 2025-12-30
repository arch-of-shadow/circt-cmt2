#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Counter Example using PyCMT2 EDSL

This example demonstrates a simple counter module using the PyCMT2 embedded DSL.
It shows the full flow from Python JIT to MLIR IR generation.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/counter_example.py
"""

import sys
import os

# Add the pycmt2 package to path
build_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
python_packages = os.path.join(build_dir, "build/tools/circt/python_packages/circt_core")
if python_packages not in sys.path:
    sys.path.insert(0, python_packages)

from circt.pycmt2 import Circuit, UInt, SInt, Clock, Reset


def main():
    print("=" * 60)
    print("PyCMT2 Counter Example")
    print("=" * 60)

    # Create a circuit - name is inferred from variable assignment (JIT naming)
    counter_circuit = Circuit("CounterCircuit")

    # Create a counter module
    with counter_circuit.module("Counter") as counter:
        # Add clock and reset ports
        clk = counter.clock("clk")
        rst = counter.reset("rst")

        # Add an input enable signal
        enable = counter.input("enable", UInt(1))

        # Define a rule to increment the counter
        with counter.rule("increment") as rule:
            with rule.guard() as g:
                # Guard: always fire when enabled
                g.returns(enable)

            with rule.body() as b:
                # Body: would increment counter value
                # (In a full implementation, we'd call methods on a register instance)
                pass

        # Define a value method to read the counter
        with counter.value("read", returns=[UInt(32)]) as val:
            with val.guard() as g:
                # Guard: always ready to read
                g.always()

            with val.body() as b:
                # Return a constant for now (placeholder)
                result = b.const(0, 32)
                b.returns(result)

    # Print the generated MLIR IR
    print("\n=== Generated CMT2 MLIR IR ===\n")
    print(counter_circuit.emit_mlir())

    print("\n" + "=" * 60)
    print("Counter module created successfully!")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
