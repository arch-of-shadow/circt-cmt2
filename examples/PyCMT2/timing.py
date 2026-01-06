#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Cycle-Precise Timing Example using PyCMT2 EDSL

This example demonstrates the cycle-precise timing features for static scheduling:
- Static steps with fixed cycle counts
- Pipelined operations with sequential control

The timing passes (cmt2-timing-inference, cmt2-static-fsm-allocation,
cmt2-compile-static) analyze and validate timing, then generate FSM
control logic.

Note: Full timing annotation (static_latency on methods, arg_timing and
result_timing on calls) is not yet exposed in the Python API but is
available at the MLIR level. This example shows the static_step feature
which is available in Python.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/timing_example.py
"""

from circt.pycmt2 import Circuit, UInt


def main():
    print("=" * 60)
    print("PyCMT2 Cycle-Precise Timing Example")
    print("=" * 60)

    circuit = Circuit("TimingExample")

    # Define external register module
    with circuit.external_module("Reg32") as reg_mod:
        reg_mod.clock("clk")
        reg_mod.reset("rst")
        reg_mod.value("read", returns=[("out", UInt(32))])
        reg_mod.method("write", args=[("data", UInt(32))])
        reg_mod.sequence_before("read", "write")

    # Define external ALU (combinational - no latency)
    with circuit.external_module("ALU") as alu_mod:
        alu_mod.value("add", args=[("a", UInt(32)), ("b", UInt(32))], returns=[("out", UInt(32))])
        alu_mod.value("sub", args=[("a", UInt(32)), ("b", UInt(32))], returns=[("out", UInt(32))])

    # Create a module that uses static scheduling
    with circuit.module("StaticScheduler") as sched:
        clk = sched.clock("clk")
        rst = sched.reset("rst")

        # Instantiate ALU and registers
        alu = sched.instance(alu_mod, "alu")
        acc_reg = sched.instance(reg_mod, "acc", clk=clk, rst=rst)
        count_reg = sched.instance(reg_mod, "count", clk=clk, rst=rst)

        # Static step with fixed 4-cycle latency
        # This step will execute for exactly 4 cycles.
        #
        # Static steps are useful when:
        # - The total execution time is known at compile time
        # - All operations within have known latencies
        # - No dynamic done signals are needed
        #
        # The timing passes will:
        # 1. Allocate FSM states (4 states for 4 cycles)
        # 2. Generate FSM register and transition logic
        # 3. Create timing guards for each operation
        with sched.static_step(4, "increment_by_10") as step:
            # Read current accumulator value (cycle 0)
            acc_val = step.call(acc_reg, "read")

            # Add 10 to it using ALU (cycle 1)
            ten = step.const(10, 32)
            new_val = step.call(alu, "add", acc_val[0], ten)

            # Write back to accumulator (cycle 2)
            step.call(acc_reg, "write", new_val[0])

            # Increment counter (cycle 3)
            count_val = step.call(count_reg, "read")
            one = step.const(1, 32)
            new_count = step.call(alu, "add", count_val[0], one)
            step.call(count_reg, "write", new_count[0])

        # Another static step: compute sum of two registers
        with sched.static_step(3, "compute_sum") as step2:
            a = step2.call(acc_reg, "read")
            b = step2.call(count_reg, "read")
            sum_val = step2.call(alu, "add", a[0], b[0])
            step2.call(acc_reg, "write", sum_val[0])

        # Proc Rule to run the first step (uses multi-cycle control)
        with sched.proc_rule("run_increment") as rule:
            with rule.guard() as g:
                g.returns(g.const(1, 1))
            with rule.control() as ctrl:
                ctrl.enable(step.ref())

        # Value to read the accumulator
        with sched.value("get_accumulator", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(acc_reg, "read")
                body.returns(result[0])

        # Value to read the counter
        with sched.value("get_count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(count_reg, "read")
                body.returns(result[0])

    # Print the generated MLIR
    print("\nGenerated MLIR with static steps:")
    print("-" * 40)
    mlir_str = circuit.emit_mlir()
    print(mlir_str)

    # Show how to run timing passes
    print("\n" + "=" * 60)
    print("To run timing analysis and FSM allocation, use:")
    print("-" * 40)
    print("""
    circt-opt input.mlir \\
        -cmt2-timing-inference \\
        -cmt2-timing-validation \\
        -cmt2-static-fsm-allocation \\
        -cmt2-compile-static
    """)

    print("\nExpected transformations:")
    print("-" * 40)
    print("""
    1. TimingInference: Infers timing for calls from method contracts
    2. TimingValidation: Verifies timing constraints are satisfied
    3. StaticFSMAllocation: Maps cycles to FSM states
       - Steps with <=8 states use one-hot encoding
       - Steps with >8 states use binary encoding
    4. CompileStatic: Generates FSM register info
       - fsm_done_expr: Condition for step completion
       - fsm_next_expr: FSM state transition logic
       - fsm_guard_expr: Per-call timing guards
    """)

    # Example of expected output attributes
    print("Example FSM attributes after compilation:")
    print("-" * 40)
    print("""
    // 4-state step with one-hot encoding:
    cmt2.proc.static_step @increment_by_10<4> {
        ...
    } {
        fsm_states = 4,
        fsm_bitwidth = 4,
        fsm_encoding = "one_hot",
        fsm_done_expr = "fsm[3]",
        fsm_next_expr = "{fsm[2:0], 1'b0}",
        fsm_init_expr = "4'b1",
        static_compiled
    }

    // 12-state step with binary encoding:
    cmt2.proc.static_step @large_step<12> {
        ...
    } {
        fsm_states = 12,
        fsm_bitwidth = 4,
        fsm_encoding = "binary",
        fsm_done_expr = "fsm == 11",
        fsm_next_expr = "fsm + 1",
        fsm_init_expr = "4'd0",
        static_compiled
    }
    """)

    # Future enhancements section
    print("\n" + "=" * 60)
    print("Future Python API Enhancements")
    print("-" * 40)
    print("""
    The following timing features will be added to the Python API:

    1. Static latency on method declarations:
       mult_mod.method("multiply", ...,
           static_latency=4)  # Result available 4 cycles after call

    2. Timing annotation on calls:
       result = step.call(mult, "multiply", a, b,
           arg_timing=[(0, 1), (0, 1)],   # Args driven cycles 0-1
           result_timing=[(4, 5)])         # Result captured cycles 4-5

    3. Initiation interval (II) for pipelining:
       mult_mod.method("multiply", ...,
           static_latency=4,
           interval=3)  # Can accept new inputs every 3 cycles

    4. Port timing for precise scheduling:
       mult_mod.method("multiply", ...,
           arg_port_timing=[port.data(0), port.data(0)],
           result_port_timing=[port.data(4)])

    These features are already available at the MLIR level via:
    - #cmt2.timing<[start, end]> for timing intervals
    - #cmt2.interval<n> for initiation intervals
    - static<n> on BindMethodOp for static latency
    """)


if __name__ == "__main__":
    main()
