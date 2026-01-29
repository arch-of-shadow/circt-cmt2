#!/usr/bin/env python3
"""Test ProcCondIfOp - conditional with condition region support.

This test verifies that if_() can accept a callable condition function,
similar to how while_() works. This allows the condition to include
cmt2.call operations for reading from instances at runtime.

This file also includes E2E simulation to verify the generated RTL.
"""

import cmt2.jit as jit

import shutil
import sys
import os
from pathlib import Path

# Add build path
build_path = os.path.join(os.path.dirname(__file__), "../../build/tools/circt/python_packages/circt_core")
if os.path.exists(build_path):
    sys.path.insert(0, build_path)

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.types import Bool
from circt.pycmt2.stl import Reg, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def test_cond_if_with_callable():
    """Test if_() with a callable condition function."""
    print("=" * 60)
    print("Test: ProcCondIfOp with callable condition")
    print("=" * 60)

    clear_stl_registry()
    circuit = Circuit("TestCondIf")

    with jit.module(circuit, "CondIfExample") as mod:
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
            with rule.guard as g:
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

    with jit.module(circuit, "LambdaCondIfExample") as mod:
        clk = mod.clock()
        rst = mod.reset()

        counter = mod.instance(Reg.create(circuit, 32), "counter", clk=clk, rst=rst)

        with mod.static_step(1, "inc") as step:
            cnt = step.call(counter, "read")
            step.call(counter, "write", step.add(cnt, step.const(1, 32)))

        with mod.proc_rule("lambda_rule") as rule:
            with rule.guard as g:
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

    with jit.module(circuit, "SignalIfExample") as mod:
        clk = mod.clock()
        rst = mod.reset()

        counter = mod.instance(Reg.create(circuit, 32), "counter", clk=clk, rst=rst)

        with mod.static_step(1, "inc") as step:
            cnt = step.call(counter, "read")
            step.call(counter, "write", step.add(cnt, step.const(1, 32)))

        with mod.proc_rule("signal_rule") as rule:
            with rule.guard as g:
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


def create_simulatable_circuit():
    """Create a circuit suitable for E2E simulation.

    This circuit uses cond_if with callable condition to:
    1. While counter < 5: increment counter
    2. When counter >= 5: set result to 42
    """
    clear_stl_registry()
    circuit = Circuit("TestCondIfSim")

    with jit.module(circuit, "CondIfSimExample") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create a counter register
        counter = mod.instance(Reg.create(circuit, 32), "counter", clk=clk, rst=rst)
        # Create a result register
        result = mod.instance(Reg.create(circuit, 32), "result", clk=clk, rst=rst)
        # Create done flag
        done = mod.instance(Reg.create(circuit, 1), "done", clk=clk, rst=rst)

        # Define a step that increments counter
        with mod.static_step(1, "increment") as step:
            cnt = step.call(counter, "read")
            step.call(counter, "write", step.add(cnt, step.const(1, 32)))

        # Define a step that sets result to 42 and marks done
        with mod.static_step(1, "set_result") as set_step:
            set_step.call(result, "write", set_step.const(42, 32))
            set_step.call(done, "write", set_step.const(1, 1))

        # Procedural rule with cond_if (callable condition)
        with mod.proc_rule("compute") as rule:
            with rule.guard as g:
                # Only run if not done
                is_done = g.call(done, "read")
                not_done = g.not_(is_done)
                g.returns(not_done)

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

        # Value: get_counter
        with jit.value(mod, "get_counter", returns=[UInt(32)]) as val:
            with val.guard as g:
                g.always()
            with val.body as body:
                cnt = body.call(counter, "read")
                body.returns(cnt)

        # Value: get_result
        with jit.value(mod, "get_result", returns=[UInt(32)]) as val:
            with val.guard as g:
                g.always()
            with val.body as body:
                res = body.call(result, "read")
                body.returns(res)

        # Value: is_done
        with jit.value(mod, "is_done", returns=[UInt(1)]) as val:
            with val.guard as g:
                g.always()
            with val.body as body:
                d = body.call(done, "read")
                body.returns(d)

    return circuit


def create_cond_if_testbench(circuit):
    """Create testbench using DSL for cond_if simulation."""
    tb = Testbench(circuit, auto_debug_ports=False)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("get_counter_res0", 0, "Counter should be 0 after reset")
        seq.expect("get_result_res0", 0, "Result should be 0 after reset")
        seq.expect("is_done_res0", 0, "Should not be done after reset")
        seq.print("Reset test passed")

    # =========================================================================
    # Test Sequence: Cond_If Execution
    # =========================================================================
    with tb.sequence("test_cond_if") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Conditional if with callable condition")
        seq.comment("=" * 60)
        seq.comment("Expected behavior:")
        seq.comment("  - While counter < 5: increment counter")
        seq.comment("  - When counter >= 5: set result to 42")
        seq.reset(5)

        seq.record_cycle("start")

        # Wait for completion
        seq.comment("Waiting for completion...")
        seq.wait_condition("dut->is_done_res0", timeout=50)
        seq.record_cycle("end")

        # Verify results
        seq.wait(1)
        seq.expect("get_counter_res0", 5, "Counter should be 5")
        seq.expect("get_result_res0", 42, "Result should be 42")
        seq.expect("is_done_res0", 1, "Should be done")

        seq.print_cycle_diff("start", "end", "Computation latency")
        seq.print("Counter = ", "get_counter_res0")
        seq.print("Result = ", "get_result_res0")
        seq.print("Cond_if test PASSED")

    return tb


def run_simulation():
    """Run E2E simulation for cond_if."""
    print("\n" + "=" * 60)
    print("E2E Simulation: Cond_If with Callable Condition")
    print("=" * 60)

    script_dir = Path(__file__).parent
    sim_dir = script_dir / "sim_cond_if"

    if sim_dir.exists():
        shutil.rmtree(sim_dir)

    print("\n1. Creating circuit for simulation...")
    circuit = create_simulatable_circuit()

    print("\n2. Creating testbench using Testbench DSL...")
    tb = create_cond_if_testbench(circuit)
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    print("\n3. Setting up simulation workspace...")
    ws = SimulationWorkspace(circuit, sim_dir, debug_ports=True)

    print("\n4. Generating workspace with testbench...")
    ws.generate_with_testbench(tb)
    print(f"   Workspace: {sim_dir}")

    print("\n5. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    print("\n6. Running simulation...")
    success, output = ws.run()
    print(output)

    print("\n" + "=" * 60)
    if success:
        print("E2E Simulation PASSED!")
    else:
        print("E2E Simulation FAILED!")
    print("=" * 60)

    return 0 if success else 1


def main():
    """Run all tests."""
    try:
        test_cond_if_with_callable()
        test_cond_if_with_lambda()
        test_signal_if_still_works()

        # Run E2E simulation
        sim_result = run_simulation()

        print("\n" + "=" * 60)
        print("All MLIR generation tests passed!")
        if sim_result == 0:
            print("E2E simulation passed!")
        else:
            print("E2E simulation failed!")
        print("=" * 60)
        return sim_result
    except Exception as e:
        print(f"\n[FAIL] {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
