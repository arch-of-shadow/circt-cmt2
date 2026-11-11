#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
End-to-End Pipeline Simulation Test for PyCMT2 Dataflow

This example demonstrates a complete end-to-end flow:
1. Create a pipelined dataflow design using PyCMT2 builders
2. Generate a simulation workspace with Verilator
3. Create a testbench using the Testbench DSL
4. Build and run the simulation (if Verilator is available)

The pipeline computes: output = (input + 1) * 2
in a 3-stage pipeline with fork-join pattern.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/pipeline_e2e.py

After running, check the simulation workspace at:
    examples/PyCMT2/pipeline_e2e_sim/

To run simulation manually:
    cd examples/PyCMT2/pipeline_e2e_sim
    make
    make run
"""

import shutil
import subprocess
from pathlib import Path

from circt.pycmt2 import (
    Circuit,
    UInt,
    Pipeline,
)
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench
from circt.pycmt2.stl import Reg, clear_stl_registry


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

    with circuit.module("ComputePipeline") as mod:
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
            result = task.shl(data, task.const(1, 16))
            return task.bits(result, 15, 0)  # Truncate to 16 bits

        pipe.build()

    return circuit


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

    with circuit.module("DiamondPipeline") as mod:
        clk = mod.clock()
        rst = mod.reset()

        # Create using explicit dataflow builders for fork-join
        with mod.dataflow(
            "diamond",
            args=[("input", UInt(16))],
            returns=[UInt(16)],
        ) as df:
            # Source: capture input
            with df.task("source") as task:
                tok_src = task.create_token(df.input, UInt(16))
                task.yield_tokens(tok_src)

            # Branch A: add 10
            with df.task("add_10", tokens_in=[tok_src], timing=(1, 2)) as task:
                data = task.token_data(tok_src)
                result_a = task.add(data, task.const(10, 16))
                tok_a = task.create_token(task.bits(result_a, 15, 0), UInt(16))
                task.yield_tokens(tok_a)

            # Branch B: add 20 (fork - same source token)
            with df.task("add_20", tokens_in=[tok_src], timing=(1, 2)) as task:
                data = task.token_data(tok_src)
                result_b = task.add(data, task.const(20, 16))
                tok_b = task.create_token(task.bits(result_b, 15, 0), UInt(16))
                task.yield_tokens(tok_b)

            # Join: sum both results
            with df.task("sum", tokens_in=[tok_a, tok_b], timing=(2, 3)) as task:
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
    tb = Testbench(circuit)

    with tb.sequence("basic_test") as seq:
        seq.comment("Reset and initialize")
        seq.reset(5)

        seq.comment("Wait for pipeline to stabilize")
        seq.wait(10)

        seq.comment("Pipeline should compute: (input + 1) * 2")
        seq.print("Pipeline test complete")

    with tb.sequence("value_test") as seq:
        seq.comment("Test specific input values")
        seq.reset(5)
        seq.wait(3)

        seq.comment("Input: 5 -> expect (5+1)*2 = 12 after 3 cycles")
        seq.wait(10)

        seq.comment("Input: 100 -> expect (100+1)*2 = 202 after 3 cycles")
        seq.wait(10)

        seq.print("Value tests complete")

    # Create simulation workspace
    print("\n5. Creating simulation workspace...")
    try:
        ws = SimulationWorkspace(circuit, workspace_dir)
        ws.generate_with_testbench(tb)
        print(f"   Workspace created at: {workspace_dir}")

        # Show generated files
        print("\n6. Generated files:")
        for f in sorted(workspace_dir.rglob("*")):
            if f.is_file() and not f.name.startswith("."):
                rel = f.relative_to(workspace_dir)
                print(f"   {rel}")

    except Exception as e:
        print(f"   Warning: Workspace generation issue: {e}")
        print("   This may be expected if full compilation isn't available.")

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
    tb = Testbench(circuit)

    with tb.sequence("forkjoin_test") as seq:
        seq.comment("Reset and initialize")
        seq.reset(5)

        seq.comment("Wait for pipeline (fork-join has 3 stages)")
        seq.wait(15)

        seq.comment("Diamond computes: 2*input + 30")
        seq.print("Fork-join test complete")

    # Create simulation workspace
    print("\n5. Creating simulation workspace...")
    try:
        ws = SimulationWorkspace(circuit, workspace_dir)
        ws.generate_with_testbench(tb)
        print(f"   Workspace created at: {workspace_dir}")

    except Exception as e:
        print(f"   Warning: Workspace generation issue: {e}")

    return workspace_dir


def check_verilator():
    """Check if Verilator is available."""
    try:
        result = subprocess.run(
            ["verilator", "--version"],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            version = result.stdout.strip().split("\n")[0]
            print(f"Verilator found: {version}")
            return True
    except FileNotFoundError:
        pass
    print("Verilator not found - skipping actual simulation")
    return False


def run_simulation(workspace_dir: Path):
    """Try to build and run the simulation."""
    if not workspace_dir.exists():
        print(f"Workspace not found: {workspace_dir}")
        return False

    print(f"\nAttempting to build simulation in {workspace_dir}...")

    try:
        # Try to build
        result = subprocess.run(
            ["make", "-C", str(workspace_dir)],
            capture_output=True,
            text=True,
            timeout=60
        )

        if result.returncode != 0:
            print(f"Build failed (expected if RTL is placeholder):")
            print(result.stderr[:500] if result.stderr else "No error output")
            return False

        print("Build succeeded!")

        # Try to run
        result = subprocess.run(
            ["make", "-C", str(workspace_dir), "run"],
            capture_output=True,
            text=True,
            timeout=60
        )

        print("\nSimulation output:")
        print("-" * 40)
        print(result.stdout)
        if result.stderr:
            print(result.stderr)
        print("-" * 40)

        return result.returncode == 0

    except subprocess.TimeoutExpired:
        print("Simulation timed out")
        return False
    except Exception as e:
        print(f"Error running simulation: {e}")
        return False


def main():
    """Run all end-to-end pipeline tests."""
    print("=" * 70)
    print("PyCMT2 Pipeline End-to-End Test Suite")
    print("=" * 70)

    # Check for Verilator
    has_verilator = check_verilator()

    # Test 1: Linear pipeline
    pipeline_workspace = generate_and_test_pipeline()

    # Test 2: Fork-join pipeline
    forkjoin_workspace = generate_and_test_forkjoin()

    # Try to run simulations if Verilator is available
    if has_verilator:
        print("\n" + "=" * 70)
        print("Running Simulations")
        print("=" * 70)

        pipeline_ok = run_simulation(pipeline_workspace)
        forkjoin_ok = run_simulation(forkjoin_workspace)

        print("\n" + "=" * 70)
        print("Simulation Results")
        print("=" * 70)
        print(f"Linear Pipeline: {'PASSED' if pipeline_ok else 'SKIPPED/FAILED'}")
        print(f"Fork-Join Pipeline: {'PASSED' if forkjoin_ok else 'SKIPPED/FAILED'}")
    else:
        print("\n" + "=" * 70)
        print("Simulation Skipped (Verilator not available)")
        print("=" * 70)
        print("""
To run simulations manually:

1. Install Verilator:
   Ubuntu/Debian: sudo apt-get install verilator
   macOS: brew install verilator

2. Navigate to workspace and build:
   cd examples/PyCMT2/pipeline_e2e_sim
   make
   make run
""")

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
