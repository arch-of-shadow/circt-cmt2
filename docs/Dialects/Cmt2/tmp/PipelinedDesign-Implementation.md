# Pipelined Design Generation Implementation

**Status:** Design Document (Revised v3)
**Date:** 2026-01-14
**Reference:** Clamp MICRO 2025 Paper, cuTile, Triton-distributed

---

## 1. Executive Summary

This document outlines the implementation plan for adding temporal hardware transaction features to CMT2. The goal is to support intuitive pipelined design generation while maintaining CMT2's GAA (Guarded Atomic Actions) semantics.

**Key Design Principles:**

1. **Tokens as SSA values** - SyncTokens are first-class SSA values, enabling MLIR's def-use analysis
2. **Multi-consumer tokens** - A token can have multiple consumers; this implies synchronization between them
3. **No special declarations** - Rules produce and consume tokens via their signatures
4. **Timing as attributes** - All timing uses existing `#cmt2.timing<start, end>` format
5. **Unified pipelining** - Single `proc.dataflow` construct covers all pipeline patterns

### Key Features to Implement

| Feature | Description | Priority |
|---------|-------------|----------|
| SyncToken Type | First-class SSA value for synchronization + data | High |
| Token-aware Rules | Rules take/return tokens in their signatures | High |
| Token Analysis | FIFO depth inference, deadlock detection | High |
| proc.dataflow | Unified pipeline/dataflow construct | High |
| Stall Controller | Hardware for LS/LI boundary management | High |

---

## 2. Background

### 2.1 Critique of Clamp's Approach

The Clamp paper introduces temporal hardware transactions with separate concepts:

- **Temporal guards**: `delay(k)`, `dyndelay(k)` - control when a rule fires
- **Channels**: `send()`, `recv()` - pass data between rules

**Problems with this separation:**

1. **Redundant specification**: Channel depth must match delay value
2. **Error-prone**: User can mismatch delay and channel parameters
3. **Split analysis**: Temporal graph vs data flow graph analyzed separately
4. **Awkward semantics**: Guard-message atomicity is a patch for the fundamental coupling

**Key insight**: A temporal relationship between rules inherently bundles:

- Synchronization (when the successor can fire)
- Communication (data flows with the synchronization)
- Timing (the delay determines when data arrives)

### 2.2 Lessons from DNN Kernel DSLs

**cuTile** (NVIDIA):

- Tokens are first-class values produced by memory operations
- `JoinTokens` merges multiple dependency chains
- Compiler automatically threads tokens via `token_order_pass`
- Uses MLIR-style def-use chains for token ordering

**Triton-distributed**:

- `wait(barrier, n, scope, semantic)` produces a token
- `consume_token(value, token)` creates explicit data dependency
- Token is an SSA value that enforces ordering through def-use

**Key insight**: Tokens should be SSA values, not symbol references. This enables standard MLIR def-use analysis.

### 2.3 Current CMT2 State

CMT2 currently supports:

1. **GAA Rules**: Atomic single-cycle rules with guards
2. **Proc Rules**: Multi-cycle FSM-based control flow
3. **Static Steps**: Fixed-latency operations with timing attributes
4. **Control Flow**: `seq`, `par`, `while`, `static_repeat`, `if`

**Existing timing mechanism:**

```mlir
cmt2.proc.step @s0 {
    // actions
} {timing = #cmt2.timing<start=0, end=1>}
```

**Limitations:**

1. No explicit temporal relationships between rules
2. FIFOs for coordination add area/latency overhead
3. No timing inference for inter-rule dependencies
4. Multi-cycle operations require explicit FSM management

---

## 3. Core Abstraction: SyncToken as SSA Value

### 3.1 SyncToken Type

A `SyncToken` is a first-class SSA value representing a synchronization point. It carries:

1. **Validity** - whether the synchronization point has been reached
2. **Optional data payload** - values passed between rules
3. **Timing mode** - latency-sensitive or latency-insensitive

```mlir
// SyncToken type - parameterized by data type and timing mode
!cmt2.sync_token                                    // Token without data
!cmt2.sync_token<data=!firrtl.uint<32>>            // Token carrying 32-bit data
!cmt2.sync_token<data=!firrtl.uint<32>, mode=ls>   // Latency-sensitive
!cmt2.sync_token<data=!firrtl.uint<32>, mode=li>   // Latency-insensitive
```

**Timing modes:**

| Mode | Mnemonic | Hardware | Behavior |
|------|----------|----------|----------|
| Latency-sensitive | `ls` | Shift register | Token valid at exact cycle, consumers must be ready |
| Latency-insensitive | `li` | FIFO | Token remains valid until consumed |

### 3.2 Multi-Consumer Token Semantics

