# Remove `proc.step_done` / `step.done()` Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove dynamic-step done signaling (`cmt2.proc.step_done` / PyCMT2 `step.done`) and make procedural FSMs transition to `NEXT` whenever the state rule fires (`ready==1`), without breaking proc control, dataflow tasks, or existing examples/tests.

**Architecture:** Treat `cmt2.proc.step` as a single-fire step (conceptually equivalent to `static_step(1)`): a step executes atomically when its state rule fires, and the FSM advances in the same cycle. Waiting for runtime conditions is expressed via explicit `proc.while` / `proc.cond_if` transitions (condition regions) rather than step-local done conditions.

**Tech Stack:** MLIR (Cmt2 dialect + transforms), FIRRTL lowering, lit/FileCheck tests, PyCMT2 frontend.

---

### Task 1: Add regression tests that fail with old semantics

**Files:**
- Create: `test/Dialect/Cmt2/proc-step-no-done.mlir`

**Step 1: Write the failing test**
- Create a minimal `cmt2.proc.rule` with two enabled steps and verify that TDCC emits unconditional transitions between the step states (no `done_step` metadata).

**Step 2: Run test to verify it fails**

Run: `build/bin/llvm-lit -v test/Dialect/Cmt2/proc-step-no-done.mlir`

Expected: FAIL (because current TDCC still emits done-guarded transitions / step_done exists).

**Step 3: Commit**

Run:
```bash
git add test/Dialect/Cmt2/proc-step-no-done.mlir
git commit -m "test: add coverage for proc step without done"
```

---

### Task 2: Remove the IR op `cmt2.proc.step_done`

**Files:**
- Modify: `include/circt/Dialect/Cmt2/Cmt2Ops.td`
- Modify: `lib/Dialect/Cmt2/Cmt2Ops.cpp`
- Modify: `lib/Dialect/Cmt2/Transforms/CompileInvoke.cpp`

**Step 1: Update TableGen**
- Delete `ProcStepDoneOp` definition.
- Update `ProcStepOp`/`ProcEnableOp` docs to remove go-done protocol references.

**Step 2: Update C++**
- Remove verifier/printing/parsing (if any) for `ProcStepDoneOp`.
- Update `CompileInvoke` to stop creating `ProcStepDoneOp`.

**Step 3: Build**

Run: `ninja -C build circt-opt`

Expected: successful build.

**Step 4: Commit**

Run:
```bash
git add include/circt/Dialect/Cmt2/Cmt2Ops.td lib/Dialect/Cmt2/Cmt2Ops.cpp lib/Dialect/Cmt2/Transforms/CompileInvoke.cpp
git commit -m "cmt2: remove proc.step_done op"
```

---

### Task 3: Remove done-guard plumbing from TDCC and ProcStmtToAction

**Files:**
- Modify: `lib/Dialect/Cmt2/Transforms/TDCC.cpp`
- Modify: `lib/Dialect/Cmt2/Transforms/ProcStmtToAction.cpp`

**Step 1: TDCC**
- Remove `GuardSpec::doneGuard`, `doneStepName`, and any serialization of `done_step`.
- For `ProcEnableOp`, always return unconditional exit edges (dynamic step latency=1, no done guard).
- For `proc.par` “simple par” exit, remove done-guard logic (join purely by state progression).

**Step 2: ProcStmtToAction**
- Stop reading `done_step` from `tdcc.transitions`.
- Remove state-transition generation branches that special-case done-guarded transitions.

**Step 3: Run targeted lit**

Run:
```bash
build/bin/llvm-lit -v test/Dialect/Cmt2/proc-step-no-done.mlir
build/bin/llvm-lit -v test/Dialect/Cmt2/
```

Expected: the new test passes; fix any unrelated breakages caused by removing done guards.

**Step 4: Commit**

Run:
```bash
git add lib/Dialect/Cmt2/Transforms/TDCC.cpp lib/Dialect/Cmt2/Transforms/ProcStmtToAction.cpp
git commit -m "cmt2: make proc steps advance on fire"
```

---

### Task 4: Update PyCMT2 frontend and examples

**Files:**
- Modify: `lib/Bindings/Python/pycmt2/proc_builders.py`
- Modify: `examples/PyCMT2/*.py`

**Step 1: Remove API**
- Remove `StepBuilder.done()` and the “must call done()” finalize check.

**Step 2: Rewrite examples**
- Replace `step.done(cond)` patterns:
  - If `cond` duplicates a callee ready (e.g. `has_data` vs `dequeue.ready`), remove the extra call and keep a single-step dequeue.
  - Otherwise, rewrite as `proc.while`/`proc.cond_if` using the condition region to control state transitions.
- Ensure examples still produce the same externally visible behavior.

**Step 3: Run at least one end-to-end sim**

Run: `PYTHONPATH=build/tools/circt/python_packages/circt_core python3 examples/PyCMT2/while_loop_example.py`

Expected: PASS and emits waveforms (if configured).

**Step 4: Commit**

Run:
```bash
git add lib/Bindings/Python/pycmt2/proc_builders.py examples/PyCMT2
git commit -m "pycmt2: remove step.done and update examples"
```

---

### Task 5: Documentation updates

**Files:**
- Modify: `docs/Cmt2/features/MultiCycle.md`
- Modify: `docs/Cmt2/features/Concepts.md` (if it references `step.done`)

**Step 1: Update semantics**
- Remove or rewrite the “Dynamic steps” section to no longer mention `step.done(cond)` as a completion mechanism.
- Explain the new rule: dynamic steps advance on fire; runtime waiting uses `proc.while`/condition regions; prefer `static_step(1)` when possible.

**Step 2: Commit**

Run:
```bash
git add docs/Cmt2/features/MultiCycle.md docs/Cmt2/features/Concepts.md
git commit -m "docs(cmt2): update proc step semantics without done"
```

---

### Task 6: Final verification

**Step 1: Full targeted suite**

Run:
```bash
ninja -C build circt-opt
build/bin/llvm-lit -v test/Dialect/Cmt2/
```

Expected: all Cmt2 tests pass.

**Step 2: Summarize PR impact**
- Prepare a short PR comment explaining the semantic change and how to migrate `step.done(cond)` to explicit `while`/`cond_if`.

