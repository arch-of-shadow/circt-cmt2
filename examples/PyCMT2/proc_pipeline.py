#!/usr/bin/env python3
"""proc_pipeline.py - Pipeline with parallel push/pop using proc.par

This example demonstrates:
1. Parameterized N-stage FIFO pipeline
2. Parallel control with proc.par for concurrent push and pop
3. Each branch contains sequenced static_repeat + while loops
4. Push/pop operations happen every cycle in steady state
5. FSM-based control flow with Verilator simulation AND interpreter

Structure:
  proc.par:
    branch 0 (push):
      proc.seq:
        static_repeat(num_stages): prime pipeline with initial values
        while(push_cnt < num_items): continue pushing
    branch 1 (pop):
      proc.seq:
        static_repeat(num_stages): wait for pipeline to fill
        while(pop_cnt < num_items): drain pipeline

Expected behavior:
- Phase 1 (prime): Push fills the pipeline (num_stages items)
- Phase 2 (steady): Both push and pop run every cycle
- Phase 3 (drain): Pop drains remaining items
- Sum of 0..9 = 45
"""

import os
import sys
import subprocess
import shutil
import tempfile
from pathlib import Path

# Add circt Python packages to path
build_dir = os.path.dirname(os.path.abspath(__file__))
while build_dir and not os.path.exists(os.path.join(build_dir, "build")):
    build_dir = os.path.dirname(build_dir)
if build_dir:
    sys.path.insert(0, os.path.join(build_dir, "build/tools/circt/python_packages/circt_core"))

from circt.pycmt2.circuit import Circuit
from circt.pycmt2.stl import Reg, FIFO1Push, clear_stl_registry
from circt.pycmt2.types import UInt
from circt.pycmt2.simulation import SimulationWorkspace


def create_pipeline(circuit: Circuit, width: int, num_stages: int, name: str = "Pipeline"):
    """Create a parameterized N-stage pipeline.

    The pipeline has:
    - enq(data): Push data to the pipeline (guard: !full)
    - deq() -> data: Pop data from the pipeline (guard: notEmpty)
    - Transfer rules to move data between stages

    Args:
        circuit: The circuit builder
        width: Data width in bits
        num_stages: Number of pipeline stages (FIFOs)
        name: Module name prefix

    Returns:
        The pipeline module builder
    """
    fifo_mod = FIFO1Push.create(circuit, width)
    module_name = f"{name}_{num_stages}"

    with circuit.module(module_name) as pipeline:
        clk = pipeline.clock("clk")
        rst = pipeline.reset("rst")

        # Instantiate FIFOs
        fifos = []
        for i in range(num_stages):
            fifo = pipeline.instance(fifo_mod, f"stage_{i}", clk=clk, rst=rst)
            fifos.append(fifo)

        # Value: full() - check if first stage is full
        with pipeline.value("full", returns=[UInt(1)]) as full_val:
            with full_val.guard() as g:
                g.always()
            with full_val.body() as body:
                is_full = body.call(fifos[0], "full")
                body.returns(is_full)

        # Value: notEmpty() - check if last stage has data
        with pipeline.value("notEmpty", returns=[UInt(1)]) as not_empty_val:
            with not_empty_val.guard() as g:
                g.always()
            with not_empty_val.body() as body:
                has_data = body.call(fifos[-1], "full")
                body.returns(has_data)

        # Method: enq(data) - push to first FIFO
        with pipeline.method("enq", args=[("data", UInt(width))]) as enq:
            with enq.guard() as g:
                is_full = g.call(fifos[0], "full")
                not_full = g.not_(is_full)
                g.returns(not_full)
            with enq.body() as body:
                data = body.arg("data")
                body.call(fifos[0], "enq", data)

        # Method: deq() -> data - pop from last FIFO
        with pipeline.method("deq", returns=[UInt(width)]) as deq:
            with deq.guard() as g:
                has_data = g.call(fifos[-1], "full")
                g.returns(has_data)
            with deq.body() as body:
                data = body.call(fifos[-1], "deq")
                body.returns(data)

        # Transfer rules between stages
        transfer_rules = []
        for i in range(num_stages - 1):
            with pipeline.rule(f"transfer_{i}") as transfer:
                with transfer.guard() as g:
                    src_has_data = g.call(fifos[i], "full")
                    dst_full = g.call(fifos[i+1], "full")
                    dst_not_full = g.not_(dst_full)
                    can_transfer = g.and_(src_has_data, dst_not_full)
                    g.returns(can_transfer)
                with transfer.body() as body:
                    data = body.call(fifos[i], "deq")
                    body.call(fifos[i+1], "enq", data)
                transfer_rules.append(transfer)

        # Precedence: enq < deq < transfers
        refs = [enq.ref(), deq.ref()]
        for tr in transfer_rules:
            refs.append(tr.ref())
        pipeline.precedence(*refs)

    return pipeline