**Tokens can have multiple consumers.** When a token is consumed by both rule A and rule B, this implies:

1. **Synchronization**: Both A and B wait for the same producer
2. **Fork pattern**: The token is logically "broadcast" to all consumers
3. **Hardware realization**:
   - **LS mode**: Fan-out valid/data signals; all consumers fire in the same cycle
   - **LI mode**: Either duplicate FIFO or broadcast FIFO with per-consumer handshake

```mlir
// Fork pattern: one token, multiple consumers
%tok = cmt2.rule @producer() tokens_out(...) {...}

// Both consumers receive the same token - they synchronize on producer
%result_a = cmt2.rule @consumer_a() tokens_in(%tok) {...}
%result_b = cmt2.rule @consumer_b() tokens_in(%tok) {...}
```

This is NOT "consume once and invalidate" - the token represents a synchronization point that multiple rules can depend on.

### 3.3 Rules Produce and Consume Tokens

Instead of separate `emit`/`wait`/`consume` operations, rules directly produce and consume tokens through their signatures.

#### 3.3.1 Rule Signature with Tokens

```mlir
// Rule with token inputs and outputs
// tokens_in: tokens this rule waits for (synchronization dependencies)
// tokens_out: tokens this rule produces (for successors to wait on)

cmt2.rule @consumer(
    %data_arg: !firrtl.uint<8>          // Regular data argument
) tokens_in(%tok_a: !cmt2.sync_token<data=!firrtl.uint<32>>,
            %tok_b: !cmt2.sync_token<data=!firrtl.uint<16>>)
  tokens_out(!cmt2.sync_token<data=!firrtl.uint<32>>)
  -> (!firrtl.uint<8>) {
    // Guard: rule fires when all input tokens are valid
    %valid_a = cmt2.token.valid %tok_a : !cmt2.sync_token<data=!firrtl.uint<32>>
    %valid_b = cmt2.token.valid %tok_b : !cmt2.sync_token<data=!firrtl.uint<16>>
    %ready = firrtl.and %valid_a, %valid_b : !firrtl.uint<1>
    cmt2.guard %ready : !firrtl.uint<1>
} body {
    // Extract data from input tokens
    %val_a = cmt2.token.data %tok_a : !cmt2.sync_token<data=!firrtl.uint<32>> -> !firrtl.uint<32>
    %val_b = cmt2.token.data %tok_b : !cmt2.sync_token<data=!firrtl.uint<16>> -> !firrtl.uint<16>

    // Compute
    %result = cmt2.call @process(%val_a, %val_b) : ...

    // Create output token with data
    %tok_out = cmt2.token.create %result : !firrtl.uint<32> -> !cmt2.sync_token<data=!firrtl.uint<32>>

    // Return data result and output tokens
    cmt2.return %data_result tokens(%tok_out)
}
```

#### 3.3.2 Token Operations

Only four token operations needed:

```mlir
// Check if token is valid (for guards)
%valid = cmt2.token.valid %tok : !cmt2.sync_token<...> -> !firrtl.uint<1>

// Extract data from token (in body, after guard ensures validity)
%data = cmt2.token.data %tok : !cmt2.sync_token<data=T> -> T

// Create a new token with data (produces output token)
%tok = cmt2.token.create %data : T -> !cmt2.sync_token<data=T>

// Create token without data
%tok = cmt2.token.create : !cmt2.sync_token

// Join multiple tokens (all must be valid)
// Mode inference: if any input is LI, output is LI
%joined = cmt2.token.join %tok_a, %tok_b : (!cmt2.sync_token<...>, !cmt2.sync_token<...>)
                                          -> !cmt2.sync_token<data=tuple<...>>
```

### 3.4 Token Wiring at Module Level

Tokens are wired between rules at module instantiation, just like regular signals:

```mlir
cmt2.module @pipeline {
    // Instantiate rules
    %prod_tok = cmt2.rule @producer() tokens_in() tokens_out(!cmt2.sync_token<data=!firrtl.uint<32>>)
                {timing = #cmt2.timing<start=0, end=2>}

    // Wire producer's output token to consumer's input
    %result = cmt2.rule @consumer() tokens_in(%prod_tok) tokens_out() -> !firrtl.uint<32>
}
```

### 3.5 Benefits of SSA Token Design

1. **MLIR def-use analysis**: Token dependencies are explicit in IR, standard passes work
2. **No symbol indirection**: Direct SSA references, no lookup needed
3. **Type safety**: Token data types checked at compile time
4. **Multiple consumers**: Natural fork pattern via SSA multi-use
5. **Composability**: Tokens can be joined, forwarded, etc. using standard ops

---

## 4. Timing as Operation Attributes

### 4.1 Timing Attribute Format

All timing information uses the existing `#cmt2.timing<start, end>` format:

```mlir
// Timing attribute format
#cmt2.timing<start=0, end=1>    // Active from cycle 0 to cycle 1
#cmt2.timing<start=0, end=0>    // Single cycle at 0
#cmt2.timing<start=2, end=5>    // 3-cycle operation starting at cycle 2
```

For tokens, the timing attribute specifies when the token becomes valid relative to the rule's start:

```mlir
// Token valid 2 cycles after rule starts
%tok = cmt2.token.create %data {timing = #cmt2.timing<start=0, end=2>} : ...

// Rule with timing
%tok = cmt2.rule @stage1() tokens_in(%prev_tok) tokens_out(...)
       {timing = #cmt2.timing<start=1, end=2>}
```

### 4.2 Untimed vs Timed

- **Untimed**: No timing attributes - compiler schedules using ASAP
- **Timed**: Timing attributes present - compiler validates and honors

---

## 5. Unified Pipeline Construct: proc.dataflow

### 5.1 Design Rationale

`proc.pipeline` and `proc.dataflow` are not fundamentally different:

- Pipeline stages are just sequential tasks with unit delay
- Dataflow tasks are stages with variable timing

**Decision**: Single `proc.dataflow` construct. Pipeline is just dataflow with constrained topology.

### 5.2 proc.dataflow Operation

```mlir
cmt2.proc.dataflow @pipeline_name(
    %input: !firrtl.uint<32>
) -> (!firrtl.uint<32>) {
    // Dataflow body contains tasks connected by tokens

    // Task 0: entry
    %tok0 = cmt2.dataflow.task @stage0(%input)
            tokens_in()
            tokens_out(!cmt2.sync_token<data=!firrtl.bundle<...>>)
            {timing = #cmt2.timing<start=0, end=1>} {
        %state = cmt2.call @init(%input) : ...
        %tok = cmt2.token.create %state : ...
        cmt2.dataflow.yield %tok
    }

    // Task 1: middle stage
    %tok1 = cmt2.dataflow.task @stage1()
            tokens_in(%tok0)
            tokens_out(!cmt2.sync_token<data=!firrtl.bundle<...>>)
            {timing = #cmt2.timing<start=1, end=2>} {
        %state = cmt2.token.data %tok0 : ...
        %next = cmt2.call @iter(%state) : ...
        %tok = cmt2.token.create %next : ...
        cmt2.dataflow.yield %tok
    }

    // ... more tasks ...

    // Final task: output
    cmt2.dataflow.task @stage_final()
            tokens_in(%tok_prev)
            tokens_out()
            {timing = #cmt2.timing<start=7, end=8>} {
        %state = cmt2.token.data %tok_prev : ...
        %result = cmt2.call @finalize(%state) : ...
        cmt2.dataflow.return %result
    }
}
```

### 5.3 Pipeline as Constrained Dataflow

An instruction-level pipeline is dataflow where:

- Tasks are linearly connected (stage 0 -> stage 1 -> ... -> stage N)
- All delays are 1 cycle
- All tokens are latency-sensitive

```mlir
// Syntactic sugar: proc.pipeline desugars to proc.dataflow
cmt2.proc.pipeline @divider(%dividend, %divisor) -> !firrtl.uint<8> {
    stages = 8,
    interval = 1
} {
    // Stages are implicitly connected with unit-delay LS tokens
    cmt2.pipeline.stage 0 {
        %state = cmt2.call @init(%dividend, %divisor) : ...
        cmt2.pipeline.forward %state
    }
    cmt2.pipeline.stage 1 {
        %state = cmt2.pipeline.receive : ...
        %next = cmt2.call @iter(%state) : ...
        cmt2.pipeline.forward %next
    }
    // ...
    cmt2.pipeline.stage 7 {
        %state = cmt2.pipeline.receive : ...
        %result = cmt2.call @iter(%state) : ...
        cmt2.pipeline.return %result
    }
}
```

Lowers to:

```mlir
cmt2.proc.dataflow @divider(%dividend, %divisor) -> !firrtl.uint<8> {
    %tok0 = cmt2.dataflow.task @stage0(...) tokens_out(...) {timing = #cmt2.timing<start=0, end=1>} {...}
    %tok1 = cmt2.dataflow.task @stage1() tokens_in(%tok0) tokens_out(...) {timing = #cmt2.timing<start=1, end=2>} {...}
    // ...
    cmt2.dataflow.task @stage7() tokens_in(%tok6) {timing = #cmt2.timing<start=7, end=8>} {...}
}
```

### 5.4 Flexible Dataflow Patterns

The unified construct supports various patterns:

```mlir
// Pattern 1: Linear pipeline (fixed latency)
// stage0 -> stage1 -> stage2 -> stage3

// Pattern 2: Fork-join (multi-consumer token)
//            ┌─> taskA ─┐
// stage0 ────┤          ├──> join -> final
//            └─> taskB ─┘

cmt2.proc.dataflow @fork_join(...) {
    %tok0 = cmt2.dataflow.task @source() tokens_out(...) {...}

    // Fork: both tasks take same input token (multi-consumer)
    %tok_a = cmt2.dataflow.task @taskA() tokens_in(%tok0) tokens_out(...) {...}
    %tok_b = cmt2.dataflow.task @taskB() tokens_in(%tok0) tokens_out(...) {...}

    // Join: final task waits for both
    cmt2.dataflow.task @final() tokens_in(%tok_a, %tok_b) {...}
}

// Pattern 3: Variable latency (latency-insensitive)
cmt2.proc.dataflow @variable_latency(...) {
    %tok0 = cmt2.dataflow.task @compute() tokens_out(...)
            {timing = #cmt2.timing<start=0, end=1>} {...}

    // Memory access with variable latency (LI token)
    %tok1 = cmt2.dataflow.task @mem_access() tokens_in(%tok0) tokens_out(...)
            attributes {token_mode = "li"} {...}

    // Continues after memory completes
    %tok2 = cmt2.dataflow.task @post_mem() tokens_in(%tok1) tokens_out(...) {...}
}
```

---

## 6. PyCMT2 API

### 6.1 Unified Token Declaration

All token declarations use the dataflow pattern as the canonical form:

```python
with m.dataflow("pipeline") as df:
    # Task with explicit token connections
    with df.task("stage0", timing=(0, 1)) as t:
        data = t.call(init, input)
        t.forward(data)  # Creates output token

    # Receives from previous task (implicit connection)
    with df.task("stage1", timing=(1, 2)) as t:
        data = t.receive()
        result = t.call(process, data)
        t.forward(result)

    # Fork: multiple tasks receive same token
    with df.task("branch_a", receives_from="stage1", timing=(2, 3)) as t:
        data = t.receive()
        t.forward(t.call(filter_a, data))

    with df.task("branch_b", receives_from="stage1", timing=(2, 4)) as t:
        data = t.receive()
        t.forward(t.call(filter_b, data))

    # Join: wait for multiple inputs
    with df.task("combine", receives_from=["branch_a", "branch_b"], timing=(4, 5)) as t:
        a = t.receive("branch_a")
        b = t.receive("branch_b")
        result = t.call(combine, a, b)
        t.returns(result)
```

### 6.2 Timing Specification

Timing is specified as `(start, end)` tuples matching the MLIR attribute:

```python
from pycmt2.timing import Timing

# Explicit timing tuple
with df.task("stage0", timing=(0, 1)) as t:
    ...

# Using Timing helper
with df.task("stage1", timing=Timing(start=1, end=2)) as t:
    ...

# Latency-insensitive task (no timing, uses LI token)
with df.task("mem_access", mode="li") as t:
    ...
```

### 6.3 Shorthand Pipeline Syntax

```python
# Shorthand for linear pipelines
with m.pipeline("divider", stages=8, interval=1) as pipe:
    @pipe.stage(0)
    def init_stage(dividend, divisor):
        return call(init_div, dividend, divisor)

    @pipe.stages(1, 7)  # Stages 1-6
    def iter_stage(state):
        return call(iter_div, state)

    @pipe.stage(7)
    def final_stage(state):
        result = call(iter_div, state)
        return call(extract_quotient, result)
```

---

## 7. Token Analysis Infrastructure

### 7.1 Leveraging MLIR Def-Use

Since tokens are SSA values, we use standard MLIR infrastructure:

```cpp
// Get all uses of a token (multi-consumer pattern)
Value token = ...;
for (OpOperand &use : token.getUses()) {
    Operation *consumer = use.getOwner();
    // Process consumer - token can have multiple!
}

// Get the producer of a token
Value token = ...;
Operation *producer = token.getDefiningOp();
```

### 7.2 FIFO Depth Inference

For latency-insensitive tokens, FIFO depth must be determined to prevent deadlock and ensure correctness.

**Analysis Algorithm:**

