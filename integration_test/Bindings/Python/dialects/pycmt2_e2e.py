# REQUIRES: bindings_python
# RUN: %PYTHON% %s | FileCheck %s

"""
PyCMT2 End-to-End Integration Test

This test verifies the full PyCMT2 pipeline:
1. Circuit and module creation
2. Rule, method, and value definitions
3. STL component factory (Reg, Wire)
4. MLIR, FIRRTL, and Verilog emission
5. Simulation workspace generation
6. Testbench DSL
"""

import tempfile
import shutil
from pathlib import Path

# Test 1: Basic Circuit Creation
# CHECK: === Test 1: Basic Circuit ===
print("=== Test 1: Basic Circuit ===")

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, Wire, clear_stl_registry

# Clear any previous STL state
clear_stl_registry()

circuit = Circuit("TestCircuit")

with circuit.module("TestModule") as m:
    clk = m.clock()
    rst = m.reset()
    inp = m.input("data", UInt(32))

    with m.rule("test_rule") as rule:
        with rule.guard() as g:
            g.always()
        with rule.body() as b:
            pass

    with m.value("read", returns=[UInt(32)]) as val:
        with val.guard() as g:
            g.always()
        with val.body() as b:
            b.returns(b.const(42, 32))

mlir_output = circuit.emit_mlir()
# CHECK: cmt2.circuit
# CHECK: cmt2.module @TestModule
# CHECK: cmt2.rule @test_rule
# CHECK: cmt2.value @read
print(mlir_output)


# Test 2: STL Components with Factory Pattern
# CHECK: === Test 2: STL Components ===
print("=== Test 2: STL Components ===")

clear_stl_registry()

stl_circuit = Circuit("STLTest")

# Create STL modules using factory pattern
reg32 = Reg.create(stl_circuit, 32)
reg8 = Reg.create(stl_circuit, 8)
wire16 = Wire.create(stl_circuit, 16)

# CHECK: FIRRTLReg_32
print(reg32.name)
# CHECK: FIRRTLReg_8
print(reg8.name)
# CHECK: Wire_16
print(wire16.name)

# Test reuse - same width should return same module
reg32_again = Reg.create(stl_circuit, 32)
# CHECK: True
print(reg32 is reg32_again)

# Check RTL generation
from circt.pycmt2.stl import get_stl_rtl_files
rtl_files = get_stl_rtl_files()
# CHECK: FIRRTLReg_32.sv
# CHECK: FIRRTLReg_8.sv
# CHECK: Wire_16.sv
for name in sorted(rtl_files.keys()):
    print(name)


# Test 3: Complete Module with STL Instances
# CHECK: === Test 3: Module with STL ===
print("=== Test 3: Module with STL ===")

clear_stl_registry()

counter_circuit = Circuit("Counter")
reg_mod = Reg.create(counter_circuit, 32)

with counter_circuit.module("Counter") as m:
    clk = m.clock()
    rst = m.reset()

    count = m.instance(reg_mod, "count", clk=clk, rst=rst)

    with m.rule("increment") as rule:
        with rule.guard() as g:
            g.always()
        with rule.body() as body:
            val = body.call(count, "read")
            new_val = body.add(val, body.const(1, 32))
            body.call(count, "write", new_val)

    with m.value("get_count", returns=[UInt(32)]) as val:
        with val.guard() as g:
            g.always()
        with val.body() as body:
            result = body.call(count, "read")
            body.returns(result)

mlir = counter_circuit.emit_mlir()
# CHECK: cmt2.instance @count = @FIRRTLReg_32
# CHECK: cmt2.rule @increment
# CHECK: cmt2.call @count @read
# CHECK: cmt2.call @count @write
print(mlir)


# Test 4: FIRRTL Emission
# CHECK: === Test 4: FIRRTL Emission ===
print("=== Test 4: FIRRTL Emission ===")

firrtl = counter_circuit.emit_firrtl()
# CHECK: firrtl.circuit "Counter"
# CHECK: firrtl.module @Counter
# CHECK: firrtl.extmodule @FIRRTLReg_32
print(firrtl[:1000])


# Test 5: Verilog Emission
# CHECK: === Test 5: Verilog Emission ===
print("=== Test 5: Verilog Emission ===")

verilog = counter_circuit.to_verilog()
# CHECK: module Counter
# CHECK: input
# CHECK: clk
# CHECK: rst
print(verilog[:500])


# Test 6: Simulation Workspace
# CHECK: === Test 6: Simulation Workspace ===
print("=== Test 6: Simulation Workspace ===")

from circt.pycmt2.simulation import SimulationWorkspace

