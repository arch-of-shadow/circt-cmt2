# Cmt2.Proc Design Document

## Overview

The `cmt2.proc` (procedural) layer extends the Cmt2 dialect with procedural control flow constructs that enable expressing multi-cycle computations. This design follows **Calyx's native TDCC (Top-Down Compile Control)** approach for control compilation, which directly generates hardware assignments with state-dependent guards rather than creating explicit FSM abstractions.

### Key Design Decisions

1. **TDCC-Style Compilation**: Use Calyx's top-down schedule-based compilation instead of visitor-based FSM generation
2. **Go-Done Protocol**: Adopt Calyx's go-done protocol, mapping it to GAA's ready-enable semantics
3. **Schedule-Based IR**: Build a schedule of state-dependent assignments and transitions, then realize as hardware
4. **Piezo Integration**: Since Piezo is built on Calyx, this approach enables direct integration of static/dynamic unification

## Protocol Mapping: GAA ↔ Calyx

### Semantic Correspondence

| Calyx Concept | GAA Concept | Description |
|---------------|-------------|-------------|
| Group | Method body | Bundle of operations that execute together |
| go signal | enable signal | Trigger execution of the group |
| done signal | ready signal (inverse sense) | Indicates completion |
| Guard condition | Guard region | Determines if execution can proceed |
| Enable | Call | Activate a group/method |

### Go-Done Protocol

Every group uses a two-signal handshake:
- **`go`** (input): Asserted by FSM when group should execute
- **`done`** (output): Group signals completion combinationally

The protocol ensures atomic execution via guards:
```
group[go] = (fsm.out == state_N) & !group[done] ? 1'd1
```

All non-hole assignments are guarded by `group[go]` to prevent dataflow when inactive.

## TDCC: Top-Down Compile Control

### Core Concept

Unlike visitor-based FSM generation that creates explicit FSM states and transitions as IR operations, TDCC:

1. **Builds a Schedule**: A data structure containing state→assignments mappings and transitions
2. **Realizes Hardware Directly**: Generates guarded assignments without intermediate FSM IR
3. **Single Output Group**: All control becomes a single "tdcc" group with state-dependent logic

### Schedule Data Structure

```cpp
/// Represents the dynamic execution schedule of a control program.
struct Schedule {
  /// Assignments that should be enabled in a given state.
  /// State → Vector of assignments (group[go] = ...)
  DenseMap<uint64_t, SmallVector<Assignment>> enables;

  /// Transition from one state to another when the guard is true.
  /// (from_state, to_state, guard)
  SmallVector<std::tuple<uint64_t, uint64_t, Guard>> transitions;

  /// Mapping from groups to FSM state IDs (for profiling)
  DenseSet<std::pair<uint64_t, StringRef>> groupsToStates;
};

// Note: No InvokeEntry in Schedule - invoke operations are compiled
// by CompileInvoke pass BEFORE TDCC runs, converting them to groups+enables.
```

### State Numbering Algorithm

Each `Enable` gets a unique FSM state ID via `compute_unique_ids()`:

```cpp
/// Assigns NODE_ID attribute to each Enable and control node.
/// Returns the next available state ID.
uint64_t computeUniqueIds(Control &con, uint64_t curState) {
  return TypeSwitch<Control &, uint64_t>(con)
    .Case<EnableOp>([&](EnableOp enable) {
      enable.setNodeId(curState);
      return curState + 1;
    })
    // Note: InvokeOp is NOT handled here.
    // It should be compiled to Enable + Group by CompileInvoke pass BEFORE TDCC runs.
    .Case<SeqOp>([&](SeqOp seq) {
      // Sequential: states numbered consecutively
      // seq { A; B; C; } → A=0, B=1, C=2
      uint64_t cur = curState;
      for (auto &stmt : seq.getBody()) {
        cur = computeUniqueIds(stmt, cur);
      }
      return cur;
    })
    .Case<ParOp>([&](ParOp par) {
      // Parallel: each branch gets independent state space starting at 0
      // par { C; D; } → both C and D get state 0 (in separate FSMs)
      par.setNodeId(curState);
      for (auto &stmt : par.getBody()) {
        computeUniqueIds(stmt, 0);  // Reset to 0 for each branch
      }
      return curState + 1;
    })
    .Case<IfOp>([&](IfOp ifOp) {
      // Branches can't get initial state (start at 1 if curState == 0)
      uint64_t cur = (curState == 0) ? 1 : curState;
      uint64_t truNext = computeUniqueIds(ifOp.getThenBranch(), cur);
      uint64_t falseNext = computeUniqueIds(ifOp.getElseBranch(), truNext);
      return falseNext;
    })
    .Case<WhileOp>([&](WhileOp whileOp) {
      uint64_t cur = (curState == 0) ? 1 : curState;
      uint64_t bodyNext = computeUniqueIds(whileOp.getBody(), cur);
      return bodyNext;
    });
}
```

**Example:**
```mlir
seq { A; B; par { C; D; }; E; }
```
Gets the state assignments:
```
@NODE_ID(0) A
@NODE_ID(1) B
@NODE_ID(2) par {
  @NODE_ID(0) C   // Independent state space
  @NODE_ID(0) D   // Independent state space
}
@NODE_ID(3) E
```

### Predecessor Edge Computation

`control_exits()` computes which states can transition to the next statement:

```cpp
/// Returns (state_id, guard) pairs for states that exit this control.
void controlExits(Control &con, SmallVectorImpl<PredEdge> &exits) {
  TypeSwitch<Control &>(con)
    .Case<EnableOp>([&](EnableOp enable) {
      uint64_t state = enable.getNodeId();
      // Exit when group's done signal is true
      exits.push_back({state, guard!(group["done"])});
    })
    // Note: InvokeOp is NOT handled here.
    // CompileInvoke converts invoke to enable+group BEFORE TDCC runs.
    .Case<SeqOp>([&](SeqOp seq) {
      // Only the last statement's exits matter
      if (!seq.getBody().empty()) {
        controlExits(seq.getBody().back(), exits);
      }
    })
    .Case<IfOp>([&](IfOp ifOp) {
      // Both branches contribute exits
      controlExits(ifOp.getThenBranch(), exits);
      controlExits(ifOp.getElseBranch(), exits);
    })
    .Case<WhileOp>([&](WhileOp whileOp) {
      // Loop exits only when condition is false
      SmallVector<PredEdge> loopExits;
      controlExits(whileOp.getBody(), loopExits);
      for (auto [state, guard] : loopExits) {
        // AND with negated loop condition
        exits.push_back({state, guard & !whileOp.getCond()});
      }
    });
}
```

### Schedule Building Algorithm

The core algorithm recursively builds a schedule:

```cpp
/// Recursively build the schedule for a control program.
/// Returns predecessor edges for the next statement.
SmallVector<PredEdge> calculateStatesRecur(
    Schedule &schedule,
    Control &con,
    SmallVector<PredEdge> preds,  // States wanting to transition here
    bool earlyTransitions         // Enable early go optimization
) {
  return TypeSwitch<Control &, SmallVector<PredEdge>>(con)
    .Case<EnableOp>([&](EnableOp enable) {
      uint64_t curState = enable.getNodeId();

      // Optimization: merge with predecessor if single unconditional edge
      if (preds.size() == 1 && preds[0].guard.isTrue()) {
        curState = preds[0].state;
        preds.clear();
      }

      // Generate enable assignment: group[go] = (fsm == state) & !done
      Value notDone = builder.create<NotOp>(group.getDone());
      Value signalOn = builder.getConstant(1, 1);
      Assignment enableGo = {
        group.getGo(),                           // dst
        signalOn,                                // src
        guard!(fsm["out"] == curState) & notDone // guard
      };
      schedule.enables[curState].push_back(enableGo);

      // Early transitions: activate next group before previous finishes
      if (earlyTransitions) {
        for (auto [predState, predGuard] : preds) {
          Assignment earlyGo = {
            group.getGo(), signalOn, predGuard
          };
          schedule.enables[predState].push_back(earlyGo);
        }
      }

      // Add transitions from predecessors
      for (auto [predState, predGuard] : preds) {
        schedule.transitions.push_back({predState, curState, predGuard});
      }

      // Return exit edge
      return SmallVector<PredEdge>{{curState, guard!(group["done"])}};
    })

    // Note: InvokeOp is NOT handled here.
    // CompileInvoke pass converts invoke to enable+group BEFORE TDCC runs.
    // By the time TDCC sees the control, all invokes are already enables.

    .Case<SeqOp>([&](SeqOp seq) {
      SmallVector<PredEdge> prev = preds;
      for (auto &stmt : seq.getBody()) {
        prev = calculateStatesRecur(schedule, stmt, prev, earlyTransitions);
      }
      return prev;
    })

    .Case<IfOp>([&](IfOp ifOp) {
      Value cond = ifOp.getCond();

      // True branch: predecessors with condition = true
      auto truPreds = transform(preds, [&](auto p) {
        return PredEdge{p.state, p.guard & cond};
      });
      auto truExits = calculateStatesRecur(
        schedule, ifOp.getThenBranch(), truPreds, earlyTransitions);

      // False branch: predecessors with condition = false
      auto falPreds = transform(preds, [&](auto p) {
        return PredEdge{p.state, p.guard & !cond};
      });
      auto falExits = calculateStatesRecur(
        schedule, ifOp.getElseBranch(), falPreds, earlyTransitions);

      // Combine exits from both branches
      SmallVector<PredEdge> allExits;
      allExits.append(truExits);
      allExits.append(falExits);
      return allExits;
    })

    .Case<WhileOp>([&](WhileOp whileOp) {
      Value cond = whileOp.getCond();

      // Step 1: Compute backward edges from body exits
      SmallVector<PredEdge> bodyExits;
      controlExits(whileOp.getBody(), bodyExits);

      // Step 2: Forward edges - predecessors + backward edges, guarded by condition
      SmallVector<PredEdge> bodyPreds;
      for (auto [state, guard] : concat(preds, bodyExits)) {
        bodyPreds.push_back({state, guard & cond});
      }
      auto loopExits = calculateStatesRecur(
        schedule, whileOp.getBody(), bodyPreds, earlyTransitions);

      // Step 3: Exit edges - when condition is false
      SmallVector<PredEdge> allExits;
      for (auto [state, guard] : concat(preds, loopExits)) {
        allExits.push_back({state, guard & !cond});
      }
      return allExits;
    });
}
```

### Schedule Realization

Once the schedule is built, `realizeSchedule()` generates hardware:

