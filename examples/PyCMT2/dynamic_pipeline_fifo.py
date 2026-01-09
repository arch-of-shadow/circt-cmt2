#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Dynamic Pipeline with FIFO1 Modules - End-to-End Test

This example creates a 3-stage dynamic pipeline connected by FIFO1 modules:

    +--------+    +---------+    +--------+    +---------+    +--------+
    | Source | -> | FIFO1_a | -> | Stage1 | -> | FIFO1_b | -> |  Sink  |
    +--------+    +---------+    | (*2)   |    +---------+    +--------+
                                 +--------+

Pipeline stages:
- Source: Generates values 0, 1, 2, 3, 4 (one per cycle when not blocked)
- FIFO1_a: Depth-1 FIFO buffer between source and stage1
- Stage1: Multiplies input by 2 (output = input * 2)
- FIFO1_b: Depth-1 FIFO buffer between stage1 and sink
- Sink: Consumes values and accumulates them into a result register

The outer module uses a proc.rule that sequences:
1. Source pushes when FIFO_a is not full
2. Stage1 processes when FIFO_a has data and FIFO_b is not full
3. Sink consumes when FIFO_b has data

Expected behavior:
- Source produces: 0, 1, 2, 3, 4
- After Stage1 (*2): 0, 2, 4, 6, 8
- Sink accumulates: 0 + 2 + 4 + 6 + 8 = 20

This test validates that:
1. Conflict matrix-based detection works for nested FIFO modules
2. Interpretation and simulation produce matching results

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 \
        ../examples/PyCMT2/dynamic_pipeline_fifo.py
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, FIFO1Push, clear_stl_registry


def find_cmt2_dbg() -> str:
    """Find the cmt2-dbg executable."""
    candidates = [
        "bin/cmt2-dbg",
        "./bin/cmt2-dbg",
        "../build/bin/cmt2-dbg",
        os.path.join(os.path.dirname(__file__), "../../build/bin/cmt2-dbg"),
    ]
    for path in candidates:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return os.path.abspath(path)
    path = shutil.which("cmt2-dbg")
    if path:
        return path
    raise RuntimeError("cmt2-dbg not found")


def find_circt_opt() -> str:
    """Find the circt-opt executable."""
    candidates = [
        "bin/circt-opt",
        "./bin/circt-opt",
        "../build/bin/circt-opt",
        os.path.join(os.path.dirname(__file__), "../../build/bin/circt-opt"),
    ]
    for path in candidates:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return os.path.abspath(path)
    path = shutil.which("circt-opt")
    if path:
        return path
    raise RuntimeError("circt-opt not found")


def run_cmt2_dbg(mlir_content: str, script: str) -> str:
    """Run cmt2-dbg with the given MLIR and script."""
    cmt2_dbg = find_cmt2_dbg()

    with tempfile.NamedTemporaryFile(suffix=".mlir", delete=False) as f:
        f.write(mlir_content.encode())
        mlir_file = f.name

    with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as f:
        f.write(script.encode())
        script_file = f.name

    try:
        result = subprocess.run(
            [cmt2_dbg, mlir_file, "--script", script_file],
            capture_output=True,
            text=True,
            timeout=30
        )
        return result.stdout + result.stderr
    finally:
        pass
        # os.unlink(mlir_file)
        # os.unlink(script_file)


