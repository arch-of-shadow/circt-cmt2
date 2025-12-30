#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Procedural Control Example using PyCMT2 EDSL

This example demonstrates procedural control flow constructs in PyCMT2,
including groups, sequences, parallel execution, and loops.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/proc_example.py
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
    print("PyCMT2 Procedural Control Example")
    print("=" * 60)

    # Create a circuit for multi-cycle operations
    circuit = Circuit("ProcCircuit")

    with circuit.module("MultiCycleALU") as alu:
        # Add ports
        clk = alu.clock("clk")
        rst = alu.reset("rst")
        start = alu.input("start", UInt(1))
        op_a = alu.input("op_a", UInt(32))
        op_b = alu.input("op_b", UInt(32))

        # Define groups for different phases of computation

        # Group 1: Load operands
        with alu.group("load_operands") as load:
            # In a real implementation, this would load values to registers
            load.done(load.const(1, 1))

        # Group 2: Compute (static latency)
        with alu.static_group(4, "compute") as compute:
            # In a real implementation, this would perform computation
            # Static group has known latency, no done signal needed
            pass

        # Group 3: Store result
        with alu.group("store_result") as store:
            # In a real implementation, this would store the result
            store.done(store.const(1, 1))

        # Procedural rule for multi-cycle operation
        with alu.proc_rule("execute") as rule:
            with rule.guard() as g:
                # Guard: fire when start signal is high
                g.returns(start)

            with rule.control() as ctrl:
                # Sequential control flow
                with ctrl.seq() as seq:
                    # First, load operands
                    seq.enable(load.ref())

                    # Then compute (4 cycles)
                    seq.enable(compute.ref())

                    # Finally, store result
                    seq.enable(store.ref())

    # Print the generated MLIR IR
    print("\n=== Generated CMT2 MLIR IR with Procedural Control ===\n")
    print(circuit.emit_mlir())

    print("\n" + "=" * 60)
    print("Multi-cycle ALU module created successfully!")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
