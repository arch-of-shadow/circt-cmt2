# Precedence Handling in CMT2 Proc Lowering

This document provides a comprehensive analysis of precedence handling during procedural rule lowering in CMT2.

---

## 1. Background: What is Precedence?

In CMT2's GAA (Guarded Atomic Actions) semantics, multiple rules may be enabled in the same cycle but conflict with each other. The **precedence** attribute defines the relative priority among rules.

```mlir
cmt2.module @Counter {
  cmt2.rule @increment { ... }
  cmt2.rule @decrement { ... }
  cmt2.rule @reset { ... }
} {precedence = [[@increment, @decrement, @reset]]}
```

**Semantics:**
- Rules earlier in the chain have higher priority
- When `@increment` and `@decrement` both fire, `@increment` wins
- Enables deterministic scheduling without runtime arbitration

**Purpose:**
1. Resolve conflicts between mutually-enabled rules
2. Define execution order for sequential composition
3. Enable optimization by avoiding unnecessary conflict checks

---

## 2. The Problem: Proc Lowering Generates Many Rules

A single `cmt2.proc.rule` with control flow lowers to multiple GAA rules:

```python
# Original proc rule
with harness.proc_rule("main") as main:
    with main.control() as ctrl:
        with ctrl.seq() as seq:
            with seq.par() as par:
                with par.seq() as push_seq:
                    push_seq.static_repeat(3): push_static
                    push_seq.while_(cond): push_item
                with par.seq() as pop_seq:
                    pop_seq.static_repeat(3): wait_static
                    pop_seq.while_(cond): pop_item
            seq.enable(mark_done)
```

**After lowering, this generates ~15+ rules:**

| Rule Type | Example Name | Purpose |
|-----------|--------------|---------|
| Main FSM state | `main_state0` | Initial state / idle check |
| Main FSM state | `main_state1` | Fork into parallel branches |
| Main FSM state | `main_state2` | Join - wait for branches |
| Main FSM state | `main_state3` | Execute mark_done |
| Main FSM reset | `main_reset` | Reset FSM on completion |
| Branch 0 state | `main___par_0_branch_0_state1` | First state of push branch |
| Branch 0 state | `main___par_0_branch_0_state5` | Push step execution |
| Branch 0 cond | `main___par_0_branch_0_cond4` | While condition check |
| Branch 0 done | `main___par_0_branch_0_done` | Branch completion signal |
| Branch 1 state | `main___par_0_branch_1_state1` | First state of pop branch |
| Branch 1 state | `main___par_0_branch_1_state5` | Pop step execution |
| Branch 1 cond | `main___par_0_branch_1_cond4` | While condition check |
| Branch 1 done | `main___par_0_branch_1_done` | Branch completion signal |

**Question:** How should these generated rules be ordered in precedence?

---

## 3. Concrete Example: proc_pipeline.py

### 3.1 Module Structure

**Pipeline module** (3-stage FIFO):
```python
with circuit.module("Pipeline_3") as pipeline:
    # FIFOs: stage_0, stage_1, stage_2

    # Methods with guards
    with pipeline.method("enq", ...) as enq:
        # guard: !full
        body.call(fifos[0], "enq", data)

    with pipeline.method("deq", ...) as deq:
        # guard: notEmpty
        body.call(fifos[-1], "deq")

    # Transfer rules (move data between stages)
    with pipeline.rule("transfer_0") as t0:
        body.call(fifos[0], "deq")
        body.call(fifos[1], "enq", data)

    # User-specified precedence: enq < deq < transfers
    pipeline.precedence(enq, deq, transfer_0, transfer_1)
```

**TestHarness module** (calls Pipeline):
```python
with circuit.module("TestHarness") as harness:
    pipe = harness.instance(pipeline_mod, "pipe", ...)

    # Step: push_static - calls pipe.enq
    with harness.static_step(1, "push_static") as push_static:
        push_static.call(pipe, "enq", cnt)

    # Step: pop_item - calls pipe.deq
    with harness.step("pop_item") as pop_step:
        data = pop_step.call(pipe, "deq")

    # Proc rule with parallel control
    with harness.proc_rule("main") as main:
        with main.control() as ctrl:
            with ctrl.par() as par:
                # Push branch
                with par.seq() as push_seq:
                    with push_seq.static_repeat(3) as prime:
                        prime.enable(push_static.ref())
                    with push_seq.while_(cond) as loop:
                        loop.enable(push_step.ref())

                # Pop branch
                with par.seq() as pop_seq:
                    with pop_seq.static_repeat(3) as wait:
                        wait.enable(wait_static.ref())
                    with pop_seq.while_(cond) as loop:
                        loop.enable(pop_step.ref())
```

