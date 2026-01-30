#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Test STL (Standard Library) Components (via PyCMT2).

This script tests the reimplemented STL components:
- Reg: Register (external module)
- Wire: Wire (external module)
- WireDefault: Wire with default value (CMT2 module)
- FIFO1Push: Depth-1 FIFO with push semantics (CMT2 module)
- FIFO1Pull: Depth-1 FIFO with pull semantics (CMT2 module)
- FIFO2I: Depth-2 FIFO with independent enq/deq (CMT2 module)
- Memory: Sync and async memory (external modules)

Usage:
    PYTHONPATH=build/tools/circt/python_packages/circt_core:python \\
      python3 examples/JIT/stl_test.py
"""

import cmt2.jit as jit

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

    with jit.module(circuit, "TestModule") as m:
        clk = m.clock()
        rst = m.reset()

        # Instantiate
        counter = m.instance(reg_mod, "counter", clk=clk, rst=rst)
        temp = m.instance(wire_mod, "temp", clk=clk, rst=rst)

        # Rule to increment counter
        with jit.rule(m, "increment") as rule:
            with rule.guard as g:
                g.always()
            with rule.body as body:
                counter.next = counter.read + 1
                temp.write(body.bits(counter.read, 15, 0))

        # Value to read counter
        @jit.value(m)
        def count(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(counter.read)

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

    with jit.module(circuit, "TestModule") as m:
        clk = m.clock()
        rst = m.reset()

        flag = m.instance(wire_default_mod, "flag", clk=clk, rst=rst)

        # Rule that conditionally writes
        with jit.rule(m, "maybe_write") as rule:
            with rule.guard as g:
                g.always()
            with rule.body as body:
                # Read current value
                val = body.call(flag, "read")
                # If non-zero, write zero
                with body.if_(val) as if_:
                    with if_.then_() as then_b:
                        then_b.call(flag, "write", then_b.const(0, 8))

        # Value to read
        @jit.value(m)
        def get_flag(val) -> UInt[8]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(flag.read)

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

    with jit.module(circuit, "Producer") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(fifo_mod, "output_fifo", clk=clk, rst=rst)

        # Method to enqueue data
        @jit.method(m)
        def produce(meth, data: UInt[32]) -> None:
            with meth.guard:
                meth.returns(meth.not_(fifo.full))
            with meth.body:
                fifo.enq(data)

        # Value to check if full
        @jit.value(m)
        def is_full(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(fifo.full)

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

    with jit.module(circuit, "Consumer") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(fifo_mod, "input_fifo", clk=clk, rst=rst)

        # Method to dequeue data
        @jit.method(m)
        def consume(meth) -> UInt[16]:
            with meth.guard:
                meth.always()
            with meth.body:
                # FIFO1Pull exposes `deq` as a value (property-like in JIT).
                meth.returns(fifo.deq)

        # Value to check if full
        @jit.value(m)
        def is_full(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(fifo.full)

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

    with jit.module(circuit, "Buffer") as m:
        clk = m.clock()
        rst = m.reset()

        fifo = m.instance(fifo_mod, "buffer", clk=clk, rst=rst)

        # Method to enqueue
        @jit.method(m)
        def push(meth, data: UInt[64]) -> None:
            with meth.guard:
                meth.returns(meth.not_(fifo.full))
            with meth.body:
                fifo.enq(data)

        # Method to dequeue
        @jit.method(m)
        def pop(meth) -> UInt[64]:
            with meth.guard:
                meth.always()
            with meth.body:
                meth.returns(fifo.deq())

        # Value to check if full
        @jit.value(m)
        def is_full(val) -> UInt[1]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(fifo.full)

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

    with jit.module(circuit, "Cache") as m:
        clk = m.clock()
        rst = m.reset()

        mem = m.instance(mem_mod, "mem", clk=clk, rst=rst)
        result_reg = m.instance(reg_mod, "result", clk=clk, rst=rst)

        # Method to initiate read
        @jit.method(m)
        def read_start(meth, addr: UInt[8]) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                mem.rd0(addr)

        # Value to get read result
        @jit.value(m)
        def read_result(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(mem.rd1)

        # Method to write
        @jit.method(m)
        def write_data(meth, addr: UInt[8], data: UInt[32]) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                mem.write(data, addr)

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

    with jit.module(circuit, "LookupTable") as m:
        clk = m.clock()
        rst = m.reset()

        mem = m.instance(mem_mod, "lut", clk=clk, rst=rst)

        # Method to read (combinational)
        @jit.method(m)
        def lookup(meth, addr: UInt[6]) -> UInt[16]:
            with meth.guard:
                meth.always()
            with meth.body:
                meth.returns(mem.read(addr))

        # Method to write
        @jit.method(m)
        def store(meth, addr: UInt[6], data: UInt[16]) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                mem.write(data, addr)

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

    with jit.module(circuit, "PrecedenceModule") as m:
        clk = m.clock()
        rst = m.reset()
        reg = m.instance(reg_mod, "reg", clk=clk, rst=rst)

        @jit.value(m)
        def read_val(val) -> UInt[32]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(reg.read)

        @jit.method(m)
        def write_meth(meth, data: UInt[32]) -> None:
            with meth.guard:
                meth.always()
            with meth.body:
                reg.write(data)

        with jit.rule(m, "update") as rule:
            with rule.guard as g:
                g.always()
            with rule.body as body:
                val = body.call(reg, "read")
                body.call(reg, "write", body.add(val, body.const(1, 32)))

        # Set precedence: read_val < write_meth < update
        m.precedence(read_val._cmt2_ref, write_meth._cmt2_ref, rule.ref())

    mlir = circuit.emit_mlir()
    print("MLIR generated successfully")
    print(f"  - Contains precedence attribute: {'precedence' in mlir}")

    return True


def main():
    """Run all STL tests."""
    print("=" * 60)
    print("STL Component Tests (via PyCMT2)")
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
