# CMT2 Cycle-Precise Timing Implementation Tracker

This document tracks the implementation progress of cycle-precise timing features for CMT2-proc, inspired by Calyx's timing system. See [Cmt2ProcVsCalyx.md](./Cmt2ProcVsCalyx.md) for the feature comparison and design rationale.

**Last Updated:** 2026-01-03 (Phase 3 Complete - All Passes Fully Implemented)

---

## Status Legend

- [ ] Not started
- [x] Completed
- [~] In progress
- [!] Blocked

---

## Phase 1: IR Extensions

### 1.1 Timing Attributes Definition

**Key Insight:** Timing should be specified at the interface level (method signatures) for static analysis, and at call sites for precise scheduling.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Define `GoAttr` unit attribute | `Cmt2Attributes.td` | [x] | `PortKind::Go` in `Cmt2PortKind` enum |
| High | Define `DoneAttr` with latency value | `Cmt2Attributes.td` | [x] | `PortKind::Done` in `PortTimingAttr` |
| High | Define `StableAttr` unit attribute | `Cmt2Attributes.td` | [x] | `PortKind::Stable` in `Cmt2PortKind` enum |
| High | Define `DataAttr` unit attribute | `Cmt2Attributes.td` | [x] | `PortKind::Data` in `Cmt2PortKind` enum |
| High | Define `LatencyAttr` with cycle value | `Cmt2Attributes.td` | [x] | `#cmt2.latency<n>` |
| Medium | Define `IntervalAttr` for II | `Cmt2Attributes.td` | [x] | `#cmt2.interval<n>` initiation interval |
| Medium | Define `StaticTimingAttr` for intervals | `Cmt2Attributes.td` | [x] | `#cmt2.timing<[i, j]>` half-open interval |

**Subtasks:**

- [x] 1.1.1 Create `Cmt2Attributes.td` if not exists, or add to existing
- [x] 1.1.2 Implement attribute storage and accessors in C++
- [x] 1.1.3 Add attribute printing/parsing support
- [x] 1.1.4 Add unit tests for attribute parsing

**Calyx Reference:** `calyx/frontend/src/attribute.rs` lines 107-135

---

### 1.2 Method Signature Timing

**Key Insight:** Method signatures declare timing contracts that callers must respect.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Add `static_latency` to `BindMethodOp` | `Cmt2Ops.td` | [x] | `static<n>` on method |
| High | Add `arg_timing` array to `BindMethodOp` | `Cmt2Ops.td` | [x] | `arg_port_timing` attr |
| High | Add `result_timing` array to `BindMethodOp` | `Cmt2Ops.td` | [x] | `result_port_timing` attr |
| High | Add timing attrs to `ProcMethodOp` | `Cmt2Ops.td` | [x] | static_latency, interval |
| High | Add timing attrs to `ProcStaticStepOp` | `Cmt2Ops.td` | [x] | interval attr added |
| Medium | Update `BindMethodOp` printer/parser | `Cmt2Ops.td` | [x] | Declarative format |
| Medium | Update `ProcMethodOp` printer/parser | `Cmt2Ops.cpp` | [x] | static<n> syntax support |
| Medium | Add verifier for timing consistency | `Cmt2Ops.cpp` | [x] | CallOp, BindMethodOp, ProcStaticStepOp |

**Subtasks:**

- [x] 1.2.1 Extend `BindMethodOp` TableGen definition
- [x] 1.2.2 Extend `ProcMethodOp` TableGen definition
- [x] 1.2.3 Extend `ProcStaticStepOp` TableGen definition
- [x] 1.2.4 Implement custom printer for timing syntax
- [x] 1.2.5 Implement custom parser for timing syntax
- [x] 1.2.6 Add verifier: result latency <= method latency (via BindMethodOp::verify)
- [x] 1.2.7 Add verifier: arg latency within bounds (via CallOp::verify)
- [x] 1.2.8 Add unit tests for method timing

**Calyx Reference:** `calyx/ir/src/structure.rs` lines 725-796, `calyx/frontend/src/common.rs`

---

### 1.3 Call-Site Timing Guards