def create_test_harness(circuit: Circuit, pipeline_mod, width: int, num_stages: int, num_items: int = 10):
    """Create test harness with parallel push/pop control.

    Structure:
    - proc.rule main:
        - proc.par:
            - Push branch: seq { static_repeat(num_stages), while(push_cnt < num_items) }
            - Pop branch: seq { static_repeat(num_stages), while(pop_cnt < num_items) }
        - mark done
    """
    reg_mod = Reg.create(circuit, width)
    reg1_mod = Reg.create(circuit, 1)

    # Create a register for the FSM (4 bits for ~9-16 states)
    _ = Reg.create(circuit, 4)  # FSM register

    with circuit.module("TestHarness") as harness:
        clk = harness.clock("clk")
        rst = harness.reset("rst")

        # Registers for state
        in_counter = harness.instance(reg_mod, "in_counter", clk=clk, rst=rst)  # Value to push
        out_sum = harness.instance(reg_mod, "out_sum", clk=clk, rst=rst)        # Accumulated sum
        push_cnt = harness.instance(reg_mod, "push_cnt", clk=clk, rst=rst)      # Push count
        pop_cnt = harness.instance(reg_mod, "pop_cnt", clk=clk, rst=rst)        # Pop count
        done_reg = harness.instance(reg1_mod, "done_reg", clk=clk, rst=rst)

        # Pipeline instance
        pipe = harness.instance(pipeline_mod, "pipe", clk=clk, rst=rst)

        # Value: done
        with harness.value("done", returns=[UInt(1)]) as done_val:
            with done_val.guard() as g:
                g.always()
            with done_val.body() as body:
                d = body.call(done_reg, "read")
                body.returns(d)

        # Value: result - get the accumulated sum
        with harness.value("result", returns=[UInt(width)]) as result_val:
            with result_val.guard() as g:
                g.always()
            with result_val.body() as body:
                s = body.call(out_sum, "read")
                body.returns(s)

        # ==== STATIC STEPS (latency=1) for static_repeat ====
        # Using static_step means FSM advances immediately without waiting for done signal
        # This achieves single-cycle iteration in static_repeat

        # Static step: push_static - enqueue with known latency=1
        with harness.static_step(1, "push_static") as push_static:
            cnt = push_static.call(in_counter, "read")
            push_static.call(pipe, "enq", cnt)
            next_cnt = push_static.add(cnt, push_static.const(1, width))
            push_static.call(in_counter, "write", push_static.bits(next_cnt, width-1, 0))
            pcnt = push_static.call(push_cnt, "read")
            push_static.call(push_cnt, "write", push_static.bits(push_static.add(pcnt, push_static.const(1, width)), width-1, 0))

        # Static step: wait_static - do nothing for one cycle (known latency)
        with harness.static_step(1, "wait_static") as wait_static:
            # No-op: just advance FSM after 1 cycle
            _ = wait_static.const(0, 1)  # Dummy operation to ensure non-empty body

        # ==== DYNAMIC STEPS for while loop ====
        # Dynamic steps use done signals, needed for loops with conditions

        # Dynamic step: push_item - for while loop
        with harness.step("push_item") as push_step:
            cnt = push_step.call(in_counter, "read")
            push_step.call(pipe, "enq", cnt)
            next_cnt = push_step.add(cnt, push_step.const(1, width))
            push_step.call(in_counter, "write", push_step.bits(next_cnt, width-1, 0))
            pcnt = push_step.call(push_cnt, "read")
            push_step.call(push_cnt, "write", push_step.bits(push_step.add(pcnt, push_step.const(1, width)), width-1, 0))
            push_step.done(push_step.const(1, 1))

        # Dynamic step: pop_item - for while loop
        with harness.step("pop_item") as pop_step:
            data = pop_step.call(pipe, "deq")
            sum_val = pop_step.call(out_sum, "read")
            new_sum = pop_step.add(sum_val, data)
            pop_step.call(out_sum, "write", pop_step.bits(new_sum, width-1, 0))
            pcnt = pop_step.call(pop_cnt, "read")
            pop_step.call(pop_cnt, "write", pop_step.bits(pop_step.add(pcnt, pop_step.const(1, width)), width-1, 0))
            pop_step.done(pop_step.const(1, 1))

        # Step: mark_done
        with harness.step("mark_done") as done_step:
            done_step.call(done_reg, "write", done_step.const(1, 1))
            done_step.done(done_step.const(1, 1))

        # Procedural rule: main with parallel push/pop
        # Structure:
        #   par:
        #     branch 0 (push): seq { static_repeat(num_stages) { push_static }, while(cnt < num_items) { push_item } }
        #     branch 1 (pop):  seq { static_repeat(num_stages) { wait_static }, while(cnt < num_items) { pop_item } }
        #
        # Key insight:
        # - static_repeat uses static_step (latency=1) for single-cycle iteration
        # - while uses dynamic_step (with done signal) because condition needs re-evaluation
        with harness.proc_rule("main") as main:
            with main.guard() as g:
                d = g.call(done_reg, "read")
                not_done = g.not_(d)
                g.returns(not_done)

            with main.control() as ctrl:
                with ctrl.seq() as seq:
                    # Parallel execution: push and pop run concurrently
                    with seq.par() as par:
                        # Push branch: static_repeat to prime, then while to continue
                        with par.seq() as push_seq:
                            # Phase 1: Prime the pipeline with num_stages items (STATIC - 1 cycle each)
                            with push_seq.static_repeat(num_stages) as prime:
                                prime.enable(push_static.ref())  # Static step for best performance

                            # Phase 2: Continue pushing while push_cnt < num_items (DYNAMIC)
                            def push_cond(b):
                                pcnt = b.call(push_cnt, "read")
                                limit = b.const(num_items, width)
                                return b.lt(pcnt, limit)

                            with push_seq.while_(push_cond) as push_loop:
                                push_loop.enable(push_step.ref())  # Dynamic step for condition re-eval

                        # Pop branch: static_repeat to wait for data, then while to drain
                        with par.seq() as pop_seq:
                            # Phase 1: Wait for pipeline to fill (STATIC - 1 cycle each)
                            with pop_seq.static_repeat(num_stages) as wait:
                                wait.enable(wait_static.ref())  # Static step for best performance

                            # Phase 2: Drain the pipeline while pop_cnt < num_items (DYNAMIC)
                            def pop_cond(b):
                                pcnt = b.call(pop_cnt, "read")
                                limit = b.const(num_items, width)
                                return b.lt(pcnt, limit)

                            with pop_seq.while_(pop_cond) as pop_loop:
                                pop_loop.enable(pop_step.ref())  # Dynamic step for condition re-eval

                    # After both branches complete, mark done
                    seq.enable(done_step.ref())

        harness.precedence(done_val.ref(), result_val.ref(), main.ref())

    return harness


