#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
End-to-End Pipeline Simulation Test (JIT stacked on PyCMT2)

This example demonstrates a complete end-to-end flow:
1. Create a pipelined dataflow design using JIT syntax
2. Generate a simulation workspace with Verilator
3. Create a testbench using the Testbench DSL
4. Build and run the simulation
5. Assert correctness from the testbench

The pipeline computes: output = (input + 1) * 2
in a 3-stage pipeline with fork-join pattern.

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/pipeline_e2e.py

After running, check the simulation workspace at:
    examples/JIT/pipeline_e2e_sim/
"""

import cmt2.jit as jit

import shutil
from pathlib import Path

from circt.pycmt2 import (
    Circuit,
    UInt,
    Pipeline,
    SyncToken,
)
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, clear_stl_registry


@jit.elaborate
def create_pipeline_circuit():
    """Create a pipelined computation circuit.

    This creates a simple 3-stage pipeline:
    - Stage 0: Capture input data
    - Stage 1: Add 1 to data
    - Stage 2: Multiply by 2 (shift left)

    Result: output = (input + 1) * 2
    """
    clear_stl_registry()

    circuit = Circuit("PipelineE2E")

    with jit.module(circuit, "ComputePipeline") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create a linear pipeline using the Pipeline builder
        # Pipeline requires: stages=N to specify number of stages
        pipe = Pipeline(mod, "compute", UInt(16), stages=3, output_type=UInt(16))

        @pipe.stage(0)  # Stage index 0
        def stage_capture(task, data):
            """Stage 0: Capture input."""
            return data

        @pipe.stage(1)  # Stage index 1
        def stage_increment(task, data):
            """Stage 1: Add 1."""
            result = task.add(data, task.const(1, 16))
            return task.bits(result, 15, 0)  # Truncate to 16 bits

        @pipe.stage(2)  # Stage index 2
        def stage_double(task, data):
            """Stage 2: Multiply by 2 (shift left)."""
            # Use a *static* shift amount to avoid FIRRTL dynamic-shift width
            # explosion (DShl with a 16-bit shift amount implies huge result width).
            result = task.shl(data, 1)
            return task.bits(result, 15, 0)  # Truncate to 16 bits

        pipe.build()

    return circuit


@jit.elaborate
def create_forkjoin_circuit():
    """Create a fork-join pipeline circuit.

    This creates a diamond-shaped pipeline:
    - Source: Capture input
    - Branch A: Add 10
    - Branch B: Add 20
    - Join: Sum both branches

    Result: output = (input + 10) + (input + 20) = 2*input + 30
    """
    clear_stl_registry()

    circuit = Circuit("ForkJoinE2E")

    with jit.module(circuit, "DiamondPipeline") as mod:
        clk = mod.clock()
        rst = mod.reset()

        @jit.dataflow(mod)
        def diamond(df, input: UInt[16]) -> UInt[16]:
            dfb = df._df

            # Source: capture input
            with dfb.task("source", tokens_out=[SyncToken(UInt(16))]) as task:
                tok_src = task.create_token(input, UInt(16))
                task.yield_tokens(tok_src)

            # Branch A: add 10
            with dfb.task(
                "add_10",
                tokens_in=[tok_src],
                tokens_out=[SyncToken(UInt(16))],
                timing=(1, 2),
            ) as task:
                data = task.token_data(tok_src)
                result_a = task.add(data, task.const(10, 16))
                tok_a = task.create_token(task.bits(result_a, 15, 0), UInt(16))
                task.yield_tokens(tok_a)

            # Branch B: add 20 (fork - same source token)
            with dfb.task(
                "add_20",
                tokens_in=[tok_src],
                tokens_out=[SyncToken(UInt(16))],
                timing=(1, 2),
            ) as task:
                data = task.token_data(tok_src)
                result_b = task.add(data, task.const(20, 16))
                tok_b = task.create_token(task.bits(result_b, 15, 0), UInt(16))
                task.yield_tokens(tok_b)

            # Join: sum both results
            with dfb.task("sum", tokens_in=[tok_a, tok_b], timing=(2, 3)) as task:
                val_a = task.token_data(tok_a)
                val_b = task.token_data(tok_b)
                sum_result = task.add(val_a, val_b)
                task.return_values(task.bits(sum_result, 15, 0))

    return circuit


def generate_and_test_pipeline():
    """Generate and test the linear pipeline."""
    print("=" * 70)
    print("Test 1: Linear Pipeline E2E Test")
    print("=" * 70)

    # Create circuit
    print("\n1. Creating pipeline circuit...")
    circuit = create_pipeline_circuit()

    # Emit MLIR for inspection
    print("\n2. Generated CMT2 MLIR:")
    print("-" * 40)
    mlir = circuit.emit_mlir()
    print(mlir[:2000] if len(mlir) > 2000 else mlir)
    if len(mlir) > 2000:
        print(f"... ({len(mlir) - 2000} more characters)")
    print("-" * 40)

    # Setup workspace
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "pipeline_e2e_sim"

    if workspace_dir.exists():
        print(f"\n3. Removing existing workspace: {workspace_dir}")
        shutil.rmtree(workspace_dir)

    # Create testbench
    print("\n4. Creating testbench with test sequences...")
    tb = Testbench(circuit, auto_debug_ports=True)

    with tb.sequence("basic_test") as seq:
        seq.comment("Reset and initialize")
        seq.reset(5)
        seq.drive("compute_stage0_input", 5)
        seq.wait(6)
        seq.expect("compute_stage2_result_0", 12, "(5+1)*2 = 12")
        seq.drive("compute_stage0_input", 100)
        seq.wait(6)
        seq.expect("compute_stage2_result_0", 202, "(100+1)*2 = 202")

    with tb.sequence("value_test") as seq:
        seq.reset(5)
        seq.drive("compute_stage0_input", 0)
        seq.wait(6)
        seq.expect("compute_stage2_result_0", 2, "(0+1)*2 = 2")

    # Create simulation workspace
    print("\n5. Creating simulation workspace...")
    ws = SimulationWorkspace(circuit, workspace_dir, debug_ports=True)
    ws.generate_with_testbench(tb)
    print(f"   Workspace created at: {workspace_dir}")

    print("\n6. Building and running simulation...")
    ok, out = ws.build_and_run()
    if not ok:
        raise RuntimeError(out)
    print(out)

    return workspace_dir


def generate_and_test_forkjoin():
    """Generate and test the fork-join pipeline."""
    print("\n" + "=" * 70)
    print("Test 2: Fork-Join Pipeline E2E Test")
    print("=" * 70)

    # Create circuit
    print("\n1. Creating fork-join circuit...")
    circuit = create_forkjoin_circuit()

    # Emit MLIR for inspection
    print("\n2. Generated CMT2 MLIR:")
    print("-" * 40)
    mlir = circuit.emit_mlir()
    print(mlir[:2000] if len(mlir) > 2000 else mlir)
    if len(mlir) > 2000:
        print(f"... ({len(mlir) - 2000} more characters)")
    print("-" * 40)

    # Setup workspace
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "forkjoin_e2e_sim"

    if workspace_dir.exists():
        print(f"\n3. Removing existing workspace: {workspace_dir}")
        shutil.rmtree(workspace_dir)

    # Create testbench
    print("\n4. Creating testbench...")
    tb = Testbench(circuit, auto_debug_ports=True)

    with tb.sequence("forkjoin_test") as seq:
        seq.comment("Reset and initialize")
        seq.reset(5)

        seq.drive("diamond_source_input", 7)
        seq.wait(8)
        seq.expect("diamond_sum_result_0", 44, "2*7+30 = 44")
        seq.drive("diamond_source_input", 0)
        seq.wait(8)
        seq.expect("diamond_sum_result_0", 30, "2*0+30 = 30")

    # Create simulation workspace
    print("\n5. Creating simulation workspace...")
    ws = SimulationWorkspace(circuit, workspace_dir, debug_ports=True)
    ws.generate_with_testbench(tb)
    print(f"   Workspace created at: {workspace_dir}")

    print("\n6. Building and running simulation...")
    ok, out = ws.build_and_run()
    if not ok:
        raise RuntimeError(out)
    print(out)

    return workspace_dir


def check_verilator():
    """Return True if Verilator appears to be installed."""
    return shutil.which("verilator") is not None


def main():
    """Run all end-to-end pipeline tests."""
    print("=" * 70)
    print("Pipeline End-to-End Test Suite (JIT stacked on PyCMT2)")
    print("=" * 70)

    if not check_verilator():
        raise RuntimeError("Verilator not found (required for this E2E example)")

    # Test 1: Linear pipeline
    pipeline_workspace = generate_and_test_pipeline()

    # Test 2: Fork-join pipeline
    forkjoin_workspace = generate_and_test_forkjoin()

    print("\n" + "=" * 70)
    print("End-to-End Tests Complete!")
    print("=" * 70)
    print(f"""
Generated workspaces:
  - {pipeline_workspace}
  - {forkjoin_workspace}

Each workspace contains:
  - rtl/: Generated Verilog RTL
  - tb/: C++ testbench
  - Makefile: Build automation
  - README.md: Instructions
""")


if __name__ == "__main__":
    main()