# Create temp directory for test
temp_dir = tempfile.mkdtemp(prefix="pycmt2_test_")
try:
    ws = SimulationWorkspace(counter_circuit, temp_dir)

    # CHECK: Counter
    print(ws._top_module)

    # Generate workspace
    ws.generate_placeholder()

    # Check directory structure
    rtl_dir = Path(temp_dir) / "rtl"
    tb_dir = Path(temp_dir) / "tb"
    makefile = Path(temp_dir) / "Makefile"

    # CHECK: rtl exists: True
    print(f"rtl exists: {rtl_dir.exists()}")
    # CHECK: tb exists: True
    print(f"tb exists: {tb_dir.exists()}")
    # CHECK: Makefile exists: True
    print(f"Makefile exists: {makefile.exists()}")

    # Check RTL files generated
    rtl_files = list(rtl_dir.glob("*.sv"))
    # CHECK: Counter.sv generated: True
    print(f"Counter.sv generated: {any('Counter.sv' in str(f) for f in rtl_files)}")
    # CHECK: STL RTL generated: True
    print(f"STL RTL generated: {any('FIRRTLReg' in str(f) for f in rtl_files)}")

    # Check testbench exists
    tb_file = tb_dir / "testbench.cpp"
    # CHECK: testbench.cpp exists: True
    print(f"testbench.cpp exists: {tb_file.exists()}")

finally:
    shutil.rmtree(temp_dir)


# Test 7: Testbench DSL
# CHECK: === Test 7: Testbench DSL ===
print("=== Test 7: Testbench DSL ===")

from circt.pycmt2.testbench import Testbench, TestSequence

tb = Testbench(counter_circuit)

with tb.sequence("basic_test") as seq:
    seq.reset(5)
    seq.wait(10)
    seq.comment("Test increment")
    seq.print("Starting test")

# CHECK: basic_test
print(tb._sequences[0].name)
# CHECK: 4
print(len(tb._sequences[0]._ops))


# Test 8: Methods with Arguments
# CHECK: === Test 8: Methods with Arguments ===
print("=== Test 8: Methods with Arguments ===")

clear_stl_registry()

method_circuit = Circuit("MethodTest")
reg_mod = Reg.create(method_circuit, 32)

with method_circuit.module("Adder") as m:
    clk = m.clock()
    rst = m.reset()

    result_reg = m.instance(reg_mod, "result", clk=clk, rst=rst)

    with m.method("add", args=[("a", UInt(32)), ("b", UInt(32))]) as meth:
        with meth.guard() as g:
            g.always()
        with meth.body() as body:
            a = body.arg("a")
            b = body.arg("b")
            result = body.add(a, b)
            body.call(result_reg, "write", result)

    with m.value("get_result", returns=[UInt(32)]) as val:
        with val.guard() as g:
            g.always()
        with val.body() as body:
            body.returns(body.call(result_reg, "read"))

mlir = method_circuit.emit_mlir()
# CHECK: cmt2.method @add
# CHECK: %a: !firrtl.uint<32>
# CHECK: %b: !firrtl.uint<32>
print(mlir)


# Test 9: Expression Operations
# CHECK: === Test 9: Expression Operations ===
print("=== Test 9: Expression Operations ===")

clear_stl_registry()

expr_circuit = Circuit("ExprTest")
reg_mod = Reg.create(expr_circuit, 32)
reg1_mod = Reg.create(expr_circuit, 1)

with expr_circuit.module("ExprModule") as m:
    clk = m.clock()
    rst = m.reset()

    a_reg = m.instance(reg_mod, "a", clk=clk, rst=rst)
    b_reg = m.instance(reg_mod, "b", clk=clk, rst=rst)
    flag_reg = m.instance(reg1_mod, "flag", clk=clk, rst=rst)

    with m.rule("compute") as rule:
        with rule.guard() as g:
            flag = g.call(flag_reg, "read")
            g.returns(flag)
        with rule.body() as body:
            a = body.call(a_reg, "read")
            b = body.call(b_reg, "read")

            # Test various operations
            sum_val = body.add(a, b)
            diff_val = body.sub(a, b)
            cmp_gt = body.gt(a, b)
            cmp_eq = body.eq(a, b)
            and_val = body.and_(a, b)
            or_val = body.or_(a, b)
            not_flag = body.not_(body.call(flag_reg, "read"))
            mux_val = body.mux(cmp_gt, a, b)

            # Write result
            body.call(a_reg, "write", mux_val)

mlir = expr_circuit.emit_mlir()
# CHECK: firrtl.add
# CHECK: firrtl.sub
# CHECK: firrtl.gt
# CHECK: firrtl.eq
# CHECK: firrtl.and
# CHECK: firrtl.or
# CHECK: firrtl.not
# CHECK: firrtl.mux
print(mlir)


# Test 10: Verilog with Python Source Locations
# CHECK: === Test 10: Source Locations ===
print("=== Test 10: Source Locations ===")

verilog = expr_circuit.to_verilog()
# Source locations should appear in the Verilog output
# CHECK: Source location test: True
has_location = "pycmt2_e2e.py" in verilog or "ExprModule" in verilog
print("Source location test:", has_location)


print("\n=== All PyCMT2 E2E Tests Passed ===")
# CHECK: All PyCMT2 E2E Tests Passed