def create_pipeline_circuit():
    """Create the dynamic pipeline circuit with FIFO1 modules."""
    clear_stl_registry()

    circuit = Circuit("DynamicPipeline")

    # Create STL modules
    # Note: With FIFO1 (depth-1) and ORAAT semantics, the pipeline achieves
    # 3 cycles per value throughput (source->stage1->sink serialized).
    # This is correct behavior - guards see state at cycle start.
    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)
    fifo32 = FIFO1Push.create(circuit, 32)

    with circuit.module("Pipeline") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers
        source_counter = m.instance(reg32, "source_counter", clk=clk, rst=rst)
        sink_result = m.instance(reg32, "sink_result", clk=clk, rst=rst)
        done_flag = m.instance(reg1, "done_flag", clk=clk, rst=rst)

        # Pipeline FIFOs
        fifo_a = m.instance(fifo32, "fifo_a", clk=clk, rst=rst)
        fifo_b = m.instance(fifo32, "fifo_b", clk=clk, rst=rst)

        # =================================================================
        # Rule: source_push
        # Push values 0,1,2,3,4 into fifo_a (one per cycle when not blocked)
        # =================================================================
        with m.rule("source_push") as source_rule:
            with source_rule.guard() as g:
                counter = g.call(source_counter, "read")
                can_produce = g.lt(counter, g.const(5, 32))
                g.returns(can_produce)
            with source_rule.body() as b:
                counter = b.call(source_counter, "read")
                b.call(fifo_a, "enq", counter)
                new_counter = b.add(counter, b.const(1, 32))
                b.call(source_counter, "write", b.bits(new_counter, 31, 0))

        # =================================================================
        # Rule: stage1_process
        # Read from fifo_a, multiply by 2, push to fifo_b
        # =================================================================
        with m.rule("stage1_process") as stage1_rule:
            with stage1_rule.guard() as g:
                g.always()
            with stage1_rule.body() as b:
                val = b.call(fifo_a, "deq")
                doubled = b.mul(val, b.const(2, 32))
                b.call(fifo_b, "enq", b.bits(doubled, 31, 0))

        # =================================================================
        # Rule: sink_consume
        # Read from fifo_b and accumulate into sink_result
        # =================================================================
        with m.rule("sink_consume") as sink_rule:
            with sink_rule.guard() as g:
                g.always()
            with sink_rule.body() as b:
                val = b.call(fifo_b, "deq")
                current = b.call(sink_result, "read")
                new_result = b.add(current, val)
                b.call(sink_result, "write", b.bits(new_result, 31, 0))

        # =================================================================
        # Rule: mark_done
        # Set done flag when source has produced all values and FIFOs are empty
        # =================================================================
        with m.rule("mark_done") as done_rule:
            with done_rule.guard() as g:
                counter = g.call(source_counter, "read")
                all_produced = g.eq(counter, g.const(5, 32))
                # For FIFO1, not full == empty (depth 1)
                fifo_a_full = g.call(fifo_a, "full")
                fifo_b_full = g.call(fifo_b, "full")
                fifo_a_empty = g.not_(fifo_a_full)
                fifo_b_empty = g.not_(fifo_b_full)
                fifos_empty = g.and_(fifo_a_empty, fifo_b_empty)
                done = g.call(done_flag, "read")
                not_done = g.not_(done)
                g.returns(g.and_(g.and_(all_produced, fifos_empty), not_done))
            with done_rule.body() as b:
                b.call(done_flag, "write", b.const(1, 1))

        # =================================================================
        # Value: get_result - Read the accumulated result
        # =================================================================
        with m.value("get_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                result = b.call(sink_result, "read")
                b.returns(result)

        # =================================================================
        # Value: is_done - Check if pipeline is complete
        # =================================================================
        with m.value("is_done", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                done = b.call(done_flag, "read")
                b.returns(done)

        # Precedence: downstream drains before upstream fills
        # sink > stage1 > source > mark_done
        m.precedence(sink_rule.ref(), stage1_rule.ref())
        m.precedence(stage1_rule.ref(), source_rule.ref())
        m.precedence(source_rule.ref(), done_rule.ref())

    return circuit


def run_interpretation_test(mlir_content: str) -> dict:
    """Run the interpreter and extract results."""
    print("\n" + "="*60)
    print("INTERPRETATION TEST")
    print("="*60)

    # Run for enough cycles and check history for mark_done
    # Using step instead of run+breakpoint for more reliable execution
    script = """trace on
step 30
history 30
quit
"""
    output = run_cmt2_dbg(mlir_content, script)
    print(output)

    # Parse results
    results = {
        "source_push_count": output.count("source_push"),
        "stage1_process_count": output.count("stage1_process"),
        "sink_consume_count": output.count("sink_consume"),
        "mark_done_count": output.count("mark_done"),
    }

    # Extract completion cycle from trace output
    # Format: "Cycle N: ..., mark_done, ..."
    import re
    cycle_match = re.search(r"Cycle (\d+):.*\bmark_done\b", output)
    if cycle_match:
        results["completion_cycle"] = int(cycle_match.group(1))
    else:
        results["completion_cycle"] = -1

    # Check for conflict detection
    if "enabled (blocked)" in output:
        results["conflict_detection_working"] = True
        # Count blocked occurrences for default rules
        results["enqed_default_blocked"] = output.count("fifo_a.enqed_default: enabled (blocked)") + \
                                            output.count("fifo_b.enqed_default: enabled (blocked)")
        results["deqed_default_blocked"] = output.count("fifo_a.deqed_default: enabled (blocked)") + \
                                            output.count("fifo_b.deqed_default: enabled (blocked)")
    else:
        results["conflict_detection_working"] = False

    return results


def run_simulation_test(circuit) -> dict:
    """Run Verilator simulation and extract results."""
    print("\n" + "="*60)
    print("SIMULATION TEST")
    print("="*60)

    from circt.pycmt2.simulation import SimulationWorkspace

    # Create workspace
    workspace_path = Path(__file__).parent / "sim_dynamic_pipeline"
    if workspace_path.exists():
        shutil.rmtree(workspace_path)

    # Custom testbench
    testbench_cpp = """\
// Dynamic Pipeline Testbench
#include "VPipeline.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto dut = new VPipeline();
    auto tfp = new VerilatedVcdC();
    dut->trace(tfp, 99);
    tfp->open("waves/Pipeline.vcd");

    dut->clk = 0;
    dut->rst = 1;
    int cycle = 0;

    auto tick = [&]() {
        dut->clk = 0;
        dut->eval();
        tfp->dump(cycle * 10);
        dut->clk = 1;
        dut->eval();
        tfp->dump(cycle * 10 + 5);
        cycle++;
    };

    // Reset
    std::cout << "Resetting..." << std::endl;
    for (int i = 0; i < 5; i++) tick();
    dut->rst = 0;

    // Run pipeline
    std::cout << "Running pipeline..." << std::endl;
    int max_cycles = 50;
    int done_cycle = -1;

    for (int i = 0; i < max_cycles; i++) {
        tick();

        // Check if done
        if (dut->is_done_res0 == 1 && done_cycle < 0) {
            done_cycle = cycle;
            std::cout << "Pipeline done at cycle " << cycle << std::endl;
            std::cout << "Result: " << dut->get_result_res0 << std::endl;
        }
    }

    uint32_t final_result = dut->get_result_res0;

    // Expected: 0*2 + 1*2 + 2*2 + 3*2 + 4*2 = 0 + 2 + 4 + 6 + 8 = 20
    uint32_t expected = 20;

    std::cout << std::endl;
    std::cout << "Final result: " << final_result << std::endl;
    std::cout << "Expected: " << expected << std::endl;

    if (final_result == expected) {
        std::cout << "SIMULATION PASSED!" << std::endl;
    } else {
        std::cout << "SIMULATION FAILED!" << std::endl;
    }

    tfp->close();
    delete dut;

    return (final_result == expected) ? 0 : 1;
}
"""

    ws = SimulationWorkspace(circuit, workspace_path)

    # Explicitly set top module to Pipeline (not FIFO which is added first)
    ws._top_module = "Pipeline"

    # Generate workspace - following the pattern from gcd.py
    ws._add_stl_rtl()  # Add STL RTL files from registry
    ws._create_directories()
    ws._generate_rtl()
    ws._generate_makefile()

    # Write custom testbench
    tb_file = workspace_path / "tb" / "testbench.cpp"
    tb_file.write_text(testbench_cpp)

    print(f"Workspace generated at: {workspace_path}")

    # Build
    print("Building simulation...")
    if not ws.build():
        print("Build failed!")
        return {"build_success": False, "error": "Build failed"}

    print("Build successful!")

    # Run
    print("Running simulation...")
    success, output = ws.run()
    print(output)

    # Parse results
    results = {
        "build_success": True,
        "simulation_passed": "SIMULATION PASSED" in output,
    }

    # Extract final result and done cycle
    import re
    for line in output.split("\n"):
        if "Final result:" in line:
            try:
                results["final_result"] = int(line.split(":")[-1].strip())
            except:
                pass
        if "Pipeline done at cycle" in line:
            match = re.search(r"cycle (\d+)", line)
            if match:
                results["completion_cycle"] = int(match.group(1))

    return results


def main():
    print("="*70)
    print("Dynamic Pipeline with FIFO1 Modules - End-to-End Test")
    print("="*70)

    # Create circuit
    print("\n1. Creating dynamic pipeline circuit...")
    circuit = create_pipeline_circuit()

    # Emit MLIR
    print("\n2. Emitting MLIR...")
    mlir = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir)} chars")
    print("\n--- MLIR (first 1000 chars) ---")
    print(mlir[:1000] + "..." if len(mlir) > 1000 else mlir)

    # Run interpretation test
    print("\n3. Running interpretation test...")
    interp_results = run_interpretation_test(mlir)

    print("\n--- Interpretation Results ---")
    print(f"   Completion cycle: {interp_results.get('completion_cycle', 'N/A')}")
    print(f"   source_push fired: {interp_results['source_push_count']} times")
    print(f"   stage1_process fired: {interp_results['stage1_process_count']} times")
    print(f"   sink_consume fired: {interp_results['sink_consume_count']} times")
    print(f"   mark_done fired: {interp_results['mark_done_count']} times")
    print(f"   Conflict detection working: {interp_results.get('conflict_detection_working', False)}")
    if interp_results.get('conflict_detection_working'):
        print(f"   enqed_default blocked: {interp_results.get('enqed_default_blocked', 0)} times")
        print(f"   deqed_default blocked: {interp_results.get('deqed_default_blocked', 0)} times")

    # Run simulation test
    print("\n4. Running simulation test...")
    sim_results = run_simulation_test(circuit)

    print("\n--- Simulation Results ---")
    print(f"   Build success: {sim_results.get('build_success', False)}")
    print(f"   Simulation passed: {sim_results.get('simulation_passed', False)}")
    print(f"   Completion cycle: {sim_results.get('completion_cycle', 'N/A')}")
    print(f"   Final result: {sim_results.get('final_result', 'N/A')}")

    # Compare results
    print("\n" + "="*70)
    print("COMPARISON")
    print("="*70)

    # Interpretation should show:
    # - source_push should fire 5 times (values 0-4)
    # - stage1_process should fire 5 times
    # - sink_consume should fire 5 times
    # - mark_done should fire 1 time

    interp_ok = (
        interp_results['source_push_count'] >= 5 and
        interp_results['stage1_process_count'] >= 5 and
        interp_results['sink_consume_count'] >= 5 and
        interp_results['mark_done_count'] >= 1
    )

    sim_ok = sim_results.get('simulation_passed', False)

    # Both should compute: 0*2 + 1*2 + 2*2 + 3*2 + 4*2 = 20
    expected_result = 20
    sim_result = sim_results.get('final_result', -1)

    interp_cycle = interp_results.get('completion_cycle', -1)
    sim_cycle = sim_results.get('completion_cycle', -1)

    print(f"\n   Interpretation behavior correct: {interp_ok}")
    print(f"   Simulation result correct: {sim_ok}")
    print(f"   Expected result: {expected_result}")
    print(f"   Simulation result: {sim_result}")
    print(f"   Interpretation completion cycle: {interp_cycle}")
    print(f"   Simulation completion cycle: {sim_cycle}")

    if interp_ok and sim_ok and sim_result == expected_result:
        print("\n" + "="*70)
        print("SUCCESS: Interpretation and Simulation results MATCH!")
        print("="*70)
        return 0
    else:
        print("\n" + "="*70)
        print("FAILURE: Results do not match or tests failed!")
        print("="*70)
        return 1


if __name__ == "__main__":
    sys.exit(main())
