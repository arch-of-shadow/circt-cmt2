# CMT2 Development Tracker

**Last Updated:** 2026-01-14

---

## Status Summary

| Area | Status | Notes |
|------|--------|-------|
| Core Infrastructure | **100%** | GAA, scheduling, lowering |
| Multi-Cycle Operations | **~85%** | Steps, control flow work |
| PyCMT2 | **~95%** | STL complete |
| Timing Validation | **100%** | TV1-TV7 complete |
| Interpreter | **~90%** | Plugin architecture complete |
| Dataflow/Pipeline | **100%** | Phase 1-6 complete, tests and examples |
| Dataflow-Proc Compat | **~10%** | Phase 7: Integration with multi-cycle proc |
| Documentation | **~80%** | Needs updates |

---

## Active Work: Dataflow/Pipeline Implementation

**Design Document:** [tmp/PipelinedDesign-Implementation.md](tmp/PipelinedDesign-Implementation.md)

**Goal:** Token-based synchronization for pipelined hardware generation with unified proc.dataflow construct.

### Phase 1: Core Token Infrastructure

| # | Task | Status | Files |
|---|------|--------|-------|
| T1 | Add `SyncTokenType` to Cmt2Types.td | [x] | `Cmt2Types.td`, `Cmt2Types.cpp` |
| T2 | Add `TokenValidOp`, `TokenDataOp`, `TokenCreateOp`, `TokenJoinOp` | [x] | `Cmt2Ops.td`, `Cmt2Ops.cpp` |
| T3 | Extend `RuleOp` signatures with `tokens_in`/`tokens_out` | [x] | `Cmt2Ops.td`, `Cmt2Ops.cpp` |
| T4 | Add verifiers for token operations | [x] | `Cmt2Ops.cpp` |

### Phase 2: Dataflow Construct

| # | Task | Status | Files |
|---|------|--------|-------|
| D1 | Add `ProcDataflowOp`, `DataflowTaskOp`, `DataflowYieldOp`, `DataflowReturnOp` | [x] | `Cmt2Ops.td`, `Cmt2Ops.cpp` |
| D2 | ~~Add `ProcPipelineOp` as syntactic sugar~~ | [deferred] | Existing `static_step` provides same functionality; rename later |
| D3 | Implement dataflow-to-rules lowering | [x] | `Transforms/DataflowLowering.cpp` |

### Phase 3: Analysis

| # | Task | Status | Files |
|---|------|--------|-------|
| A1 | Implement `TokenAnalysis` using def-use chains | [x] | `Analysis/TokenAnalysis.cpp` |
| A2 | Implement FIFO depth inference | [x] | `Analysis/FIFODepthAnalysis.cpp` |
| A3 | Implement deadlock detection | [x] | `Analysis/DeadlockAnalysis.cpp` |
| A4 | Implement timing inference pass | [x] | `Transforms/TimingInference.cpp` (extended) |

### Phase 4: Lowering

| # | Task | Status | Files |
|---|------|--------|-------|
| L1 | Implement token lowering (LS → shift reg, LI → FIFO) | [x] | `Transforms/TokenLowering.cpp` |
| L2 | Implement stall controller generation | [x] | `Transforms/StallControllerGen.cpp` |
| L3 | Implement multi-consumer fork lowering | [x] | `Transforms/TokenLowering.cpp` (integrated) |
| L4 | Integrate with cmt2-to-firrtl | [x] | `Transforms/Cmt2ToFIRRTLPipeline.cpp` |

### Phase 5: PyCMT2

| # | Task | Status | Files |
|---|------|--------|-------|
| P1 | Add unified dataflow builder | [x] | `pycmt2/dataflow_builders.py` |
| P2 | Add pipeline shorthand builder | [x] | `pycmt2/pipeline_builders.py` |
| P3 | Add timing helpers | [x] | `pycmt2/timing.py` |

### Phase 6: Testing

| # | Task | Status | Files |
|---|------|--------|-------|
| E1 | Division pipeline example | [x] | `examples/PyCMT2/division_pipeline.py` |
| E2 | Fork-join dataflow example | [x] | `examples/PyCMT2/dataflow_forkjoin.py` |
| E3 | Test suite | [x] | `test/Dialect/Cmt2/token-lowering.mlir` |
| E4 | End-to-end simulation test | [x] | `examples/PyCMT2/pipeline_e2e.py` |

### Phase 7: Dataflow-Proc Compatibility

**Goal:** Full integration between dataflow/pipeline features and existing multi-cycle proc lowering.

**Design Document:** [tmp/PipelinedDesign-Implementation.md](tmp/PipelinedDesign-Implementation.md) Section 11

| # | Task | Status | Notes |
|---|------|--------|-------|
| C1 | Task body proc lowering | [ ] | Apply TDCC/CompileStatic to DataflowTaskOp bodies |
| C2 | Task done signal extraction | [ ] | Connect internal FSM done to token production |
| C3 | Unified timing validation | [ ] | Cross-validate task timing with internal control |
| C4 | Stall propagation to task FSMs | [ ] | Gate task-internal FSM registers on stall |
| C5 | Automatic LS/LI mode inference | [ ] | Infer token mode from task body analysis |
| C6 | Nested dataflow support | [ ] | Hierarchical dataflow composition |
| C7 | PyCMT2 task control builders | [ ] | Python API for control flow in tasks |

**Compatibility Scenarios:**

| Scenario | Status | Notes |
|----------|--------|-------|
| Static tasks (single-cycle) | [x] | Current implementation |
| Static tasks (multi-cycle, fixed timing) | [ ] | Needs C1-C2 |
| Dynamic tasks (while loops) | [ ] | Needs C1-C2, requires LI mode |
| Mixed LS/LI regions | [~] | Stall controller exists, needs C4 |
| Tasks calling external modules with timing | [ ] | Needs C3 |