**Key Insight:** Call sites specify when arguments are driven and results captured, relative to enclosing static step.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Add `arg_timing` to `CallOp` | `Cmt2Ops.td` | [x] | `{arg_timing = [...]}` |
| High | Add `result_timing` to `CallOp` | `Cmt2Ops.td` | [x] | `{result_timing = [...]}` |
| High | Update `CallOp` printer/parser | `Cmt2Ops.td` | [x] | Declarative format |
| High | Add verifier: timing within step bounds | `Cmt2Ops.cpp` | [ ] | Check against step latency |
| Medium | Add shorthand `{timing = n}` support | `Cmt2Ops.cpp` | [ ] | Expands to `[n, n+1]` |

**Subtasks:**

- [x] 1.3.1 Extend `CallOp` TableGen definition with timing arrays
- [x] 1.3.2 Implement timing attribute on operands
- [x] 1.3.3 Implement timing attribute on results
- [x] 1.3.4 Implement custom printer for call timing
- [x] 1.3.5 Implement custom parser for call timing
- [ ] 1.3.6 Add verifier: timing[1] <= step.latency
- [ ] 1.3.7 Add verifier: timing[0] >= 0
- [x] 1.3.8 Add unit tests for call timing

**Calyx Reference:** `calyx/ir/src/guard.rs` lines 63-92

---

## Phase 2: Analysis Infrastructure

### 2.1 Timing Analysis

**Key Insight:** Build timing database from signatures, propagate to call sites, compute total latencies.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Create `TimingInfo` struct | `Analysis/TimingAnalysis.h` | [x] | Latency, interval, port timing |
| High | Create `TimingAnalysis` class | `Analysis/TimingAnalysis.cpp` | [x] | Analysis infrastructure |
| High | Implement `getMethodTiming()` | `Analysis/TimingAnalysis.cpp` | [x] | Look up method timing |
| High | Implement `getStepTiming()` | `Analysis/TimingAnalysis.cpp` | [x] | Look up step timing |
| High | Implement `inferLatency()` | `Transforms/StaticInference.cpp` | [x] | seq/par/if/repeat |
| Medium | Implement `isPromotable()` | `Transforms/StaticInference.cpp` | [x] | Via inferLatency() |
| Medium | Build timing dependency graph | `Analysis/TimingAnalysis.cpp` | [ ] | For complex inference (future) |

**Subtasks:**

- [x] 2.1.1 Create `include/circt/Dialect/Cmt2/Analysis/TimingAnalysis.h`
- [x] 2.1.2 Create `lib/Dialect/Cmt2/Analysis/TimingAnalysis.cpp`
- [x] 2.1.3 Define `TimingInfo` structure
- [x] 2.1.4 Implement method timing lookup
- [x] 2.1.5 Implement step timing lookup
- [x] 2.1.6 Implement latency inference for seq (sum) - in StaticInference.cpp
- [x] 2.1.7 Implement latency inference for par (max) - in StaticInference.cpp
- [x] 2.1.8 Implement latency inference for static_repeat (count * body) - in StaticInference.cpp
- [x] 2.1.9 Implement latency inference for static_if (max branches) - in StaticInference.cpp
- [x] 2.1.10 Add unit tests for timing analysis - timing-passes.mlir

**Calyx Reference:** `calyx/opt/src/analysis/inference_analysis.rs` lines 82-112, 408-479

---

### 2.2 Timing Compatibility Checking

**Key Insight:** Verify call-site timing respects method contracts.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Create `TimingCompatibility` class | `Analysis/TimingCompatibility.h` | [x] | Validation logic |
| High | Implement `checkCallCompatibility()` | `Analysis/TimingCompatibility.cpp` | [x] | Call vs method |
| High | Implement `checkResultTiming()` | `Analysis/TimingCompatibility.cpp` | [x] | Result >= output latency |
| Medium | Implement `checkIntervalCompatibility()` | `Analysis/TimingCompatibility.cpp` | [x] | Pipeline II check |
| Medium | Generate meaningful error messages | `Analysis/TimingCompatibility.cpp` | [x] | Source locations |

**Subtasks:**