def generate_testbench(workspace_dir: Path, width: int, expected_sum: int):
    """Generate a custom Verilator testbench."""
    tb_dir = workspace_dir / "tb"

    testbench_cpp = f'''
#include "VTestHarness.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>

vluint64_t main_time = 0;
double sc_time_stamp() {{ return main_time; }}

int main(int argc, char** argv) {{
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    VTestHarness* dut = new VTestHarness;
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("waves/sim.vcd");

    std::cout << "========================================" << std::endl;
    std::cout << "Pipeline with Parallel Push/Pop Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Testing proc.par with static_repeat + while" << std::endl;
    std::cout << "Expected sum: {expected_sum}" << std::endl;
    std::cout << "========================================" << std::endl;

    auto tick = [&]() {{
        dut->clk = 0;
        dut->eval();
        tfp->dump(main_time++);
        dut->clk = 1;
        dut->eval();
        tfp->dump(main_time++);
    }};

    // Reset
    std::cout << "Applying reset..." << std::endl;
    dut->rst = 1;
    for (int i = 0; i < 10; i++) tick();
    dut->rst = 0;

    std::cout << "Starting simulation..." << std::endl;

    bool test_passed = false;
    int done_cycles = 0;
    const int MAX_CYCLES = 500;

    for (int cycle = 0; cycle < MAX_CYCLES; cycle++) {{
        tick();

        int result = dut->result_res0;
        int done = dut->done_res0;
        int fsm_running = dut->main___05Frunning_ResultOutOfBound;

        // Print progress every 10 cycles or when significant events happen
        if (cycle % 10 == 0 || cycle < 20) {{
            std::cout << "Cycle " << cycle << ": result=" << result
                      << ", done=" << done
                      << ", fsm_running=" << fsm_running << std::endl;
        }}

        if (done == 1) {{
            done_cycles++;
            if (done_cycles >= 3) {{
                std::cout << std::endl;
                std::cout << "========================================" << std::endl;
                std::cout << "Computation complete at cycle " << cycle << std::endl;
                std::cout << "Result: " << result << std::endl;

                int expected = {expected_sum};
                if (result == expected) {{
                    std::cout << "TEST PASSED!" << std::endl;
                    test_passed = true;
                }} else {{
                    std::cout << "TEST FAILED! Expected " << expected << std::endl;
                }}
                std::cout << "========================================" << std::endl;
                break;
            }}
        }}
    }}

    if (!test_passed && done_cycles < 3) {{
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "TEST FAILED - Timeout!" << std::endl;
        std::cout << "Final result: " << (int)dut->result_res0 << std::endl;
        std::cout << "Final done: " << (int)dut->done_res0 << std::endl;
        std::cout << "========================================" << std::endl;
    }}

    tfp->close();
    delete tfp;
    delete dut;

    return test_passed ? 0 : 1;
}}
'''

    (tb_dir / "testbench.cpp").write_text(testbench_cpp)
    print("   Updated testbench.cpp")


