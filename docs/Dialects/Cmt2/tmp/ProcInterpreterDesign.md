# Proc Interpreter Design

## Overview

This document describes the execution model for `cmt2.proc.rule` and the design for direct interpretation in `cmt2-dbg`. The interpreter should directly interpret the proc operations according to their semantics, NOT simulate the lowered TDCC FSM.

**Key Principle:** The interpreter works on the original proc IR, maintaining hierarchical execution state that mirrors the control structure.

---

## Proc Execution Model

### Proc Operations Hierarchy

```
cmt2.proc.rule @name
├── guard region        (returns i1 - when to start)
└── control region      (hierarchical control flow)
    └── proc.seq / proc.par / proc.while / proc.static_repeat / proc.if
        └── proc.enable @step_name  (activates a step)
            └── proc.step / proc.static_step  (defined at module level)
```

### Operation Semantics

#### `proc.rule`
- **Guard**: Evaluated when FSM is idle. If true, start executing control region.
- **Control**: Hierarchical control flow that activates steps.
- **Lifetime**: Runs until control region completes, then returns to idle.

#### `proc.seq` (Sequential Composition)
- Children execute one after another.
- Next child starts when current child completes.
- Completes when last child completes.

```
cycle 0: child[0] starts
cycle N: child[0] done → child[1] starts
cycle M: child[1] done → child[2] starts
...
cycle K: last child done → seq completes
```

#### `proc.par` (Parallel Composition)
- All children start simultaneously.
- Each child runs independently.
- Completes when ALL children complete (fork-join).

```
cycle 0: all children start
cycle N: child[0] done (marked complete)
cycle M: child[1] done (marked complete)
...
cycle K: last child done → par completes
```

#### `proc.while` (Dynamic Loop)
- Has condition region and body region.
- Each iteration: evaluate condition → if true, execute body → repeat.
- Exits when condition evaluates to false.

```
cycle 0: evaluate condition
         if false → exit, while completes
         if true → body starts
cycle N: body done → re-evaluate condition
         ...
```

#### `proc.static_repeat` (Static Loop)
- Fixed iteration count known at compile time.
- Body executes N times unconditionally.
- No condition evaluation overhead.

```
cycle 0: iteration 0 starts
cycle L: iteration 0 done → iteration 1 starts
cycle 2L: iteration 1 done → iteration 2 starts
...
cycle (N-1)*L: iteration N-1 done → static_repeat completes
```

#### `proc.enable @step` (Step Activation)
- Activates the named step for execution.
- Completes when the step signals done.

#### `proc.step` (Dynamic Step)
- Executes operations when activated (go signal).
- Signals done via `proc.step_done` operation.
- Completion determined at runtime.

#### `proc.static_step` (Static Step)
- Has fixed latency (L cycles).
- Automatically completes after L cycles.
- No done signal needed.

---

## Interpretation Design

### Execution State Structure

The interpreter maintains a **hierarchical execution state** for each `proc.rule`:

```cpp
/// Execution state for a control construct
struct ProcExecState {
  enum Kind { Idle, Running, Done };
  Kind status = Idle;

  // Position tracking (depends on construct type)
  union {
    size_t seqIndex;           // For proc.seq: which child
    std::vector<bool> parDone; // For proc.par: which children done
    struct {                   // For proc.while
      bool evaluatingCond;
      bool bodyActive;
    } whileState;
    size_t repeatIter;         // For proc.static_repeat: iteration count
  };

  // Nested state for currently active child
  std::unique_ptr<ProcExecState> activeChild;
};

/// Root execution state for a proc.rule
struct ProcRuleExecState {
  bool isRunning = false;
  std::unique_ptr<ProcExecState> controlState;
};
```

### Execution Algorithm

#### Per-Cycle Step

```
For each proc.rule:
  1. If idle and guard is true:
     - Set isRunning = true
     - Initialize controlState for control region

  2. If running:
     - Advance execution state by one cycle
     - Execute any steps that are active this cycle
     - Check for completion

  3. If control region complete:
     - Set isRunning = false
     - Return to idle
```

#### Advancing Execution State

```cpp
void advanceState(Operation *op, ProcExecState &state) {
  if (auto seq = dyn_cast<ProcSeqOp>(op)) {
    advanceSeq(seq, state);
  } else if (auto par = dyn_cast<ProcParOp>(op)) {
    advancePar(par, state);
  } else if (auto whileOp = dyn_cast<ProcWhileOp>(op)) {
    advanceWhile(whileOp, state);
  } else if (auto repeat = dyn_cast<ProcStaticRepeatOp>(op)) {
    advanceStaticRepeat(repeat, state);
  } else if (auto enable = dyn_cast<ProcEnableOp>(op)) {
    advanceEnable(enable, state);
  }
}
```

#### Sequential Execution (`proc.seq`)

```cpp
void advanceSeq(ProcSeqOp seq, ProcExecState &state) {
  auto children = seq.getBody().front().getOperations();

  if (state.status == Idle) {
    // Start first child
    state.status = Running;
    state.seqIndex = 0;
    state.activeChild = createStateFor(children[0]);
  }

  if (state.status == Running) {
    Operation *currentChild = children[state.seqIndex];
    advanceState(currentChild, *state.activeChild);

    if (state.activeChild->status == Done) {
      // Move to next child
      state.seqIndex++;
      if (state.seqIndex >= children.size()) {
        state.status = Done;  // All children complete
      } else {
        state.activeChild = createStateFor(children[state.seqIndex]);
      }
    }
  }
}
```

