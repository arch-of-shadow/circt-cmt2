# Pipelined Design Implementation - Test Coverage

**Reference:** [tmp/PipelinedDesign-Implementation.md](tmp/PipelinedDesign-Implementation.md)

---

## Test Files

| File | Purpose |
|------|---------|
| `test/Dialect/Cmt2/sync-token-type.mlir` | Phase 1: Token type and operations |
| `test/Dialect/Cmt2/token-verifier-errors.mlir` | Phase 1: Verifier error cases |
| `test/Dialect/Cmt2/dataflow-ops.mlir` | Phase 2: Dataflow operation parsing |
| `test/Dialect/Cmt2/dataflow-lowering.mlir` | Phase 2: Dataflow-to-rules lowering |
| `test/Dialect/Cmt2/token-lowering.mlir` | Phase 4: Token lowering (LS/LI modes, fork) |

---

## Phase 1: Core Token Infrastructure

### T1: SyncTokenType

| Test Case | Status | File | Line |
|-----------|--------|------|------|
| Token without data: `!cmt2.sync_token` | [x] | sync-token-type.mlir | 8 |
| Token with data: `!cmt2.sync_token<data = !firrtl.uint<32>>` | [x] | sync-token-type.mlir | 10 |
| LS mode (default, elided): `mode = ls` | [x] | sync-token-type.mlir | 13 |
| LI mode: `mode = li` | [x] | sync-token-type.mlir | 15 |
| Different data widths (8, 16, 32-bit) | [x] | sync-token-type.mlir | 10-15 |

### T2: Token Operations

| Test Case | Status | File | Line |
|-----------|--------|------|------|
| `cmt2.token.valid` - extract valid signal | [x] | sync-token-type.mlir | 23-28 |
| `cmt2.token.data` - extract data payload | [x] | sync-token-type.mlir | 31-36 |
| `cmt2.token.create` - without data | [x] | sync-token-type.mlir | 41 |
| `cmt2.token.create` - with data | [x] | sync-token-type.mlir | 43 |
| `cmt2.token.join` - multiple tokens | [x] | sync-token-type.mlir | 49-57 |

### T3: RuleOp with Token Signatures

| Test Case | Status | File | Line |
|-----------|--------|------|------|
| Basic rule without tokens | [x] | sync-token-type.mlir | 65-69 |
| Rule with `tokens_in` only | [x] | sync-token-type.mlir | 92-98 |
| Rule with `tokens_in` and `tokens_out` | [x] | sync-token-type.mlir | 75-87 |
| Rule with both regular args and tokens | [x] | sync-token-type.mlir | 92-98 |
| Token validity check in guard | [x] | sync-token-type.mlir | 81 |
| Token data extraction in body | [x] | sync-token-type.mlir | 85 |
| Multiple token inputs | [x] | sync-token-type.mlir | 76-77 |

### T4: Verifiers

| Test Case | Status | File | Line |
|-----------|--------|------|------|
| TokenValidOp: result must be 1-bit | N/A | - | - |
| TokenDataOp: token must have data | [x] | token-verifier-errors.mlir | 8-12 |
| TokenDataOp: result type must match token data | [x] | token-verifier-errors.mlir | 17-21 |
| TokenCreateOp: token with data but no operand | [x] | token-verifier-errors.mlir | 26-30 |
| TokenCreateOp: data type must match token | [x] | token-verifier-errors.mlir | 35-39 |
| TokenJoinOp: must have at least one input | [x] | token-verifier-errors.mlir | 44-48 |

---

## Phase 2: Dataflow Construct

### D1: Dataflow Operations

