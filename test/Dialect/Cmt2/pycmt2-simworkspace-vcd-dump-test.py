#!/usr/bin/env python3
# RUN: cd %S && PYTHONPATH=%circt_build_path/tools/circt/python_packages/circt_core %python %s | FileCheck %s

"""Regression: Testbench DSL should dump non-empty VCD waveforms.

SimulationWorkspace's DSL testbench generator must emit VCD dump calls so the
generated `waves/<Top>.vcd` contains timestamps. Without `tfp->dump(...)`, the
VCD contains only headers and GTKWave reports a zero time range.
"""

from __future__ import annotations

import tempfile
from pathlib import Path
import os

from circt.pycmt2 import Circuit
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


def test_dsl_testbench_emits_vcd_dump_calls():
    circuit = Circuit("TestVcdDump")

    with circuit.module("Top") as m:
        m.clock()
        m.reset()

        with m.value("get_one", returns=[]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as b:
                b.returns()

    tb = Testbench(circuit, auto_debug_ports=False)
    with tb.sequence("smoke") as seq:
        seq.reset(1)
        seq.wait(1)

    with tempfile.TemporaryDirectory() as td:
        ws_dir = Path(td) / "ws"
        # Force tracing on for this regression test.
        os.environ["PYCMT2_TRACE"] = "1"
        ws = SimulationWorkspace(circuit, ws_dir)
        ws.generate_with_testbench(tb)

        cpp = (ws_dir / "tb" / "testbench.cpp").read_text()
        has_dump = "tfp->dump" in cpp

        print(f"HAS_DUMP: {1 if has_dump else 0}")
        assert has_dump, "expected DSL-generated testbench to call tfp->dump(...)"

    print("OK")


if __name__ == "__main__":
    test_dsl_testbench_emits_vcd_dump_calls()