### 3.2 Generated Rules

After lowering, TestHarness contains these rules:

```mlir
// Main FSM rules
cmt2.rule @main_state0 { ... }     // Check guard, init fork
cmt2.rule @main_state1 { ... }     // Fork state
cmt2.rule @main_state2 { ... }     // Join state
cmt2.rule @main_state3 { ... }     // Final step
cmt2.rule @main_reset { ... }      // Reset FSM

// Branch 0 (push) rules
cmt2.rule @main___par_0_branch_0_state1 { ... }  // static_repeat iter 1
cmt2.rule @main___par_0_branch_0_state2 { ... }  // static_repeat iter 2
cmt2.rule @main___par_0_branch_0_state3 { ... }  // static_repeat iter 3
cmt2.rule @main___par_0_branch_0_cond4 { ... }   // while condition
cmt2.rule @main___par_0_branch_0_state5 {        // while body
  // Step body operations cloned here:
  cmt2.call @pipe @enq(%cnt) : ...               // <-- Calls pipe.enq!
  cmt2.call @in_counter @write(%next) : ...
  // FSM transition:
  cmt2.call @__branch_0_fsm @write(%c4) : ...
}
cmt2.value @main___par_0_branch_0_done { ... }   // Branch done

// Branch 1 (pop) rules
cmt2.rule @main___par_0_branch_1_state1 { ... }
cmt2.rule @main___par_0_branch_1_state2 { ... }
cmt2.rule @main___par_0_branch_1_state3 { ... }
cmt2.rule @main___par_0_branch_1_cond4 { ... }
cmt2.rule @main___par_0_branch_1_state5 {
  // Step body operations cloned here:
  %data = cmt2.call @pipe @deq() : ...           // <-- Calls pipe.deq!
  cmt2.call @out_sum @write(%new_sum) : ...
  // FSM transition:
  cmt2.call @__branch_1_fsm @write(%c4) : ...
}
cmt2.value @main___par_0_branch_1_done { ... }
```

### 3.3 Conflict Analysis

**Observation:** The generated state rules contain not just FSM transitions, but **cloned step body operations**.

| Rule | Operations | Potential Conflicts |
|------|------------|---------------------|
| `@main___par_0_branch_0_state5` | `pipe.enq`, counter writes, FSM write | With rules calling `pipe.deq` |
| `@main___par_0_branch_1_state5` | `pipe.deq`, sum write, FSM write | With rules calling `pipe.enq` |

**Pipeline's internal constraint:**
```mlir
{precedence = [[@enq, @deq, @transfer_0, @transfer_1]]}
// Means: enq should happen before deq in the same cycle
```

**Implication for TestHarness:**
- `@main___par_0_branch_0_state5` calls `pipe.enq`
- `@main___par_0_branch_1_state5` calls `pipe.deq`
- If both fire in the same cycle, they need precedence matching Pipeline's constraint

---

## 4. Current Implementation Analysis

### 4.1 What ProcStmtToAction Currently Does

From `lib/Dialect/Cmt2/Transforms/ProcStmtToAction.cpp`:

```cpp
// Lines 1915-1991: Update module precedence
{
  SmallVector<Attribute> newPrecedenceChains;

  // 1. Preserve existing precedence
  if (auto existingPrec = module->getAttrOfType<ArrayAttr>("precedence")) {
    for (auto chain : existingPrec) {
      newPrecedenceChains.push_back(chain);
    }
  }

  // 2. Parse par_blocks for fork/join and branch states
  SmallVector<uint64_t> forkJoinStates;
  SmallVector<uint64_t> branchHeaderStates;
  SmallVector<uint64_t> branchBodyStates;

  if (auto parBlocksAttr = rule->getAttrOfType<ArrayAttr>("tdcc.par_blocks")) {
    // Extract fork/join states and branch states from TDCC metadata
    ...
  }

  // 3. Build precedence chain: body >> header >> fork/join >> reset
  SmallVector<Attribute> stateChain;
  for (uint64_t s : branchBodyStates) { /* add _stateN */ }
  for (uint64_t s : branchHeaderStates) { /* add _stateN */ }
  for (uint64_t s : forkJoinStates) { /* add _stateN */ }
  stateChain.push_back(resetRule);

  newPrecedenceChains.push_back(ArrayAttr::get(ctx, stateChain));
  module->setAttr("precedence", ArrayAttr::get(ctx, newPrecedenceChains));
}
```