| Test Case | Status | File | Line |
|-----------|--------|------|------|
| `cmt2.proc.dataflow` - basic parsing | [x] | dataflow-ops.mlir | 11 |
| `cmt2.proc.dataflow` - with arguments | [x] | dataflow-ops.mlir | 11 |
| `cmt2.proc.dataflow` - with results | [x] | dataflow-ops.mlir | 11 |
| `cmt2.proc.dataflow` - with interval attr | [x] | dataflow-ops.mlir | 80 |
| `cmt2.dataflow.task` - no token inputs | [x] | dataflow-ops.mlir | 14 |
| `cmt2.dataflow.task` - with token inputs | [x] | dataflow-ops.mlir | 23 |
| `cmt2.dataflow.task` - with token outputs | [x] | dataflow-ops.mlir | 14, 23 |
| `cmt2.dataflow.task` - with timing attr | [x] | dataflow-ops.mlir | 81, 87 |
| `cmt2.dataflow.yield` - yield tokens | [x] | dataflow-ops.mlir | 17, 29 |
| `cmt2.dataflow.return` - return values | [x] | dataflow-ops.mlir | 36, 74 |
| Linear pipeline pattern | [x] | dataflow-ops.mlir | 11-38 |
| Fork pattern (multi-consumer token) | [x] | dataflow-ops.mlir | 49-66 |
| Join pattern (multi-input task) | [x] | dataflow-ops.mlir | 69-75 |

### D2: ProcPipelineOp

| Test Case | Status | Notes |
|-----------|--------|-------|
| (Deferred) | N/A | Existing `static_step` suffices |

### D3: Dataflow-to-Rules Lowering

| Test Case | Status | File | Line |
|-----------|--------|------|------|
| Task → Rule conversion | [x] | dataflow-lowering.mlir | 14-20 |
| Token inputs → rule token args | [x] | dataflow-lowering.mlir | 17-18 |
| Token outputs → rule token results | [x] | dataflow-lowering.mlir | 15-16 |
| Guard with token.valid checks | [x] | dataflow-lowering.mlir | (implicit) |
| Body cloning with value mapping | [x] | dataflow-lowering.mlir | (implicit) |
| `dataflow.lowered` attribute added | [x] | dataflow-lowering.mlir | 22-23 |
| Fork-join lowering | [x] | dataflow-lowering.mlir | 50-63 |
| Timing attribute preservation | [x] | dataflow-lowering.mlir | 107-113 |

---

## Phase 3: Analysis

| Task | Implementation | Test File | Status |
|------|----------------|-----------|--------|
| A1: TokenAnalysis | Analysis/TokenAnalysis.cpp | - | [x] Implemented, tests pending |
| A2: FIFO depth inference | Analysis/FIFODepthAnalysis.cpp | - | [x] Implemented, tests pending |
| A3: Deadlock detection | Analysis/DeadlockAnalysis.cpp | - | [x] Implemented, tests pending |
| A4: Timing inference | Transforms/TimingInference.cpp | - | [x] Extended for dataflow, tests pending |

---

## Phase 4: Lowering

| Task | Implementation | Test File | Status |
|------|----------------|-----------|--------|
| L1: Token lowering (LS→shift reg, LI→FIFO) | Transforms/TokenLowering.cpp | token-lowering.mlir | [x] Complete |
| L2: Stall controller generation | Transforms/StallControllerGen.cpp | - | [x] Implemented |
| L3: Multi-consumer fork lowering | Transforms/TokenLowering.cpp | token-lowering.mlir | [x] Complete |
| L4: cmt2-to-firrtl integration | Transforms/Cmt2ToFIRRTLPipeline.cpp | - | [x] Pipeline updated |

---

## Summary

| Phase | Tasks | Implementation | Test Coverage |
|-------|-------|----------------|---------------|
| Phase 1 | T1-T4 | Complete | ~95% |
| Phase 2 | D1-D3 | Complete | ~95% |
| Phase 3 | A1-A4 | Complete | Tests pending |
| Phase 4 | L1-L4 | Complete | ~90% |

**Notes:**
- T4 TokenValidOp verifier test N/A: TokenValidOp has no additional constraints beyond basic MLIR type checking
- All critical test cases for Phase 1 and Phase 2 are covered
- Phase 3 analysis infrastructure implemented: TokenAnalysis, FIFODepthAnalysis, DeadlockAnalysis, extended TimingInference
- Phase 4 lowering infrastructure implemented: TokenLowering, StallControllerGen, pipeline integration
- Phase 4 tests: token-lowering.mlir tests LS/LI tokens, fork patterns, void tokens
- Phase 5 (PyCMT2) complete: DataflowBuilder, TaskBuilder, Pipeline, ForkJoinPipeline, timing helpers
- Phase 6 examples: division_pipeline.py, dataflow_forkjoin.py, pipeline_e2e.py (end-to-end simulation)