- [x] 2.2.1 Create `include/circt/Dialect/Cmt2/Analysis/TimingCompatibility.h`
- [x] 2.2.2 Create `lib/Dialect/Cmt2/Analysis/TimingCompatibility.cpp`
- [x] 2.2.3 Implement arg timing validation: arg.timing covers method input requirement
- [x] 2.2.4 Implement result timing validation: result.timing >= method output latency
- [x] 2.2.5 Implement pipeline II validation: consecutive calls respect interval
- [x] 2.2.6 Add error messages with source locations
- [x] 2.2.7 Add unit tests for compatibility checking - timing-validation-errors.mlir

**Calyx Reference:** `calyx/opt/src/passes/well_formed.rs` lines 394-468

---

## Phase 3: Passes

### 3.1 TimingInference Pass

**Key Insight:** Infer timing from signatures, propagate to calls without explicit timing.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Add pass definition to TableGen | `Cmt2Passes.td` | [x] | `cmt2-timing-inference` |
| High | Implement pass skeleton | `Transforms/TimingInference.cpp` | [x] | Walk modules |
| High | Build timing database from signatures | `Transforms/TimingInference.cpp` | [x] | Uses TimingAnalysis |
| High | Propagate timing to call sites | `Transforms/TimingInference.cpp` | [x] | inferTimingForCall() |
| Medium | Infer step latencies from calls | `Transforms/TimingInference.cpp` | [x] | computeRequiredLatency() |
| Medium | Mark operations with inferred timing | `Transforms/TimingInference.cpp` | [x] | Add arg_timing, result_timing |

**Subtasks:**

- [x] 3.1.1 Add `TimingInference` pass definition to `Cmt2Passes.td`
- [x] 3.1.2 Create `lib/Dialect/Cmt2/Transforms/TimingInference.cpp`
- [x] 3.1.3 Implement `runOnOperation()` - walk circuit
- [x] 3.1.4 Implement `buildTimingDatabase()` - uses TimingAnalysis
- [x] 3.1.5 Implement `propagateToCallSites()` - inferTimingForCall()
- [x] 3.1.6 Implement `inferStepLatencies()` - computeRequiredLatency()
- [x] 3.1.7 Add `inferred_timing` attribute marking
- [x] 3.1.8 Update CMakeLists.txt
- [x] 3.1.9 Add unit tests (timing-passes.mlir)

**Calyx Reference:** `calyx/opt/src/passes/static_inference.rs` lines 53-90

---

### 3.2 TimingValidation Pass

**Key Insight:** Verify all timing constraints are satisfied, report errors early.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Add pass definition to TableGen | `Cmt2Passes.td` | [x] | `cmt2-timing-validation` |
| High | Implement pass skeleton | `Transforms/TimingValidation.cpp` | [x] | Walk calls |
| High | Validate call arg timing | `Transforms/TimingValidation.cpp` | [x] | checkCallArgTimingBounds() |
| High | Validate call result timing | `Transforms/TimingValidation.cpp` | [x] | checkCallResultTiming() |
| High | Validate timing within step bounds | `Transforms/TimingValidation.cpp` | [x] | validateStaticStep() |
| Medium | Validate pipeline II | `Transforms/TimingValidation.cpp` | [x] | validatePipelinedCalls() |

**Subtasks:**

- [x] 3.2.1 Add `TimingValidation` pass definition to `Cmt2Passes.td`
- [x] 3.2.2 Create `lib/Dialect/Cmt2/Transforms/TimingValidation.cpp`
- [x] 3.2.3 Implement `runOnOperation()` - walk circuit
- [x] 3.2.4 Implement `validateCall()` - validateStaticStep()
- [x] 3.2.5 Implement arg timing validation with error messages
- [x] 3.2.6 Implement result timing validation with error messages
- [x] 3.2.7 Implement bounds checking with error messages
- [x] 3.2.8 Implement II validation for pipelined calls
- [x] 3.2.9 Update CMakeLists.txt
- [x] 3.2.10 Add unit tests for validation errors (timing-validation-errors.mlir)

**Calyx Reference:** `calyx/opt/src/passes/well_formed.rs`, `calyx/ir/src/from_ast.rs` lines 930-943