```cpp
class FIFODepthAnalysis {
public:
    // Infer minimum FIFO depth for an LI token
    unsigned inferDepth(Value token) {
        // 1. Compute producer rate: how often producer fires
        unsigned prodRate = analyzeProducerRate(token.getDefiningOp());

        // 2. Compute consumer rate: how often each consumer fires
        unsigned minConsRate = UINT_MAX;
        for (OpOperand &use : token.getUses()) {
            unsigned rate = analyzeConsumerRate(use.getOwner());
            minConsRate = std::min(minConsRate, rate);
        }

        // 3. Compute maximum outstanding tokens
        // If producer faster than consumers, tokens accumulate
        if (prodRate > minConsRate) {
            // Need buffering for rate mismatch
            return computeBufferDepth(prodRate, minConsRate);
        }

        // 4. Account for latency variation
        unsigned maxLatencyVar = analyzeLatencyVariation(token);

        // 5. Minimum depth = max(rate_mismatch_buffer, latency_variation)
        return std::max(1u, maxLatencyVar);
    }

    // Static analysis of producer firing rate
    unsigned analyzeProducerRate(Operation *producer);

    // Static analysis of consumer firing rate
    unsigned analyzeConsumerRate(Operation *consumer);

    // Analyze latency variation in the path
    unsigned analyzeLatencyVariation(Value token);
};
```

**Depth Inference Rules:**

| Pattern | FIFO Depth |
|---------|------------|
| Single producer, single consumer, matched rates | 1 |
| Producer 2x faster than consumer | 2 |
| Variable latency consumer (memory access) | max_latency - min_latency + 1 |
| Fork with different consumer latencies | max(consumer_latencies) - min + 1 |

### 7.3 Deadlock Detection

Cyclic dependencies with LI tokens can cause deadlock if all FIFOs fill simultaneously.

```cpp
class DeadlockAnalysis {
public:
    LogicalResult checkForDeadlock(ModuleOp module) {
        // Build token dependency graph
        TokenGraph graph = buildTokenGraph(module);

        // Find cycles involving only LI tokens
        SmallVector<Cycle> liCycles = findLICycles(graph);

        for (Cycle &cycle : liCycles) {
            // Check if cycle can deadlock
            // Deadlock if: sum of FIFO depths < cycle length
            unsigned totalDepth = 0;
            for (Value token : cycle.tokens) {
                totalDepth += getFIFODepth(token);
            }

            if (totalDepth < cycle.length) {
                return emitError(cycle.loc)
                    << "Potential deadlock: cycle of " << cycle.length
                    << " LI tokens with total FIFO depth " << totalDepth;
            }
        }

        return success();
    }
};
```

**Deadlock Prevention Rules:**