```cpp
/// Convert a Schedule into hardware assignments in a single group.
GroupOp realizeSchedule(Schedule &schedule, OpBuilder &builder) {
  // Create the output group
  GroupOp tdccGroup = builder.create<GroupOp>("tdcc");

  // Build FSM register
  uint64_t lastState = schedule.getLastState();
  uint64_t fsmWidth = llvm::Log2_64_Ceil(lastState + 1);
  Value fsm = builder.create<RegisterOp>(fsmWidth);
  Value signalOn = builder.getConstant(1, 1);
  Value firstState = builder.getConstant(0, fsmWidth);

  // 1. Generate enable assignments (state-dependent group activation)
  for (auto [state, assigns] : schedule.enables) {
    Value stateConst = builder.getConstant(state, fsmWidth);
    Guard stateGuard = guard!(fsm["out"] == stateConst["out"]);

    for (auto &assign : assigns) {
      // AND state guard with existing assignment guard
      assign.guard = assign.guard & stateGuard;
      tdccGroup.addAssignment(assign);
    }
  }

  // 2. Generate transition assignments
  for (auto [fromState, toState, guard] : schedule.transitions) {
    Value fromConst = builder.getConstant(fromState, fsmWidth);
    Value toConst = builder.getConstant(toState, fsmWidth);
    Guard transGuard = guard!(fsm["out"] == fromConst["out"]) & guard;

    // fsm.in = toState when transition guard is true
    tdccGroup.addAssignment({fsm.getIn(), toConst, transGuard});
    tdccGroup.addAssignment({fsm.getWriteEn(), signalOn, transGuard});
  }

  // 3. Generate done condition
  Value lastConst = builder.getConstant(lastState, fsmWidth);
  Guard doneGuard = guard!(fsm["out"] == lastConst["out"]);
  tdccGroup.addAssignment({tdccGroup.getDone(), signalOn, doneGuard});

  // 4. Generate reset to initial state (continuous assignment)
  builder.addContinuousAssignment({fsm.getIn(), firstState, doneGuard});
  builder.addContinuousAssignment({fsm.getWriteEn(), signalOn, doneGuard});

  return tdccGroup;
}
```

### Generated Hardware Example

**Input Control:**
```mlir
cmt2.proc.seq {
  cmt2.proc.enable @A
  cmt2.proc.enable @B
  cmt2.proc.enable @C
}
```

**After TDCC Compilation:**
```mlir
// FSM register: 2-bit (states 0, 1, 2, 3)
%fsm = cmt2.instance @fsm = @std.reg(2)
%c0 = hw.constant 0 : i2
%c1 = hw.constant 1 : i2
%c2 = hw.constant 2 : i2
%c3 = hw.constant 3 : i2
%signal_on = hw.constant 1 : i1

cmt2.proc.group @tdcc {
  // State 0: Enable group A
  // A[go] = (fsm == 0) & !A[done]
  %state0 = comb.icmp eq %fsm.out, %c0 : i2
  %not_a_done = comb.xor %A.done, %signal_on : i1
  %a_go_guard = comb.and %state0, %not_a_done : i1
  cmt2.proc.assign %A.go = %a_go_guard ? %signal_on : i1

  // State 1: Enable group B
  %state1 = comb.icmp eq %fsm.out, %c1 : i2
  %not_b_done = comb.xor %B.done, %signal_on : i1
  %b_go_guard = comb.and %state1, %not_b_done : i1
  cmt2.proc.assign %B.go = %b_go_guard ? %signal_on : i1

  // State 2: Enable group C
  %state2 = comb.icmp eq %fsm.out, %c2 : i2
  %not_c_done = comb.xor %C.done, %signal_on : i1
  %c_go_guard = comb.and %state2, %not_c_done : i1
  cmt2.proc.assign %C.go = %c_go_guard ? %signal_on : i1

  // Transition 0 → 1 when A[done]
  %trans_0_1 = comb.and %state0, %A.done : i1
  cmt2.proc.assign %fsm.in = %trans_0_1 ? %c1 : i2
  cmt2.proc.assign %fsm.write_en = %trans_0_1 ? %signal_on : i1

  // Transition 1 → 2 when B[done]
  %trans_1_2 = comb.and %state1, %B.done : i1
  cmt2.proc.assign %fsm.in = %trans_1_2 ? %c2 : i2
  cmt2.proc.assign %fsm.write_en = %trans_1_2 ? %signal_on : i1

  // Transition 2 → 3 when C[done]
  %trans_2_3 = comb.and %state2, %C.done : i1
  cmt2.proc.assign %fsm.in = %trans_2_3 ? %c3 : i2
  cmt2.proc.assign %fsm.write_en = %trans_2_3 ? %signal_on : i1

  // Done when in final state
  %state3 = comb.icmp eq %fsm.out, %c3 : i2
  cmt2.proc.group_done %state3 : i1
}

// Reset FSM to 0 when done (continuous assignment)
cmt2.proc.assign %fsm.in = %state3 ? %c0 : i2
cmt2.proc.assign %fsm.write_en = %state3 ? %signal_on : i1
```

### TDCC Optimizations

Following Calyx's TDCC implementation, we apply two key optimizations:

#### 1. State Merging

When an enable has exactly one predecessor with a true guard, the enable's state is merged into the predecessor's state. This avoids unnecessary FSM transitions.

**Example:**
```mlir
// Input
cmt2.proc.seq {
  cmt2.proc.enable @A    // Would get state 1
  cmt2.proc.enable @B    // Would get state 2
}
```

**Without merging:**
- State 0: idle
- State 1: A
- State 2: B
- State 3: done
- Transitions: 0→1, 1→2, 2→3

**With merging:**
- State 0: A (merged from initial true edge)
- State 1: B
- State 2: done
- Transitions: 0→1, 1→2

**Implementation in `calculate_states_recur`:**
```cpp
.Case<EnableOp>([&](EnableOp enable) {
  uint64_t curState = enable.getNodeId();

  // Optimization: merge with predecessor if single unconditional edge
  if (preds.size() == 1 && preds[0].guard.isTrue()) {
    curState = preds[0].state;  // Use predecessor's state
    preds.clear();              // No transitions needed
  }
  // ... rest of enable handling
})
```

#### 2. Early Transitions

Instead of waiting for `group[done]` before enabling the next group, we can start the next group **in the same cycle** that the previous group completes. This saves one cycle per transition.

**Without early transitions:**
```
Cycle t:   fsm=0, A executes, A[done]=1
Cycle t+1: fsm=1, B starts
```

**With early transitions:**
```
Cycle t:   fsm=0, A executes, A[done]=1, B starts (early!)
Cycle t+1: fsm=1, B continues
```

**Generated assignments:**
```mlir
// Without early transitions:
A[go] = (fsm == 0) & !A[done] ? 1
B[go] = (fsm == 1) & !B[done] ? 1

// With early transitions:
A[go] = (fsm == 0) & !A[done] ? 1
B[go] = ((fsm == 1) & !B[done]) | (A[done] & (fsm == 0)) ? 1
//                                 ^^^^^^^^^^^^^^^^^^^^^^^^^
//                                 Early enable from previous state
```

**Implementation:**
```cpp
if (earlyTransitions || hasFastGuarantee) {
  for (auto [st, g] : prevStates) {
    // Enable group in previous states when their done fires
    Assignment earlyGo = {
      group.getGo(),
      signalOn,
      g  // Previous state's done guard
    };
    schedule.enables[st].push_back(earlyGo);
  }
}
```

**Note:** Early transitions are safe because groups are guaranteed to run for at least one cycle, and the early enable only fires for one cycle before the normal enable takes over.

#### 3. FSM Encoding Options

Following Calyx, the FSM can use different encodings:

| Encoding | States | Register Width | Comparison |
|----------|--------|----------------|------------|
| Binary | N | log2(N) | `fsm == state_const` |
| One-Hot | N | N | Bit slice check |

**Selection heuristic:**
- One-hot for small FSMs (≤ cutoff states) - faster comparison
- Binary for large FSMs - smaller register width

**One-hot query:**
```cpp
// Binary: fsm == 2 → compare full register
Value stateGuard = builder.create<ICmpOp>(fsm, constant2);

// One-hot: fsm[2] == 1 → slice single bit
Value slicer = builder.create<BitSliceOp>(fsm, /*start=*/2, /*end=*/2);
Value stateGuard = builder.create<ICmpOp>(slicer, constant1);
```

## IR Operations

### 1. GroupOp - Execution Unit

```mlir
cmt2.proc.group @compute {
  %a = cmt2.call @reg_a @read() : () -> i32
  %b = cmt2.call @reg_b @read() : () -> i32
  %sum = comb.add %a, %b : i32
  cmt2.call @reg_out @write(%sum) : (i32) -> ()
  cmt2.proc.group_done %true : i1
}
```

```tablegen
def GroupOp : Cmt2Op<"proc.group", [Symbol, SingleBlock, NoTerminator]> {
  let summary = "Dynamic group with go-done interface";
  let description = [{
    A group bundles operations that execute atomically when activated.
    Interface:
    - `go` signal (input): When high, group activates
    - `done` signal (output): High when group has completed
  }];
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
}
```

### 2. StaticGroupOp - Fixed Latency Group

```mlir
cmt2.proc.static_group @multiply <4> {
  %a = cmt2.call @reg_a @read() : () -> i32 {guard = #cmt2.timing_guard<0, 1>}
  %b = cmt2.call @reg_b @read() : () -> i32 {guard = #cmt2.timing_guard<0, 1>}
  %prod = cmt2.call @mult @compute(%a, %b) {guard = #cmt2.timing_guard<1, 4>}
  cmt2.call @reg_out @write(%prod) {guard = #cmt2.timing_guard<3, 4>}
}
```

### 3. AssignOp - Guarded Assignment

The `cmt2.proc.assign` operation is used within groups to express guarded assignments to ports/signals. This is distinct from `cmt2.call` which invokes methods on instances.

```mlir
// Guarded assignment: dst = guard ? src
cmt2.proc.assign %fsm.in = %guard ? %new_state : i2
cmt2.proc.assign %group.go = %enable_guard ? %signal_on : i1
```

```tablegen
def AssignOp : Cmt2Op<"proc.assign", []> {
  let summary = "Guarded assignment within procedural groups";
  let description = [{
    Assigns a value to a destination when the guard is true.
    Used in TDCC-generated groups to express state-dependent
    assignments to FSM registers and group go signals.

    This operation is specific to the procedural layer and should
    NOT be confused with regular cmt2 method calls. Use `cmt2.call`
    for invoking methods on instances; use `cmt2.proc.assign` for
    direct port/signal assignments in synthesized control logic.
  }];
  let arguments = (ins
    AnyType:$dest,
    I1:$guard,
    AnyType:$src
  );
}
```

**When to use `cmt2.proc.assign` vs `cmt2.call`:**

| Operation | Use Case |
|-----------|----------|
| `cmt2.call @inst @method(args)` | Invoke a method on an instance (GAA semantics) |
| `cmt2.proc.assign %port = %guard ? %val` | Direct port assignment in generated control logic |

The `cmt2.proc.assign` is primarily used in the **output of TDCC compilation** - users typically don't write it directly. It appears in:
- FSM register updates: `cmt2.proc.assign %fsm.in = %trans_guard ? %next_state`
- Group go signals: `cmt2.proc.assign %group.go = %state_guard ? %signal_on`
- Write enables: `cmt2.proc.assign %fsm.write_en = %trans_guard ? %signal_on`