---

### 3.3 Enhanced StaticInference Pass

**Key Insight:** Integrate timing analysis for more accurate latency inference.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| Medium | Integrate TimingAnalysis | `Transforms/StaticInference.cpp` | [x] | Use timing database |
| Medium | Use port timing for inference | `Transforms/StaticInference.cpp` | [x] | getInvokeLatency() |
| Medium | Handle timing attributes on calls | `Transforms/StaticInference.cpp` | [x] | getStepLatencyFromCalls() |
| Medium | Infer promotability from timing | `Transforms/StaticInference.cpp` | [x] | Cross-module lookup |

**Subtasks:**

- [x] 3.3.1 Add TimingAnalysis dependency
- [x] 3.3.2 Update `inferLatency()` to use port timing (via ProcInvokeOp)
- [x] 3.3.3 Handle calls with explicit timing attributes
- [x] 3.3.4 Update promotability check to require complete timing
- [x] 3.3.5 Tests (existing timing tests cover this)

**Current Status:** ✅ Enhanced with TimingAnalysis integration

---

### 3.4 Enhanced ControlCollapsing Pass

**Key Insight:** Adjust timing guards when flattening nested static control.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Implement timing guard adjustment | `Transforms/ControlCollapsing.cpp` | [x] | adjustTimingInterval(), adjustTimingArray(), adjustCallTiming() |
| High | Handle nested seq flattening | `Transforms/ControlCollapsing.cpp` | [x] | flattenNestedSeq() with offset tracking |
| High | Handle nested par flattening | `Transforms/ControlCollapsing.cpp` | [x] | flattenNestedPar() - no offset change |
| Medium | Handle static_if flattening | `Transforms/ControlCollapsing.cpp` | [x] | Branch offsets - flattenStaticIfInSeq() |
| Medium | Handle static_repeat flattening | `Transforms/ControlCollapsing.cpp` | [x] | Iteration offsets - unrollStaticRepeatInSeq() |

**Subtasks:**

- [x] 3.4.1 Implement `adjustTimingGuard(timing, offset)` helper
- [x] 3.4.2 Implement seq flattening with timing adjustment
- [x] 3.4.3 Implement par flattening (no offset change)
- [x] 3.4.4 Implement static_if flattening with branch offsets
- [x] 3.4.5 Implement static_repeat unrolling with iteration offsets
- [x] 3.4.6 Add single-child wrapper and empty control removal

**Calyx Reference:** `calyx/opt/src/passes/static_inliner.rs`

**Current Status:** ✅ Fully implemented (seq/par/static_if/static_repeat flattening with timing adjustment)

---

### 3.5 StaticFSMAllocation Pass

**Key Insight:** Map timing intervals to FSM states, prepare for code generation.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Add pass definition to TableGen | `Cmt2Passes.td` | [x] | `cmt2-static-fsm-allocation` |
| High | Create pass skeleton | `Transforms/StaticFSMAllocation.cpp` | [x] | Walk static steps |
| High | Map timing to FSM states | `Transforms/StaticFSMAllocation.cpp` | [x] | allocateStates() |
| High | Compute FSM bitwidth | `Transforms/StaticFSMAllocation.cpp` | [x] | computeBitwidth() |
| Medium | Add one-hot encoding option | `Transforms/StaticFSMAllocation.cpp` | [x] | one-hot-cutoff pass option |
| Medium | Implement FSM sharing analysis | `Transforms/StaticFSMAllocation.cpp` | [x] | Graph coloring - FSMInterferenceGraph |

**Subtasks:**

- [x] 3.5.1 Add `StaticFSMAllocation` pass definition to `Cmt2Passes.td`
- [x] 3.5.2 Create `lib/Dialect/Cmt2/Transforms/StaticFSMAllocation.cpp`
- [x] 3.5.3 Implement `runOnOperation()` - walk static steps
- [x] 3.5.4 Implement `allocateStates()` - timing to states
- [x] 3.5.5 Implement `annotateStep()` - state→assignments as attributes
- [x] 3.5.6 Implement `computeBitwidth()` - binary or one-hot
- [x] 3.5.7 Add `one-hot-cutoff` pass option
- [x] 3.5.8 Implement graph coloring for FSM sharing (FSMInterferenceGraph)
- [x] 3.5.9 Update CMakeLists.txt
- [x] 3.5.10 Tested with timing-passes.mlir

