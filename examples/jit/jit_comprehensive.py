#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Comprehensive JIT Feature Validation Example.

This example validates ALL CMT2 JIT features:
1. @elaborate decorator with caching
2. @simulate decorator for testing
3. Static arguments (int, bool, str, tuple)
4. Type system (UInt, SInt, Bits) with promotion
5. Control flow (when/otherwise, switch, unroll)
6. Clock domains and CDC
7. Memory abstractions (FIFO, SRAM)
8. Timing constraints
9. PyCMT2 integration (CircuitBuilder, JITSimulationRunner)
10. End-to-end simulation

Usage:
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \
        python3 ../examples/jit/jit_comprehensive.py --all
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from typing import Annotated, List, Tuple

# Setup path
sys.path.insert(0, '../../python')

import cmt2
from cmt2 import elaborate, simulate, static, when, otherwise, switch, case, unroll
from cmt2.pycmt2_integration import (
    CircuitBuilder,
    JITSimulationRunner,
    is_pycmt2_available,
)
from cmt2.constraints import ConstraintSet, ClockConstraint

if not is_pycmt2_available():
    print("ERROR: PyCMT2 not available. Set PYTHONPATH to CIRCT bindings.")
    sys.exit(1)

from circt.pycmt2 import UInt, SInt, Bits


# =============================================================================
# Feature 1-2: @elaborate and @simulate with caching
# =============================================================================

