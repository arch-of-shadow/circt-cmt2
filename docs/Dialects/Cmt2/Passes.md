# CMT2 Passes Reference

Complete reference for CMT2 transformation and analysis passes.

---

## Pass Categories

| Category | Purpose |
|----------|---------|
| **Analysis** | Understand module structure |
| **Optimization** | Inline and simplify |
| **Procedural** | Lower procedural control |
| **Static** | Optimize static control |
| **Timing** | Cycle-precise scheduling |
| **Verification** | Validate constraints |

---

## Analysis Passes

### cmt2-print-instance-graph

Prints module hierarchy as DOT graph.

```bash
circt-opt input.mlir -cmt2-print-instance-graph
```

**Output:** DOT format graph showing module instantiation relationships.

### cmt2-print-call-info

Shows method/value call patterns.

```bash
circt-opt input.mlir -cmt2-print-call-info
```

**Output:** For each rule/method/value, lists called methods and values.

### cmt2-print-conflict-matrix

Displays scheduling conflict relationships.

```bash
circt-opt input.mlir -cmt2-print-conflict-matrix
```

**Output:** Matrix showing conflict, sequence-before, and conflict-free relationships.

### cmt2-print-scheduler

Prints scheduler analysis results.

```bash
circt-opt input.mlir -cmt2-print-scheduler
```

---

## Module Optimization Passes

### cmt2-inline-modules

Inlines modules into instantiation sites (bottom-up).

```bash
circt-opt input.mlir -cmt2-inline-modules
```

**Effect:** Flattens module hierarchy for synthesis optimization.

### cmt2-inline-private-funcs

Inlines private methods/values called via `@this`.

```bash
circt-opt input.mlir -cmt2-inline-private-funcs
```

**Effect:** Eliminates internal method call overhead.

### cmt2-module-inliner

General module inlining pass.

```bash
circt-opt input.mlir -cmt2-module-inliner
```

### cmt2-control-collapsing

Simplifies procedural control structures.

```bash
circt-opt input.mlir -cmt2-control-collapsing
```

**Effects:**
- Flattens nested `seq`/`par` blocks
- Merges consecutive `enable` operations
- Removes empty control blocks

---

## Verification Passes

### cmt2-verify-private-funcs-inlined

Verifies all private functions have been inlined.

```bash
circt-opt input.mlir -cmt2-verify-private-funcs-inlined
```

**Errors:** Reports any remaining private function calls.

### cmt2-verify-call-sequence

Validates call sequences respect conflict matrix constraints.

```bash
circt-opt input.mlir -cmt2-verify-call-sequence
```

**Errors:** Reports scheduling constraint violations.

---

## Procedural Layer Passes

### cmt2-compile-invoke

Converts `proc.invoke` to `proc.enable` with generated steps.

```bash
circt-opt input.mlir -cmt2-compile-invoke
```

**Transform:**
```mlir
// Before
cmt2.proc.invoke @inst @method(%arg)

// After
cmt2.proc.step @__invoke_inst_method { ... }
cmt2.proc.enable @__invoke_inst_method
```

### cmt2-tdcc

**Top-Down Compile Control** - Core procedural lowering.

```bash
circt-opt input.mlir -cmt2-tdcc
```

**Effects:**
1. Assigns unique `NODE_ID` to each `proc.enable`
2. Builds execution schedule
3. Generates FSM metadata
4. Computes state transitions

### cmt2-proc-stmt-to-action

Converts procedural statements to FSM-based action rules.

```bash
circt-opt input.mlir -cmt2-proc-stmt-to-action
```

**Transform:** Procedural control → FSM state machine rules.

### cmt2-proc-to-gaa

Converts `proc.rule`/`proc.method` to regular GAA rules.

```bash
circt-opt input.mlir -cmt2-proc-to-gaa
```

**Effect:** Final conversion to standard GAA semantics.

---

## Static Control Passes

### cmt2-static-inference

Infers latencies for control structures.

```bash
circt-opt input.mlir -cmt2-static-inference
```

**Effects:**
- Computes latencies for `seq` (sum), `par` (max)
- Marks structures promotable to static
- Propagates timing information

### cmt2-static-promotion

Promotes dynamic control to static when beneficial.

```bash
circt-opt input.mlir -cmt2-static-promotion
```

**Transforms:**
- `seq` → static `seq` (when all children are static)
- `if` → `static_if` (when branches have known latency)
- `while` → `static_repeat` (when iteration count is known)

### cmt2-simplify-static-guards

Simplifies timing guards.

```bash
circt-opt input.mlir -cmt2-simplify-static-guards
```

**Effects:** Merges overlapping timing intervals.

### cmt2-compile-static

Compiles static control to optimized FSM hardware.

```bash
circt-opt input.mlir -cmt2-compile-static
```

**Effects:**
1. Generates FSM wrapper for static steps
2. Creates tick rule for FSM advancement
3. Creates done value for completion signal
4. Creates start rule for activation
5. Applies early-reset optimization

**Generated Components:**
```mlir
cmt2.proc.static_step @compute<4> { ... } {wrapper_generated}
cmt2.instance @__fsm_compute = @Reg1(...)
cmt2.rule @compute__tick () -> () { ... }
cmt2.value @compute__done () -> (!firrtl.uint<1>) { ... }
cmt2.rule @compute__start () -> () { ... }
```