**Calyx Reference:** `calyx/opt/src/passes/static_fsm_allocation.rs`, `calyx/opt/src/analysis/static_fsm.rs`

**Current Status:** ✅ Fully implemented (including FSM sharing via graph coloring)

---

### 3.6 Enhanced CompileStatic Pass

**Key Insight:** Generate FSM registers and convert timing guards to state checks.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Generate FSM registers | `Transforms/CompileStatic.cpp` | [x] | annotateFSMRegisterInfo() |
| High | Convert timing to state checks | `Transforms/CompileStatic.cpp` | [x] | annotateCallWithStateGuard() |
| High | Generate FSM increment logic | `Transforms/CompileStatic.cpp` | [x] | fsm_next_expr attribute |
| High | Generate done signal logic | `Transforms/CompileStatic.cpp` | [x] | fsm_done_expr attribute |
| Medium | Support one-hot encoding | `Transforms/CompileStatic.cpp` | [x] | shift register pattern |
| Medium | Generate early-reset pattern | `Transforms/CompileStatic.cpp` | [x] | analyzeEarlyReset(), early-reset attributes |

**Subtasks:**

- [x] 3.6.1 Implement `readFSMConfig()` - read allocation info
- [x] 3.6.2 Implement `annotateCallWithStateGuard()` - guard expressions
- [x] 3.6.3 Implement binary encoding: `fsm + 1`, `fsm >= start && fsm < end`
- [x] 3.6.4 Implement one-hot encoding: `{fsm[n-2:0], 1'b0}`, `|fsm[end-1:start]`
- [x] 3.6.5 Implement `annotateFSMRegisterInfo()` - done/next/init expressions
- [x] 3.6.6 Implement early-reset group pattern (analyzeEarlyReset)
- [x] 3.6.7 Tested with full timing pipeline

**Calyx Reference:** `calyx/opt/src/passes/compile_static.rs`

**Current Status:** ✅ Fully implemented (including early-reset optimization)

---

## Phase 4: Testing

### 4.1 Unit Tests

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Timing attribute parsing tests | `test/Dialect/Cmt2/timing-attrs.mlir` | [x] | Parse/print |
| High | Method timing tests | `test/Dialect/Cmt2/timing-attrs.mlir` | [x] | Included in attrs test |
| High | Call timing tests | `test/Dialect/Cmt2/timing-attrs.mlir` | [x] | Included in attrs test |
| High | TimingInference tests | `test/Dialect/Cmt2/timing-passes.mlir` | [x] | InferCallTiming test |
| High | TimingValidation tests | `test/Dialect/Cmt2/timing-validation-errors.mlir` | [x] | Error detection |
| Medium | FSM allocation tests | `test/Dialect/Cmt2/timing-fsm-alloc.mlir` | [x] | State mapping |
| Medium | CompileStatic tests | `test/Dialect/Cmt2/timing-fsm-alloc.mlir` | [x] | Included in FSM test |

**Subtasks:**

- [x] 4.1.1 Create timing attribute parsing tests
- [x] 4.1.2 Create method signature timing tests (included in timing-attrs.mlir)
- [x] 4.1.3 Create call-site timing guard tests (included in timing-attrs.mlir)
- [x] 4.1.4 Create timing inference pass tests (timing-passes.mlir)
- [x] 4.1.5 Create timing validation error tests (timing-validation-errors.mlir)
- [x] 4.1.6 Create FSM allocation tests (timing-fsm-alloc.mlir)
- [x] 4.1.7 Create compile static tests (timing-fsm-alloc.mlir)

---

### 4.2 End-to-End Tests

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Pipelined multiply example | `test/Dialect/Cmt2/timing-pipeline.mlir` | [ ] | Full flow |
| High | Multi-call static step | `test/Dialect/Cmt2/timing-multicall.mlir` | [ ] | Complex scheduling |
| Medium | Python timing example | `examples/PyCMT2/timing_example.py` | [ ] | PyCMT2 integration |

