#!/usr/bin/env python3
"""Test ProcCondIfOp - conditional with condition region support.

This test verifies that if_() can accept a callable condition function,
similar to how while_() works. This allows the condition to include
cmt2.call operations for reading from instances at runtime.
"""

import sys
import os

# Add build path
build_path = os.path.join(os.path.dirname(__file__), "../../build/tools/circt/python_packages/circt_core")
if os.path.exists(build_path):
    sys.path.insert(0, build_path)

from circt.pycmt2 import Circuit
from circt.pycmt2.types import UInt, Bool
from circt.pycmt2.stl import Reg, clear_stl_registry


def test_cond_if_with_callable():
    """Test if_() with a callable condition function."""
    print("=" * 60)
    print("Test: ProcCondIfOp with callable condition")
    print("=" * 60)

    clear_stl_registry()
    circuit = Circuit("TestCondIf")

    with circuit.module("CondIfExample") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create a counter register
        counter = mod.instance(Reg.create(circuit, 32), "counter", clk=clk, rst=rst)
        # Create a result register
        result = mod.instance(Reg.create(circuit, 32), "result", clk=clk, rst=rst)

        # Define a step that increments counter
        with mod.static_step(1, "increment") as step:
            cnt = step.call(counter, "read")
            step.call(counter, "write", step.add(cnt, step.const(1, 32)))

        # Define a step that sets result
        with mod.static_step(1, "set_result") as set_step:
            set_step.call(result, "write", set_step.const(42, 32))

        # Procedural rule with cond_if (callable condition)
        with mod.proc_rule("test_rule") as rule:
            with rule.guard() as g:
                g.always()

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # Use callable condition - reads counter at runtime
                    def check_counter(b):
                        cnt = b.call(counter, "read")
                        return b.lt(cnt, b.const(5, 32))

                    with seq.if_(check_counter) as if_:
                        with if_.then_() as then_:
                            then_.enable(step.ref())
                        with if_.else_() as else_:
                            else_.enable(set_step.ref())

    # Print generated MLIR
    mlir_str = circuit.emit_mlir()
    print("\nGenerated MLIR:")
    print(mlir_str)

    # Verify ProcCondIfOp is generated
    assert "cmt2.proc.cond_if" in mlir_str, "Expected cmt2.proc.cond_if in output"
    assert "cmt2.proc.cond_if_yield" in mlir_str, "Expected cmt2.proc.cond_if_yield in output"
    print("\n[PASS] ProcCondIfOp generated correctly")


def test_cond_if_with_lambda():
    """Test if_() with a lambda condition function."""
    print("\n" + "=" * 60)
    print("Test: ProcCondIfOp with lambda condition")
    print("=" * 60)

    clear_stl_registry()
    circuit = Circuit("TestCondIfLambda")

    with circuit.module("LambdaCondIfExample") as mod:
        clk = mod.clock()
        rst = mod.reset()

        counter = mod.instance(Reg.create(circuit, 32), "counter", clk=clk, rst=rst)

        with mod.static_step(1, "inc") as step:
            cnt = step.call(counter, "read")
            step.call(counter, "write", step.add(cnt, step.const(1, 32)))

        with mod.proc_rule("lambda_rule") as rule:
            with rule.guard() as g:
                g.always()

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # Lambda form
                    with seq.if_(lambda b: b.lt(b.call(counter, "read"), b.const(10, 32))) as if_:
                        with if_.then_() as then_:
                            then_.enable(step.ref())

    mlir_str = circuit.emit_mlir()
    print("\nGenerated MLIR:")
    print(mlir_str)

    assert "cmt2.proc.cond_if" in mlir_str, "Expected cmt2.proc.cond_if in output"
    print("\n[PASS] Lambda condition works correctly")


def test_signal_if_still_works():
    """Test that if_() with Signal condition still works (backward compatibility)."""
    print("\n" + "=" * 60)
    print("Test: ProcIfOp with Signal condition (backward compat)")
    print("=" * 60)

    clear_stl_registry()
    circuit = Circuit("TestSignalIf")

    with circuit.module("SignalIfExample") as mod:
        clk = mod.clock()
        rst = mod.reset()

        counter = mod.instance(Reg.create(circuit, 32), "counter", clk=clk, rst=rst)

        with mod.static_step(1, "inc") as step:
            cnt = step.call(counter, "read")
            step.call(counter, "write", step.add(cnt, step.const(1, 32)))

        with mod.proc_rule("signal_rule") as rule:
            with rule.guard() as g:
                g.always()

            with rule.control() as ctrl:
                with ctrl.seq() as seq:
                    # Pre-computed Signal condition (old style)
                    cond = seq.const(1, 1)  # Always true
                    with seq.if_(cond) as if_:
                        with if_.then_() as then_:
                            then_.enable(step.ref())

    mlir_str = circuit.emit_mlir()
    print("\nGenerated MLIR:")
    print(mlir_str)

    # Should use ProcIfOp, not ProcCondIfOp
    assert "cmt2.proc.if" in mlir_str, "Expected cmt2.proc.if in output"
    # Should NOT have cond_if (unless there's another one)
    lines = [l for l in mlir_str.split('\n') if "cmt2.proc.cond_if" in l]
    assert len(lines) == 0, f"Did not expect cmt2.proc.cond_if in output for Signal condition, got: {lines}"
    print("\n[PASS] Signal condition backward compatibility works")


def main():
    """Run all tests."""
    try:
        test_cond_if_with_callable()
        test_cond_if_with_lambda()
        test_signal_if_still_works()
        print("\n" + "=" * 60)
        print("All tests passed!")
        print("=" * 60)
        return 0
    except Exception as e:
        print(f"\n[FAIL] {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