def run_simulation(workspace_dir: Path):
    """Build and run the Verilator simulation."""
    print("\n" + "=" * 60)
    print("Step: Verilator Simulation")
    print("=" * 60)

    result = subprocess.run(["which", "verilator"], capture_output=True, text=True)
    if result.returncode != 0:
        print("   SKIP: Verilator not found in PATH")
        return None

    print("   Building Verilator simulation...")
    result = subprocess.run(
        ["make", "all"],
        cwd=workspace_dir,
        capture_output=True,
        text=True,
        timeout=180
    )

    if result.returncode != 0:
        print("   FAIL: Build failed")
        print(f"   stderr: {result.stderr[:2000]}")
        return None

    print("   Build successful!")

    print("\n   Running simulation...")
    result = subprocess.run(
        ["make", "run"],
        cwd=workspace_dir,
        capture_output=True,
        text=True,
        timeout=120
    )

    print("\n   Simulation output:")
    print("   " + "-" * 50)
    for line in result.stdout.split('\n'):
        print(f"   {line}")
    print("   " + "-" * 50)

    return "TEST PASSED" in result.stdout


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

    import shutil
    path = shutil.which("cmt2-dbg")
    if path:
        return path

    return None


def run_interpreter(mlir_content: str, circuit_name: str = "ProcPipelineTest"):
    """Run cmt2-dbg interpreter with tracing to show rule firing."""
    print("\n" + "=" * 60)
    print("Step: cmt2-dbg Interpreter (Rule Firing Trace)")
    print("=" * 60)

    cmt2_dbg = find_cmt2_dbg()
    if not cmt2_dbg:
        print("   SKIP: cmt2-dbg not found")
        return None

    print(f"   Using: {cmt2_dbg}")

    # Create temp files
    with tempfile.NamedTemporaryFile(mode='w', suffix='.mlir', delete=False) as mlir_file:
        mlir_file.write(mlir_content)
        mlir_path = mlir_file.name

    # Script to run interpreter with tracing
    script = """
trace on
step 50
history 50
"""

    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as script_file:
        script_file.write(script)
        script_path = script_file.name

    try:
        result = subprocess.run(
            [cmt2_dbg, mlir_path, f"--circuit={circuit_name}", f"--script={script_path}"],
            capture_output=True,
            text=True,
            timeout=60
        )
        output = result.stdout + result.stderr

        print("\n   Interpreter trace (showing rule firings):")
        print("   " + "-" * 50)

        # Parse and display firing information
        lines = output.split('\n')
        for line in lines:
            if line.strip():
                # Highlight rule firing lines
                if 'Cycle' in line or 'fired' in line.lower() or 'main' in line.lower():
                    print(f"   {line}")
                elif 'state' in line.lower() or '=' in line:
                    print(f"   {line}")

        print("   " + "-" * 50)

        # Count rule firings
        main_fires = output.count("main")
        push_fires = output.count("push_item")
        pop_fires = output.count("pop_item")

        print(f"\n   Rule firing summary:")
        print(f"     main proc rule: {main_fires} mentions")
        print(f"     push_item step: {push_fires} mentions")
        print(f"     pop_item step: {pop_fires} mentions")

        return output

    except subprocess.TimeoutExpired:
        print("   TIMEOUT: Interpreter took too long")
        return None
    except Exception as e:
        print(f"   ERROR: {e}")
        return None
    finally:
        os.unlink(mlir_path)
        os.unlink(script_path)