### 4.2 What IS Included in Precedence

| Rule Category | Included? | Source |
|--------------|-----------|--------|
| Fork/join states (from `tdcc.par_blocks`) | Yes | Lines 1939-1940 |
| Branch header states (first state per branch) | Yes | Line 1950 |
| Branch body states (middle states) | Yes | Lines 1951-1953 |
| Reset rule | Yes | Lines 1984-1985 |

### 4.3 What is NOT Included

| Rule Category | Included? | Impact |
|--------------|-----------|--------|
| Regular sequential states (non-par) | **No** | May serialize unnecessarily |
| Branch FSM state rules (`_branch_*_state*`) | **No** | Cross-branch conflicts unhandled |
| Condition rules (`_branch_*_cond*`) | **No** | While loop conditions unordered |
| Done values (`_branch_*_done`) | **No** | Values excluded from rule precedence |
| State rules from steps with conflicting methods | **No** | Step body conflicts unhandled |

### 4.4 Why Current Implementation "Works"

**Correctness is preserved** because:
1. All FSM state rules have **mutually exclusive guards** (`fsm == stateN`)
2. Only one state per FSM can match at any time
3. Guards prevent conflicting transitions

**But performance may be suboptimal**:
1. Scheduler may not know rules can run in parallel
2. Conservative conflict detection adds overhead
3. Cross-branch method conflicts may cause unexpected serialization

---

## 5. Precedence Principles for Generated Rules

### Key Insight: Control Flow Determines Precedence

**Important:** Precedence among proc-generated rules is determined by the **control flow structure**, NOT by callee modules' constraints.

Consider a 2-stage `static_step`:
```python
with m.static_step(2, "pipeline_op") as step:
    # Stage 0: enqueue
    step.call(fifo, "enq", data)
    # Stage 1: dequeue (after 1 cycle)
    result = step.call(fifo, "deq")
```

This generates two state rules:
- `@main_state1` - Stage 0 (enq)
- `@main_state2` - Stage 1 (deq)

**Question:** What's the precedence between these rules?

**Wrong answer:** Look at FIFO's `sequenceBefore = [[@enq, @deq]]` and derive that stage1 should precede stage2.

**Correct answer:** Stage 2 has higher precedence than Stage 1, because:
1. When both rules want to fire simultaneously, they're processing **different iterations**
2. Stage 2 is executing an **older iteration** (started earlier)
3. In pipeline semantics, **older iterations have higher priority**

```
Cycle 0: Stage1 fires (iteration 0)
Cycle 1: Stage1 fires (iteration 1), Stage2 fires (iteration 0)
         → Stage2 has priority (older iteration)
Cycle 2: Stage1 fires (iteration 2), Stage2 fires (iteration 1)
         → Stage2 has priority (older iteration)
```

### Principle: Proc Structure > Callee Constraints

**We do NOT derive proc-generated rules' precedence from callee modules.**

The precedence is inherent in the proc's control structure:
- **Sequential (`seq`)**: Later steps > earlier steps (later = older iteration when pipelined)
- **Static repeat**: Later iterations > earlier iterations
- **Parallel branches**: Independent (no inherent ordering between branches)

### Category 1: Intra-FSM Conflicts

Multiple rules write the same FSM register:

```mlir
// Both write @__fsm
cmt2.rule @main_state1 { ... } { cmt2.call @__fsm @write(%c2) }
cmt2.rule @main_state2 { ... } { cmt2.call @__fsm @write(%c3) }
```

**Resolution:** Mutual exclusion via guards. Only one can fire per cycle.

### Category 2: Sequential State Ordering

Within a sequential flow, later states have higher precedence:

```mlir
// state2 > state1 (state2 executes older iteration)
cmt2.rule @main_state1 { ... }  // Lower precedence
cmt2.rule @main_state2 { ... }  // Higher precedence
```

**Resolution:** Add to precedence chain in reverse order: `[@state2, @state1]`

### Category 3: Generated vs User-Defined Rules

Generated rules may need ordering relative to user rules:

```mlir
// User-defined rule
cmt2.rule @user_rule { ... }

// Generated state rules
cmt2.rule @main_state1 { ... }
```