#### Parallel Execution (`proc.par`)

```cpp
void advancePar(ProcParOp par, ProcExecState &state) {
  auto children = par.getBody().front().getOperations();

  if (state.status == Idle) {
    // Start all children
    state.status = Running;
    state.parDone.resize(children.size(), false);
    state.parChildren.clear();
    for (auto &child : children) {
      state.parChildren.push_back(createStateFor(&child));
    }
  }

  if (state.status == Running) {
    bool allDone = true;
    for (size_t i = 0; i < children.size(); i++) {
      if (!state.parDone[i]) {
        advanceState(children[i], *state.parChildren[i]);
        if (state.parChildren[i]->status == Done) {
          state.parDone[i] = true;
        } else {
          allDone = false;
        }
      }
    }
    if (allDone) {
      state.status = Done;
    }
  }
}
```

#### While Loop (`proc.while`)

```cpp
void advanceWhile(ProcWhileOp whileOp, ProcExecState &state) {
  if (state.status == Idle) {
    state.status = Running;
    state.whileState.evaluatingCond = true;
    state.whileState.bodyActive = false;
  }

  if (state.status == Running) {
    if (state.whileState.evaluatingCond) {
      // Evaluate condition region
      bool cond = evaluateCondition(whileOp.getCondRegion());
      if (!cond) {
        state.status = Done;  // Exit loop
      } else {
        // Start body
        state.whileState.evaluatingCond = false;
        state.whileState.bodyActive = true;
        state.activeChild = createStateFor(whileOp.getBody());
      }
    } else if (state.whileState.bodyActive) {
      advanceState(whileOp.getBody(), *state.activeChild);
      if (state.activeChild->status == Done) {
        // Body complete, re-evaluate condition
        state.whileState.evaluatingCond = true;
        state.whileState.bodyActive = false;
        state.activeChild.reset();
      }
    }
  }
}
```

#### Static Repeat (`proc.static_repeat`)

```cpp
void advanceStaticRepeat(ProcStaticRepeatOp repeat, ProcExecState &state) {
  uint64_t count = repeat.getCount();

  if (state.status == Idle) {
    state.status = Running;
    state.repeatIter = 0;
    state.activeChild = createStateFor(repeat.getBody());
  }

  if (state.status == Running) {
    advanceState(repeat.getBody(), *state.activeChild);

    if (state.activeChild->status == Done) {
      state.repeatIter++;
      if (state.repeatIter >= count) {
        state.status = Done;  // All iterations complete
      } else {
        // Start next iteration
        state.activeChild = createStateFor(repeat.getBody());
      }
    }
  }
}
```

#### Enable Step (`proc.enable`)

```cpp
void advanceEnable(ProcEnableOp enable, ProcExecState &state) {
  StringRef stepName = enable.getStepName();

  if (state.status == Idle) {
    state.status = Running;
    activateStep(stepName);  // Execute step body
  }

  if (state.status == Running) {
    if (isStepDone(stepName)) {
      state.status = Done;
    }
  }
}
```

### Step Execution

#### Dynamic Step (`proc.step`)
- Execute the step body operations
- Check for `proc.step_done` to determine completion
- Step is done when done signal is asserted

#### Static Step (`proc.static_step`)
- Execute the step body operations
- Track cycle count since activation
- Step is done after `latency` cycles

---

## Comparison: Direct vs TDCC-based Interpretation

| Aspect | Direct Interpretation | TDCC-based Interpretation |
|--------|----------------------|---------------------------|
| Dependency | None - works on original IR | Requires running TDCC pass first |
| Structure | Hierarchical (matches semantic model) | Flat FSM (loses structure) |
| Complexity | Simple recursive traversal | Complex state machine tracking |
| Correctness | Directly implements semantics | Must match TDCC encoding exactly |
| Debugging | Easy to trace execution | Hard to map FSM states to source |
| Flexibility | Can interpret any valid proc IR | Only works with TDCC-annotated IR |

---

## Implementation Plan

### Phase 1: Core Infrastructure
1. Define `ProcExecState` hierarchy
2. Implement state creation for each control construct
3. Add execution state to `ProcFSMStates_` (rename to `ProcExecStates_`)

### Phase 2: Control Flow
1. Implement `advanceSeq()`
2. Implement `advancePar()`
3. Implement `advanceWhile()`
4. Implement `advanceStaticRepeat()`
5. Implement `advanceIf()` / `advanceStaticIf()`

### Phase 3: Step Execution
1. Implement `advanceEnable()`
2. Connect to existing `executeProcStep()` for step body execution
3. Track step completion (done signals for dynamic, cycle count for static)

### Phase 4: Integration
1. Replace TDCC-based execution in `executeProcRuleStep()`
2. Remove TDCC parsing code
3. Test with proc_pipeline.py and other examples

---

## Files to Modify

- `tools/cmt2-dbg/Interpreter.h`
  - Add `ProcExecState` hierarchy
  - Rename `ProcFSMState` to `ProcRuleExecState`
  - Remove TDCC-specific data structures

- `tools/cmt2-dbg/Interpreter.cpp`
  - Remove `parseTDCCAttributes()`
  - Remove `executeProcRuleTDCC()` and related functions
  - Add `advanceControlState()` and related functions
  - Update `executeProcRuleStep()` to use direct interpretation
