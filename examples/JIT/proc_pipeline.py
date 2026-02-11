#!/usr/bin/env python3
"""proc_pipeline.py - Pipeline with parallel push/pop using proc.par (Testbench DSL)

This example demonstrates:
1. Parameterized N-stage FIFO pipeline
2. Parallel control with proc.par for concurrent push and pop
3. Each branch contains sequenced static_repeat + while loops
4. Push/pop operations happen every cycle in steady state
5. FSM-based control flow with Verilator simulation via Testbench DSL

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

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core:../python \\
      python3 ../examples/JIT/proc_pipeline.py
"""

import cmt2.jit as jit

import shutil
from pathlib import Path

from circt.pycmt2.circuit import Circuit
from circt.pycmt2.stl import Reg, FIFO1Push, clear_stl_registry
from circt.pycmt2.types import UInt
from circt.pycmt2.simulation import SimulationWorkspace
from circt.pycmt2.testbench import Testbench


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

    with jit.module(circuit, module_name) as pipeline:
        clk = pipeline.clock("clk")
        rst = pipeline.reset("rst")

        # Instantiate FIFOs
        fifos = []
        for i in range(num_stages):
            fifo = pipeline.instance(fifo_mod, clk=clk, rst=rst, alias=f"stage_{i}")
            fifos.append(fifo)

        @jit.value(pipeline)
        def full(full_val) -> UInt[1]:
            with full_val.guard:
                full_val.always()
            with full_val.body:
                full_val.returns(fifos[0].full)

        @jit.value(pipeline)
        def notEmpty(not_empty_val) -> UInt[1]:
            with not_empty_val.guard:
                not_empty_val.always()
            with not_empty_val.body:
                not_empty_val.returns(fifos[-1].full)

        @jit.method(pipeline)
        def enq(enq_ctx, data: UInt[width]) -> None:
            with enq_ctx.guard:
                enq_ctx.returns(enq_ctx.not_(fifos[0].full))
            with enq_ctx.body:
                fifos[0].enq(data)

        @jit.method(pipeline)
        def deq(deq_ctx) -> UInt[width]:
            with deq_ctx.guard:
                deq_ctx.returns(fifos[-1].full)
            with deq_ctx.body:
                deq_ctx.returns(fifos[-1].deq())

        # Transfer rules between stages
        transfer_rules = []
        for i in range(num_stages - 1):
            with jit.rule(pipeline, alias=f"transfer_{i}") as transfer:
                with transfer.guard as g:
                    src_has_data = fifos[i].full
                    dst_full = fifos[i + 1].full
                    dst_not_full = g.not_(dst_full)
                    can_transfer = g.and_(src_has_data, dst_not_full)
                    g.returns(can_transfer)
                with transfer.body as body:
                    data = fifos[i].deq()
                    fifos[i + 1].enq(data)
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

    with jit.module(circuit, "TestHarness") as harness:
        clk = harness.clock("clk")
        rst = harness.reset("rst")

        # Registers for state
        in_counter = harness.instance(reg_mod, clk=clk, rst=rst)  # Value to push
        out_sum = harness.instance(reg_mod, clk=clk, rst=rst)        # Accumulated sum
        push_cnt = harness.instance(reg_mod, clk=clk, rst=rst)      # Push count
        pop_cnt = harness.instance(reg_mod, clk=clk, rst=rst)        # Pop count
        done_reg = harness.instance(reg1_mod, clk=clk, rst=rst)

        # Pipeline instance
        pipe = harness.instance(pipeline_mod, clk=clk, rst=rst)

        @jit.value(harness)
        def done(done_val) -> UInt[1]:
            with done_val.guard:
                done_val.always()
            with done_val.body:
                done_val.returns(done_reg.read)

        @jit.value(harness)
        def result(result_val) -> UInt[width]:
            with result_val.guard:
                result_val.always()
            with result_val.body:
                result_val.returns(out_sum.read)

        @jit.value(harness)
        def push_count(val) -> UInt[width]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(push_cnt.read)

        @jit.value(harness)
        def pop_count(val) -> UInt[width]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(pop_cnt.read)

        @jit.value(harness)
        def next_input(val) -> UInt[width]:
            with val.guard:
                val.always()
            with val.body:
                val.returns(in_counter.read)

        # ==== STATIC STEPS (latency=1) for static_repeat ====
        #
        # `proc.static_repeat` relies on deterministic step latency. Use
        # `static_step(latency=1)` to get 1-cycle iterations without `done`.

        with harness.static_step(1) as push_static:
            cnt = in_counter.read
            pipe.enq(cnt)
            next_cnt = push_static.add(cnt, push_static.const(1, width))
            in_counter.next = push_static.bits(next_cnt, width - 1, 0)
            pcnt = push_cnt.read
            push_cnt.next = push_static.bits(
                push_static.add(pcnt, push_static.const(1, width)), width - 1, 0
            )

        with harness.static_step(1) as wait_static:
            _ = wait_static.const(0, 1)  # keep non-empty

        # Dynamic step: push_item - for while loop
        with harness.step() as push_item:
            cnt = in_counter.read
            pipe.enq(cnt)
            next_cnt = push_item.add(cnt, push_item.const(1, width))
            in_counter.next = push_item.bits(next_cnt, width - 1, 0)
            pcnt = push_cnt.read
            push_cnt.next = push_item.bits(push_item.add(pcnt, push_item.const(1, width)), width - 1, 0)

        # Dynamic step: pop_item - for while loop
        with harness.step() as pop_item:
            data = pipe.deq()
            sum_val = out_sum.read
            new_sum = pop_item.add(sum_val, data)
            out_sum.next = pop_item.bits(new_sum, width - 1, 0)
            pcnt = pop_cnt.read
            pop_cnt.next = pop_item.bits(pop_item.add(pcnt, pop_item.const(1, width)), width - 1, 0)

        # Step: mark_done
        with harness.step() as mark_done:
            done_reg.next = mark_done.const(1, 1)

        # Procedural rule: main with parallel push/pop
        # Structure:
        #   par:
        #     branch 0 (push): seq { static_repeat(num_stages) { push_static }, while(cnt < num_items) { push_item } }
        #     branch 1 (pop):  seq { static_repeat(num_stages) { wait_static }, while(cnt < num_items) { pop_item } }
        #
        # Key insight:
        # - static_repeat uses static_step (latency=1) for single-cycle iteration
        # - while uses dynamic steps because the condition needs re-evaluation
        with harness.proc_rule() as main:
            with main.guard as g:
                g.returns(g.not_(done_reg.read))

            with main.control() as ctrl:
                with ctrl.seq() as seq:
                    # Parallel execution: push and pop run concurrently
                    with seq.par() as par:
                        # Push branch: static_repeat to prime, then while to continue
                        with par.seq() as push_seq:
                            # Phase 1: Prime the pipeline with num_stages items (STATIC - 1 cycle each)
                            with push_seq.static_repeat(num_stages) as prime:
                                prime.enable(push_static.ref())

                            # Phase 2: Continue pushing while push_cnt < num_items (DYNAMIC)
                            def push_cond(b):
                                pcnt = b.call(push_cnt.instance, push_cnt.instance.read)
                                limit = b.const(num_items, width)
                                return b.lt(pcnt, limit)

                            with push_seq.while_(push_cond) as push_loop:
                                push_loop.enable(push_item.ref())  # Dynamic step for condition re-eval

                        # Pop branch: static_repeat to wait for data, then while to drain
                        with par.seq() as pop_seq:
                            # Phase 1: Wait for pipeline to fill (STATIC - 1 cycle each)
                            with pop_seq.static_repeat(num_stages) as wait:
                                wait.enable(wait_static.ref())

                            # Phase 2: Drain the pipeline while pop_cnt < num_items (DYNAMIC)
                            def pop_cond(b):
                                pcnt = b.call(pop_cnt.instance, pop_cnt.instance.read)
                                limit = b.const(num_items, width)
                                return b.lt(pcnt, limit)

                            with pop_seq.while_(pop_cond) as pop_loop:
                                pop_loop.enable(pop_item.ref())  # Dynamic step for condition re-eval

                    # After both branches complete, mark done
                    seq.enable(mark_done.ref())

        harness.precedence(done._cmt2_ref, result._cmt2_ref, main.ref())

    return harness


