#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Simulation Workspace Generation Example for PyCMT2.

This example demonstrates how to use SimulationWorkspace to generate
a complete Verilator simulation environment with a placeholder testbench.

The generated workspace includes:
- rtl/          : Generated Verilog RTL files (including STL modules)
- tb/           : Placeholder C++ testbench for Verilator
- build/        : Build output directory
- waves/        : VCD waveform output directory
- Makefile      : Build and run automation
- README.md     : Instructions for using the workspace

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/simulation_workspace_example.py

After running, the workspace will be at:
    examples/PyCMT2/sim_workspace_demo/

To build and run the simulation:
    cd examples/PyCMT2/sim_workspace_demo
    make        # Build the simulation
    make run    # Run the simulation
    make waves  # View waveforms (requires GTKWave)
"""

import shutil
from pathlib import Path

from circt.pycmt2 import Circuit, UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.stl import Reg, clear_stl_registry


def create_demo_circuit():
    """Create a simple counter circuit for demonstration."""
    clear_stl_registry()

    circuit = Circuit("CounterDemo")

    # Use STL Reg module
    reg32 = Reg.create(circuit, 32)
    reg1 = Reg.create(circuit, 1)

    with circuit.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()

        # State registers
        count_reg = m.instance(reg32, "count", clk=clk, rst=rst)
        running_reg = m.instance(reg1, "running", clk=clk, rst=rst)

        # Method: start - Start counting
        with m.method("start") as meth:
            with meth.guard() as g:
                running = g.call(running_reg, "read")
                not_running = g.not_(running)
                g.returns(not_running)
            with meth.body() as body:
                body.call(running_reg, "write", body.const(1, 1))

        # Method: stop - Stop counting
        with m.method("stop") as meth:
            with meth.guard() as g:
                running = g.call(running_reg, "read")
                g.returns(running)
            with meth.body() as body:
                body.call(running_reg, "write", body.const(0, 1))

        # Method: reset_count - Reset counter to 0
        with m.method("reset_count") as meth:
            with meth.guard() as g:
                g.always()
            with meth.body() as body:
                body.call(count_reg, "write", body.const(0, 32))

        # Rule: increment - Increment counter when running
        with m.rule("increment") as rule:
            with rule.guard() as g:
                running = g.call(running_reg, "read")
                g.returns(running)
            with rule.body() as body:
                count = body.call(count_reg, "read")
                new_count = body.add(count, body.const(1, 32))
                body.call(count_reg, "write", new_count)

        # Value: get_count - Read current count
        with m.value("get_count", returns=[UInt(32)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                count = body.call(count_reg, "read")
                body.returns(count)

        # Value: is_running - Check if running
        with m.value("is_running", returns=[UInt(1)]) as val:
            with val.guard() as g:
                g.always()
            with val.body() as body:
                running = body.call(running_reg, "read")
                body.returns(running)

    return circuit


def main():
    print("=" * 70)
    print("Simulation Workspace Generation Example")
    print("=" * 70)

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "sim_workspace_demo"

    # Clean previous workspace
    if workspace_dir.exists():
        print(f"\nRemoving existing workspace: {workspace_dir}")
        shutil.rmtree(workspace_dir)

    # Create circuit
    print("\n1. Creating Counter circuit...")
    circuit = create_demo_circuit()

    # Create simulation workspace
    print("\n2. Creating SimulationWorkspace...")
    ws = SimulationWorkspace(circuit, workspace_dir)

    # Generate the workspace with placeholder testbench
    print("\n3. Generating workspace with placeholder testbench...")
    ws.generate_placeholder()

    # Show the generated structure
    print("\n4. Generated workspace structure:")
    print("-" * 70)

    def show_tree(path, prefix=""):
        items = sorted(path.iterdir(), key=lambda x: (x.is_file(), x.name))
        for i, item in enumerate(items):
            is_last = i == len(items) - 1
            connector = "└── " if is_last else "├── "
            print(f"{prefix}{connector}{item.name}")
            if item.is_dir() and item.name not in ["build", "waves"]:
                extension = "    " if is_last else "│   "
                show_tree(item, prefix + extension)

    show_tree(workspace_dir)
    print("-" * 70)

    # Show generated RTL files
    print("\n5. Generated RTL files:")
    rtl_dir = workspace_dir / "rtl"
    for rtl_file in sorted(rtl_dir.iterdir()):
        print(f"\n   {rtl_file.name}:")
        content = rtl_file.read_text()
        lines = content.split("\n")[:20]  # First 20 lines
        for line in lines:
            print(f"      {line}")
        if len(content.split("\n")) > 20:
            print(f"      ... ({len(content.split(chr(10))) - 20} more lines)")

    # Show the placeholder testbench
    print("\n6. Generated placeholder testbench (tb/testbench.cpp):")
    print("-" * 70)
    tb_file = workspace_dir / "tb" / "testbench.cpp"
    tb_content = tb_file.read_text()
    print(tb_content[:2000])
    if len(tb_content) > 2000:
        print(f"... ({len(tb_content) - 2000} more characters)")
    print("-" * 70)

    # Show the Makefile
    print("\n7. Generated Makefile:")
    print("-" * 70)
    makefile = workspace_dir / "Makefile"
    print(makefile.read_text()[:1500])
    print("-" * 70)

    # Instructions
    print("\n" + "=" * 70)
    print("Workspace generated successfully!")
    print("=" * 70)
    print(f"""
Next steps:

1. Navigate to the workspace:
   cd {workspace_dir}

2. Edit the testbench to add your test logic:
   vim tb/testbench.cpp

3. Build the simulation:
   make

4. Run the simulation:
   make run

5. View waveforms (requires GTKWave):
   make waves

Example testbench modifications:

   // In the test logic section, you can:

   // Start the counter:
   dut->start_enable = 1;
   tick();
   dut->start_enable = 0;

   // Wait and check count:
   for (int i = 0; i < 10; i++) tick();
   std::cout << "Count: " << dut->get_count_res0 << std::endl;

   // Stop the counter:
   dut->stop_enable = 1;
   tick();
   dut->stop_enable = 0;

   // Check if running:
   std::cout << "Running: " << (int)dut->is_running_res0 << std::endl;
""")


if __name__ == "__main__":
    main()
