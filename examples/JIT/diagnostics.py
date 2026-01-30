#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Diagnostics Example (JIT stacked on PyCMT2).

This example demonstrates the multi-level diagnostic system with:
- Error, warning, info, and debug levels
- Python source location tracking
- Diagnostic formatting with source context
- DiagnosticHandler for capturing diagnostics

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/diagnostics.py
"""

import cmt2.jit as jit

from circt.pycmt2 import (
    Circuit,
    UInt,
    DiagnosticLevel,
    Diagnostic,
    DiagnosticHandler,
    emit_error,
    emit_warning,
    emit_info,
    emit_debug,
    type_mismatch_error,
    undefined_reference_error,
    scheduling_conflict_warning,
    format_diagnostic_with_source,
    get_python_location,
    PythonLocation,
)
from circt.pycmt2.stl import Reg, clear_stl_registry


def demonstrate_diagnostic_levels():
    """Demonstrate the four diagnostic levels."""
    print("=" * 70)
    print("1. Diagnostic Levels")
    print("=" * 70)

    loc = get_python_location()

    # Error - fatal issues
    error = emit_error(loc, "method 'write' requires exactly 1 argument",
                       notes=["expected: write(data: UInt<32>)",
                              "got: write()"],
                       hint="add the missing data argument")
    print("\nERROR diagnostic:")
    print(error.format(color=True))

    # Warning - non-fatal issues
    warning = emit_warning(loc, "static step contains conflicting methods",
                           notes=["method 'read' conflicts with 'write'"],
                           hint="use dynamic step or explicit sequencing")
    print("\nWARNING diagnostic:")
    print(warning.format(color=True))

    # Info - informational messages
    info = emit_info(loc, "step promoted to static (3-cycle latency)",
                     notes=["no conflicting methods detected"])
    print("\nINFO diagnostic:")
    print(info.format(color=True))

    # Debug - detailed debugging info
    debug = emit_debug(loc, "evaluating guard for rule 'increment'")
    print("\nDEBUG diagnostic:")
    print(debug.format(color=True))


def demonstrate_source_location_tracking():
    """Demonstrate Python source location tracking."""
    print("\n" + "=" * 70)
    print("2. Source Location Tracking")
    print("=" * 70)

    # Capture location at definition point
    def create_rule_with_error():
        loc = get_python_location()  # Line ~75
        return emit_error(loc, "rule guard must return boolean",
                          notes=["guard returns UInt<32> instead of UInt<1>"],
                          hint="use comparison operator to produce boolean")

    diag = create_rule_with_error()
    print("\nDiagnostic with source location:")
    print(diag.format())

    # Show formatted with source context
    print("\nDiagnostic with source context:")
    print(format_diagnostic_with_source(diag, context_lines=2))


def demonstrate_type_errors():
    """Demonstrate type-related error diagnostics."""
    print("\n" + "=" * 70)
    print("3. Type Error Diagnostics")
    print("=" * 70)

    loc = get_python_location()

    # Type mismatch in method argument
    diag = type_mismatch_error(loc, "UInt<32>", "UInt<16>",
                                "in method 'write' argument 'data'")
    print("\nType mismatch error:")
    print(diag.format())

    # Width mismatch
    width_error = emit_error(loc, "width mismatch in addition",
                              notes=["left operand: UInt<32>",
                                     "right operand: UInt<16>",
                                     "result width: UInt<33>"],
                              hint="use pad() or truncate() to match widths")
    print("\nWidth mismatch error:")
    print(width_error.format())


def demonstrate_undefined_reference_errors():
    """Demonstrate undefined reference error diagnostics."""
    print("\n" + "=" * 70)
    print("4. Undefined Reference Errors")
    print("=" * 70)

    loc = get_python_location()

    # Undefined method
    diag1 = undefined_reference_error(loc, "method", "wriet",
                                        "module 'Counter'")
    print("\nUndefined method error:")
    print(diag1.format())

    # Undefined module
    diag2 = undefined_reference_error(loc, "module", "FIRRTLReg_64")
    print("\nUndefined module error:")
    print(diag2.format())

    # Undefined instance
    diag3 = undefined_reference_error(loc, "instance", "counter_reg",
                                        "rule body")
    print("\nUndefined instance error:")
    print(diag3.format())


def demonstrate_scheduling_conflicts():
    """Demonstrate scheduling conflict warnings."""
    print("\n" + "=" * 70)
    print("5. Scheduling Conflict Warnings")
    print("=" * 70)

    loc = get_python_location()

    # Conflict in static step
    diag1 = scheduling_conflict_warning(loc, "reg.read", "reg.write",
                                          "in static step 'compute'")
    print("\nScheduling conflict in static step:")
    print(diag1.format())

    # Conflict with concurrent rule
    diag2 = emit_warning(loc,
                          "static step may conflict with concurrent rule",
                          notes=["step method 'x.write' conflicts with rule 'increment'",
                                 "rule may fire concurrently causing backpressure"],
                          hint="ensure concurrent operations don't conflict")
    print("\nConflict with concurrent rule:")
    print(diag2.format())


def demonstrate_diagnostic_handler():
    """Demonstrate the DiagnosticHandler context manager."""
    print("\n" + "=" * 70)
    print("6. Diagnostic Handler")
    print("=" * 70)

    with DiagnosticHandler(min_level=DiagnosticLevel.DEBUG,
                           capture_debug=True) as handler:
        loc = get_python_location()

        # Simulate diagnostics during compilation
        handler.add_diagnostic(emit_error(loc, "missing clock argument"))
        handler.add_diagnostic(emit_warning(loc, "unused value 'temp'"))
        handler.add_diagnostic(emit_info(loc, "inlining module 'Helper'"))
        handler.add_diagnostic(emit_debug(loc, "processing rule 'main'"))

        print("\nCaptured diagnostics:")
        for diag in handler.get_diagnostics():
            print(f"  [{diag.level.prefix}] {diag.message}")

        print(f"\nSummary: {handler.format_summary()}")
        print(f"Has errors: {handler.has_errors()}")
        print(f"Has warnings: {handler.has_warnings()}")
        print(f"Error count: {handler.error_count}")
        print(f"Warning count: {handler.warning_count}")


def demonstrate_complex_diagnostic():
    """Demonstrate a complex diagnostic with multiple notes."""
    print("\n" + "=" * 70)
    print("7. Complex Diagnostic Example")
    print("=" * 70)

    loc = get_python_location()

    # Complex conversion error with full context
    diag = Diagnostic(
        level=DiagnosticLevel.ERROR,
        message="cannot convert procedural rule to FSM",
        location=loc
    )
    diag.add_note("rule contains infinite loop without exit condition")
    diag.add_note("loop defined here",
                  PythonLocation(__file__, 180, 0, "create_rule"))
    diag.add_note("FSM requires bounded execution")
    diag.set_hint("add a condition to the while loop that eventually becomes false")

    print("\nComplex error diagnostic:")
    print(diag.format(color=True))


def demonstrate_real_circuit_diagnostics():
    """Demonstrate diagnostics with a real circuit that has issues."""
    print("\n" + "=" * 70)
    print("8. Real Circuit Diagnostic Scenarios")
    print("=" * 70)

    clear_stl_registry()

    circuit = Circuit("DiagnosticsDemo")
    reg_mod = Reg.create(circuit, 32)

    with jit.module(circuit, "Counter") as m:
        clk = m.clock()
        rst = m.reset()

        count = m.instance(reg_mod, clk=clk, rst=rst)

        # Demonstrate what an error would look like
        loc = get_python_location()
        print("\nScenario: Calling undefined method on instance")
        error = undefined_reference_error(loc, "method", "increment",
                                           "instance 'count'")
        print(error.format())

        # What a type error would look like
        print("\nScenario: Type mismatch in method call")
        type_err = type_mismatch_error(loc, "UInt<32>", "UInt<8>",
                                        "in count.write() argument")
        print(type_err.format())

        # Actually create a valid rule
        with jit.rule(m) as increment:
            with increment.guard as g:
                g.always()
            with increment.body as body:
                val = count.read
                new_val = body.add(val, body.const(1, 32))
                count.next = new_val

    # Show what diagnostics would look like during compilation
    print("\nScenario: Compilation with warnings")
    with DiagnosticHandler() as handler:
        loc = get_python_location()
        handler.add_diagnostic(
            emit_info(loc, "compiling module 'Counter'")
        )
        handler.add_diagnostic(
            emit_info(loc, "rule 'increment' compiled successfully")
        )

        # Simulate the actual compilation
        try:
            mlir = circuit.emit_mlir()
            handler.add_diagnostic(
                emit_info(loc, "MLIR generation complete")
            )
            print("  MLIR generation: OK")
        except Exception as e:
            handler.add_diagnostic(
                emit_error(loc, f"MLIR generation failed: {e}")
            )
            print(f"  MLIR generation: FAILED ({e})")

        print(f"\n  {handler.format_summary()}")


def demonstrate_no_color_output():
    """Demonstrate diagnostics without ANSI color codes."""
    print("\n" + "=" * 70)
    print("9. Plain Text Output (no color)")
    print("=" * 70)

    loc = get_python_location()
    diag = emit_error(loc, "plain text diagnostic for log files",
                      notes=["note 1", "note 2"],
                      hint="this is a hint")

    print("\nWith color=False:")
    print(diag.format(color=False))


def main():
    print("=" * 70)
    print("Diagnostics System Demonstration (JIT stacked on PyCMT2)")
    print("=" * 70)

    demonstrate_diagnostic_levels()
    demonstrate_source_location_tracking()
    demonstrate_type_errors()
    demonstrate_undefined_reference_errors()
    demonstrate_scheduling_conflicts()
    demonstrate_diagnostic_handler()
    demonstrate_complex_diagnostic()
    demonstrate_real_circuit_diagnostics()
    demonstrate_no_color_output()

    print("\n" + "=" * 70)
    print("Diagnostics Example Complete")
    print("=" * 70)


if __name__ == "__main__":
    main()