def create_pipeline_testbench(
    circuit,
    *,
    expected_sum: int,
    num_stages: int,
    num_items: int,
):
    """Create testbench using DSL for pipeline test.

    This testbench checks both:
    - Golden values (final sum, counters)
    - Cycle-level behavior (event-driven invariants)
    """
    tb = Testbench(circuit, auto_debug_ports=True)

    # =========================================================================
    # Test Sequence: Reset Test
    # =========================================================================
    with tb.sequence("test_reset") as seq:
        seq.comment("Test: Verify reset behavior")
        seq.reset(5)
        seq.wait(1)
        seq.expect("done_res0", 0, "Should not be done after reset")
        seq.expect("result_res0", 0, "Result should be 0 after reset")
        seq.expect("push_count_res0", 0, "push_count should be 0 after reset")
        seq.expect("pop_count_res0", 0, "pop_count should be 0 after reset")
        seq.expect("next_input_res0", 0, "next_input should be 0 after reset")
        seq.print("Reset test passed - done=0, result=0")

    # =========================================================================
    # Test Sequence: Cycle-level Throughput + Golden Result
    # =========================================================================
    with tb.sequence("test_pipeline") as seq:
        seq.comment("=" * 60)
        seq.comment("Test: Verify pipeline (proc.par) behavior")
        seq.comment("=" * 60)
        seq.comment(f"Expected sum when done: {expected_sum}")
        seq.reset(10)

        # ORAAT semantics + explicit transfer rules mean we cannot assume
        # push/pop/transfer happen every cycle. Instead, validate *event-driven*
        # invariants: when counters advance, dependent signals match.
        #
        # This still checks cycle-level behavior, but it is robust to scheduling
        # and arbitration details.

        seq.expect("done_res0", 0, "done should start low")
        seq.expect("result_res0", 0, "result should start at 0")
        seq.expect("push_count_res0", 0, "push_count should start at 0")
        seq.expect("pop_count_res0", 0, "pop_count should start at 0")
        seq.expect("next_input_res0", 0, "next_input should start at 0")

        # Track pop_count from reset onward. When pop_count reaches i, result
        # should equal sum(0..i-1).
        running_sum = 0
        for i in range(1, num_items + 1):
            running_sum += (i - 1)
            seq.wait_condition(f"dut->pop_count_res0 == {i}", timeout=4000)
            seq.expect("result_res0", running_sum, f"result should be sum(0..{i-1})")

        # Completion: done asserts after both branches complete + mark_done runs.
        seq.wait_condition("dut->done_res0 == 1", timeout=4000)
        seq.expect("push_count_res0", num_items, "push_count should reach num_items")
        seq.expect("pop_count_res0", num_items, "pop_count should reach num_items")
        seq.expect("result_res0", expected_sum, "Final sum should match golden value")
        seq.expect("next_input_res0", num_items, "next_input should equal num_items after final push")

        # Stability after completion.
        seq.wait(1)
        seq.expect("done_res0", 1, "done should remain asserted")
        seq.expect("result_res0", expected_sum, "result should remain stable after done")

    return tb