### 4. Control Operations

```tablegen
def SeqOp : Cmt2Op<"proc.seq", [SingleBlock, NoTerminator, ControlLike]> {
  let summary = "Sequential composition";
  let description = [{
    Executes children sequentially. Each child's done triggers the next.
    TDCC assigns consecutive state IDs to each Enable.
  }];
  let regions = (region SizedRegion<1>:$body);
}

def ParOp : Cmt2Op<"proc.par", [SingleBlock, NoTerminator, ControlLike]> {
  let summary = "Parallel composition";
  let description = [{
    Executes children concurrently. Each branch gets independent FSM.
    Done when all branches complete.
  }];
  let regions = (region SizedRegion<1>:$body);
}

def IfOp : Cmt2Op<"proc.if", [ControlLike]> {
  let summary = "Conditional execution";
  let description = [{
    Evaluates condition and executes one branch.
    TDCC adds condition to transition guards.
  }];
  let arguments = (ins I1:$cond);
  let regions = (region SizedRegion<1>:$thenRegion, AnyRegion:$elseRegion);
}

def WhileOp : Cmt2Op<"proc.while", [SingleBlock, NoTerminator, ControlLike]> {
  let summary = "Loop control";
  let description = [{
    Repeatedly executes body while condition is true.
    TDCC creates backward edges guarded by condition.
  }];
  let arguments = (ins I1:$cond);
  let regions = (region SizedRegion<1>:$body);
}

def EnableOp : Cmt2Op<"proc.enable", [ControlLike]> {
  let summary = "Enable a group";
  let description = [{
    Activates a group. Gets a unique NODE_ID during TDCC.
    Generates: group[go] = (fsm == NODE_ID) & !group[done]
  }];
  let arguments = (ins FlatSymbolRefAttr:$groupName);
}

def InvokeOp : Cmt2Op<"proc.invoke", [ControlLike]> {
  let summary = "Invoke a method on an instance";
  let description = [{
    Invokes a method on an instance within procedural control.
    Can invoke either:
    - Single-cycle methods (regular cmt2.method)
    - Multi-cycle procedural methods (cmt2.proc.method)

    For multi-cycle methods, this operation waits until the
    method's FSM returns to idle before proceeding.
  }];
  let arguments = (ins
    FlatSymbolRefAttr:$instance,
    FlatSymbolRefAttr:$method,
    Variadic<AnyType>:$inputs
  );
  let results = (outs Variadic<AnyType>:$outputs);
}
```

### 4. Procedural Rule Integration

```mlir
cmt2.proc.rule @compute() {
  %idle = cmt2.call @tdcc @isIdle() : () -> i1
  cmt2.return %idle : i1
} control {
  cmt2.proc.seq {
    cmt2.proc.enable @load
    cmt2.proc.enable @compute
    cmt2.proc.enable @store
  }
}
```

## Control Flow Examples

### If Statement

**Input:**
```mlir
cmt2.proc.if %cond {
  cmt2.proc.enable @A
} else {
  cmt2.proc.enable @B
}
cmt2.proc.enable @C
```

**State Assignment:**
- State 1: @A (true branch)
- State 2: @B (false branch)
- State 3: @C

**Generated Transitions:**
```
(0, 1, cond)        // 0 → 1 when cond=true
(0, 2, !cond)       // 0 → 2 when cond=false
(1, 3, A[done])     // 1 → 3 when A completes
(2, 3, B[done])     // 2 → 3 when B completes
```

### While Loop

**Input:**
```mlir
cmt2.proc.while %cond {
  cmt2.proc.enable @body
}
cmt2.proc.enable @after
```

**State Assignment:**
- State 0: Entry (check condition)
- State 1: @body
- State 2: @after

**Generated Transitions:**
```
(0, 1, cond)                    // Enter loop when cond=true
(0, 2, !cond)                   // Skip loop when cond=false
(1, 1, body[done] & cond)       // Loop back when cond still true
(1, 2, body[done] & !cond)      // Exit loop when cond becomes false
```

## Lowering Procedural Rules to GAA

This section describes how `cmt2.proc.rule` and `cmt2.proc.method` are lowered to normal GAA `cmt2.rule` and `cmt2.method` operations.

### The Challenge: Multi-Cycle vs. Atomic Semantics

**GAA Semantics:**
- Rules are **atomic** - they execute completely in one clock cycle
- Rules have **ready-enable** protocol: ready = guard is true, enable = scheduler fires it
- **ORAAT** (One-Rule-At-A-Time): Logically one rule fires per cycle
- Methods are called via `cmt2.call`, with implicit enable when called

**Calyx/Procedural Semantics:**
- Groups can span **multiple cycles**
- **Go-done** protocol: go triggers execution, done signals completion
- FSM controls which group is active

**The Core Insight:**
A procedural rule is NOT a single atomic rule - it generates:
1. **An FSM state register** to track execution progress
2. **Multiple normal rules** that handle transitions between FSM states
3. Each transition rule executes **atomically in one cycle**

### Protocol Mapping: Go-Done to Ready-Enable

| Calyx (Go-Done) | GAA (Ready-Enable) | Implementation |
|-----------------|-------------------|----------------|
| `group[go] = 1` | Rule fires when in state | Transition rule for entering this state |
| `group[done]` | Transition guard | Used to guard transition to next state |
| Waiting in state | No rule fires | FSM stays in current state |
| FSM idle | Procedural rule ready | `fsm == IDLE && original_guard` |

**Key Mapping:**
- **Group activation** (setting go=1) → Transition rule body executes group's entry actions
- **Group completion** (done=1) → Guards the transition OUT of that state
- **Continuous assignment** in group → Becomes a `cmt2.value` driven by FSM state

### Transition-Based Lowering Model

The key insight is that **actions execute on transitions, not on states**:

```
Transition (State_A → State_B, guard=G):
  Guard: (fsm == State_A) && G
  Body:
    - Execute State_B's entry actions (group activation)
    - Set fsm = State_B
```

This ensures each group's actions execute **exactly once** - when transitioning INTO that state.

### Lowering `cmt2.proc.rule`

**Input:**
```mlir
cmt2.proc.rule @compute(%arg: i32) -> () {
  // Guard region
  %ready = cmt2.call @input @valid() : () -> i1
  cmt2.return %ready : i1
} control {
  cmt2.proc.seq {
    cmt2.proc.enable @load
    cmt2.proc.enable @process
    cmt2.proc.enable @store
  }
}
```

**Lowered Output:**

```mlir
// FSM state register (states: 0=IDLE, 1=LOAD, 2=PROCESS, 3=STORE)
%compute_fsm = cmt2.instance @compute_fsm = @stl.reg(2, 0)

// Group definitions (inlined or as separate methods)
// @load group body, @process group body, @store group body defined elsewhere

//=== Transition Rules ===

// Rule 1: IDLE → LOAD (start the procedural execution)
cmt2.rule @compute__idle_to_load() -> () {
  // Guard: FSM is idle AND original guard is true
  %fsm_state = cmt2.call @compute_fsm @read() : () -> i2
  %is_idle = comb.icmp eq %fsm_state, %c0_i2 : i2
  %original_ready = cmt2.call @input @valid() : () -> i1
  %can_start = comb.and %is_idle, %original_ready : i1
  cmt2.return %can_start : i1
} {
  // Body: Execute @load group's entry actions, transition to LOAD state
  // ... @load group's actions (e.g., cmt2.call @mem @read_start()) ...
  cmt2.call @compute_fsm @write(%c1_i2) : (i2) -> ()
}

// Rule 2: LOAD → PROCESS (when load completes)
cmt2.rule @compute__load_to_process() -> () {
  %fsm_state = cmt2.call @compute_fsm @read() : () -> i2
  %in_load = comb.icmp eq %fsm_state, %c1_i2 : i2
  %load_done = cmt2.call @mem @read_done() : () -> i1  // Group's done signal
  %can_transition = comb.and %in_load, %load_done : i1
  cmt2.return %can_transition : i1
} {
  // Body: Execute @process group's entry actions, transition to PROCESS state
  // ... @process group's actions ...
  cmt2.call @compute_fsm @write(%c2_i2) : (i2) -> ()
}

// Rule 3: PROCESS → STORE (when process completes)
cmt2.rule @compute__process_to_store() -> () {
  %fsm_state = cmt2.call @compute_fsm @read() : () -> i2
  %in_process = comb.icmp eq %fsm_state, %c2_i2 : i2
  %process_done = %true  // Single-cycle group, always done
  %can_transition = comb.and %in_process, %process_done : i1
  cmt2.return %can_transition : i1
} {
  // ... @store group's actions ...
  cmt2.call @compute_fsm @write(%c3_i2) : (i2) -> ()
}

// Rule 4: STORE → IDLE (completion, back to idle)
cmt2.rule @compute__store_to_idle() -> () {
  %fsm_state = cmt2.call @compute_fsm @read() : () -> i2
  %in_store = comb.icmp eq %fsm_state, %c3_i2 : i2
  %store_done = cmt2.call @mem @write_done() : () -> i1
  %can_transition = comb.and %in_store, %store_done : i1
  cmt2.return %can_transition : i1
} {
  cmt2.call @compute_fsm @write(%c0_i2) : (i2) -> ()
}

//=== Interface for External Callers ===

// Value to check if procedural rule is idle (ready for new execution)
cmt2.value @compute__idle() -> (i1) {
  cmt2.return %true : i1
} {
  %fsm_state = cmt2.call @compute_fsm @read() : () -> i2
  %is_idle = comb.icmp eq %fsm_state, %c0_i2 : i2
  cmt2.return %is_idle : i1
}

// Value to check if procedural rule is running
cmt2.value @compute__running() -> (i1) {
  cmt2.return %true : i1
} {
  %fsm_state = cmt2.call @compute_fsm @read() : () -> i2
  %is_running = comb.icmp ne %fsm_state, %c0_i2 : i2
  cmt2.return %is_running : i1
}
```

### Lowering `cmt2.proc.method`

Procedural methods are similar but callable externally:

**Input:**
```mlir
cmt2.proc.method @multiply(%a: i32, %b: i32) -> (i32) {
  %idle = cmt2.call @mult_fsm @isIdle() : () -> i1
  cmt2.return %idle : i1
} control {
  cmt2.proc.seq {
    cmt2.proc.enable @start_mult
    cmt2.proc.enable @wait_result
  }
}
```

**Lowered Output:**