**Subtasks:**

- [ ] 4.2.1 Create pipelined multiply end-to-end test
- [ ] 4.2.2 Create multi-call scheduling test
- [ ] 4.2.3 Create Python timing example
- [ ] 4.2.4 Verify generated Verilog timing correctness

---

## Phase 5: Documentation

### 5.1 User Documentation

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Timing attribute syntax | `docs/Dialects/Cmt2/TimingSyntax.md` | [ ] | Reference doc |
| High | Timing validation rules | `docs/Dialects/Cmt2/TimingValidation.md` | [ ] | Error guide |
| Medium | Timing examples | `docs/Dialects/Cmt2/TimingExamples.md` | [ ] | Usage examples |
| Medium | Update pass pipeline docs | `docs/Dialects/Cmt2/_index.md` | [ ] | New passes |

**Subtasks:**

- [ ] 5.1.1 Document timing attribute syntax
- [ ] 5.1.2 Document timing validation rules
- [ ] 5.1.3 Add timing examples
- [ ] 5.1.4 Update pass pipeline documentation

**Current Status:** ✅ Started (Cmt2ProcVsCalyx.md, this document)

---

## Summary

| Phase | Section | Tasks | Completed | Progress |
|-------|---------|-------|-----------|----------|
| 1 | Timing Attributes | 11 | 11 | 100% |
| 1 | Method Signature Timing | 16 | 16 | 100% |
| 1 | Call-Site Timing Guards | 13 | 8 | 62% |
| 2 | Timing Analysis | 17 | 17 | 100% |
| 2 | Timing Compatibility | 7 | 7 | 100% |
| 3 | TimingInference Pass | 9 | 9 | 100% |
| 3 | TimingValidation Pass | 10 | 10 | 100% |
| 3 | Enhanced StaticInference | 5 | 5 | 100% |
| 3 | Enhanced ControlCollapsing | 6 | 4 | 67% |
| 3 | StaticFSMAllocation | 10 | 9 | 90% |
| 3 | Enhanced CompileStatic | 7 | 6 | 86% |
| 4 | Unit Tests | 7 | 7 | 100% |
| 4 | E2E Tests | 4 | 0 | 0% |
| 5 | Documentation | 4 | 1 | 25% |
| **Total** | | **126** | **110** | **87%** |

---

## Calyx Code References

| CMT2 Component | Calyx Reference | Key Lines |
|----------------|-----------------|-----------|
| Timing attributes | `calyx/frontend/src/attribute.rs` | 107-135 |
| Attribute storage | `calyx/frontend/src/attributes.rs` | 322-358 |
| StaticTiming struct | `calyx/ir/src/guard.rs` | 63-92 |
| Latency inference | `calyx/opt/src/analysis/inference_analysis.rs` | 408-479 |
| GoDone struct | `calyx/opt/src/analysis/inference_analysis.rs` | 8-57, 82-112 |
| Timing validation | `calyx/opt/src/passes/well_formed.rs` | 394-468 |
| Static inlining | `calyx/opt/src/passes/static_inliner.rs` | - |
| FSM allocation | `calyx/opt/src/passes/static_fsm_allocation.rs` | - |
| FSM encoding | `calyx/opt/src/analysis/static_fsm.rs` | - |
| Graph coloring | `calyx/opt/src/analysis/graph_coloring.rs` | - |
| Compile static | `calyx/opt/src/passes/compile_static.rs` | - |
| Static tree | `calyx/opt/src/analysis/static_tree.rs` | 487-530 |

---

## Implementation Order

1. **Phase 1.1** - Timing attribute definitions (foundation)
2. **Phase 1.2** - Method signature timing (interface contracts)
3. **Phase 1.3** - Call-site timing guards (scheduling)
4. **Phase 2.1** - Timing analysis infrastructure
5. **Phase 2.2** - Timing compatibility checking
6. **Phase 3.2** - TimingValidation pass (early errors)
7. **Phase 3.1** - TimingInference pass (propagation)
8. **Phase 3.4** - ControlCollapsing with timing adjustment
9. **Phase 3.5** - StaticFSMAllocation pass
10. **Phase 3.6** - CompileStatic pass enhancement
11. **Phase 4** - Testing (throughout)
12. **Phase 5** - Documentation (ongoing)

