#!/usr/bin/env python3
# RUN: cd %S && PYTHONPATH=%circt_build_path/tools/circt/python_packages/circt_core %python %s | FileCheck %s

"""Test for interval parameter on static_step in PyCMT2."""

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.stl import Reg, clear_stl_registry


def test_static_step_interval():
    """Test that static_step with interval generates correct MLIR."""
    clear_stl_registry()

    circuit = Circuit("TestIntervalCircuit")

    reg32 = Reg.create(circuit, 32)

    with circuit.module("TestModule") as m:
        clk = m.clock()
        rst = m.reset()

        data_reg = m.instance(reg32, "data_reg", clk=clk, rst=rst)

        # Simple static step (no interval)
        # CHECK: cmt2.proc.static_step @simple_step<4>
        # CHECK-NOT: interval
        with m.static_step(4, "simple_step") as step:
            val = step.call(data_reg, "read")
            step.call(data_reg, "write", val)

        # Pipelined static step with interval=2
        # CHECK: cmt2.proc.static_step @pipelined_step<8>
        # CHECK: interval = #cmt2.interval<2>
        with m.static_step(8, "pipelined_step", interval=2) as step:
            val = step.call(data_reg, "read")
            new_val = step.add(val, step.const(1, 32))
            step.call(data_reg, "write", new_val)

    mlir_str = circuit.emit_mlir()
    print(mlir_str)


if __name__ == "__main__":
    test_static_step_interval()