```mlir
// FSM register for this method
%multiply_fsm = cmt2.instance @multiply_fsm = @stl.reg(2, 0)

// Argument registers (to hold arguments across cycles)
%multiply_arg_a = cmt2.instance @multiply_arg_a = @stl.reg(32, 0)
%multiply_arg_b = cmt2.instance @multiply_arg_b = @stl.reg(32, 0)

// Result register
%multiply_result = cmt2.instance @multiply_result = @stl.reg(32, 0)

// The method interface - starts execution
cmt2.method @multiply(%a: i32, %b: i32) -> () {
  // Guard: Method can only be called when idle
  %fsm_state = cmt2.call @multiply_fsm @read() : () -> i2
  %is_idle = comb.icmp eq %fsm_state, %c0_i2 : i2
  cmt2.return %is_idle : i1
} {
  // Body: Latch arguments, start FSM
  cmt2.call @multiply_arg_a @write(%a) : (i32) -> ()
  cmt2.call @multiply_arg_b @write(%b) : (i32) -> ()
  cmt2.call @multiply_fsm @write(%c1_i2) : (i2) -> ()
}

// Value method to get result (only valid when done)
cmt2.value @multiply_result() -> (i32) {
  %fsm_state = cmt2.call @multiply_fsm @read() : () -> i2
  %is_idle = comb.icmp eq %fsm_state, %c0_i2 : i2
  cmt2.return %is_idle : i1  // Ready when idle (completed)
} {
  %result = cmt2.call @multiply_result @read() : () -> i32
  cmt2.return %result : i32
}

// Value method to check if method is done
cmt2.value @multiply_done() -> (i1) {
  cmt2.return %true : i1
} {
  %fsm_state = cmt2.call @multiply_fsm @read() : () -> i2
  %is_idle = comb.icmp eq %fsm_state, %c0_i2 : i2
  cmt2.return %is_idle : i1
}

// Transition rules (similar to proc.rule)
// ...
```

### Group Types and Their Lowering

#### 1. Combinational Groups (Single-Cycle)

Groups where `done` is always true:

```mlir
cmt2.proc.group @add_values {
  %a = cmt2.call @reg_a @read() : () -> i32
  %b = cmt2.call @reg_b @read() : () -> i32
  %sum = comb.add %a, %b : i32
  cmt2.call @reg_sum @write(%sum) : (i32) -> ()
  cmt2.proc.group_done %true : i1  // Always done immediately
}
```

**Lowered:** Actions inlined directly into the transition rule body. Transition happens in the same cycle.

#### 2. Multi-Cycle Groups (External Done)

Groups that wait for external completion:

```mlir
cmt2.proc.group @memory_read {
  cmt2.call @mem @read_start(%addr) : (i32) -> ()
  %done = cmt2.call @mem @read_done() : () -> i1
  cmt2.proc.group_done %done : i1
}
```

**Lowered:**
- Entry actions (`@read_start`) execute in the transition INTO this state
- FSM stays in this state until `done` is true
- `done` signal guards the transition OUT of this state

#### 3. Groups with Continuous Outputs

Groups that need to drive outputs while active:

```mlir
cmt2.proc.group @hold_output {
  // Output needs to be held while in this state
  cmt2.call @output @write(%value) : (i32) -> ()
  %done = cmt2.call @timer @expired() : () -> i1
  cmt2.proc.group_done %done : i1
}
```

**Lowered:** Create a `cmt2.value` that returns the value based on FSM state:

```mlir
cmt2.value @output_value() -> (i32) {
  cmt2.return %true : i1
} {
  %fsm_state = cmt2.call @fsm @read() : () -> i2
  %in_hold = comb.icmp eq %fsm_state, %hold_state_id : i2
  %output = comb.mux %in_hold, %value, %default : i32
  cmt2.return %output : i32
}
```

### Handling Control Flow

#### If Statements

```mlir
cmt2.proc.if %cond {
  cmt2.proc.enable @then_group
} else {
  cmt2.proc.enable @else_group
}
```

**Lowered:** Two transition rules from the same state:

```mlir
// Transition: STATE_N → THEN_STATE (when cond = true)
cmt2.rule @..._to_then() -> () {
  %in_state = comb.icmp eq %fsm, %state_n : i2
  %cond = ...
  %can_trans = comb.and %in_state, %cond : i1
  cmt2.return %can_trans : i1
} {
  // @then_group's entry actions
  cmt2.call @fsm @write(%then_state) : (i2) -> ()
}

// Transition: STATE_N → ELSE_STATE (when cond = false)
cmt2.rule @..._to_else() -> () {
  %in_state = comb.icmp eq %fsm, %state_n : i2
  %cond = ...
  %not_cond = comb.xor %cond, %true : i1
  %can_trans = comb.and %in_state, %not_cond : i1
  cmt2.return %can_trans : i1
} {
  // @else_group's entry actions
  cmt2.call @fsm @write(%else_state) : (i2) -> ()
}
```

#### While Loops

```mlir
cmt2.proc.while %cond {
  cmt2.proc.enable @body_group
}
```

**Lowered:** Transition rules that loop back:

```mlir
// Enter loop: BEFORE → BODY (when cond = true)
cmt2.rule @..._enter_loop() -> () {
  %before_state = ...
  %cond = ...
  %can_enter = comb.and %before_state, %cond : i1
  cmt2.return %can_enter : i1
} {
  // @body_group's entry actions
  cmt2.call @fsm @write(%body_state) : (i2) -> ()
}

// Loop back: BODY → BODY (when body_done AND cond still true)
cmt2.rule @..._loop_back() -> () {
  %in_body = comb.icmp eq %fsm, %body_state : i2
  %body_done = ...
  %cond = ...
  %can_loop = comb.and %in_body, %body_done, %cond : i1
  cmt2.return %can_loop : i1
} {
  // Re-execute @body_group's entry actions
  cmt2.call @fsm @write(%body_state) : (i2) -> ()
}

// Exit loop: BODY → AFTER (when body_done AND cond = false)
cmt2.rule @..._exit_loop() -> () {
  %in_body = comb.icmp eq %fsm, %body_state : i2
  %body_done = ...
  %cond = ...
  %not_cond = comb.xor %cond, %true : i1
  %can_exit = comb.and %in_body, %body_done, %not_cond : i1
  cmt2.return %can_exit : i1
} {
  cmt2.call @fsm @write(%after_state) : (i2) -> ()
}

// Skip loop: BEFORE → AFTER (when cond = false initially)
cmt2.rule @..._skip_loop() -> () {
  %before_state = ...
  %cond = ...
  %not_cond = comb.xor %cond, %true : i1
  %can_skip = comb.and %before_state, %not_cond : i1
  cmt2.return %can_skip : i1
} {
  cmt2.call @fsm @write(%after_state) : (i2) -> ()
}
```

### Parallel Composition

```mlir
cmt2.proc.par {
  cmt2.proc.enable @group_a
  cmt2.proc.seq { cmt2.proc.enable @group_b; cmt2.proc.enable @group_c }
}
```

**TDCC Handling:** Following Calyx's `finish_par`, parallel composition is handled specially:

1. **Each child is compiled independently** - simple enables stay as enables, complex control gets its own schedule/group
2. **Done registers track completion** - each child has a `pd` register to remember it finished
3. **Par group coordinates everything** - single group that enables all children and waits for all to complete

**Compiled Output:**

```mlir
// Done registers for each child
%pd_0 = cmt2.instance @pd_0 = @stl.reg(1, 0)  // For group_a
%pd_1 = cmt2.instance @pd_1 = @stl.reg(1, 0)  // For tdcc_group (b;c)

// Complex child (seq {b; c}) compiled to its own group
cmt2.proc.group @tdcc_par_child_1 {
  // Contains FSM logic for: seq { group_b; group_c }
  // ... (compiled by TDCC)
  cmt2.proc.group_done %child1_done : i1
}

// Par group coordinates all children
cmt2.proc.group @par {
  // Child 0: Simple enable
  // Enable when not done yet: !(pd.out | group.done)
  %child0_go = comb.and (comb.not %pd_0_out), (comb.not %group_a_done)
  cmt2.proc.assign @group_a.go = %true ? %child0_go : i1

  // Child 1: Complex schedule (already compiled to group)
  %child1_go = comb.and (comb.not %pd_1_out), (comb.not %tdcc_child1_done)
  cmt2.proc.assign @tdcc_par_child_1.go = %true ? %child1_go : i1

  // Save done conditions in pd registers
  cmt2.proc.assign %pd_0.in = @group_a.done ? %signal_on : i1
  cmt2.proc.assign %pd_0.write_en = @group_a.done ? %signal_on : i1

  cmt2.proc.assign %pd_1.in = @tdcc_par_child_1.done ? %signal_on : i1
  cmt2.proc.assign %pd_1.write_en = @tdcc_par_child_1.done ? %signal_on : i1

  // Par done when all pd registers are set
  %all_done = comb.and %pd_0_out, %pd_1_out
  cmt2.proc.group_done %all_done : i1
}

// Cleanup: Reset pd registers when par completes (continuous assignments)
cmt2.proc.assign %pd_0.in = %all_done ? %signal_off : i1
cmt2.proc.assign %pd_0.write_en = %all_done ? %signal_on : i1
cmt2.proc.assign %pd_1.in = %all_done ? %signal_off : i1
cmt2.proc.assign %pd_1.write_en = %all_done ? %signal_on : i1
```

**Key Points:**
- Simple enables (single group) stay as direct enables in the par group
- Complex control (seq, if, while) gets compiled by TDCC into its own sub-group first
- Done registers (`pd`) latch when each child completes
- Par is done when ALL `pd` registers are set
- Cleanup logic resets `pd` registers when par completes

**Algorithm:**

```cpp
void finishPar(ParOp par, OpBuilder &builder) {
  // Create par group
  auto parGroup = builder.create<GroupOp>("par");
  SmallVector<Value> doneRegs;

  for (auto &child : par.getBody()) {
    // Compile child
    Value group;
    if (isa<EnableOp>(child)) {
      // Simple enable - use directly
      group = cast<EnableOp>(child).getGroup();
    } else {
      // Complex control - compile to TDCC group
      Schedule sch;
      sch.calculateStates(child, earlyTransitions);
      group = sch.realizeSchedule(dumpFsm, fsmGroups, fsmImpl);
    }

    // Create done register
    auto pd = builder.create<RegOp>(1);
    doneRegs.push_back(pd);

    // Enable logic: group.go = !(pd.out | group.done)
    Value groupGo = builder.create<AndOp>(
      builder.create<NotOp>(pd.getOut()),
      builder.create<NotOp>(group.getDone()));

    // Save done in pd
    parGroup.addAssignment(group.getGo(), groupGo);
    parGroup.addAssignment(pd.getIn(), group.getDone());
    parGroup.addAssignment(pd.getWriteEn(), group.getDone());
  }

  // Par done = AND of all pd.out
  Value allDone = builder.create<AndOp>(doneRegs...);
  parGroup.setDone(allDone);

  // Cleanup: Reset pd registers (continuous assignments)
  for (auto pd : doneRegs) {
    component.addContinuousAssignment(pd.getIn(), allDone, signalOff);
    component.addContinuousAssignment(pd.getWriteEn(), allDone, signalOn);
  }

  // Replace par with enable of par group
  par.replaceWith(builder.create<EnableOp>(parGroup));
}
```