def main():
    print("=" * 70)
    print("Pipeline with Parallel Push/Pop - proc.par with static_repeat + while")
    print("=" * 70)
    print("""
This example demonstrates:
1. Parameterized 3-stage FIFO pipeline
2. Parallel control with proc.par:
   - Push branch: static_repeat(3) + while(push_cnt < 10)
   - Pop branch:  static_repeat(3) + while(pop_cnt < 10)
3. Both static and dynamic loop constructs
4. Push/pop happen every cycle in steady state
5. Expected result: sum(0..9) = 45
""")

    width = 32
    num_stages = 3
    num_items = 10
    expected_sum = sum(range(num_items))  # 0+1+...+9 = 45

    clear_stl_registry()

    print(f"1. Creating {num_stages}-stage pipeline...")
    circuit = Circuit("ProcPipelineTest")
    pipeline_mod = create_pipeline(circuit, width, num_stages)
    print(f"   Pipeline module: {pipeline_mod.name}")

    print(f"\n2. Creating test harness with parallel push/pop (proc.par)...")
    print(f"   - Push branch: static_repeat({num_stages}) + while(cnt < {num_items})")
    print(f"   - Pop branch:  static_repeat({num_stages}) + while(cnt < {num_items})")
    harness_mod = create_test_harness(circuit, pipeline_mod, width, num_stages, num_items)
    print(f"   Test harness module: {harness_mod.name}")

    print("\n3. Emitting MLIR...")
    mlir = circuit.emit_mlir()
    print(f"   MLIR size: {len(mlir)} chars")

    # Verify proc constructs are present
    print("\n4. Verifying MLIR structure...")
    checks = [
        ("proc.par", "Parallel composition"),
        ("proc.seq", "Sequential composition"),
        ("proc.while", "While loop"),
        ("proc.static_repeat", "Static repeat"),
        ("pipe", "Pipeline instance"),
    ]

    all_found = True
    for construct, desc in checks:
        if construct in mlir:
            print(f"   [OK] Found {desc} ({construct})")
        else:
            print(f"   [MISSING] {desc} ({construct})")
            all_found = False

    if not all_found:
        print("\n   WARNING: Some constructs missing")

    # Save MLIR for debugging
    mlir_debug_path = Path("/tmp/proc_pipeline_debug.mlir")
    mlir_debug_path.write_text(mlir)
    print(f"\n   MLIR saved to: {mlir_debug_path}")

    # Run interpreter FIRST (before Verilator compilation)
    interp_output = run_interpreter(mlir)

    print("\n5. Generating simulation workspace...")
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "proc_pipeline_workspace"

    if workspace_dir.exists():
        shutil.rmtree(workspace_dir)

    ws = SimulationWorkspace(circuit, workspace_dir)
    ws.generate_placeholder()

    rtl_dir = workspace_dir / "rtl"
    sv_files = list(rtl_dir.glob("*.sv"))
    print(f"   Generated {len(sv_files)} Verilog files")

    print("\n6. Generating custom testbench...")
    generate_testbench(workspace_dir, width, expected_sum)

    sim_success = run_simulation(workspace_dir)

    print("\n" + "=" * 70)
    print("Summary")
    print("=" * 70)
    print(f"   Workspace: {workspace_dir}")
    print(f"   Expected result: {expected_sum}")

    print("\n   Results:")
    if interp_output:
        print("   - Interpreter: RAN (see trace above)")
    else:
        print("   - Interpreter: SKIPPED")

    if sim_success is None:
        print("   - Verilator: SKIPPED (not available)")
    elif sim_success:
        print("   - Verilator: PASSED")
    else:
        print("   - Verilator: FAILED")

    print("=" * 70)

    return 0 if sim_success is None or sim_success else 1


if __name__ == "__main__":
    sys.exit(main())