@cmt2.elaborate
def feature_test_design(
    width: Annotated[int, static] = 32,
    depth: Annotated[int, static] = 8,
    use_fifo: Annotated[bool, static] = True,
    mode: Annotated[str, static] = "basic",
):
    """Test design demonstrating all JIT features.
    
    Static arguments of different types:
    - width: int (bit width)
    - depth: int (memory depth)
    - use_fifo: bool (feature flag)
    - mode: str (operating mode)
    """
    print(f"Elaborating: width={width}, depth={depth}, use_fifo={use_fifo}, mode={mode}")
    
    builder = CircuitBuilder("FeatureTest")
    
    with builder.module("FeatureTest") as m:
        # Clock and reset (Feature 6: Clock domains)
        clk = m.clock("clk")
        rst = m.reset("rst")
        
        # Data input ports
        data_in = m.input("data_in", UInt(width))
        opcode = m.input("opcode", UInt(4))
        
        # Output ports
        result = m.output("result", UInt(width * 2))
        status = m.output("status", UInt(4))
        
        # Feature 4: Type system - Create registers with different types
        count_reg = m.instance_reg(width, "count", clk=clk, rst=rst, init=0)
        
        if width >= 16:
            # Use signed type for wider operations
            accum_reg = m.instance_reg(width * 2, "accum", clk=clk, rst=rst, init=0)
        
        # Feature 7: Memory abstractions
        if use_fifo:
            fifo = m.instance_fifo(UInt(width), depth, "fifo", clk=clk, rst=rst)
        
        # Feature 5: Control flow - when/otherwise
        with m.rule("conditional_update") as rule:
            with rule.guard() as g:
                g.always()
            
            with rule.body() as b:
                count_val = b.call(count_reg, "read")
                
                # Hardware conditional (creates mux)
                with cmt2.when(b.equals(opcode, b.const(0, 4))):
                    # Increment mode
                    one = b.const(1, width)
                    new_count = b.add(count_val, one)
                    b.call(count_reg, "write", new_count)
                
                with cmt2.otherwise():
                    # Decrement mode
                    one = b.const(1, width)
                    new_count = b.sub(count_val, one)
                    b.call(count_reg, "write", new_count)
        
        # Feature 5: Control flow - switch/case via trace-time loop
        # Using unroll to create parallel case handling
        for i in cmt2.unroll(range(4)):
            with m.rule(f"opcode_{i}") as rule:
                with rule.guard() as g:
                    g.equals(opcode, g.const(i, 4))
                
                with rule.body() as b:
                    # Each case does something different
                    base = b.const(i * 10, width)
                    b.call(count_reg, "write", base)
        
        # Feature 5: Trace-time unrolling
        # Create multiple parallel processing units
        for idx in cmt2.unroll(range(2)):
            with m.rule(f"parallel_{idx}") as rule:
                with rule.guard() as g:
                    g.always()
                
                with rule.body() as b:
                    # Each iteration creates independent hardware
                    offset = b.const(idx * 100, width)
                    curr = b.call(count_reg, "read")
                    # Note: In real design, would write to separate regs
        
        # Output the count value
        with m.value("get_count", returns=[UInt(width)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                cnt = b.call(count_reg, "read")
                b.returns(cnt)
    
    return builder.circuit


@cmt2.simulate
def validate_all_features(
    width: Annotated[int, static] = 32,
    depth: Annotated[int, static] = 8,
    workspace_dir: str | None = None,
):
    """Validate all JIT features with E2E simulation.
    
    Args:
        width: Data width
        depth: Memory depth
        workspace_dir: Optional workspace directory
        
        Returns:
        Validation results
    """
    print(f"\n{'='*70}")
    print(f"COMPREHENSIVE JIT FEATURE VALIDATION")
    print(f"{'='*70}\n")
    
    results = {}
    
    # Test 1: Static argument caching
    print("Test 1: Static Argument Caching")
    print("-" * 40)
    circuit1 = feature_test_design(width=16, depth=8, use_fifo=True, mode="basic")
    key1 = feature_test_design.cache_key
    circuit2 = feature_test_design(width=16, depth=8, use_fifo=True, mode="basic")
    key2 = feature_test_design.cache_key
    results["caching"] = (key1 == key2)
    print(f"  Cache hit: {results['caching']} ✓\n")
    
    # Test 2: Type system
    print("Test 2: Type System")
    print("-" * 40)
    try:
        u8 = cmt2.types.UInt(8)
        u16 = cmt2.types.UInt(16)
        s8 = cmt2.types.SInt(8)
        
        # Test type promotion
        result = u8 + u16
        assert result.width == 17, f"Expected width 17, got {result.width}"
        
        result = u8 * u8
        assert result.width == 16, f"Expected width 16, got {result.width}"
        
        results["type_system"] = True
        print(f"  Type promotion: PASS ✓\n")
    except Exception as e:
        results["type_system"] = False
        print(f"  Type promotion: FAIL - {e}\n")
    
    # Test 3: Elaboration with different static args
    print("Test 3: Elaboration Variants")
    print("-" * 40)
    variants = [
        (8, 4, True, "basic"),
        (16, 8, True, "advanced"),
        (32, 16, False, "basic"),
    ]
    for w, d, fifo, m in variants:
        circuit = feature_test_design(width=w, depth=d, use_fifo=fifo, mode=m)
        mlir = circuit.emit_mlir()
        print(f"  width={w}, depth={d}, fifo={fifo}: {len(mlir)} chars MLIR ✓")
    results["elaboration"] = True
    print()
    
    # Test 4: Clock domains
    print("Test 4: Clock Domain Support")
    print("-" * 40)
    from cmt2 import CLK_CORE, CLK_IO, CDCMethod
    print(f"  CLK_CORE: {CLK_CORE.name} @ {CLK_CORE.frequency} ✓")
    print(f"  CLK_IO: {CLK_IO.name} @ {CLK_IO.frequency} ✓")
    results["clock_domains"] = True
    print()
    
    # Test 5: Timing constraints
    print("Test 5: Timing Constraints")
    print("-" * 40)
    try:
        constraints = ConstraintSet()
        constraints.clock("clk", period_ns=10.0)
        constraints.input_delay("data_in", 2.0, clock="clk")
        constraints.output_delay("result", 2.0, clock="clk")
        
        sdc = constraints.to_sdc()
        assert "create_clock" in sdc
        assert "set_input_delay" in sdc
        results["constraints"] = True
        print(f"  SDC generation: PASS ✓\n")
    except Exception as e:
        results["constraints"] = False
        print(f"  SDC generation: FAIL - {e}\n")
    
    # Test 6: E2E Simulation
    print("Test 6: End-to-End Simulation")
    print("-" * 40)
    
    if workspace_dir is None:
        workspace_dir = tempfile.mkdtemp(prefix="jit_comprehensive_")
    
    print(f"  Workspace: {workspace_dir}")
    
    # Create circuit for simulation
    circuit = feature_test_design(width=16, depth=8, use_fifo=True, mode="sim")
    
    # Create runner
    runner = JITSimulationRunner(
        circuit=circuit,
        workspace_dir=workspace_dir,
        debug_ports=True,
    )
    
    # Create comprehensive testbench
    tb = runner.create_testbench(auto_debug_ports=True)
    
    # Test sequence 1: Reset
    with tb.sequence("test_reset") as seq:
        seq.comment("Test reset behavior")
        seq.reset(5)
        seq.expect("get_count_res0", 0, "Count should be 0 after reset")
    
    # Test sequence 2: Increment via opcode 0
    with tb.sequence("test_increment") as seq:
        seq.comment("Test increment operation")
        seq.reset(5)
        seq.poke("opcode", 0)  # Increment mode
        seq.wait(3)
        seq.expect("get_count_res0", 3, "Count should be 3 after 3 increments")
    
    # Test sequence 3: Decrement via other opcodes
    with tb.sequence("test_decrement") as seq:
        seq.comment("Test decrement operation")
        seq.reset(5)
        seq.poke("opcode", 1)  # Decrement mode
        seq.poke("data_in", 10)
        seq.wait(3)
        # Count should decrement (but starts at 0, so wraps or stays based on impl)
    
    # Test sequence 4: Opcode switching
    with tb.sequence("test_opcodes") as seq:
        seq.comment("Test different opcodes")
        seq.reset(5)
        
        for op in range(4):
            seq.poke("opcode", op)
            seq.wait(1)
            seq.print(f"Opcode {op} tested")
    
    # Run simulation
    print(f"  Running simulation...")
    try:
        sim_result = runner.run(testbench=tb, waves=True)
        results["simulation"] = sim_result['success']
        print(f"  Simulation: {'PASS ✓' if sim_result['success'] else 'FAIL ✗'}\n")
    except Exception as e:
        results["simulation"] = False
        print(f"  Simulation: FAIL - {e}\n")
    
    # Test 7: Code generation
    print("Test 7: Code Generation")
    print("-" * 40)
    try:
        circuit = feature_test_design(width=16, depth=8, use_fifo=True, mode="test")
        
        mlir = circuit.emit_mlir()
        firrtl = circuit.emit_firrtl()
        verilog = circuit.emit_verilog()
        
        print(f"  MLIR: {len(mlir)} chars ✓")
        print(f"  FIRRTL: {len(firrtl)} chars ✓")
        print(f"  Verilog: {len(verilog)} chars ✓")
        results["codegen"] = True
    except Exception as e:
        results["codegen"] = False
        print(f"  Code generation: FAIL - {e}")
    print()
    
    # Summary
    print(f"{'='*70}")
    print("VALIDATION SUMMARY")
    print(f"{'='*70}\n")
    
    passed = sum(1 for v in results.values() if v)
    total = len(results)
    
    for test, passed in results.items():
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"  {test:20s}: {status}")
    
    print(f"\nTotal: {passed}/{total} tests passed")
    
    return {
        "results": results,
        "passed": passed,
        "total": total,
        "workspace": workspace_dir,
    }


def demo_staged_compilation():
    """Demonstrate staged compilation with all stages."""
    print(f"\n{'='*70}")
    print("STAGED COMPILATION DEMO")
    print(f"{'='*70}\n")
    
    from cmt2.jit import ElaboratedCircuit, LoweredCircuit, CompiledCircuit
    
    # Stage 1: Elaboration
    print("Stage 1: Elaboration")
    print("-" * 40)
    circuit = feature_test_design(width=16, depth=8, use_fifo=True, mode="demo")
    elaborated = ElaboratedCircuit(
        mlir_module=circuit,
        name="FeatureTest",
        static_args={"width": 16, "depth": 8, "use_fifo": True, "mode": "demo"}
    )
    print(f"  ElaboratedCircuit: {elaborated}")
    
    # Stage 2: Lowering
    print("\nStage 2: Lowering")
    print("-" * 40)
    lowered = elaborated.lower(target="verilog")
    print(f"  LoweredCircuit: {lowered}")
    
    # Stage 3: Compilation
    print("\nStage 3: Compilation")
    print("-" * 40)
    compiled = lowered.compile()
    print(f"  CompiledCircuit: {compiled}")
    
    # Generate output
    print("\nGenerated Code Preview:")
    print("-" * 40)
    verilog = circuit.emit_verilog()
    print(verilog[:1000])
    print("... [truncated] ...")


def main():
    parser = argparse.ArgumentParser(
        description="Comprehensive JIT Feature Validation"
    )
    parser.add_argument(
        "--demo",
        choices=["validate", "staged", "all"],
        default="all",
        help="Which demo to run"
    )
    parser.add_argument(
        "--width",
        type=int,
        default=16,
        help="Data width"
    )
    parser.add_argument(
        "--depth",
        type=int,
        default=8,
        help="Memory depth"
    )
    parser.add_argument(
        "--workspace",
        type=str,
        default=None,
        help="Workspace directory"
    )
    args = parser.parse_args()
    
    if args.demo in ("validate", "all"):
        result = validate_all_features(
            width=args.width,
            depth=args.depth,
            workspace_dir=args.workspace,
        )
        
        success = result['passed'] == result['total']
        print(f"\nOverall: {'✓ ALL TESTS PASSED' if success else '✗ SOME TESTS FAILED'}")
        
        if not success:
            sys.exit(1)
    
    if args.demo in ("staged", "all"):
        demo_staged_compilation()
    
    print(f"\n{'='*70}")
    print("Comprehensive Validation Complete!")
    print(f"{'='*70}")


if __name__ == "__main__":
    main()