> **Note:** Par is processed during TDCC traversal (in `finish_par`), not in `calculate_states_recur`. The resulting par group is then treated as a single enable by the parent control.

### Invoke Operation

The `cmt2.proc.invoke` operation allows calling methods on instances within procedural control. Unlike `cmt2.call` which is instantaneous within a rule body, `proc.invoke` is a control operation that can span multiple cycles.

> **IMPORTANT: Pass Ordering**
>
> Following Calyx's design, `invoke` operations are compiled by a **separate `CompileInvoke` pass that runs BEFORE TDCC**. The pass converts each invoke into a group + enable:
> 1. `CompileInvoke`: invoke → group + enable
> 2. `TDCC`: processes enables (including converted invokes)
>
> This separation ensures that TDCC only sees `enable` and `par` at the control level, simplifying the schedule building algorithm.

#### Invoke Semantics

**Syntax:**
```mlir
// Invoke without results
cmt2.proc.invoke @instance @method(%arg1, %arg2) : (i32, i32) -> ()

// Invoke with results
%result = cmt2.proc.invoke @instance @method(%arg) : (i32) -> (i32)
```

**Key Properties:**
1. **Control Operation**: `proc.invoke` is a control operation (like `enable`, `seq`, `if`)
2. **Multi-Cycle**: Can take multiple cycles if the invoked method is multi-cycle
3. **Blocking**: Procedural control waits until the invoke completes
4. **Result Capture**: Results are captured when the method completes

#### Invoke vs. Call vs. Enable

| Operation | Context | Duration | Semantics |
|-----------|---------|----------|-----------|
| `cmt2.call` | Rule/Method body | Single-cycle | Direct method invocation |
| `cmt2.proc.enable` | Procedural control | Multi-cycle | Activate a local group |
| `cmt2.proc.invoke` | Procedural control | Variable | Call method on instance |

**When to use each:**
- `cmt2.call`: Inside rule/method bodies for instantaneous calls
- `cmt2.proc.enable`: Activate groups defined in the same module
- `cmt2.proc.invoke`: Call methods on submodule instances within procedural control

#### Types of Invoke Targets

**Type 1: Invoking Single-Cycle Methods**

When invoking a regular `cmt2.method`:

```mlir
// Target method (single-cycle)
cmt2.method @read() -> (i32) {
  cmt2.return %true : i1
} {
  %val = ...
  cmt2.return %val : i32
}

// Procedural invoke
cmt2.proc.seq {
  %data = cmt2.proc.invoke @reg @read() : () -> (i32)
  cmt2.proc.enable @process_data
}
```

**Lowering:** Since the method is single-cycle, invoke behaves like enable:
- Entry action: call the method, capture result
- Done: immediately true

**Type 2: Invoking Multi-Cycle Procedural Methods**

When invoking a `cmt2.proc.method`:

```mlir
// Target method (multi-cycle)
cmt2.proc.method @compute(%a: i32, %b: i32) -> (i32) {
  %idle = ...
  cmt2.return %idle : i1
} control {
  cmt2.proc.seq {
    cmt2.proc.enable @multiply
    cmt2.proc.enable @accumulate
  }
}

// Procedural invoke
cmt2.proc.seq {
  %result = cmt2.proc.invoke @alu @compute(%x, %y) : (i32, i32) -> (i32)
  cmt2.proc.enable @store_result
}
```

**Lowering:** Creates a wrapper group that:
- Entry action: call the method to start its FSM
- Done: method's `done` value (its FSM returns to idle)
- Result: captured from method's result value

#### CompileInvoke Pass (Runs BEFORE TDCC)

Following Calyx's architecture, invoke operations are compiled by a separate pass that runs **before** TDCC. This pass converts each invoke into a group + enable, so TDCC only needs to handle enables.

**CompileInvoke Algorithm:**

```cpp
class CompileInvoke {
  void visitInvoke(InvokeOp invoke, OpBuilder &builder) {
    // Step 1: Create a group for this invoke
    auto invokeGroup = builder.create<GroupOp>(
      "invoke_" + invoke.getInstance() + "_" + invoke.getMethod());

    // Step 2: Build the group body
    {
      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(invokeGroup.getBody());

      // 2a. Assert component.go = 1
      Value signalOn = builder.create<hw::ConstantOp>(1, 1);
      Value goPort = getGoPort(invoke.getInstance());
      builder.create<AssignOp>(goPort, /*guard=*/true, signalOn);

      // 2b. Connect input arguments
      for (auto [portName, value] : zip(invoke.getInputPorts(),
                                         invoke.getInputs())) {
        Value port = getPort(invoke.getInstance(), portName);
        builder.create<AssignOp>(port, /*guard=*/true, value);
      }

      // 2c. Connect output arguments
      for (auto [portName, dest] : zip(invoke.getOutputPorts(),
                                        invoke.getOutputs())) {
        Value port = getPort(invoke.getInstance(), portName);
        builder.create<AssignOp>(dest, /*guard=*/true, port);
      }

      // 2d. Group done = component.done
      Value donePort = getDonePort(invoke.getInstance());
      builder.create<GroupDoneOp>(donePort);
    }

    // Step 3: Replace invoke with enable
    auto enable = builder.create<EnableOp>(invokeGroup.getName());
    invoke.replaceAllUsesWith(enable);
    invoke.erase();
  }
};
```

**Example Transformation:**

```mlir
// BEFORE CompileInvoke:
cmt2.proc.seq {
  %result = cmt2.proc.invoke @alu @compute(%x, %y) : (i32, i32) -> (i32)
  cmt2.proc.enable @use_result
}

// AFTER CompileInvoke:
cmt2.proc.group @invoke_alu_compute_0 {
  // Assert go signal
  cmt2.proc.assign @alu.go = %true ? %signal_on : i1

  // Connect inputs
  cmt2.proc.assign @alu.x = %true ? %x : i32
  cmt2.proc.assign @alu.y = %true ? %y : i32

  // Connect outputs
  cmt2.proc.assign %result = %true ? @alu.result : i32

  // Done when component done
  cmt2.proc.group_done @alu.done : i1
}

cmt2.proc.seq {
  cmt2.proc.enable @invoke_alu_compute_0   // Invoke replaced with enable
  cmt2.proc.enable @use_result
}
```

Now TDCC only sees enables, simplifying schedule building.

#### Pass Ordering

The complete procedural lowering requires this pass order:

```
1. CompileInvoke    - invoke → group + enable
2. TDCC             - control → schedule → hardware
3. ProcToGAA        - proc constructs → GAA rules/methods
```

#### Complete Invoke Example

**Input:**

```mlir
cmt2.module @Top {
  %alu = cmt2.instance @alu = @ALU

  cmt2.proc.rule @process() {
    %idle = ...
    cmt2.return %idle : i1
  } control {
    cmt2.proc.seq {
      cmt2.proc.enable @load_operands           // State 1
      %sum = cmt2.proc.invoke @alu @add(%a, %b) // State 2 (multi-cycle)
        : (i32, i32) -> (i32)
      cmt2.proc.enable @store_result            // State 3
    }
  }
}

cmt2.module @ALU {
  cmt2.proc.method @add(%x: i32, %y: i32) -> (i32) {
    %idle = ...
    cmt2.return %idle : i1
  } control {
    cmt2.proc.seq {
      cmt2.proc.enable @setup   // Internal state 1
      cmt2.proc.enable @compute // Internal state 2
    }
  }
}
```

**After Invoke Lowering (in @Top):**

```mlir
cmt2.module @Top {
  %alu = cmt2.instance @alu = @ALU
  %process_fsm = cmt2.instance @process_fsm = @stl.reg(2, 0)
  %invoke_result = cmt2.instance @invoke_result = @stl.reg(32, 0)

  // Group for invoke (done = ALU's done)
  cmt2.proc.group @invoke_alu_add_0 {
    %done = cmt2.call @alu @add__done() : () -> i1
    cmt2.proc.group_done %done : i1
  }

  // Transition 1→2: Start invoke (call ALU's add method)
  cmt2.rule @process__s1_to_s2() {
    %fsm = cmt2.call @process_fsm @read() : () -> i2
    %in_s1 = comb.icmp eq %fsm, %c1 : i2
    %load_done = ...
    cmt2.return comb.and %in_s1, %load_done : i1
  } {
    // Start the ALU method
    cmt2.call @alu @add(%a, %b) : (i32, i32) -> ()
    cmt2.call @process_fsm @write(%c2) : (i2) -> ()
  }

  // Transition 2→3: Invoke complete, capture result
  cmt2.rule @process__s2_to_s3() {
    %fsm = cmt2.call @process_fsm @read() : () -> i2
    %in_s2 = comb.icmp eq %fsm, %c2 : i2
    %alu_done = cmt2.call @alu @add__done() : () -> i1
    cmt2.return comb.and %in_s2, %alu_done : i1
  } {
    // Capture result
    %result = cmt2.call @alu @add__result() : () -> i32
    cmt2.call @invoke_result @write(%result) : (i32) -> ()
    // Execute @store_result entry actions
    // ...
    cmt2.call @process_fsm @write(%c3) : (i2) -> ()
  }

  // ...
}
```

#### Invoke with Control Flow

Invoke can appear inside control flow constructs:

```mlir
cmt2.proc.if %cond {
  %r1 = cmt2.proc.invoke @alu @add(%a, %b) : (i32, i32) -> (i32)
} else {
  %r2 = cmt2.proc.invoke @alu @sub(%a, %b) : (i32, i32) -> (i32)
}
```

**State Assignment:**
```
State 1: invoke @add (true branch)
State 2: invoke @sub (false branch)
```

**Generated Transitions:**
```
(0, 1, cond)              // Enter true branch, start @add
(0, 2, !cond)             // Enter false branch, start @sub
(1, 3, add_done)          // @add complete
(2, 3, sub_done)          // @sub complete
```

#### Re-invoking the Same Method

If you need to invoke the same method multiple times:

```mlir
cmt2.proc.seq {
  %r1 = cmt2.proc.invoke @alu @compute(%x) : (i32) -> (i32)
  %r2 = cmt2.proc.invoke @alu @compute(%y) : (i32) -> (i32)  // Same method
}
```

Each invoke gets its own state and result register:

```mlir
// State 1: First invoke
cmt2.rule @..._s0_to_s1() { ... } {
  cmt2.call @alu @compute(%x) : (i32) -> ()
  cmt2.call @fsm @write(%c1) : (i2) -> ()
}
cmt2.rule @..._s1_to_s2() { ... } {
  %r1 = cmt2.call @alu @compute__result() : () -> i32
  cmt2.call @result_0 @write(%r1) : (i32) -> ()
  // Start second invoke
  cmt2.call @alu @compute(%y) : (i32) -> ()
  cmt2.call @fsm @write(%c2) : (i2) -> ()
}
// State 2: Second invoke
cmt2.rule @..._s2_to_s3() { ... } {
  %r2 = cmt2.call @alu @compute__result() : () -> i32
  cmt2.call @result_1 @write(%r2) : (i32) -> ()
  cmt2.call @fsm @write(%c3) : (i2) -> ()
}
```

