#!/usr/bin/env python3
# RUN: cd %S && PYTHONPATH=%circt_build_path/tools/circt/python_packages/circt_core %python %s | FileCheck %s

"""Test for timing attributes on external module bindings in PyCMT2."""

from circt.pycmt2 import Circuit, UInt


def test_external_module_timing():
    """Test that external module method timing attributes generate correct MLIR."""

    circuit = Circuit("TestExternalTimingCircuit")

    # Define an external module with timing attributes
    # CHECK: cmt2.module.extern.firrtl @TimedMem
    with circuit.external_module("TimedMem") as mem:
        mem.clock("clk")
        mem.reset("rst")

        # Value with static latency (e.g., registered read)
        # CHECK: cmt2.bind.value @read
        # CHECK: static_latency = 2
        mem.value(
            "read",
            ready_name="read_ready",
            args=[("addr", UInt(8))],
            returns=[("data", UInt(32))],
            static_latency=2,
        )

        # Method with static latency only (non-pipelined)
        # CHECK: cmt2.bind.method @write_slow static<4>
        # CHECK-NOT: interval
        mem.method(
            "write_slow",
            enable_name="write_slow_en",
            ready_name="write_slow_ready",
            args=[("addr", UInt(8)), ("data", UInt(32))],
            static_latency=4,
        )

        # Method with both latency and interval (pipelined)
        # CHECK: cmt2.bind.method @write_fast static<4>
        # CHECK: interval = #cmt2.interval<1>
        mem.method(
            "write_fast",
            enable_name="write_fast_en",
            ready_name="write_fast_ready",
            args=[("addr", UInt(8)), ("data", UInt(32))],
            static_latency=4,
            interval=1,
        )

    # Use the external module in a test module
    with circuit.module("TestModule") as m:
        clk = m.clock()
        rst = m.reset()
        mem_inst = m.instance(mem, "timed_mem", clk=clk, rst=rst)

    mlir_str = circuit.emit_mlir()
    print(mlir_str)


if __name__ == "__main__":
    test_external_module_timing()