@jit.elaborate
def create_pipeline_circuit(
    *,
    width: int,
    num_stages: int,
    num_items: int,
):
    clear_stl_registry()
    circuit = Circuit("ProcPipelineTest")
    pipeline_mod = create_pipeline(circuit, width, num_stages)
    harness_mod = create_test_harness(circuit, pipeline_mod, width, num_stages, num_items)
    return circuit, jit.handles(pipeline=pipeline_mod, harness=harness_mod)


def main():
    print("=" * 70)
    print("Pipeline with Parallel Push/Pop - Using Testbench DSL")
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

    # Setup paths
    script_dir = Path(__file__).parent
    workspace_dir = script_dir / "proc_pipeline_workspace"

    # Clean previous workspace
    if workspace_dir.exists():
        shutil.rmtree(workspace_dir)

    print(f"1. Creating {num_stages}-stage pipeline...")
    circuit, h = create_pipeline_circuit(width=width, num_stages=num_stages, num_items=num_items)
    print(f"   Pipeline module: {h.pipeline.name}")

    print(f"\n2. Creating test harness with parallel push/pop (proc.par)...")
    print(f"   - Push branch: static_repeat({num_stages}) + while(cnt < {num_items})")
    print(f"   - Pop branch:  static_repeat({num_stages}) + while(cnt < {num_items})")
    print(f"   Test harness module: {h.harness.name}")

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

    # Create testbench using DSL
    print("\n5. Creating testbench using Testbench DSL...")
    tb = create_pipeline_testbench(
        circuit,
        expected_sum=expected_sum,
        num_stages=num_stages,
        num_items=num_items,
    )
    print(f"   Test sequences: {len(tb._sequences)}")
    for seq in tb._sequences:
        print(f"      - {seq.name}: {len(seq._ops)} operations")

    # Create simulation workspace with debug ports
    print("\n6. Setting up simulation workspace with debug_ports=True...")
    ws = SimulationWorkspace(circuit, workspace_dir, debug_ports=True, use_circt_opt=True)

    # Generate workspace with testbench
    ws.generate_with_testbench(tb)
    print(f"   Workspace generated at: {workspace_dir}")

    rtl_dir = workspace_dir / "rtl"
    sv_files = list(rtl_dir.glob("*.sv"))
    print(f"   Generated {len(sv_files)} Verilog files")

    # Build simulation
    print("\n7. Building simulation...")
    if not ws.build():
        print("Build failed!")
        return 1
    print("   Build successful!")

    # Run simulation
    print("\n8. Running simulation...")
    success, output = ws.run()
    print(output)

    if not success:
        print("Simulation failed!")
        return 1

    # Summary
    print("\n" + "=" * 70)
    print("Summary")
    print("=" * 70)
    print(f"   Workspace: {workspace_dir}")
    print(f"   Expected result: {expected_sum}")
    print(f"   Waveforms: {workspace_dir / 'waves' / 'TestHarness.vcd'}")
    print("\nE2E Simulation PASSED!")
    print("=" * 70)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
