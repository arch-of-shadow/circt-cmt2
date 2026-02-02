# Cmt2 JIT Example Pass Marker Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the `examples/JIT/run_examples.py` pass/fail signal stable by requiring a single, explicit pass marker printed by each example script (not by Verilator/Testbench output).

**Architecture:** Add a single canonical marker string printed by every JIT example on success. Update the runner’s `--strict-pass-markers` mode to require that marker, so it is independent of tool output changes. Roll the marker into examples in small batches to keep diffs reviewable.

**Tech Stack:** Python (`examples/JIT/*`, `examples/JIT/run_examples.py`), CIRCT Python bindings (PyCMT2), Verilator (for E2E examples).

---

## Pre-flight (Worktree + Baseline)

### Task 0: Create a dedicated worktree

**Files:** none

**Step 1: Create worktree**

Run:
```bash
cd /home/uvxiao/circt-cmt2
git worktree add -b cmt2-jit-pass-marker ../wt-cmt2-jit-pass-marker
cd ../wt-cmt2-jit-pass-marker
```

Expected: new directory `../wt-cmt2-jit-pass-marker` checked out on branch `cmt2-jit-pass-marker`.

**Step 2: Baseline-run the suite (non-strict)**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going --log-dir examples/JIT/_logs
```

Expected: `Total: 33 passed, 0 failed` (or current count if examples list changes).

**Step 3: Quick manual log scan**

Run:
```bash
rg -n "Traceback|\\bFAIL\\b" examples/JIT/_logs -S || true
```

Expected: no matches.

**Step 4: Commit (baseline bookkeeping only if needed)**

If you changed nothing: skip commit.

---

## Core Change (Runner Contract)

### Task 1: Require a stable marker in strict mode

**Files:**
- Modify: `examples/JIT/run_examples.py`

**Step 1: Write the failing “test” (runner behavior)**

Edit `examples/JIT/run_examples.py` so `--strict-pass-markers` requires the exact marker string:

```python
PASS_MARKER = "CMT2_JIT_EXAMPLE_PASS"
```

And check:

```python
has_pass_marker = PASS_MARKER in out
```

(Keep non-strict mode unchanged except it should still warn when the marker is missing.)

**Step 2: Run to verify it fails (expected until examples updated)**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going --strict-pass-markers --log-dir examples/JIT/_logs
```

Expected: failures like “produced no PASS marker”.

**Step 3: Commit**

Run:
```bash
git add examples/JIT/run_examples.py
git commit -m "examples/JIT: make strict runner require stable pass marker"
```

---

## Rollout (Add Marker to Every Example)

**Rule:** Each example must print the exact line on success:

```python
print("CMT2_JIT_EXAMPLE_PASS")
```

Place it on the success path only (just before returning `0`, or at the end of `main()` after all assertions/checks).

### Task 2: Add marker to the first batch (basic + runner-adjacent)

**Files:**
- Modify: `examples/JIT/jit_counter.py`
- Modify: `examples/JIT/jit_fifo.py`
- Modify: `examples/JIT/interface_hello.py`
- Modify: `examples/JIT/simulation_workspace.py`
- Modify: `examples/JIT/diagnostics.py`

**Step 1: Add the marker to each script**

Add:
```python
print("CMT2_JIT_EXAMPLE_PASS")
```
on the success path.

**Step 2: Run these scripts directly**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python python3 examples/JIT/jit_counter.py
PYTHONPATH=build/tools/circt/python_packages/circt_core:python python3 examples/JIT/jit_fifo.py
PYTHONPATH=build/tools/circt/python_packages/circt_core:python python3 examples/JIT/interface_hello.py
PYTHONPATH=build/tools/circt/python_packages/circt_core:python python3 examples/JIT/simulation_workspace.py
PYTHONPATH=build/tools/circt/python_packages/circt_core:python python3 examples/JIT/diagnostics.py
```

Expected: each prints `CMT2_JIT_EXAMPLE_PASS` once, and exits `0`.

**Step 3: Run strict runner and ensure progress**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going --strict-pass-markers --log-dir examples/JIT/_logs
```

Expected: fewer failures than before (the batch should now pass strict marker checks).

**Step 4: Commit**

Run:
```bash
git add examples/JIT/jit_counter.py examples/JIT/jit_fifo.py examples/JIT/interface_hello.py \
  examples/JIT/simulation_workspace.py examples/JIT/diagnostics.py
git commit -m "examples/JIT: add stable pass marker (batch 1)"
```

### Task 3: Add marker to batch 2 (core E2E)

**Files:**
- Modify: `examples/JIT/counter.py`
- Modify: `examples/JIT/gcd.py`
- Modify: `examples/JIT/proc.py`
- Modify: `examples/JIT/static_proc.py`
- Modify: `examples/JIT/timing.py`

**Step 1: Add marker to each script (success path)**

