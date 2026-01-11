#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Comprehensive Procedural Control Example for PyCMT2.

This example demonstrates and validates all procedural control features:
1. Dynamic steps (with explicit done signal)
2. Static steps (with fixed latency)
3. Sequential composition (proc.seq)
4. Parallel composition (proc.par)
5. Conditional control (proc.if)
6. While loops (proc.while)
7. Proc rules with multi-cycle control

This serves as a validation test for the proc-related passes:
- cmt2-compile-invoke: proc.invoke -> proc.enable + step
- cmt2-tdcc: compute FSM states and transitions
- cmt2-proc-stmt-to-action: generate FSM registers, state rules, status values
- cmt2-proc-to-gaa: mark proc ops as converted

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/proc_comprehensive_example.py
"""

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry


def create_proc_comprehensive_circuit():
    """Create a circuit that exercises all procedural control features."""
    clear_stl_registry()

    circuit = Circuit("ProcComprehensive")

    # Create register modules for state
    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)

    with circuit.module("ProcTest") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers
        reg_a = m.instance(reg32, "reg_a", clk=clk, rst=rst)
        reg_b = m.instance(reg32, "reg_b", clk=clk, rst=rst)
        reg_result = m.instance(reg32, "reg_result", clk=clk, rst=rst)
        reg_counter = m.instance(reg32, "reg_counter", clk=clk, rst=rst)
        reg_flag = m.instance(reg1, "reg_flag", clk=clk, rst=rst)

        # =====================================================================
        # Dynamic Steps (explicit done signal based on method call readiness)
        # =====================================================================

        # Step: read_a - Read from reg_a (dynamic, depends on read method ready)
        with m.step("read_a") as step:
            val = step.call(reg_a, "read")
            # Done when the read succeeds
            step.done(step.const(1, 1))

        # Step: write_result - Write to reg_result (dynamic)
        with m.step("write_result") as step:
            val = step.call(reg_a, "read")
            step.call(reg_result, "write", val)
            step.done(step.const(1, 1))

        # Step: increment_counter - Increment counter (dynamic)
        with m.step("increment_counter") as step:
            count = step.call(reg_counter, "read")
            new_count = step.add(count, step.const(1, 32))
            step.call(reg_counter, "write", new_count)
            step.done(step.const(1, 1))

        # Step: decrement_counter - Decrement counter (dynamic)
        with m.step("decrement_counter") as step:
            count = step.call(reg_counter, "read")
            new_count = step.sub(count, step.const(1, 32))
            step.call(reg_counter, "write", new_count)
            step.done(step.const(1, 1))

        # Step: set_flag - Set the flag register (dynamic)
        with m.step("set_flag") as step:
            step.call(reg_flag, "write", step.const(1, 1))
            step.done(step.const(1, 1))

        # Step: clear_flag - Clear the flag register (dynamic)
        with m.step("clear_flag") as step:
            step.call(reg_flag, "write", step.const(0, 1))
            step.done(step.const(1, 1))

        # =====================================================================
        # Static Steps (fixed latency, no explicit done needed)
        # =====================================================================

        # Static step: delay_1 - 1-cycle delay (effectively a NOP)
        with m.static_step(1, "delay_1") as step:
            # Just a delay, no operations
            pass

        # Static step: delay_3 - 3-cycle delay
        with m.static_step(3, "delay_3") as step:
            # Multi-cycle delay
            pass

        # Static step: compute_sum - Fixed 2-cycle computation
        with m.static_step(2, "compute_sum") as step:
            a = step.call(reg_a, "read")
            b = step.call(reg_b, "read")
            result = step.add(a, b)
            step.call(reg_result, "write", result)

        # =====================================================================
        # Proc Rule 1: Sequential Control
        # Demonstrates: proc.seq, proc.enable
        # =====================================================================

        with m.proc_rule("seq_test") as rule:
            with rule.guard() as g:
                # Always enabled
                g.always()
            with rule.control() as ctrl:
                with ctrl.seq():
                    # Execute steps in sequence
                    ctrl.enable(m._steps["read_a"].ref())
                    ctrl.enable(m._steps["write_result"].ref())

        # =====================================================================
        # Proc Rule 2: Parallel Control
        # Demonstrates: proc.par with multiple concurrent steps
        # =====================================================================

        with m.proc_rule("par_test") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                with ctrl.par():
                    # Execute steps in parallel (if they don't conflict)
                    ctrl.enable(m._steps["set_flag"].ref())
                    ctrl.enable(m._steps["increment_counter"].ref())

        # =====================================================================
        # Proc Rule 3: Conditional Control
        # Demonstrates: proc.if with then and else branches
        # =====================================================================

        with m.proc_rule("if_test") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                # Read flag to decide branch
                flag = ctrl.const(1, 1)  # Simplified for now
                with ctrl.if_(flag) as if_ctrl:
                    with if_ctrl.then_() as then_ctrl:
                        with then_ctrl.seq():
                            then_ctrl.enable(m._steps["set_flag"].ref())
                    with if_ctrl.else_() as else_ctrl:
                        with else_ctrl.seq():
                            else_ctrl.enable(m._steps["clear_flag"].ref())

        # =====================================================================
        # Proc Rule 4: While Loop Control
        # Demonstrates: proc.while with loop body
        # =====================================================================

        with m.proc_rule("while_test") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                # Loop condition function (simplified - returns false, so won't loop)
                with ctrl.while_(lambda b: b.const(0, 1)) as loop:
                    with loop.seq():
                        loop.enable(m._steps["increment_counter"].ref())

        # =====================================================================
        # Proc Rule 5: Mixed Static and Dynamic Steps
        # Demonstrates: combining static and dynamic steps in sequence
        # =====================================================================

        with m.proc_rule("mixed_test") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                with ctrl.seq():
                    # Dynamic step
                    ctrl.enable(m._steps["read_a"].ref())
                    # Static step (fixed 3-cycle delay)
                    ctrl.enable(m._steps["delay_3"].ref())
                    # Another dynamic step
                    ctrl.enable(m._steps["write_result"].ref())

        # =====================================================================
        # Proc Rule 6: Nested Control Structures
        # Demonstrates: nested seq/par/if combinations
        # =====================================================================

        with m.proc_rule("nested_test") as rule:
            with rule.guard() as g:
                g.always()
            with rule.control() as ctrl:
                with ctrl.seq():
                    # First: parallel operations
                    with ctrl.par():
                        ctrl.enable(m._steps["set_flag"].ref())
                        ctrl.enable(m._steps["delay_1"].ref())
                    # Then: conditional
                    flag = ctrl.const(1, 1)
                    with ctrl.if_(flag) as if_ctrl:
                        with if_ctrl.then_() as then_ctrl:
                            with then_ctrl.seq():
                                then_ctrl.enable(m._steps["compute_sum"].ref())
                    # Finally: more sequential
                    ctrl.enable(m._steps["clear_flag"].ref())

        # =====================================================================
        # Regular (non-procedural) Rule for comparison
        # =====================================================================

        with m.rule("regular_rule") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as body:
                # Simple combinational logic
                val = body.call(reg_a, "read")
                # No state changes, just demonstrates non-proc rule

        # =====================================================================
        # Method: load - Load input values (non-procedural)
        # =====================================================================

        with m.method("load", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                a_in = body.arg("a")
                b_in = body.arg("b")
                body.call(reg_a, "write", a_in)
                body.call(reg_b, "write", b_in)

        # =====================================================================
        # Value: get_result - Read result (non-procedural)
        # =====================================================================

        with m.value("get_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(reg_result, "read")
                body.returns(result)

        # =====================================================================
        # Value: get_counter - Read counter (non-procedural)
        # =====================================================================

        with m.value("get_counter", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                count = body.call(reg_counter, "read")
                body.returns(count)

        # =====================================================================
        # Value: get_flag - Read flag (non-procedural)
        # =====================================================================

        with m.value("get_flag", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                flag = body.call(reg_flag, "read")
                body.returns(flag)

    return circuit


def main():
    print("=" * 70)
    print("Comprehensive Procedural Control Example")
    print("=" * 70)

    print("\n1. Creating circuit with procedural control features...")
    circuit = create_proc_comprehensive_circuit()

    print("\n2. Emitting CMT2 MLIR (before proc passes)...")
    mlir = circuit.emit_mlir()
    print("-" * 70)
    print(mlir[:3000])  # First 3000 chars
    if len(mlir) > 3000:
        print(f"... ({len(mlir) - 3000} more characters)")
    print("-" * 70)

    # Verify proc constructs are present
    proc_constructs = [
        ("proc.step", "cmt2.proc.step"),
        ("proc.static_step", "cmt2.proc.static_step"),
        ("proc.rule", "cmt2.proc.rule"),
        ("proc.seq", "cmt2.proc.seq"),
        ("proc.par", "cmt2.proc.par"),
        ("proc.if", "cmt2.proc.if"),
        ("proc.while", "cmt2.proc.while"),
        ("proc.enable", "cmt2.proc.enable"),
        ("proc.step_done", "cmt2.proc.step_done"),
        ("proc.control_end", "cmt2.proc.control_end"),
    ]

    print("\n3. Verifying procedural constructs in MLIR:")
    all_found = True
    for name, pattern in proc_constructs:
        found = pattern in mlir
        status = "FOUND" if found else "MISSING"
        print(f"   {name}: {status}")
        if not found:
            all_found = False

    if not all_found:
        print("\nWARNING: Some procedural constructs are missing!")

    print("\n4. Emitting FIRRTL (after proc passes)...")
    try:
        firrtl = circuit.emit_firrtl()
        print("-" * 70)
        print(firrtl[:2000])  # First 2000 chars
        if len(firrtl) > 2000:
            print(f"... ({len(firrtl) - 2000} more characters)")
        print("-" * 70)

        # Verify FSM-related constructs were generated
        fsm_constructs = [
            ("FSM state register", "__fsm_"),
            ("State rules", "rule @"),
            ("Idle status", "_idle"),
            ("Running status", "_running"),
        ]

        print("\n5. Verifying FSM generation in FIRRTL:")
        for name, pattern in fsm_constructs:
            found = pattern in firrtl
            status = "FOUND" if found else "NOT FOUND"
            print(f"   {name}: {status}")

    except Exception as e:
        print(f"FIRRTL generation failed: {e}")
        print("This may indicate issues with proc passes.")

    print("\n6. Emitting Verilog (full pipeline)...")
    try:
        verilog = circuit.to_verilog()
        print("-" * 70)
        print(verilog[:2000])  # First 2000 chars
        if len(verilog) > 2000:
            print(f"... ({len(verilog) - 2000} more characters)")
        print("-" * 70)
        print("\nVerilog generation successful!")
    except Exception as e:
        print(f"Verilog generation failed: {e}")

    print("\n" + "=" * 70)
    print("Procedural control example completed!")
    print("=" * 70)


if __name__ == "__main__":
    main()