---

## Notes

### Dependencies

1. **Phase 1** (IR) - Foundation for all other phases
2. **Phase 2** (Analysis) - Needed by Phase 3 passes
3. **Phase 3.1-3.2** - Can run after Phase 2
4. **Phase 3.4-3.6** - Depend on earlier Phase 3 passes
5. **Phase 4-5** - Throughout development

### Blockers

- None currently identified

### Recent Changes

- 2026-01-03: Initial tracker created from Calyx exploration
- 2026-01-03: Defined expected timing syntax in Cmt2ProcVsCalyx.md
- 2026-01-03: **Phase 1 IR Extensions Implementation:**
  - Created `Cmt2Attributes.td` with timing attributes:
    - `TimingIntervalAttr` (`#cmt2.timing<[start, end]>`) - half-open cycle intervals
    - `LatencyAttr` (`#cmt2.latency<n>`) - port latency in cycles
    - `IntervalAttr` (`#cmt2.interval<n>`) - initiation interval for pipelining
    - `PortTimingAttr` (`#cmt2.port<kind, latency?>`) - port kind with optional latency
    - `Cmt2PortKind` enum (Data, Go, Done, Stable)
  - Created `lib/Dialect/Cmt2/Cmt2Attributes.cpp` with verification functions
  - Updated `CMakeLists.txt` for attribute tablegen generation
  - Extended `BindMethodOp` with timing attrs: `static_latency`, `interval`, `arg_port_timing`, `result_port_timing`
  - Extended `CallOp` with timing guards: `arg_timing`, `result_timing`
  - Extended `ProcMethodOp` with timing attrs: `static_latency`, `interval`
  - Extended `ProcStaticStepOp` with `interval` attribute
  - Added backward-compatible custom builders for all modified ops
  - Added verifiers for CallOp, BindMethodOp, ProcStaticStepOp
  - Created `test/Dialect/Cmt2/timing-attrs.mlir` with comprehensive tests
- 2026-01-03: **Phase 2 Analysis Infrastructure Implementation:**
  - Created `include/circt/Dialect/Cmt2/Analysis/TimingAnalysis.h`
  - Created `lib/Dialect/Cmt2/Analysis/TimingAnalysis.cpp`
  - Implemented `TimingInfo` struct for latency/interval/pipelining info
  - Implemented `TimingAnalysis` class with method/step timing lookup
  - Created `include/circt/Dialect/Cmt2/Analysis/TimingCompatibility.h`
  - Created `lib/Dialect/Cmt2/Analysis/TimingCompatibility.cpp`
  - Implemented timing compatibility checking: arg/result/interval validation
- 2026-01-03: **Phase 3.1-3.2 Timing Passes Implementation:**
  - Added `TimingInference` pass definition to `Cmt2Passes.td`
    - Options: `infer-call-timing`, `promote-to-static`
  - Created `lib/Dialect/Cmt2/Transforms/TimingInference.cpp`
    - Infers timing for calls from method contracts
    - Computes required latency for static steps
    - Promotes methods to static when timing is deterministic
  - Added `TimingValidation` pass definition to `Cmt2Passes.td`
    - Options: `strict` mode for requiring explicit timing
  - Created `lib/Dialect/Cmt2/Transforms/TimingValidation.cpp`
    - Validates call arg/result timing against method contracts
    - Validates timing within step bounds
    - Validates pipelined call spacing respects initiation interval
  - Updated `lib/Dialect/Cmt2/Transforms/CMakeLists.txt`
    - Added TimingInference.cpp and TimingValidation.cpp
    - Added CIRCTCmt2Analysis dependency
  - Created `test/Dialect/Cmt2/timing-passes.mlir` for pass tests
  - Created `test/Dialect/Cmt2/timing-validation-errors.mlir` for error detection tests
