#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Python Interpreter Example for PyCMT2

This example demonstrates using the Python interpreter for cycle-accurate
simulation of CMT2 circuits with GAA semantics.

Features demonstrated:
- Creating an interpreter from a circuit
- Reset and step-by-step execution
- State inspection and modification (manual)
- Tracing execution
- Breakpoints (on rule, on cycle, on condition)

NOTE: The Python interpreter is a simplified scheduler/tracer that:
- Tracks rule firing based on precedence
- Manages register state (but requires manual updates for actual values)
- Supports breakpoints and tracing

For full MLIR interpretation (executing rule bodies), use the cmt2-dbg CLI tool.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/interpreter_example.py
"""

from circt.pycmt2 import Circuit, UInt


def main():
    print("=" * 60)
    print("PyCMT2 Python Interpreter Example")
    print("=" * 60)

    # Create a simple counter circuit
    circuit = Circuit("InterpreterDemo")

    # Define external 32-bit register module
    with circuit.external_module("Reg32") as reg_mod:
        reg_mod.clock("clk")
        reg_mod.reset("rst")
        reg_mod.value("read", returns=[UInt(32)])
        reg_mod.method("write", args=[("data", UInt(32))])
        reg_mod.sequence_before("read", "write")
        reg_mod.conflict("write", "write")
        reg_mod.conflict_free("read", "read")

    with circuit.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()

        # Create a 32-bit counter register
        counter = m.instance(reg_mod, "counter", clk=clk, rst=rst)

        # Rule: increment counter
        with m.rule("increment") as incr_rule:
            with incr_rule.guard() as g:
                g.always()
            with incr_rule.body() as b:
                val = b.call(counter, "read")
                new_val = b.bits(b.add(val, b.const(1, 32)), 31, 0)
                b.call(counter, "write", new_val)

        # Rule: reset when counter reaches 10
        with m.rule("reset_at_10") as reset_rule:
            with reset_rule.guard() as g:
                val = g.call(counter, "read")
                g.returns(g.eq(val, g.const(10, 32)))
            with reset_rule.body() as b:
                b.call(counter, "write", b.const(0, 32))

        # Set precedence: reset_at_10 has higher priority than increment
        m.precedence(reset_rule.ref(), incr_rule.ref())

    # Show the generated MLIR
    print("\n=== Generated CMT2 MLIR ===\n")
    mlir_text = circuit.emit_mlir()
    print(mlir_text)

    # Create the interpreter
    print("\n=== Interpreter Simulation ===\n")
    interp = circuit.interpreter()

    # Show initial state
    print("Initial state:")
    interp.print_state()
    interp.print_rules()

    # Enable tracing
    interp.enable_tracing(True)
    print("\n--- Tracing enabled ---\n")

    # Step through 15 cycles
    print("Stepping through 15 cycles:")
    for i in range(15):
        results = interp.step()
        fired = [r.name for r in results[0] if r.fired]
        counter_val = interp.get_register("counter")
        print(f"  Cycle {interp.cycle}: counter={counter_val}, fired={fired}")

    # Show traces
    print("\n--- Trace history ---")
    for trace in interp.traces[-5:]:  # Show last 5 traces
        interp.print_trace(trace)

    # Demonstrate breakpoints
    print("\n=== Breakpoint Demo ===\n")
    interp.reset()
    print("Reset circuit")

    # Add breakpoint at cycle 5
    bp_id = interp.add_breakpoint_at_cycle(5)
    print(f"Added breakpoint at cycle 5 (id={bp_id})")

    # Run until breakpoint
    print("Running until breakpoint...")
    bp = interp.run(max_cycles=100)
    if bp:
        print(f"Cycle breakpoint hit at cycle {interp.cycle}!")
        interp.print_state()
    else:
        print("No breakpoint hit within 100 cycles")

    # Remove breakpoint and continue
    interp.remove_breakpoint(bp_id)
    print("\nBreakpoint removed, stepping 3 more cycles:")
    for i in range(3):
        interp.step()
        counter_val = interp.get_register("counter")
        print(f"  Cycle {interp.cycle}: counter={counter_val}")

    # Demonstrate callback-based simulation
    print("\n=== Callback-Based Simulation ===\n")
    print("Register Python callbacks for guards and rule bodies.")
    print("The interpreter handles scheduling; callbacks handle execution.\n")

    interp.reset()
    interp.enable_tracing(True)

    # Register guard callback for reset_at_10 rule
    def reset_guard(interp):
        return interp.get_register("counter") == 10

    interp.register_guard("reset_at_10", reset_guard)

    # Register body callbacks for rules
    def increment_body(interp):
        val = interp.get_register("counter")
        interp.set_register("counter", val + 1)

    def reset_body(interp):
        interp.set_register("counter", 0)

    interp.register_body("increment", increment_body)
    interp.register_body("reset_at_10", reset_body)

    print("Registered callbacks:")
    print("  - increment: guard=always, body=counter++")
    print("  - reset_at_10: guard=counter==10, body=counter=0")
    print()

    # Now the interpreter will automatically execute correctly
    for cycle in range(15):
        results = interp.step()
        fired_rules = [r.name for r in results[0] if r.fired]
        enabled_rules = [r.name for r in results[0] if r.guard_enabled]
        counter_val = interp.get_register("counter")
        print(f"  Cycle {interp.cycle}: counter={counter_val}, enabled={enabled_rules}, fired={fired_rules}")

    # Demonstrate conditional breakpoint with automatic execution
    print("\n=== Conditional Breakpoint with Automatic Execution ===\n")
    interp.reset()
    interp.clear_breakpoints()

    # Break when counter reaches 5
    def counter_is_5(interp):
        return interp.get_register("counter") == 5

    bp_id = interp.add_breakpoint_on_condition(counter_is_5)
    print("Added conditional breakpoint: counter == 5")

    print("Running until condition...")
    bp = interp.run(max_cycles=100)
    if bp:
        print(f"Condition met at cycle {interp.cycle}!")
        interp.print_state()

    print("\n" + "=" * 60)
    print("Interpreter example completed!")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