#### Invoke Interface Requirements

For `cmt2.proc.invoke` to work, the target method must expose:

| Interface | Provided By | Purpose |
|-----------|-------------|---------|
| `@method(args)` | Method itself | Start execution |
| `@method__done()` | Generated value | Check if complete |
| `@method__result()` | Generated value | Get result (if any) |

For `cmt2.proc.method`, these are automatically generated during lowering.
For regular `cmt2.method`, `done` is implicitly true (single-cycle).

### Complete Schedule Realization

The TDCC schedule contains two key components that must both be lowered:

1. **`enables`**: State → enable assignments (what to do in each state)
2. **`transitions`**: (from_state, to_state, guard) (when to change state)

Both must be realized as GAA constructs.

#### Lowering `enables` (State Actions)

In TDCC, `enables` are essentially: `group[go] = (fsm == state) & !group[done]`

These represent **continuous activation** of groups while in a state. In GAA, we model this differently based on group type:

**Type 1: Single-Cycle Action Groups**

Groups that complete in one cycle (done = true immediately):

```mlir
// TDCC enables entry:
// State 1: group_A[go] = (fsm == 1) & !group_A[done]

// Lowered to GAA: Actions merged into transition rule
cmt2.rule @proc__trans_0_to_1() {
  %in_s0 = comb.icmp eq %fsm, %c0
  %guard = ...
  cmt2.return comb.and %in_s0, %guard
} {
  // group_A's actions execute here (on entry to state 1)
  %a = cmt2.call @reg_a @read() : () -> i32
  %b = cmt2.call @reg_b @read() : () -> i32
  %sum = comb.add %a, %b : i32
  cmt2.call @reg_sum @write(%sum) : (i32) -> ()
  // Transition to state 1
  cmt2.call @fsm @write(%c1) : (i2) -> ()
}
// Since done=true, next transition rule fires immediately next cycle
```

**Type 2: Multi-Cycle Waiting Groups**

Groups that wait for external done signal:

```mlir
// TDCC enables entry:
// State 2: mem_read[go] = (fsm == 2) & !mem_read[done]

// Lowered to GAA:

// Rule 1: Entry action (fires once on entering state 2)
cmt2.rule @proc__trans_1_to_2() {
  %in_s1 = comb.icmp eq %fsm, %c1
  %s1_done = ...  // Previous state's done
  cmt2.return comb.and %in_s1, %s1_done
} {
  // Start the memory read (entry action)
  cmt2.call @mem @read_start(%addr) : (i32) -> ()
  cmt2.call @fsm @write(%c2) : (i2) -> ()
}

// Rule 2: Exit transition (fires when done)
cmt2.rule @proc__trans_2_to_3() {
  %in_s2 = comb.icmp eq %fsm, %c2
  %mem_done = cmt2.call @mem @read_done() : () -> i1
  cmt2.return comb.and %in_s2, %mem_done
} {
  // Capture result if needed
  %data = cmt2.call @mem @read_data() : () -> i32
  cmt2.call @result_reg @write(%data) : (i32) -> ()
  cmt2.call @fsm @write(%c3) : (i2) -> ()
}

// Note: While in state 2 waiting, NO rule fires - FSM just holds state
```

**Type 3: Continuous Output Groups**

Groups that need to drive outputs while active:

```mlir
// TDCC enables entry:
// State 3: output_hold[go] = (fsm == 3) & !output_hold[done]
//          output = value (continuous while go=1)

// Lowered to GAA:

// Value method for the continuous output (computed every cycle)
cmt2.value @output() -> (i32) {
  cmt2.return %true : i1
} {
  %fsm_state = cmt2.call @fsm @read() : () -> i2
  %in_s3 = comb.icmp eq %fsm_state, %c3 : i2
  %hold_value = cmt2.call @hold_reg @read() : () -> i32
  %default = hw.constant 0 : i32
  %result = comb.mux %in_s3, %hold_value, %default : i32
  cmt2.return %result : i32
}

// Entry rule (sets up the hold value)
cmt2.rule @proc__trans_2_to_3() {
  ...
} {
  cmt2.call @hold_reg @write(%value) : (i32) -> ()
  cmt2.call @fsm @write(%c3) : (i2) -> ()
}

// Exit rule (when done)
cmt2.rule @proc__trans_3_to_4() {
  %in_s3 = comb.icmp eq %fsm, %c3
  %timer_done = cmt2.call @timer @expired() : () -> i1
  cmt2.return comb.and %in_s3, %timer_done
} {
  cmt2.call @fsm @write(%c4) : (i2) -> ()
}
```

#### Lowering `transitions` (State Changes)

Each transition in the schedule becomes a GAA rule:

```cpp
for (auto [from_state, to_state, guard] : schedule.transitions) {
  // Create transition rule
  createRule(
    name: "{proc_name}__s{from}_to_s{to}",
    guard: (fsm == from_state) && guard,
    body: {
      // Execute to_state's entry actions (from enables)
      if (schedule.enables.count(to_state)) {
        for (action : schedule.enables[to_state].entryActions)
          emit(action);
      }
      // Update FSM
      emit(fsm.write(to_state));
    }
  );
}
```

> **Note:** Since invoke operations are compiled to groups+enables by `CompileInvoke` before TDCC runs, there's no special invoke handling needed here. The generated invoke group handles go/done signaling just like any other group.

### Generated GAA Operations Summary

For a complete `cmt2.proc.rule`, the lowering generates:

| Generated Operation | Purpose | When Generated |
|---------------------|---------|----------------|
| `cmt2.instance @fsm` | FSM state register | Always |
| `cmt2.rule @...__trans_X_to_Y` | State transitions | One per transition in schedule |
| `cmt2.value @...__idle` | Check if FSM is idle | Always |
| `cmt2.value @...__running` | Check if FSM is running | Always |
| `cmt2.value @...__output_*` | Continuous outputs | For groups with continuous outputs |
| `cmt2.instance @...__hold_*` | Hold registers | For values that must persist across states |

> **Note:** Invoke operations don't add additional generated operations here because `CompileInvoke` runs first and converts each invoke to a group + enable. The invoke's group handles the go/done protocol.

For a complete `cmt2.proc.method`, additionally:

| Generated Operation | Purpose |
|---------------------|---------|
| `cmt2.instance @...__arg_*` | Argument registers |
| `cmt2.instance @...__result_*` | Result registers |
| `cmt2.method @...` | Callable interface (starts FSM) |
| `cmt2.value @...__result` | Get result (valid when done) |
| `cmt2.value @...__done` | Check if method completed |

For each `cmt2.proc.invoke` target, the target module must provide:

| Required Interface | Provider | Purpose |
|--------------------|----------|---------|
| `@method(args)` | Target module | Start method execution |
| `@method__done()` | Generated (for proc.method) | Check if method completed |
| `@method__result()` | Generated (for proc.method) | Get result value |

### Integration with Non-Procedural Rules/Methods

**Non-procedural rules/methods in the same module are unchanged.** They coexist with the generated procedural rules:

```mlir
cmt2.module @MyModule {
  // Instances
  %reg_a = cmt2.instance @reg_a = @stl.reg(32, 0)
  %reg_b = cmt2.instance @reg_b = @stl.reg(32, 0)

  // ===== Non-procedural (unchanged) =====

  // Regular rule - fires every cycle when guard is true
  cmt2.rule @increment() {
    %ready = cmt2.call @reg_a @canWrite() : () -> i1
    cmt2.return %ready : i1
  } {
    %val = cmt2.call @reg_a @read() : () -> i32
    %new = comb.add %val, %c1 : i32
    cmt2.call @reg_a @write(%new) : (i32) -> ()
  }

  // Regular method - callable by other modules
  cmt2.method @get_sum() -> (i32) {
    cmt2.return %true : i1
  } {
    %a = cmt2.call @reg_a @read() : () -> i32
    %b = cmt2.call @reg_b @read() : () -> i32
    %sum = comb.add %a, %b : i32
    cmt2.return %sum : i32
  }

  // ===== Generated from cmt2.proc.rule @compute =====

  %compute_fsm = cmt2.instance @compute_fsm = @stl.reg(2, 0)

  cmt2.rule @compute__trans_0_to_1() { ... } { ... }
  cmt2.rule @compute__trans_1_to_2() { ... } { ... }
  cmt2.rule @compute__trans_2_to_0() { ... } { ... }

  cmt2.value @compute__idle() -> (i1) { ... } { ... }
  cmt2.value @compute__running() -> (i1) { ... } { ... }
}
```

### Conflict Analysis and Scheduling

All rules (both non-procedural and generated procedural) participate in GAA scheduling:

**Conflict Types:**

1. **Intra-procedural conflicts**: Transition rules from the same proc.rule are **mutually exclusive** by construction (FSM can only be in one state)

2. **Inter-procedural conflicts**: Rules from different proc.rules may conflict if they:
   - Both write to the same register
   - Both call the same non-CF (conflict-free) method

3. **Procedural vs. Non-procedural conflicts**: A generated transition rule may conflict with a regular rule if they access the same resources

**Scheduling Example:**

```
Rules in module:
  @increment           - regular rule
  @compute__trans_0_1  - proc transition rule (writes reg_a)
  @compute__trans_1_2  - proc transition rule (reads reg_a)

Conflict matrix:
  @increment vs @compute__trans_0_1: CONFLICT (both write reg_a)
  @increment vs @compute__trans_1_2: CF (read vs write, SA order)
  @compute__trans_0_1 vs @compute__trans_1_2: MUTEX (same FSM, different states)

Scheduler output:
  - @increment and @compute__trans_0_1 cannot fire together
  - @increment and @compute__trans_1_2 can fire together (read happens before write)
  - Only one of the @compute rules can fire per cycle
```

### Complete Lowering Algorithm

Following Calyx's pass ordering, the complete procedural lowering runs these passes in sequence:

```
CompleteProcLowering(module):
  // ========== Phase 1: CompileInvoke ==========
  // Convert all invoke operations to groups + enables
  // This runs BEFORE TDCC so TDCC only sees enables
  for invoke in module.collectInvokeOps():
    invokeGroup = createGroup("invoke_" + invoke.name)
    invokeGroup.addAssign(invoke.instance.go, signalOn)
    for (port, value) in invoke.inputs:
      invokeGroup.addAssign(invoke.instance.port, value)
    for (port, dest) in invoke.outputs:
      invokeGroup.addAssign(dest, invoke.instance.port)
    invokeGroup.setDone(invoke.instance.done)
    invoke.replaceWith(Enable(invokeGroup))

  // ========== Phase 2: TDCC ==========
  // For each procedural op, compile control to schedule then to hardware
  procOps = collectProcOps(module)
  for proc_op in procOps:
    // 2a. Assign state IDs
    computeUniqueIds(proc_op.control)

    // 2b. Build schedule
    schedule = buildSchedule(proc_op.control)

    // 2c. Realize schedule as hardware
    tdccGroup = realizeSchedule(schedule)

    // 2d. Generate FSM register
    fsm_width = log2(schedule.lastState + 1)
    fsm_reg = createInstance("{proc_op.name}_fsm", stl.reg(fsm_width, 0))

    // 2e. Generate transition rules
    for (from, to, guard) in schedule.transitions:
      entry_actions = schedule.enables[to].entryActions if exists
      createTransitionRule(proc_op.name, from, to, guard, entry_actions, fsm_reg)

    // 2f. Generate continuous output values
    for (state, enables) in schedule.enables:
      for enable in enables:
        if enable.hasContinuousOutput():
          createOutputValue(proc_op.name, state, enable, fsm_reg)

    // 2g. Generate interface values
    createIdleValue(proc_op.name, fsm_reg)
    createRunningValue(proc_op.name, fsm_reg)

    // 2h. For proc.method: generate interface
    if proc_op is ProcMethodOp:
      for arg in proc_op.arguments:
        createArgRegister(proc_op.name, arg)
      for result in proc_op.results:
        createResultRegister(proc_op.name, result)
      createMethodInterface(proc_op)
      createResultValue(proc_op)
      createDoneValue(proc_op)

    // 2i. Remove original proc_op
    proc_op.erase()

  // ========== Phase 3: GAA Scheduling ==========
  // Run normal GAA conflict analysis and scheduling
  // All generated rules participate in conflict analysis
```

**Key insight:** By running `CompileInvoke` first, TDCC only needs to handle `enable` and `par` operations. This matches Calyx's architecture and simplifies the TDCC implementation significantly.

### Example: Complete Lowering

**Input:**

```mlir
cmt2.module @Example {
  %mem = cmt2.instance @mem = @stl.memory(32, 256)
  %acc = cmt2.instance @acc = @stl.reg(32, 0)

  // Non-procedural rule
  cmt2.rule @clear_acc() {
    %can_clear = ...
    cmt2.return %can_clear : i1
  } {
    cmt2.call @acc @write(%c0) : (i32) -> ()
  }

  // Procedural rule
  cmt2.proc.rule @sum_array(%base: i32, %len: i32) {
    %idle = ...
    cmt2.return %idle : i1
  } control {
    cmt2.proc.seq {
      cmt2.proc.enable @init_acc      // State 1
      cmt2.proc.while %i_lt_len {
        cmt2.proc.enable @load_elem   // State 2 (multi-cycle)
        cmt2.proc.enable @add_to_acc  // State 3
      }
    }
  }

  cmt2.proc.group @init_acc { ... done = true }
  cmt2.proc.group @load_elem { ... done = mem.read_done }
  cmt2.proc.group @add_to_acc { ... done = true }
}
```

**Output after lowering:**

```mlir
cmt2.module @Example {
  %mem = cmt2.instance @mem = @stl.memory(32, 256)
  %acc = cmt2.instance @acc = @stl.reg(32, 0)

  // ===== Non-procedural (unchanged) =====
  cmt2.rule @clear_acc() {
    %can_clear = ...
    cmt2.return %can_clear : i1
  } {
    cmt2.call @acc @write(%c0) : (i32) -> ()
  }

  // ===== Generated from @sum_array =====

  // FSM and loop state
  %sum_array_fsm = cmt2.instance @sum_array_fsm = @stl.reg(3, 0)  // States 0-4
  %sum_array_i = cmt2.instance @sum_array_i = @stl.reg(32, 0)     // Loop counter
  %sum_array_base = cmt2.instance @sum_array_base = @stl.reg(32, 0)
  %sum_array_len = cmt2.instance @sum_array_len = @stl.reg(32, 0)

  // Transition 0→1: Start (init_acc)
  cmt2.rule @sum_array__s0_to_s1() {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %in_s0 = comb.icmp eq %fsm, %c0 : i3
    %orig_guard = ...  // Original guard
    cmt2.return comb.and %in_s0, %orig_guard : i1
  } {
    // @init_acc entry actions
    cmt2.call @acc @write(%c0) : (i32) -> ()
    cmt2.call @sum_array_i @write(%c0) : (i32) -> ()
    // Latch arguments
    cmt2.call @sum_array_base @write(%base) : (i32) -> ()
    cmt2.call @sum_array_len @write(%len) : (i32) -> ()
    cmt2.call @sum_array_fsm @write(%c1) : (i3) -> ()
  }

  // Transition 1→2: Enter loop (load_elem) when i < len
  cmt2.rule @sum_array__s1_to_s2() {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %in_s1 = comb.icmp eq %fsm, %c1 : i3
    %i = cmt2.call @sum_array_i @read() : () -> i32
    %len = cmt2.call @sum_array_len @read() : () -> i32
    %i_lt_len = comb.icmp ult %i, %len : i32
    cmt2.return comb.and %in_s1, %i_lt_len : i1
  } {
    // @load_elem entry actions
    %base = cmt2.call @sum_array_base @read() : () -> i32
    %i = cmt2.call @sum_array_i @read() : () -> i32
    %addr = comb.add %base, %i : i32
    cmt2.call @mem @read_start(%addr) : (i32) -> ()
    cmt2.call @sum_array_fsm @write(%c2) : (i3) -> ()
  }

  // Transition 2→3: load_elem done → add_to_acc
  cmt2.rule @sum_array__s2_to_s3() {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %in_s2 = comb.icmp eq %fsm, %c2 : i3
    %load_done = cmt2.call @mem @read_done() : () -> i1
    cmt2.return comb.and %in_s2, %load_done : i1
  } {
    // @add_to_acc entry actions
    %elem = cmt2.call @mem @read_data() : () -> i32
    %acc_val = cmt2.call @acc @read() : () -> i32
    %new_acc = comb.add %acc_val, %elem : i32
    cmt2.call @acc @write(%new_acc) : (i32) -> ()
    // Increment loop counter
    %i = cmt2.call @sum_array_i @read() : () -> i32
    %next_i = comb.add %i, %c1 : i32
    cmt2.call @sum_array_i @write(%next_i) : (i32) -> ()
    cmt2.call @sum_array_fsm @write(%c3) : (i3) -> ()
  }

  // Transition 3→2: Loop back when i < len
  cmt2.rule @sum_array__s3_to_s2() {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %in_s3 = comb.icmp eq %fsm, %c3 : i3
    %i = cmt2.call @sum_array_i @read() : () -> i32
    %len = cmt2.call @sum_array_len @read() : () -> i32
    %i_lt_len = comb.icmp ult %i, %len : i32
    cmt2.return comb.and %in_s3, %i_lt_len : i1
  } {
    // @load_elem entry actions (again)
    %base = cmt2.call @sum_array_base @read() : () -> i32
    %i = cmt2.call @sum_array_i @read() : () -> i32
    %addr = comb.add %base, %i : i32
    cmt2.call @mem @read_start(%addr) : (i32) -> ()
    cmt2.call @sum_array_fsm @write(%c2) : (i3) -> ()
  }

  // Transition 3→0: Exit loop when i >= len
  cmt2.rule @sum_array__s3_to_s0() {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %in_s3 = comb.icmp eq %fsm, %c3 : i3
    %i = cmt2.call @sum_array_i @read() : () -> i32
    %len = cmt2.call @sum_array_len @read() : () -> i32
    %i_ge_len = comb.icmp uge %i, %len : i32
    cmt2.return comb.and %in_s3, %i_ge_len : i1
  } {
    cmt2.call @sum_array_fsm @write(%c0) : (i3) -> ()
  }

  // Transition 1→0: Skip loop when len == 0
  cmt2.rule @sum_array__s1_to_s0() {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %in_s1 = comb.icmp eq %fsm, %c1 : i3
    %i = cmt2.call @sum_array_i @read() : () -> i32
    %len = cmt2.call @sum_array_len @read() : () -> i32
    %i_ge_len = comb.icmp uge %i, %len : i32
    cmt2.return comb.and %in_s1, %i_ge_len : i1
  } {
    cmt2.call @sum_array_fsm @write(%c0) : (i3) -> ()
  }

  // Interface values
  cmt2.value @sum_array__idle() -> (i1) {
    cmt2.return %true : i1
  } {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %idle = comb.icmp eq %fsm, %c0 : i3
    cmt2.return %idle : i1
  }

  cmt2.value @sum_array__running() -> (i1) {
    cmt2.return %true : i1
  } {
    %fsm = cmt2.call @sum_array_fsm @read() : () -> i3
    %running = comb.icmp ne %fsm, %c0 : i3
    cmt2.return %running : i1
  }
}
```

**Scheduling Analysis:**
- `@clear_acc` conflicts with `@sum_array__s2_to_s3` (both write to @acc)
- `@clear_acc` is CF with all other @sum_array rules
- All @sum_array transition rules are mutually exclusive (FSM state)

### Example: Invoke with Multi-Cycle Method

**Input (with invoke):**

```mlir
cmt2.module @ALU {
  cmt2.proc.method @multiply(%a: i32, %b: i32) -> (i32) {
    %idle = cmt2.call @mult_fsm @isIdle() : () -> i1
    cmt2.return %idle : i1
  } control {
    cmt2.proc.seq {
      cmt2.proc.enable @setup_mult
      cmt2.proc.enable @wait_result
    }
  }
  // ... groups and FSM ...
}

cmt2.module @Processor {
  %alu = cmt2.instance @alu = @ALU

  cmt2.proc.rule @compute_square(%x: i32) {
    %idle = ...
    cmt2.return %idle : i1
  } control {
    cmt2.proc.seq {
      %result = cmt2.proc.invoke @alu @multiply(%x, %x) : (i32, i32) -> (i32)
      cmt2.proc.enable @store_result
    }
  }
}
```

**Output after lowering @Processor:**