1. **Acyclic LI graphs**: No deadlock possible
2. **Cyclic LI graphs**: Total FIFO depth in cycle must exceed cycle length
3. **Mixed LS/LI cycles**: LS edges break deadlock (they don't buffer)

### 7.4 Token Graph Analysis

```cpp
// lib/Dialect/Cmt2/Analysis/TokenAnalysis.h
class TokenAnalysis {
public:
    // Build from module using def-use chains
    static TokenAnalysis build(cmt2::ModuleOp module);

    // Get timing of a token
    TimingAttr getTiming(Value token);

    // Check if token is latency-sensitive
    bool isLatencySensitive(Value token);

    // Get all consumers of a token
    SmallVector<Operation*> getConsumers(Value token);

    // Get all tokens in a latency-sensitive region
    SmallVector<Value> getLatencySensitiveRegion(Value token);

    // Validate timing consistency
    LogicalResult checkCoordination();

    // Infer FIFO depths for all LI tokens
    DenseMap<Value, unsigned> inferFIFODepths();

    // Check for potential deadlocks
    LogicalResult checkDeadlock();
};
```

### 7.5 Analysis Passes

| Pass | Purpose |
|------|---------|
| `cmt2-token-timing-inference` | Infer timing for untimed tokens |
| `cmt2-token-coordination-check` | Detect timing mismatches |
| `cmt2-fifo-depth-inference` | Compute LI FIFO depths |
| `cmt2-deadlock-check` | Detect potential deadlocks |
| `cmt2-dataflow-to-rules` | Lower dataflow to rules with tokens |

---

## 8. Synthesis Flow

### 8.1 Overall Pipeline

```
Input: proc.dataflow / rules with tokens
         │
         ▼
┌─────────────────────────────────────────┐
│ 1. Token Analysis (using def-use)       │
│    - Build token dependency graph       │
│    - Identify LS regions and LI tokens  │
│    - Infer timing for untimed tokens    │
└─────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────┐
│ 2. FIFO Depth Inference                 │
│    - Analyze producer/consumer rates    │
│    - Compute latency variation          │
│    - Assign FIFO depths to LI tokens    │
└─────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────┐
│ 3. Deadlock Detection                   │
│    - Find LI cycles                     │
│    - Verify FIFO depths prevent deadlock│
│    - Emit warnings/errors               │
└─────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────┐
│ 4. Dataflow Lowering                    │
│    - Lower proc.dataflow to rules       │
│    - Insert token connections           │
└─────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────┐
│ 5. Token Lowering                       │
│    - LS tokens → shift registers        │
│    - LI tokens → FIFOs (with depth)     │
│    - Insert stall controllers           │
└─────────────────────────────────────────┘
         │
         ▼
    Standard CMT2 lowering (cmt2-to-firrtl)
```

### 8.2 Token Lowering Details

**Latency-sensitive token (shift register):**

```mlir
// Before: LS token with delay=2
%tok = cmt2.token.create %data {timing = #cmt2.timing<start=0, end=2>} : ...
// ... later ...
%valid = cmt2.token.valid %tok : ...
%data = cmt2.token.data %tok : ...

// After
cmt2.instance @__tok_shift = @ShiftReg<depth=2, data=!firrtl.uint<32>>
// Producer: @__tok_shift.enq(%data)
// Consumer guard: @__tok_shift.valid()
// Consumer body: @__tok_shift.peek()
// Note: For multi-consumer, fan-out the peek signal
```

**Latency-insensitive token (FIFO):**

```mlir
// Before: LI token
%tok = cmt2.token.create %data {mode = "li"} : ...

// After (depth inferred by analysis)
cmt2.instance @__tok_fifo = @FIFO<depth=4, data=!firrtl.uint<32>>
// Producer: @__tok_fifo.enq(%data)
// Consumer guard: !@__tok_fifo.empty()
// Consumer body: @__tok_fifo.deq()
// Note: For multi-consumer fork, use broadcast FIFO
```

**Multi-consumer fork (LS):**

```mlir
// Before: one token, two consumers
%tok = cmt2.rule @producer() tokens_out(...) {...}
%a = cmt2.rule @consumer_a() tokens_in(%tok) {...}
%b = cmt2.rule @consumer_b() tokens_in(%tok) {...}

// After: fan-out valid/data signals
// @__tok_shift.valid() -> consumer_a guard AND consumer_b guard
// @__tok_shift.peek() -> both consumer bodies
// Both consumers fire in same cycle
```

**Multi-consumer fork (LI):**

```mlir
// Before: LI token with two consumers
// After: broadcast FIFO with per-consumer ready/valid handshake
cmt2.instance @__tok_bcast = @BroadcastFIFO<depth=4, consumers=2, data=!firrtl.uint<32>>
// Producer: @__tok_bcast.enq(%data)
// Consumer A guard: @__tok_bcast.valid(0)
// Consumer A body: @__tok_bcast.deq(0)
// Consumer B guard: @__tok_bcast.valid(1)
// Consumer B body: @__tok_bcast.deq(1)
// Data only removed when ALL consumers have dequeued
```

### 8.3 Stall Controller Design

For hybrid LS/LI designs, stall controllers manage the boundary between timing domains.

**Stall Controller Interface:**

```
Inputs:
  - li_ready[N]: Ready signals from LI FIFOs (not full for write, not empty for read)
  - li_valid[N]: Valid signals from LI FIFOs

Outputs:
  - stall: Global stall signal for the LS region
  - enable[N]: Per-FIFO enable signals
```

**Stall Controller Logic:**

```verilog
// Stall when ANY LI interface is blocked
assign stall = |(~li_ready) | |(~li_valid);

// Enable FIFOs only when not stalled
assign enable = {N{~stall}};
```

**Stall Propagation:**

```
LS Region: tokens connected by LS edges
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│  task0 ──LS──> task1 ──LS──> task2 ──LS──> task3            │
│    │                           │              │              │
│    │                           ▼              ▼              │
│    │                    ┌─────────────────────────┐          │
│    └──────────────────> │     Stall Controller    │          │
│                         │                         │          │
│    stall_all <──────────│  li_in_ready  li_out_ready         │
│        │                └─────────────────────────┘          │
│        │                         ▲              ▲            │
│        ▼                         │              │            │
│   [gate all                  [LI FIFO]     [LI FIFO]         │
│    registers]                   in            out            │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

**Multiple LI Boundaries:**

When an LS region has multiple LI entry/exit points:

1. **Collect all LI signals**: Gather ready/valid from all LI interfaces
2. **AND all ready signals**: Stall if ANY interface is blocked
3. **Gate all LS registers**: Single stall signal controls entire region
4. **Per-FIFO enables**: Each FIFO gets individual enable based on its transaction

```cpp
// Stall controller generation
void generateStallController(LSRegion &region) {
    SmallVector<Value> liReadySignals;
    SmallVector<Value> liValidSignals;

    // Collect signals from all LI boundaries
    for (LIBoundary &boundary : region.liBoundaries) {
        if (boundary.isInput) {
            liValidSignals.push_back(boundary.fifo.getNotEmpty());
        } else {
            liReadySignals.push_back(boundary.fifo.getNotFull());
        }
    }

    // Generate stall = NOT(AND(all_ready) AND AND(all_valid))
    Value allReady = andReduce(liReadySignals);
    Value allValid = andReduce(liValidSignals);
    Value stall = not(and(allReady, allValid));

    // Gate all registers in LS region
    for (Register &reg : region.registers) {
        reg.setEnable(not(stall));
    }
}
```

---

## 9. Implementation Plan

### Phase 1: Core Token Infrastructure (Priority: High)

| # | Task | Files |
|---|------|-------|
| T1 | Add `SyncTokenType` to Cmt2Types.td | `Cmt2Types.td`, `Cmt2Types.cpp` |
| T2 | Add `TokenValidOp`, `TokenDataOp`, `TokenCreateOp`, `TokenJoinOp` | `Cmt2Ops.td`, `Cmt2Ops.cpp` |
| T3 | Extend `RuleOp` signatures with `tokens_in`/`tokens_out` | `Cmt2Ops.td`, `Cmt2Ops.cpp` |
| T4 | Add verifiers for token operations | `Cmt2Ops.cpp` |

### Phase 2: Dataflow Construct (Priority: High)

| # | Task | Files |
|---|------|-------|
| D1 | Add `ProcDataflowOp`, `DataflowTaskOp`, `DataflowYieldOp` | `Cmt2Ops.td` |
| D2 | Add `ProcPipelineOp` as syntactic sugar | `Cmt2Ops.td` |
| D3 | Implement dataflow-to-rules lowering | `Transforms/DataflowLowering.cpp` |

### Phase 3: Analysis (Priority: High)

| # | Task | Files |
|---|------|-------|
| A1 | Implement `TokenAnalysis` using def-use chains | `Analysis/TokenAnalysis.cpp` |
| A2 | Implement FIFO depth inference | `Analysis/FIFODepthAnalysis.cpp` |
| A3 | Implement deadlock detection | `Analysis/DeadlockAnalysis.cpp` |
| A4 | Implement timing inference pass | `Transforms/TimingInference.cpp` |

### Phase 4: Lowering (Priority: High)

| # | Task | Files |
|---|------|-------|
| L1 | Implement token lowering (LS → shift reg, LI → FIFO) | `Transforms/TokenLowering.cpp` |
| L2a | Design stall controller interface | `Transforms/StallControllerGen.cpp` |
| L2b | Implement single-LI-boundary stall controller | `Transforms/StallControllerGen.cpp` |
| L2c | Implement multi-LI-boundary composition | `Transforms/StallControllerGen.cpp` |
| L3 | Implement multi-consumer fork lowering | `Transforms/TokenLowering.cpp` |
| L4 | Integrate with cmt2-to-firrtl | `Transforms/*.cpp` |

### Phase 5: PyCMT2 (Priority: High)

| # | Task | Files |
|---|------|-------|
| P1 | Add unified dataflow builder | `pycmt2/dataflow_builders.py` |
| P2 | Add pipeline shorthand builder | `pycmt2/pipeline_builders.py` |
| P3 | Add timing helpers | `pycmt2/timing.py` |

### Phase 6: Testing (Priority: High)

| # | Task | Files |
|---|------|-------|
| E1 | Division pipeline example | `examples/PyCMT2/division_pipeline.py` |
| E2 | Fork-join dataflow example | `examples/PyCMT2/dataflow_forkjoin.py` |
| E3 | Test suite | `test/Dialect/Cmt2/token-*.mlir` |
| E4 | End-to-end simulation test | `examples/PyCMT2/pipeline_e2e.py` |

---

## 10. Appendix: Operation Definitions (TableGen)

### 10.1 Token Mode Enum

```tablegen
def Cmt2TokenMode : I32EnumAttr<"TokenMode", "Token timing mode", [
    I32EnumAttrCase<"LS", 0, "ls">,
    I32EnumAttrCase<"LI", 1, "li">
]> {
    let cppNamespace = "::circt::cmt2";
}
```

### 10.2 SyncToken Type

```tablegen
def SyncTokenType : Cmt2_Type<"SyncToken", "sync_token"> {
    let summary = "Synchronization token type";
    let description = [{
        A first-class SSA value representing a synchronization point.
        Optionally carries data and has timing mode (ls/li).
        Tokens can have multiple consumers (fork pattern).
    }];

    let parameters = (ins
        OptionalParameter<"Type">:$dataType,
        DefaultValuedParameter<"TokenMode", "TokenMode::LS">:$mode
    );

    let assemblyFormat = [{
        (`<` `data` `=` $dataType^ (`,` `mode` `=` $mode^)? `>`)?
    }];
}
```

### 10.3 Token Operations

```tablegen
def TokenValidOp : Cmt2_Op<"token.valid", [Pure]> {
    let summary = "Check if token is valid";
    let arguments = (ins SyncTokenType:$token);
    let results = (outs I1:$valid);
    let hasVerifier = 1;
}

def TokenDataOp : Cmt2_Op<"token.data", [Pure]> {
    let summary = "Extract data from token";
    let arguments = (ins SyncTokenType:$token);
    let results = (outs AnyType:$data);
    let hasVerifier = 1;
}

def TokenCreateOp : Cmt2_Op<"token.create", [
    DeclareOpInterfaceMethods<InferTypeOpInterface>
]> {
    let summary = "Create a token with optional data";
    let arguments = (ins
        Optional<AnyType>:$data,
        OptionalAttr<TimingAttr>:$timing,
        DefaultValuedAttr<Cmt2TokenMode, "TokenMode::LS">:$mode
    );
    let results = (outs SyncTokenType:$token);
    let hasVerifier = 1;
}

def TokenJoinOp : Cmt2_Op<"token.join", [Pure]> {
    let summary = "Join multiple tokens (mode: LI if any input is LI)";
    let arguments = (ins Variadic<SyncTokenType>:$tokens);
    let results = (outs SyncTokenType:$joined);
    let hasVerifier = 1;
}
```

### 10.4 Extended Rule Operation

```tablegen
def RuleOp : Cmt2_Op<"rule", [...]> {
    let arguments = (ins
        SymbolNameAttr:$sym_name,
        Variadic<SyncTokenType>:$token_inputs,
        // ... existing arguments ...
    );

    let results = (outs
        Variadic<SyncTokenType>:$token_outputs
    );

    let regions = (region
        SizedRegion<1>:$guard,
        SizedRegion<1>:$body
    );

    let extraClassDeclaration = [{
        // Get token input types
        ArrayRef<Type> getTokenInputTypes();
        // Get token output types
        ArrayRef<Type> getTokenOutputTypes();
        // Check if rule has token dependencies
        bool hasTokenDependencies() { return !getTokenInputs().empty(); }
    }];

    let hasVerifier = 1;
}
```

### 10.5 Dataflow Operations

```tablegen
def ProcDataflowOp : Cmt2_Op<"proc.dataflow", [IsolatedFromAbove]> {
    let summary = "Dataflow pipeline container";
    let arguments = (ins
        SymbolNameAttr:$sym_name,
        OptionalAttr<I64Attr>:$interval
    );
    let regions = (region SizedRegion<1>:$body);
    let hasVerifier = 1;
}

def DataflowTaskOp : Cmt2_Op<"dataflow.task", [
    HasParent<"ProcDataflowOp">,
    SingleBlockImplicitTerminator<"DataflowYieldOp">
]> {
    let summary = "Task within dataflow";
    let arguments = (ins
        SymbolNameAttr:$sym_name,
        Variadic<SyncTokenType>:$token_inputs,
        OptionalAttr<TimingAttr>:$timing,
        DefaultValuedAttr<Cmt2TokenMode, "TokenMode::LS">:$mode
    );
    let results = (outs Variadic<SyncTokenType>:$token_outputs);
    let regions = (region SizedRegion<1>:$body);
    let hasVerifier = 1;
}

def DataflowYieldOp : Cmt2_Op<"dataflow.yield", [
    Terminator, HasParent<"DataflowTaskOp">
]> {
    let summary = "Yield tokens from task";
    let arguments = (ins Variadic<SyncTokenType>:$tokens);
}

def DataflowReturnOp : Cmt2_Op<"dataflow.return", [
    Terminator, HasParent<"DataflowTaskOp">
]> {
    let summary = "Return final result from dataflow";
    let arguments = (ins AnyType:$result);
}
```

---

## 11. Glossary

| Term | Definition |
|------|------------|
| **SyncToken** | SSA value representing synchronization point + optional data |
| **Multi-consumer** | Token used by multiple rules; implies fork/broadcast pattern |
| **Latency-Sensitive (LS)** | Fixed timing, shift register impl, consumers must be ready |
| **Latency-Insensitive (LI)** | Variable timing, FIFO impl, waits until consumed |
| **proc.dataflow** | Unified construct for pipeline/dataflow patterns |
| **Timing Attribute** | `#cmt2.timing<start, end>` format |
| **Stall Controller** | Hardware gating LS registers when blocked by LI edge |
| **FIFO Depth** | Buffer size for LI tokens, inferred by analysis |

---

## 12. References

1. Clamp MICRO 2025 Paper: "Temporal Hardware Transactions"
2. cuTile: NVIDIA's tile-centric GPU programming with token-based memory ordering
3. Triton-distributed: Distributed Triton with token-based synchronization
4. MLIR Documentation: SSA value semantics, def-use chains
5. CMT2 Documentation: `docs/Dialects/Cmt2/`