**Resolution:** Place generated rules after user rules in module precedence (proc executes "within" module's normal behavior).

---

## 6. Limitations of Current Ad-Hoc Approach

### 6.1 Scattered Logic

Precedence handling is split across:
- `TDCC.cpp`: State allocation, par_blocks metadata
- `ProcStmtToAction.cpp`: Precedence chain construction
- No centralized conflict analysis

### 6.2 Incomplete Coverage

Current implementation only handles:
- Fork/join states from `par` blocks
- Some branch states

Missing:
- Regular sequential states
- Branch FSM rules
- Condition rules
- Step body conflict analysis

### 6.3 No Cross-Module Awareness

When TestHarness calls `pipe.enq` and `pipe.deq`, it doesn't know about Pipeline's internal `sequenceBefore` constraints.

### 6.4 Ad-Hoc State Collection

States are collected by walking `tdcc.par_blocks` attribute. This misses:
- States not in par blocks
- States generated for while/if constructs
- Dynamic step states

---

## 7. Proposed Architecture

### 7.1 Design Principles

1. **Control flow determines precedence**: Derive ordering from proc structure, not callee modules
2. **Later states > earlier states**: Pipeline semantics - older iterations have higher priority
3. **FSM grouping**: Rules writing the same FSM belong together
4. **Generated after user**: Proc-generated rules follow user-defined rules

### 7.2 Two-Phase Design

```
┌─────────────────────────────────────────────────────────────────┐
│  Phase 1: State Analysis (TDCC Pass)                            │
│  - Assign states to control flow nodes                          │
│  - Track sequential ordering (which states follow which)        │
│  - Identify FSM groupings (main FSM vs branch FSMs)             │
│  - Store state metadata for precedence generation               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  Phase 2: Precedence Generation (ProcStmtToAction)              │
│  - Generate state rules (as now)                                │
│  - Build precedence chains based on state ordering:             │
│    • Later states before earlier states (pipeline semantics)    │
│    • Group by FSM register                                      │
│    • Place after user-defined rules                             │
└─────────────────────────────────────────────────────────────────┘
```

### 7.3 Data Structures

**StateOrderInfo** (TDCC output):
```cpp
struct StateOrderInfo {
  uint64_t state;
  StringRef fsmRegister;      // Which FSM this state belongs to
  uint64_t sequenceIndex;     // Position in sequential flow (higher = later)
  bool isConditionState;      // For while loop headers
};
```

Serialized as:
```mlir
tdcc.state_order = [
  {state = 1, fsm = "@__fsm", seq_idx = 0},
  {state = 2, fsm = "@__fsm", seq_idx = 1},
  {state = 3, fsm = "@__fsm", seq_idx = 2},
  {state = 1, fsm = "@__branch_0_fsm", seq_idx = 0},
  {state = 2, fsm = "@__branch_0_fsm", seq_idx = 1}
]
```

**GeneratedRuleInfo** (ProcStmtToAction):
```cpp
struct GeneratedRuleInfo {
  StringRef ruleName;           // e.g., "main_state2"
  FlatSymbolRefAttr fsmInstance;// Which FSM register
  uint64_t sequenceIndex;       // From TDCC state_order
};
```

### 7.4 Algorithm: Precedence from Control Flow

```cpp
void buildPrecedenceFromControlFlow(
    ModuleOp module,
    ArrayRef<GeneratedRuleInfo> rules) {

  // 1. Group rules by FSM register
  DenseMap<FlatSymbolRefAttr, SmallVector<GeneratedRuleInfo*>> fsmGroups;
  for (auto &rule : rules) {
    fsmGroups[rule.fsmInstance].push_back(&rule);
  }

  SmallVector<Attribute> precedenceChains;

  // 2. Preserve existing user-defined precedence
  if (auto existing = module.getPrecedence()) {
    precedenceChains.append(existing.begin(), existing.end());
  }

  // 3. For each FSM group, order by sequence index (descending)
  //    Later states (higher seq_idx) have higher precedence
  for (auto &[fsm, group] : fsmGroups) {
    // Sort by sequence index descending
    llvm::sort(group, [](auto *a, auto *b) {
      return a->sequenceIndex > b->sequenceIndex;
    });

    SmallVector<Attribute> chain;
    for (auto *rule : group) {
      chain.push_back(FlatSymbolRefAttr::get(ctx, rule->ruleName));
    }
    precedenceChains.push_back(ArrayAttr::get(ctx, chain));
  }

  module->setAttr("precedence", ArrayAttr::get(ctx, precedenceChains));
}
```

### 7.5 Precedence Generation Rules

1. **Same-FSM Grouping**: All rules writing the same FSM register go in one chain
2. **Descending Sequence Order**: Later states (higher sequence index) first in chain
3. **User Rules Preserved**: Existing user-defined precedence unchanged
4. **No Cross-Module Analysis**: Precedence comes from proc structure alone

### 7.6 Example Output

For proc_pipeline.py's TestHarness:

```mlir
cmt2.module @TestHarness {
  ...
} {precedence = [
  // User-defined: done, result values first (preserved)
  [@done, @result],

  // Main FSM states - ordered by sequence index (descending)
  // state3 > state2 > state1 > state0 (later states have higher precedence)
  [@main_state3, @main_state2, @main_state1, @main_state0, @main_reset],

  // Branch 0 FSM - ordered by sequence index (descending)
  // state5 > cond4 > state3 > state2 > state1
  [@main___par_0_branch_0_state5, @main___par_0_branch_0_cond4,
   @main___par_0_branch_0_state3, @main___par_0_branch_0_state2,
   @main___par_0_branch_0_state1],

  // Branch 1 FSM - ordered by sequence index (descending)
  [@main___par_0_branch_1_state5, @main___par_0_branch_1_cond4,
   @main___par_0_branch_1_state3, @main___par_0_branch_1_state2,
   @main___par_0_branch_1_state1]

  // Note: NO cross-branch precedence needed!
  // Branch 0 and Branch 1 write different FSM registers
  // Their ordering is independent
]}
```

**Key insight:** Even though branch 0 calls `pipe.enq` and branch 1 calls `pipe.deq`, we do NOT add cross-branch precedence based on FIFO's constraints. The branches are independent parallel executions with separate FSMs.

---

## 8. Implementation Outline

### Phase A: TDCC Enhancement

**File:** `lib/Dialect/Cmt2/Transforms/TDCC.cpp`

1. Track state ordering during state allocation:
   - For each state, record its sequence index within the control flow
   - Track which FSM register each state belongs to (main vs branch FSMs)

2. Store state ordering metadata:
   ```cpp
   SmallVector<Attribute> stateOrderAttrs;
   for (auto &[state, info] : stateOrderMap) {
     stateOrderAttrs.push_back(DictionaryAttr::get(ctx, {
       NamedAttr("state", builder.getI64IntegerAttr(state)),
       NamedAttr("fsm", info.fsmRegister),
       NamedAttr("seq_idx", builder.getI64IntegerAttr(info.sequenceIndex))
     }));
   }
   rule->setAttr("tdcc.state_order", ArrayAttr::get(ctx, stateOrderAttrs));
   ```

### Phase B: ProcStmtToAction Enhancement

**File:** `lib/Dialect/Cmt2/Transforms/ProcStmtToAction.cpp`

1. Read state ordering from TDCC:
   ```cpp
   auto stateOrderAttr = rule->getAttrOfType<ArrayAttr>("tdcc.state_order");
   DenseMap<uint64_t, StateOrderInfo> stateOrder = parseStateOrder(stateOrderAttr);
   ```

2. Track generated rules with their sequence info:
   ```cpp
   SmallVector<GeneratedRuleInfo> allGeneratedRules;

   // In generateStateRule():
   auto &orderInfo = stateOrder[state];
   allGeneratedRules.push_back({ruleName, orderInfo.fsmRegister, orderInfo.sequenceIndex});
   ```

3. Build precedence from control flow structure:
   ```cpp
   // Group by FSM, sort by sequence index descending
   buildPrecedenceFromControlFlow(module, allGeneratedRules);
   ```

### Phase C: Validation (Optional)

**File:** `lib/Dialect/Cmt2/Transforms/ProcToGAA.cpp`

Add optional verification:
```cpp
// Verify all generated rules are in precedence
// Warn if any FSM group is missing from precedence
```

---

## 9. Summary

### Key Principles

1. **Control flow determines precedence** - NOT callee module constraints
2. **Later states have higher precedence** - Pipeline semantics (older iterations first)
3. **FSM grouping** - Rules writing same FSM register belong in one chain
4. **No cross-module analysis** - Precedence is internal to the proc structure

### Current vs Proposed

| Aspect | Current State | Proposed State |
|--------|--------------|----------------|
| Precedence source | Ad-hoc (fork/join only) | Control flow structure |
| State ordering | Partial | All states, descending seq index |
| FSM grouping | Incomplete | All FSMs (main + branches) |
| Coverage | Fork/join + some branches | All generated rules |
| User rules | Separate | Preserved, generated after |

### What's NOT Needed

- Cross-module conflict analysis
- Step body conflict tracking
- CalleE sequenceBefore propagation

The precedence within proc-generated rules is entirely determined by the control flow structure, following pipeline semantics where later states (older iterations) have higher priority.