```mlir
cmt2.module @Processor {
  %alu = cmt2.instance @alu = @ALU

  // FSM for @compute_square (states: 0=IDLE, 1=INVOKE, 2=STORE)
  %compute_square_fsm = cmt2.instance @compute_square_fsm = @stl.reg(2, 0)

  // Result register for invoke
  %compute_square__invoke_result_1_0 = cmt2.instance
    @compute_square__invoke_result_1_0 = @stl.reg(32, 0)

  // Transition 0→1: Start invoke (call ALU's multiply)
  cmt2.rule @compute_square__s0_to_s1() {
    %fsm = cmt2.call @compute_square_fsm @read() : () -> i2
    %in_s0 = comb.icmp eq %fsm, %c0 : i2
    %orig_guard = ...
    cmt2.return comb.and %in_s0, %orig_guard : i1
  } {
    // Entry action: start the multiply method
    cmt2.call @alu @multiply(%x, %x) : (i32, i32) -> ()
    cmt2.call @compute_square_fsm @write(%c1) : (i2) -> ()
  }

  // Transition 1→2: Invoke done, capture result, enable store
  cmt2.rule @compute_square__s1_to_s2() {
    %fsm = cmt2.call @compute_square_fsm @read() : () -> i2
    %in_s1 = comb.icmp eq %fsm, %c1 : i2
    %mult_done = cmt2.call @alu @multiply__done() : () -> i1
    cmt2.return comb.and %in_s1, %mult_done : i1
  } {
    // Exit action: capture invoke result
    %result = cmt2.call @alu @multiply__result() : () -> i32
    cmt2.call @compute_square__invoke_result_1_0 @write(%result) : (i32) -> ()
    // Entry action: @store_result actions
    // ...
    cmt2.call @compute_square_fsm @write(%c2) : (i2) -> ()
  }

  // Transition 2→0: Store done, back to idle
  cmt2.rule @compute_square__s2_to_s0() {
    %fsm = cmt2.call @compute_square_fsm @read() : () -> i2
    %in_s2 = comb.icmp eq %fsm, %c2 : i2
    %store_done = ...
    cmt2.return comb.and %in_s2, %store_done : i1
  } {
    cmt2.call @compute_square_fsm @write(%c0) : (i2) -> ()
  }

  // Interface values
  cmt2.value @compute_square__idle() -> (i1) { ... }
  cmt2.value @compute_square__running() -> (i1) { ... }
}
```

**Key points:**
- Invoke creates a state (state 1) that waits for `@alu @multiply__done()`
- Entry action (state 0→1): calls `@alu @multiply(%x, %x)` to start the method
- Exit action (state 1→2): captures `@alu @multiply__result()` into a result register
- The result can be read from `@compute_square__invoke_result_1_0` in subsequent states

## Piezo Integration

### Static Control Compilation

Static groups with known latency use timing guards instead of done signals:

```cpp
void compileStaticGroup(StaticGroupOp group, OpBuilder &builder) {
  uint64_t latency = group.getLatency();
  uint64_t counterWidth = llvm::Log2_64_Ceil(latency);

  // Create FSM counter
  Value counter = builder.create<CounterOp>(counterWidth, latency);

  // Replace timing guards with counter comparisons
  for (auto &op : group.getBody()) {
    if (auto guard = op.getAttr<TimingGuardAttr>("guard")) {
      // %[i:j] → (counter >= i) & (counter < j)
      Value start = builder.getConstant(guard.getStart(), counterWidth);
      Value end = builder.getConstant(guard.getEnd(), counterWidth);
      Value inRange = builder.create<AndOp>(
        builder.create<UGeOp>(counter, start),
        builder.create<ULtOp>(counter, end));
      op.setGuard(inRange);
    }
  }

  // Done when counter wraps
  Value done = builder.create<EqOp>(counter, builder.getConstant(0, counterWidth));
  group.setDone(done);
}
```

### Control Collapsing

Merges static control into single static groups:

```cpp
void collapseStaticSeq(StaticSeqOp seq) {
  uint64_t offset = 0;
  SmallVector<Assignment> collapsed;

  for (auto &child : seq.getBody()) {
    if (auto group = dyn_cast<StaticGroupOp>(child)) {
      for (auto &assign : group.getAssignments()) {
        // Shift timing guard by offset
        if (auto guard = assign.getAttr<TimingGuardAttr>("guard")) {
          assign.setAttr("guard", TimingGuardAttr::get(
            guard.getStart() + offset,
            guard.getEnd() + offset));
        }
        collapsed.push_back(assign);
      }
      offset += group.getLatency();
    }
  }

  // Create merged group with total latency
  auto merged = builder.create<StaticGroupOp>("collapsed", offset);
  merged.getBody().splice(merged.getBody().end(), collapsed);
}
```

### Schedule Compaction

ASAP scheduling to maximize parallelism:

```cpp
void compactSchedule(StaticSeqOp seq) {
  // Build dependency graph
  auto deps = buildDependencyGraph(seq);

  // Compute ASAP start times
  DenseMap<Operation*, uint64_t> startTimes;
  for (auto &op : topologicalSort(deps)) {
    uint64_t earliest = 0;
    for (auto pred : deps.getPredecessors(op)) {
      earliest = std::max(earliest, startTimes[pred] + getLatency(pred));
    }
    startTimes[op] = earliest;
  }

  // Rebuild as parallel with appropriate delays
  rebuildAsStaticPar(seq, startTimes);
}
```

## Pass Pipeline

```
Input: cmt2.proc IR
  |
  v
[Timing Analysis] - Infer latencies for static operations
  |
  v
[Static Promotion] - Promote dynamic → static where profitable
  |
  v
[Compile Repeat] - Convert repeat to while
  |
  v
[Schedule Compaction] - ASAP scheduling (static code)
  |
  v
[Control Collapsing] - Merge static groups
  |
  v
[Cell Sharing] - Resource sharing based on live ranges
  |
  v
[Compile Static] - Static groups → dynamic wrappers
  |
  v
[Top-Down Compile Control] - Build schedule, realize hardware
  |
  v
[Go Insertion] - Add go guards to all assignments
  |
  v
Output: cmt2 IR with TDCC groups
  |
  v
[cmt2-to-firrtl] - Lower to FIRRTL
```

## Implementation Structure

### Core Classes

```cpp
namespace circt::cmt2::proc {

/// Predecessor edge: (state_id, guard)
using PredEdge = std::pair<uint64_t, Guard>;

/// Execution schedule for control compilation
class Schedule {
public:
  /// State → enable assignments
  DenseMap<uint64_t, SmallVector<Assignment>> enables;

  /// Transitions: (from, to, guard)
  SmallVector<std::tuple<uint64_t, uint64_t, Guard>> transitions;

  /// Build schedule from control
  void calculateStates(Control &con, bool earlyTransitions);

  /// Generate hardware from schedule
  GroupOp realizeSchedule(OpBuilder &builder);

private:
  SmallVector<PredEdge> calculateStatesRecur(
    Control &con, SmallVector<PredEdge> preds, bool earlyTransitions);
};

/// TDCC pass implementation
class TopDownCompileControl : public PassBase {
public:
  void runOnOperation() override;

private:
  void compileControl(ProcRuleOp rule);
  uint64_t computeUniqueIds(Control &con, uint64_t curState);
  void controlExits(Control &con, SmallVectorImpl<PredEdge> &exits);
};

} // namespace circt::cmt2::proc
```

### File Organization

```
include/circt/Dialect/Cmt2/
├── Cmt2Proc.h              # Main header
├── Cmt2ProcOps.td          # Operation definitions
├── Cmt2ProcPasses.td       # Pass declarations
└── ECMT2/Proc/
    ├── ProcBuilder.h       # Low-level API
    └── Schedule.h          # Schedule data structure

lib/Dialect/Cmt2/
├── Cmt2ProcOps.cpp
├── Transforms/
│   ├── TopDownCompileControl.cpp   # TDCC pass
│   ├── CompileStatic.cpp           # Static control compilation
│   ├── GoInsertion.cpp             # Go guard insertion
│   ├── ControlCollapsing.cpp       # Static control merging
│   └── ScheduleCompaction.cpp      # ASAP scheduling
└── ECMT2/Proc/
    ├── ProcBuilder.cpp
    └── Schedule.cpp
```

## Complete Example

### Input

```cpp
class GCD : public Cmt2Module {
  Reg<32> x, y;

  CMT2_GROUP(subtract) {
    x.write(x.read() - y.read());
  }

  CMT2_GROUP(swap) {
    auto tmp = x.read();
    x.write(y.read());
    y.write(tmp);
  }

  CMT2_PROC_RULE(compute) {
    While(x.read() != y.read(), [&] {
      If(x.read() > y.read(), [&] {
        Enable("subtract");
      }).Else([&] {
        Seq([&] {
          Enable("swap");
          Enable("subtract");
        });
      });
    });
  }
};
```

### After State Numbering

```mlir
cmt2.proc.rule @compute() control {
  cmt2.proc.while %neq {              // States branch at 0
    cmt2.proc.if %gt {
      @NODE_ID(1) cmt2.proc.enable @subtract
    } else {
      cmt2.proc.seq {
        @NODE_ID(2) cmt2.proc.enable @swap
        @NODE_ID(3) cmt2.proc.enable @subtract
      }
    }
  }
}
// Final state: 4
```

### Generated Schedule

```
enables:
  State 1: subtract[go] = !subtract[done]
  State 2: swap[go] = !swap[done]
  State 3: subtract[go] = !subtract[done]

transitions:
  (0, 1, neq & gt)                    // Enter if-true branch
  (0, 2, neq & !gt)                   // Enter if-false branch
  (0, 4, !neq)                        // Exit while (initially false)
  (1, 1, subtract[done] & neq & gt)   // Loop back via if-true
  (1, 2, subtract[done] & neq & !gt)  // Loop to if-false
  (1, 4, subtract[done] & !neq)       // Exit while
  (2, 3, swap[done])                  // Seq: swap → subtract
  (3, 1, subtract[done] & neq & gt)   // Loop back via if-true
  (3, 2, subtract[done] & neq & !gt)  // Loop back via if-false
  (3, 4, subtract[done] & !neq)       // Exit while
```

### After TDCC Realization

```mlir
cmt2.proc.group @tdcc {
  // FSM: 3-bit register for states 0-4
  %fsm = ...

  // State 1: Enable subtract
  %s1 = comb.icmp eq %fsm.out, 1
  %subtract_go_1 = comb.and %s1, %not_subtract_done
  cmt2.proc.assign subtract[go] = %subtract_go_1 ? 1

  // State 2: Enable swap
  %s2 = comb.icmp eq %fsm.out, 2
  %swap_go = comb.and %s2, %not_swap_done
  cmt2.proc.assign swap[go] = %swap_go ? 1

  // State 3: Enable subtract
  %s3 = comb.icmp eq %fsm.out, 3
  %subtract_go_3 = comb.and %s3, %not_subtract_done
  cmt2.proc.assign subtract[go] = %subtract_go_3 ? 1

  // Transitions (simplified)
  // 0 → 1: neq & gt
  // 0 → 2: neq & !gt
  // 0 → 4: !neq
  // 1 → 1/2/4: based on subtract[done] & condition
  // etc.

  // Done when state == 4
  %s4 = comb.icmp eq %fsm.out, 4
  cmt2.proc.group_done %s4
}
```

## References

1. **Calyx TDCC**: `calyx/calyx/opt/src/passes/top_down_compile_control.rs`
2. **Calyx**: Nigam et al., "A Compiler Infrastructure for Accelerator Generators", ASPLOS 2021
3. **Piezo**: Kim et al., "Unifying Static and Dynamic Intermediate Languages", OOPSLA 2024
4. **Bluespec**: Nikhil, "Bluespec System Verilog: Efficient, Correct RTL from High Level Specifications"
5. **GAA**: Hoe, "Operation-Centric Hardware Description and Synthesis", MIT PhD Thesis, 2000
