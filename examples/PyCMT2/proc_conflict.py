#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Procedural Conflict Test using PyCMT2 EDSL

This example demonstrates conflict resolution between a proc.rule and a regular rule:
- proc.rule @incr_loop: Continuously increments a counter register each cycle
- rule @div_by_2: When counter == 4, divides counter by 2

The regular rule has higher scheduling priority (via precedence), so when
counter reaches 4, div_by_2 fires and blocks incr_loop for that cycle.

Expected behavior:
  Cycle 0: reset, counter = 0
  Cycle 1: incr_loop fires, counter = 1
  Cycle 2: incr_loop fires, counter = 2
  Cycle 3: incr_loop fires, counter = 3
  Cycle 4: incr_loop fires, counter = 4
  Cycle 5: div_by_2 fires (higher priority), counter = 2
  Cycle 6: incr_loop fires, counter = 3
  Cycle 7: incr_loop fires, counter = 4
  Cycle 8: div_by_2 fires, counter = 2
  ... (pattern repeats: 2 -> 3 -> 4 -> 2 -> 3 -> 4 ...)

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/proc_conflict_example.py

    # Run with interpreter:
    cmt2-dbg /tmp/proc_conflict_pycmt2.mlir < ../examples/PyCMT2/proc_conflict_script.txt
"""

import sys
import os
from pathlib import Path

from circt.pycmt2 import Circuit, UInt


def main():
    print("=" * 60)
    print("PyCMT2 Procedural Conflict Test Example")
    print("=" * 60)

    circuit = Circuit("ProcConflict")

    # Define external 32-bit register module
    with circuit.external_module("Reg32") as reg_mod:
        reg_mod.clock("clk")
        reg_mod.reset("rst")
        reg_mod.value("read", returns=[UInt(32)])
        reg_mod.method("write", args=[("data", UInt(32))])
        # read must happen before write in same cycle
        reg_mod.sequence_before("read", "write")
        # write conflicts with itself (can't write twice in same cycle)
        reg_mod.conflict("write", "write")
        # read doesn't conflict with itself (can read multiple times)
        reg_mod.conflict_free("read", "read")

    with circuit.module("ProcConflictTest") as mod:
        # Ports
        clk = mod.clock("clk")
        rst = mod.reset("rst")

        # Counter register instance
        counter = mod.instance(reg_mod, "counter", clk=clk, rst=rst)

        # Procedural step: increment register by 1
        with mod.step("incr_step") as step:
            # Read current value
            val = step.call(counter, "read")
            # Add 1
            one = step.const(1, 32)
            sum_val = step.add(val, one)
            # Truncate to 32 bits (add produces 33-bit result)
            new_val = step.bits(sum_val, 31, 0)
            # Write back
            step.call(counter, "write", new_val)
            # Always done after one cycle
            done = step.const(1, 1)
            step.done(done)

        # Procedural rule: continuously increment (infinite loop)
        with mod.proc_rule("incr_loop") as incr_rule:
            with incr_rule.guard() as g:
                # Always enabled (infinite loop)
                g.always()
            with incr_rule.control() as ctrl:
                # Enable the increment step
                ctrl.enable(step.ref())

        # Regular rule: when counter == 4, divide by 2
        with mod.rule("div_by_2") as div_rule:
            with div_rule.guard() as g:
                val = g.call(counter, "read")
                four = g.const(4, 32)
                is_4 = g.eq(val, four)
                g.returns(is_4)
            with div_rule.body() as body:
                val = body.call(counter, "read")
                # Divide by 2 = shift right by 1
                shifted = body.shr(val, 1)
                # Pad back to 32 bits
                new_val = body.pad(shifted, 32)
                body.call(counter, "write", new_val)

        # Set precedence: div_by_2 has higher priority than incr_loop
        # When both are enabled, div_by_2 wins and blocks incr_loop
        mod.precedence(div_rule.ref(), incr_rule.ref())

    # Emit CMT2 MLIR
    print("\n=== CMT2 MLIR ===\n")
    mlir_text = circuit.emit_mlir()
    print(mlir_text)

    # Write to file for interpreter testing
    output_path = Path("/tmp/proc_conflict_pycmt2.mlir")
    output_path.write_text(mlir_text)
    print(f"\nMLIR written to: {output_path}")

    # Try to emit FIRRTL (after running lowering passes)
    print("\n=== FIRRTL (after lowering) ===\n")
    try:
        firrtl_text = circuit.emit_firrtl()
        print(firrtl_text)

        # Write lowered MLIR for interpreter
        lowered_path = Path("/tmp/proc_conflict_pycmt2_lowered.mlir")
        lowered_path.write_text(firrtl_text)
        print(f"\nLowered MLIR written to: {lowered_path}")
    except Exception as e:
        print(f"FIRRTL emission failed: {e}")
        import traceback
        traceback.print_exc()

    # Create interpreter script
    script = """\
# PyCMT2 Proc Conflict Test Script
# Tests conflict resolution between proc.rule and regular rule

# Initialize and select circuit
init ProcConflictTest

# Reset circuit
reset

# Check initial state
print "Cycle 0: After reset"
registers

# Run 10 cycles to observe behavior
# Expected: counter oscillates 0 -> 1 -> 2 -> 3 -> 4 -> 2 -> 3 -> 4 -> 2 ...
step
print "Cycle 1:"
registers

step
print "Cycle 2:"
registers

step
print "Cycle 3:"
registers

step
print "Cycle 4:"
registers

step
print "Cycle 5: (div_by_2 should fire, counter = 2)"
registers

step
print "Cycle 6:"
registers

step
print "Cycle 7:"
registers

step
print "Cycle 8: (div_by_2 should fire, counter = 2)"
registers

step
print "Cycle 9:"
registers

quit
"""

    script_path = Path("/home/uvxiao/circt-cmt2/examples/PyCMT2/proc_conflict_script.txt")
    script_path.write_text(script)
    print(f"\nInterpreter script written to: {script_path}")

    print("\n" + "=" * 60)
    print("To run with interpreter:")
    print(f"  cd {os.path.dirname(script_path)}")
    print(f"  ../../build/bin/cmt2-dbg /tmp/proc_conflict_pycmt2.mlir < proc_conflict_script.txt")
    print("Or for lowered IR:")
    print(f"  ../../build/bin/cmt2-dbg /tmp/proc_conflict_pycmt2_lowered.mlir < proc_conflict_script.txt")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