---

## Other Remaining Work

### High Priority

#### Proc Lowering Performance

Dynamic steps have 2-cycle overhead due to done signal synchronization. `static_repeat` + `static_step` achieves 1-cycle iterations; `while` loops are limited to 2 cycles/iteration.

| Task | Status | Notes |
|------|--------|-------|
| Optimize while loop FSM for static steps | [ ] | Allow immediate transitions |
| Add `static_while` construct | [ ] | Compile-time known iteration count |
| Pipeline II support for proc control | [ ] | Pipelined execution within proc |

**Workaround:** Use `static_repeat` with `static_step` when iteration count is known.

#### Complex Par Testing

Per-branch FSM infrastructure is implemented. Needs testing with nested control.

| Task | Status | Notes |
|------|--------|-------|
| Handle nested par | [~] | Infrastructure ready, needs testing |
| Add proc_testbench tests for complex par | [ ] | `par { seq {...}, seq {...} }` |
| Test with nested control in branches | [ ] | par with if/while inside |

#### Interpreter Cleanup

| Task | Status | Notes |
|------|--------|-------|
| Remove TDCC-based code | [ ] | Clean up abandoned implementation |

---

### Medium Priority

#### Precedence Handling for FSM Rules

When proc rules are lowered to GAA rules, precedence should be determined by control flow structure (later states = higher precedence).

**Design:** [tmp/PrecedenceHandling.md](tmp/PrecedenceHandling.md)

| Task | Pass | Status |
|------|------|--------|
| Track state sequence index | TDCC | [ ] |
| Store in `tdcc.state_order` attribute | TDCC | [ ] |
| Read state ordering | ProcStmtToAction | [ ] |
| Group rules by FSM register | ProcStmtToAction | [ ] |
| Order by sequence index (descending) | ProcStmtToAction | [ ] |
| Generate precedence chains | ProcStmtToAction | [ ] |
| Validate all rules in precedence | ProcToGAA | [ ] |

#### Debugging & Simulation

| Task | Status | Notes |
|------|--------|-------|
| AddRuleFiringPort pass | [ ] | Debug output ports for rule firing |
| Testbench DSL | [ ] | Python DSL for testbenches |
| JIT Python frontend | [ ] | Interactive/incremental compilation |

#### Documentation

| Task | Status | Notes |
|------|--------|-------|
| ECMT2-Guide.md (C++ API) | [ ] | Port from ecmt2-EDSL.md |
| Update Debugging.md | [~] | Add interpreter details |
| Add more code examples | [ ] | In each guide |
| API reference generation | [ ] | From docstrings |

---

### Low Priority

#### Features

| Task | Status | Notes |
|------|--------|-------|
| Shorthand `{timing = n}` syntax | [ ] | Expands to `[n, n+1]` |
| Timing dependency graph | [ ] | For complex inference |
| Memory timing integration | [ ] | Memory with read latency |

#### Tooling

| Task | Status | Notes |
|------|--------|-------|
| cmt2-dbg enhancements | [~] | Basic framework exists |
| Waveform annotation | [ ] | FSM state visualization |
| IDE integration | [ ] | VSCode extension |

---

## Completed Features

### Core
- GAA rules, methods, values with guards
- Scheduling constraints (conflict, sequence-before, conflict-free)
- FIRRTL conversion and Verilog generation

### Multi-Cycle
- Dynamic/static steps, seq/par/if/while/static_repeat
- TDCC FSM generation, CompileStatic wrapper
- Timing attributes, inference, and validation passes

### PyCMT2
- Circuit/Module/Rule/Method/Value builders
- Procedural control builders
- STL components (Reg, Wire, FIFO1Push, FIFO1Pull, FIFO2I, Memory)
- ModuleLibrary integration

### Interpreter
- Plugin architecture (StateManager, OpHandlerRegistry, ControlFlowPlugin)
- Scheduler plugins (ORAAT, Annotation, Priority)
- Direct proc interpretation (seq, par, while, static_repeat)
- On-demand guard evaluation with wire propagation

---

## Known Issues

1. **Type widths in arithmetic**: Some operations produce wider results than expected
   - Workaround: Use `bits()` to truncate

2. **Empty static_step**: Steps with no operations may cause issues
   - Workaround: Add at least one operation

---

## Test Coverage

| Category | Files | Status |
|----------|-------|--------|
| Basic ops | 8 | Pass |
| Procedural | 12 | Pass |
| Timing | 8 | Pass |
| Integration | 6 | Pass |
| PyCMT2 | 5 | Pass |
| Token/Dataflow | 5 | Pass |
| **Total** | **45** | **100%** |

---

## Running Tests

```bash
# All CMT2 tests
build/bin/llvm-lit -v test/Dialect/Cmt2/

# Single test
build/bin/llvm-lit -v test/Dialect/Cmt2/gcd.mlir

# PyCMT2 examples
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/gcd.py
```

---

## Design Documents

| Document | Content |
|----------|---------|
| [tmp/PipelinedDesign-Implementation.md](tmp/PipelinedDesign-Implementation.md) | Token-based dataflow/pipeline design + Proc compatibility (Sec 11) |
| [tmp/PrecedenceHandling.md](tmp/PrecedenceHandling.md) | FSM rule precedence design |
| [tmp/ProcInterpreterDesign.md](tmp/ProcInterpreterDesign.md) | Direct proc interpretation |
| [tmp/InterpreterModularization-Design.md](tmp/InterpreterModularization-Design.md) | Plugin architecture |
| [tmp/PyCMT2-STL-Reimplementation.md](tmp/PyCMT2-STL-Reimplementation.md) | STL component design |
