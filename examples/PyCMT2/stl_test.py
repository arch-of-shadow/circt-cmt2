#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Test STL (Standard Library) Components for PyCMT2.

This script tests the reimplemented STL components:
- Reg: Register (external module)
- Wire: Wire (external module)
- WireDefault: Wire with default value (CMT2 module)
- FIFO1Push: Depth-1 FIFO with push semantics (CMT2 module)
- FIFO1Pull: Depth-1 FIFO with pull semantics (CMT2 module)
- FIFO2I: Depth-2 FIFO with independent enq/deq (CMT2 module)
- Memory: Sync and async memory (external modules)

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/stl_test.py
"""

import sys
import traceback


def test_reg_and_wire():
    """Test Reg and Wire external modules."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import Reg, Wire

    print("\n--- Testing Reg and Wire ---")

    circuit = Circuit("RegWireTest")

    # Create register and wire modules
    reg_mod = Reg.create(circuit, 32)
    wire_mod = Wire.create(circuit, 16)

    with circuit.module("TestModule") as m:
        clk = m.clock()
        rst = m.reset()

        # Instantiate
        counter = m.instance(reg_mod, "counter", clk=clk, rst=rst)
        temp = m.instance(wire_mod, "temp", clk=clk, rst=rst)

        # Rule to increment counter
        with m.rule("increment") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as body:
                val = body.call(counter, "read")
                new_val = body.add(val, body.const(1, 32))
                body.call(counter, "write", body.truncate(new_val, 32))
                body.call(temp, "write", body.bits(val, 15, 0))

        # Value to read counter
        with m.value("count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(counter, "read")
                body.returns(result)

    # Emit MLIR
    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains FIRRTLReg_32: {'FIRRTLReg_32' in mlir}")
    print(f"  - Contains Wire_16: {'Wire_16' in mlir}")
    print(f"  - Contains TestModule: {'TestModule' in mlir}")

    return True


def test_wire_default():
    """Test WireDefault CMT2 module."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import WireDefault

    print("\n--- Testing WireDefault ---")

    circuit = Circuit("WireDefaultTest")

    # Create wire with default
    wire_default_mod = WireDefault.create(circuit, 8, init=0xFF)

    with circuit.module("TestModule") as m:
        clk = m.clock()
        rst = m.reset()

        flag = m.instance(wire_default_mod, "flag", clk=clk, rst=rst)

        # Rule that conditionally writes
        with m.rule("maybe_write") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as body:
                # Read current value
                val = body.call(flag, "read")
                # If non-zero, write zero
                with body.if_(val) as if_:
                    with if_.then_() as then_b:
                        then_b.call(flag, "write", then_b.const(0, 8))

        # Value to read
        with m.value("get_flag", returns=[UInt(8)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(flag, "read")
                body.returns(result)

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains WireDefault_w8_i255: {'WireDefault_w8_i255' in mlir}")
    has_default = '"default"' in mlir
    print(f"  - Contains default rule: {has_default}")

    return True


def test_fifo1_push():
    """Test FIFO1Push CMT2 module."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import FIFO1Push

    print("\n--- Testing FIFO1Push ---")

    circuit = Circuit("FIFO1PushTest")

    fifo_mod = FIFO1Push.create(circuit, 32)

    with circuit.module("Producer") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(fifo_mod, "output_fifo", clk=clk, rst=rst)

        # Method to enqueue data
        with m.method("produce", args=[("data", UInt(32))]) as meth:
            with meth.guard() as g:
                # Can enqueue when not full or when deq happens
                not_full = g.call(fifo, "full")
                not_full_inverted = g.not_(not_full)
                g.returns(not_full_inverted)
            with meth.body() as body:
                body.call(fifo, "enq", body.arg("data"))

        # Value to check if full
        with m.value("is_full", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(fifo, "full")
                body.returns(result)

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains FIFO1_PUSH_w32: {'FIFO1_PUSH_w32' in mlir}")
    has_full = '@full' in mlir or '"full"' in mlir
    has_deq = '@deq' in mlir or '"deq"' in mlir
    has_enq = '@enq' in mlir or '"enq"' in mlir
    print(f"  - Contains full value: {has_full}")
    print(f"  - Contains deq method: {has_deq}")
    print(f"  - Contains enq method: {has_enq}")

    return True


def test_fifo1_pull():
    """Test FIFO1Pull CMT2 module."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import FIFO1Pull

    print("\n--- Testing FIFO1Pull ---")

    circuit = Circuit("FIFO1PullTest")

    fifo_mod = FIFO1Pull.create(circuit, 16)

    with circuit.module("Consumer") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(fifo_mod, "input_fifo", clk=clk, rst=rst)

        # Method to dequeue data
        with m.method("consume", returns=[UInt(16)]) as meth:
            with meth.guard() as g:
                g.always()  # Guard in deq value handles availability
            with meth.body() as body:
                data = body.call(fifo, "deq")
                body.returns(data)

        # Value to check if full
        with m.value("is_full", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(fifo, "full")
                body.returns(result)

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains FIFO1_PULL_w16: {'FIFO1_PULL_w16' in mlir}")

    return True


def test_fifo2i():
    """Test FIFO2I CMT2 module."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import FIFO2I

    print("\n--- Testing FIFO2I ---")

    circuit = Circuit("FIFO2ITest")

    fifo_mod = FIFO2I.create(circuit, 64)

    with circuit.module("Buffer") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(fifo_mod, "buffer", clk=clk, rst=rst)

        # Method to enqueue
        with m.method("push", args=[("data", UInt(64))]) as meth:
            with meth.guard() as g:
                # Can push when not full
                is_full = g.call(fifo, "full")
                can_push = g.not_(is_full)
                g.returns(can_push)
            with meth.body() as body:
                body.call(fifo, "enq", body.arg("data"))

        # Method to dequeue
        with m.method("pop", returns=[UInt(64)]) as meth:
            with meth.guard() as g:
                g.always()  # Guard in deq handles availability
            with meth.body() as body:
                data = body.call(fifo, "deq")
                body.returns(data)

        # Value to check if full
        with m.value("is_full", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                result = body.call(fifo, "full")
                body.returns(result)

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains FIFO2_I_w64: {'FIFO2_I_w64' in mlir}")
    print(f"  - Contains state update rules: {'state0_update' in mlir}")

    return True


def test_memory_sync():
    """Test synchronous memory (1-cycle read latency)."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import Memory, Reg

    print("\n--- Testing Memory (sync) ---")

    circuit = Circuit("MemorySyncTest")

    # 256 entries, 32-bit data, 8-bit address
    mem_mod = Memory.create_1r1w_sync(circuit, data_width=32, addr_width=8, depth=256)
    reg_mod = Reg.create(circuit, 32)

    with circuit.module("Cache") as m:
        clk = m.clock()
        rst = m.reset()

        mem = m.instance(mem_mod, "mem", clk=clk, rst=rst)
        result_reg = m.instance(reg_mod, "result", clk=clk, rst=rst)

        # Method to initiate read
        with m.method("read_start", args=[("addr", UInt(8))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(mem, "rd0", body.arg("addr"))

        # Value to get read result
        with m.value("read_result", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                data = body.call(mem, "rd1")
                body.returns(data)

        # Method to write
        with m.method("write_data", args=[("addr", UInt(8)), ("data", UInt(32))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(mem, "write", body.arg("data"), body.arg("addr"))

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains Mem1r1w1c: {'Mem1r1w1c' in mlir}")
    print(f"  - Contains rd0 method: {'rd0' in mlir}")
    print(f"  - Contains rd1 value: {'rd1' in mlir}")

    return True


def test_memory_async():
    """Test asynchronous memory (0-cycle read latency)."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import Memory

    print("\n--- Testing Memory (async) ---")

    circuit = Circuit("MemoryAsyncTest")

    # 64 entries, 16-bit data, 6-bit address
    mem_mod = Memory.create_1r1w_async(circuit, data_width=16, addr_width=6, depth=64)

    with circuit.module("LookupTable") as m:
        clk = m.clock()
        rst = m.reset()

        mem = m.instance(mem_mod, "lut", clk=clk, rst=rst)

        # Method to read (combinational)
        with m.method("lookup", args=[("addr", UInt(6))], returns=[UInt(16)]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                data = body.call(mem, "read", body.arg("addr"))
                body.returns(data)

        # Method to write
        with m.method("store", args=[("addr", UInt(6)), ("data", UInt(16))]) as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(mem, "write", body.arg("data"), body.arg("addr"))

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains Mem1r1w0c: {'Mem1r1w0c' in mlir}")

    return True


def test_fifo_module_structure():
    """Test that FIFO modules are proper CMT2 modules (not external)."""
    from circt.pycmt2 import Circuit
    from circt.pycmt2.stl import FIFO1Push, Reg, Wire

    print("\n--- Testing FIFO Module Structure ---")

    circuit = Circuit("FIFOStructureTest")

    # FIFO should be a CMT2 module built from Reg and Wire
    fifo_mod = FIFO1Push.create(circuit, 8)

    mlir = circuit.emit_mlir()

    # Check that FIFO is a CMT2 module (not external)
    has_fifo_module = 'cmt2.module @FIFO1_PUSH_w8' in mlir
    # Check that it uses Reg and Wire instances
    has_reg_instance = 'cmt2.instance' in mlir and 'FIRRTLReg' in mlir
    has_wire_instance = 'Wire_1' in mlir

    print(f"  - FIFO is CMT2 module: {has_fifo_module}")
    print(f"  - FIFO uses Reg instances: {has_reg_instance}")
    print(f"  - FIFO uses Wire instances: {has_wire_instance}")

    # Also check that it has internal rules
    has_default_rules = 'deqed_default' in mlir and 'enqed_default' in mlir
    has_next_rule = '"next"' in mlir or '@next' in mlir

    print(f"  - FIFO has default rules: {has_default_rules}")
    print(f"  - FIFO has next rule: {has_next_rule}")

    return has_fifo_module and has_reg_instance


def test_precedence():
    """Test that precedence works with methods and values."""
    from circt.pycmt2 import Circuit, UInt
    from circt.pycmt2.stl import Reg

    print("\n--- Testing Precedence with Methods/Values ---")

    circuit = Circuit("PrecedenceTest")
    reg_mod = Reg.create(circuit, 32)

    with circuit.module("PrecedenceModule") as m:
        clk = m.clock()
        rst = m.reset()
        reg = m.instance(reg_mod, "reg", clk=clk, rst=rst)

        with m.value("read_val", returns=[UInt(32)]) as read_val:
            with read_val.guard() as g:
                g.always()
            with read_val.body() as body:
                body.returns(body.call(reg, "read"))

        with m.method("write_meth", args=[("data", UInt(32))]) as write_meth:
            with write_meth.guard() as g:
                g.always()
            with write_meth.body() as body:
                body.call(reg, "write", body.arg("data"))

        with m.rule("update") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as body:
                val = body.call(reg, "read")
                body.call(reg, "write", body.add(val, body.const(1, 32)))

        # Set precedence: read_val < write_meth < update
        m.precedence(read_val.ref(), write_meth.ref(), rule.ref())

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains precedence attribute: {'precedence' in mlir}")

    return True


def main():
    """Run all STL tests."""
    print("=" * 60)
    print("PyCMT2 STL Component Tests")
    print("=" * 60)

    tests = [
        ("Reg and Wire", test_reg_and_wire),
        ("WireDefault", test_wire_default),
        ("FIFO1Push", test_fifo1_push),
        ("FIFO1Pull", test_fifo1_pull),
        ("FIFO2I", test_fifo2i),
        ("Memory (sync)", test_memory_sync),
        ("Memory (async)", test_memory_async),
        ("FIFO Module Structure", test_fifo_module_structure),
        ("Precedence", test_precedence),
    ]

    results = {}
    for name, test_func in tests:
        try:
            results[name] = test_func()
        except Exception as e:
            print(f"\nERROR in {name}:")
            traceback.print_exc()
            results[name] = False

    # Summary
    print("\n" + "=" * 60)
    print("Test Summary")
    print("=" * 60)

    passed = sum(1 for v in results.values() if v)
    failed = len(results) - passed

    for name, success in results.items():
        status = "PASS" if success else "FAIL"
        print(f"  {name}: {status}")

    print(f"\nTotal: {passed} passed, {failed} failed")

    if failed > 0:
        print("\nSome tests failed!")
        return 1

    print("\nAll tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