**Step 2: Run strict runner**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going --strict-pass-markers --log-dir examples/JIT/_logs
```

Expected: more examples pass.

**Step 3: Commit**

Run:
```bash
git add examples/JIT/counter.py examples/JIT/gcd.py examples/JIT/proc.py examples/JIT/static_proc.py examples/JIT/timing.py
git commit -m "examples/JIT: add stable pass marker (batch 2)"
```

### Task 4: Add marker to batch 3 (dataflow + pipelines)

**Files:**
- Modify: `examples/JIT/dataflow_forkjoin.py`
- Modify: `examples/JIT/dataflow_proc_control.py`
- Modify: `examples/JIT/dynamic_pipeline_fifo.py`
- Modify: `examples/JIT/division_pipeline.py`
- Modify: `examples/JIT/pipeline_e2e.py`

**Step 1: Add marker to each script**

**Step 2: Run strict runner**

**Step 3: Commit**

Run:
```bash
git add examples/JIT/dataflow_forkjoin.py examples/JIT/dataflow_proc_control.py \
  examples/JIT/dynamic_pipeline_fifo.py examples/JIT/division_pipeline.py examples/JIT/pipeline_e2e.py
git commit -m "examples/JIT: add stable pass marker (batch 3)"
```

### Task 5: Add marker to batch 4 (bigger/longer examples)

**Files:**
- Modify: `examples/JIT/alu.py`
- Modify: `examples/JIT/systolic.py`
- Modify: `examples/JIT/banked_gemm_dataflow.py`
- Modify: `examples/JIT/comprehensive_example.py`
- Modify: `examples/JIT/comprehensive_dataflow_example.py`

**Step 1: Add marker to each script**

**Step 2: Run strict runner**

**Step 3: Commit**

Run:
```bash
git add examples/JIT/alu.py examples/JIT/systolic.py examples/JIT/banked_gemm_dataflow.py \
  examples/JIT/comprehensive_example.py examples/JIT/comprehensive_dataflow_example.py
git commit -m "examples/JIT: add stable pass marker (batch 4)"
```

### Task 6: Add marker to batch 5 (remaining)

**Files:**
- Modify: `examples/JIT/debug_testbench_example.py`
- Modify: `examples/JIT/interpret.py`
- Modify: `examples/JIT/li_token_pipeline.py`
- Modify: `examples/JIT/memory_proc.py`
- Modify: `examples/JIT/nested_dataflow_example.py`
- Modify: `examples/JIT/pipeline_fifo_testbench.py`
- Modify: `examples/JIT/proc_par_test.py`
- Modify: `examples/JIT/proc_pipeline.py`
- Modify: `examples/JIT/proc_testbench.py`
- Modify: `examples/JIT/stl_test.py`
- Modify: `examples/JIT/test_cond_if.py`
- Modify: `examples/JIT/test_submodule_proc_step.py`
- Modify: `examples/JIT/while_loop_example.py`

**Step 1: Add marker to each script**

**Step 2: Run strict runner (must fully pass)**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going --strict-pass-markers --log-dir examples/JIT/_logs
```

Expected: `Total: 33 passed, 0 failed`.

**Step 3: Commit**

Run:
```bash
git add examples/JIT/debug_testbench_example.py examples/JIT/interpret.py examples/JIT/li_token_pipeline.py \
  examples/JIT/memory_proc.py examples/JIT/nested_dataflow_example.py examples/JIT/pipeline_fifo_testbench.py \
  examples/JIT/proc_par_test.py examples/JIT/proc_pipeline.py examples/JIT/proc_testbench.py examples/JIT/stl_test.py \
  examples/JIT/test_cond_if.py examples/JIT/test_submodule_proc_step.py examples/JIT/while_loop_example.py
git commit -m "examples/JIT: add stable pass marker (batch 5)"
```

---

## Docs + Final Verification

### Task 7: Document the stable marker contract

**Files:**
- Modify: `examples/JIT/README.md`
- Modify: `docs/Cmt2/guides/JIT.md`
- Modify: `docs/Cmt2/examples/Examples.md`

**Step 1: Update docs**

Add a short note that `examples/JIT/run_examples.py --strict-pass-markers` requires the line:

```text
CMT2_JIT_EXAMPLE_PASS
```

**Step 2: Run strict runner one last time**

Run:
```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going --strict-pass-markers --log-dir examples/JIT/_logs
```

Expected: full PASS.

**Step 3: Commit**

Run:
```bash
git add examples/JIT/README.md docs/Cmt2/guides/JIT.md docs/Cmt2/examples/Examples.md
git commit -m "docs: document JIT example stable pass marker"
```

---

## Optional Hardening (Only if Needed)

### Task 8 (Optional): Make strict mode default in CI scripts

**Files:**
- Modify: any CI/helper scripts that run examples (if present)

**Step 1: Switch invocation to include `--strict-pass-markers`**

**Step 2: Run and confirm no regressions**

**Step 3: Commit**

---

## Completion Checklist

- `python3 -m compileall -q python/cmt2/jit examples/JIT` passes
- `python3 examples/JIT/run_examples.py --keep-going --strict-pass-markers` passes
- Manual log scan shows no `Traceback` / unexpected `FAIL`