---

## Timing Passes

### cmt2-timing-inference

Infers cycle-precise timing for methods and steps.

```bash
circt-opt input.mlir -cmt2-timing-inference
```

**Options:**
- `--infer-call-timing`: Infer timing for calls from method contracts
- `--promote-to-static`: Promote methods to static when timing is deterministic

**Effects:**
- Post-order module traversal
- Bottom-up timing propagation
- Adds `arg_timing` and `result_timing` attributes

### cmt2-timing-validation

Validates timing constraints.

```bash
circt-opt input.mlir -cmt2-timing-validation
```

**Options:**
- `--strict`: Require explicit timing on all calls

**Validates:**
1. Call-site timing compatibility with method contracts
2. Pipelined call spacing respects initiation interval
3. Step timing consistency
4. Result timing after output latency

**Error Examples:**
```
error: call timing [5, 6) exceeds step latency 4
error: pipelined calls must be at least 2 cycles apart
error: result timing [1, 2) before method output latency 3
```

### cmt2-static-fsm-allocation

Maps timing intervals to FSM states.

```bash
circt-opt input.mlir -cmt2-static-fsm-allocation
```

**Options:**
- `--one-hot-cutoff=N`: Use one-hot encoding for ≤N states (default: 8)

**Effects:**
1. Allocates FSM states for each cycle
2. Computes bitwidth (binary or one-hot)
3. Performs FSM sharing analysis (graph coloring)
4. Annotates steps with FSM metadata

**Generated Attributes:**
```mlir
{
    fsm_states = 4 : i64,
    fsm_bitwidth = 4 : i64,
    fsm_encoding = "one_hot"
}
```

---

## Complete Pipeline

### Procedural to GAA

```bash
circt-opt input.mlir \
    -cmt2-compile-invoke \
    -cmt2-tdcc \
    -cmt2-static-inference \
    -cmt2-static-promotion \
    -cmt2-compile-static \
    -cmt2-proc-stmt-to-action \
    -cmt2-proc-to-gaa
```

### With Timing Analysis

```bash
circt-opt input.mlir \
    -cmt2-timing-inference \
    -cmt2-timing-validation \
    -cmt2-static-fsm-allocation \
    -cmt2-compile-static
```

### Full Pipeline (PyCMT2)

```python
# In PyCMT2, the full pipeline is automatic:
verilog = circuit.emit_verilog()  # Runs all passes
```

---

## Pass Dependencies

```
┌─────────────────┐
│ cmt2-compile-   │
│ invoke          │
└────────┬────────┘
         │
┌────────▼────────┐
│ cmt2-tdcc       │
└────────┬────────┘
         │
┌────────▼────────┐     ┌─────────────────┐
│ cmt2-static-    │────►│ cmt2-timing-    │
│ inference       │     │ inference       │
└────────┬────────┘     └────────┬────────┘
         │                       │
┌────────▼────────┐     ┌────────▼────────┐
│ cmt2-static-    │     │ cmt2-timing-    │
│ promotion       │     │ validation      │
└────────┬────────┘     └────────┬────────┘
         │                       │
         └───────────┬───────────┘
                     │
         ┌───────────▼───────────┐
         │ cmt2-static-fsm-      │
         │ allocation            │
         └───────────┬───────────┘
                     │
         ┌───────────▼───────────┐
         │ cmt2-compile-static   │
         └───────────┬───────────┘
                     │
         ┌───────────▼───────────┐
         │ cmt2-proc-stmt-to-    │
         │ action                │
         └───────────┬───────────┘
                     │
         ┌───────────▼───────────┐
         │ cmt2-proc-to-gaa      │
         └───────────────────────┘
```

---

## Summary Table

| Pass | Category | Purpose |
|------|----------|---------|
| `cmt2-print-instance-graph` | Analysis | Module hierarchy |
| `cmt2-print-call-info` | Analysis | Call patterns |
| `cmt2-print-conflict-matrix` | Analysis | Scheduling conflicts |
| `cmt2-inline-modules` | Optimization | Flatten hierarchy |
| `cmt2-inline-private-funcs` | Optimization | Inline internal calls |
| `cmt2-control-collapsing` | Optimization | Simplify control |
| `cmt2-verify-private-funcs-inlined` | Verification | Check inlining |
| `cmt2-verify-call-sequence` | Verification | Check scheduling |
| `cmt2-compile-invoke` | Procedural | Lower invoke |
| `cmt2-tdcc` | Procedural | Generate FSM |
| `cmt2-proc-stmt-to-action` | Procedural | To FSM rules |
| `cmt2-proc-to-gaa` | Procedural | To GAA |
| `cmt2-static-inference` | Static | Infer latencies |
| `cmt2-static-promotion` | Static | Promote to static |
| `cmt2-compile-static` | Static | Generate FSM HW |
| `cmt2-timing-inference` | Timing | Infer timing |
| `cmt2-timing-validation` | Timing | Validate timing |
| `cmt2-static-fsm-allocation` | Timing | Allocate FSM states |
