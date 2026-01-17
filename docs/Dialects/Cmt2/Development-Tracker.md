# CMT2 Development Tracker

**Last Updated:** 2026-01-18 (Tests: 54 passing, 16+ PyCMT2 E2E simulations verified, API fixes applied)

---

## Status Summary

| Area | Status | Notes |
|------|--------|-------|
| Core Infrastructure | **100%** | GAA, scheduling, lowering |
| Multi-Cycle Operations | **~90%** | Steps, control flow, complex par tested |
| PyCMT2 | **~95%** | STL complete |
| Timing Validation | **100%** | TV1-TV7 complete |
| Interpreter | **~90%** | Plugin architecture complete |
| Dataflow/Pipeline | **100%** | Phase 1-4b complete; E2E simulation working |
| Dataflow-Proc Compat | **~90%** | Phase 7: C1-C5, C7 complete; C6 (nested dataflow) pending |
| Documentation | **~95%** | All guides complete; examples could be expanded |

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

**CRITICAL GAP:** Phase 4 passes add **annotations** only - they do NOT generate actual RTL hardware.
The cmt2-to-firrtl conversion clones token ops generically without special handling.

| # | Task | Status | Files | Notes |
|---|------|--------|-------|-------|
| L1 | Implement token lowering (LS → shift reg, LI → FIFO) | [~] | `Transforms/TokenLowering.cpp` | **Annotation only** - adds `token.impl=shiftreg/fifo` attrs |
| L2 | Implement stall controller generation | [~] | `Transforms/StallControllerGen.cpp` | **Annotation only** - marks `stall.gated` attrs |
| L3 | Implement multi-consumer fork lowering | [~] | `Transforms/TokenLowering.cpp` | **Annotation only** - marks `token.fanout` attrs |
| L4 | Integrate with cmt2-to-firrtl | [~] | `Conversion/Cmt2ToFIRRTL/` | **Basic pass-through implemented** (2026-01-16) |

### Phase 4b: Token RTL Generation (COMPLETE)

**Goal:** Convert token operations to storage modules BEFORE cmt2-to-firrtl runs.

**Architecture Decision (2026-01-16):** Token materialization should happen in `cmt2-token-rtl-gen` pass,
NOT in cmt2-to-firrtl. This provides cleaner separation of concerns.

**Design Document:** [tmp/PipelinedDesign-Implementation.md](tmp/PipelinedDesign-Implementation.md) Section 8 & 12

**Pass Pipeline:**
```
cmt2-dataflow-lowering → cmt2-token-lowering → cmt2-token-rtl-gen → cmt2-to-firrtl
```

**2026-01-17 Progress (MAJOR COMPLETION):**
- [x] TokenRTLGen pass structure implemented (`Transforms/TokenRTLGen.cpp`)
- [x] ModuleGenerator utility for pass-friendly storage module creation
- [x] Storage instance creation using ModuleGenerator
- [x] token.create rewritten to storage.write() calls
- [x] token.valid rewritten to storage.valid() calls
- [x] token.data rewritten to storage.peek() calls
- [x] Block argument tokens mapped to producer storage instances
- [x] Token signatures removed from rules after rewriting
- [x] Full pipeline to FIRRTL verified working

**Example:**
```mlir
// Before TokenRTLGen:
cmt2.rule @producer() tokens_out(!cmt2.sync_token<data = !firrtl.uint<32>>) {
  %tok = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<...>
}
cmt2.rule @consumer() tokens_in(%tok: !cmt2.sync_token<data = !firrtl.uint<32>>) {
  %valid = cmt2.token.valid %tok : ...
} {
  %data = cmt2.token.data %tok : ...
}

// After TokenRTLGen:
cmt2.instance @__tok_0 = @ShiftReg_w32_d1(%clk, %rst)
cmt2.rule @producer() {
  cmt2.call @__tok_0 @write(%data) : (!firrtl.uint<32>) -> ()
}
cmt2.rule @consumer() {
  %valid = cmt2.call @__tok_0 @valid() : () -> !firrtl.uint<1>
} {
  %data = cmt2.call @__tok_0 @peek() : () -> !firrtl.uint<32>
}
```

| # | Task | Status | Files | Notes |
|---|------|--------|-------|-------|
| L5 | TokenRTLGen pass structure | [x] | `Transforms/TokenRTLGen.cpp` | Collects tokens, creates storage |
| L6 | ModuleGenerator utility | [x] | `Transforms/ModuleGenerator.cpp` | Creates Reg/ShiftReg/FIFO modules |
| L7 | Integrate ModuleGenerator into TokenRTLGen | [x] | `Transforms/TokenRTLGen.cpp` | Storage instances created at module level |
| L8 | Rewrite token.create to storage.write | [x] | `Transforms/TokenRTLGen.cpp` | Producer rules call storage.write() |
| L9 | Rewrite token.valid to storage.valid | [x] | `Transforms/TokenRTLGen.cpp` | Consumer guard calls storage.valid() |
| L10 | Rewrite token.data to storage.peek | [x] | `Transforms/TokenRTLGen.cpp` | Consumer body calls storage.peek() |
| L11 | Rewrite token.join to AND of valids | [x] | `Transforms/TokenRTLGen.cpp` | Generates firrtl.and chain for joined valids |
| L12 | Remove token types from rule signatures | [x] | `Transforms/TokenRTLGen.cpp` | tokens_in/tokens_out attrs removed |
| L13 | Wire fork/broadcast for multi-consumer | [x] | `Transforms/TokenRTLGen.cpp` | Type-based mapping routes multiple consumers to same storage |
| L14 | Generate stall controller RTL | [~] | `Transforms/StallControllerGen.cpp` | Annotations only; RTL gen needs enable/stall ports on storage |

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
| E5 | Comprehensive example (non-dataflow) | [x] | `examples/PyCMT2/comprehensive_example.py` - ALL TESTS PASS |
| E6 | Comprehensive dataflow example | [x] | `examples/PyCMT2/comprehensive_dataflow_example.py` - ALL TESTS PASS |

### Phase 7: Dataflow-Proc Compatibility

**Goal:** Full integration between dataflow/pipeline features and existing multi-cycle proc lowering.

**Design Document:** [tmp/PipelinedDesign-Implementation.md](tmp/PipelinedDesign-Implementation.md) Section 11

| # | Task | Status | Notes |
|---|------|--------|-------|
| C1 | Task body proc lowering | [x] | TDCC processes DataflowTaskOp; tests `dataflow-proc-compat.mlir`, `dataflow-task-tdcc.mlir` |
| C2 | Task done signal extraction | [x] | FSM gen in ProcStmtToAction; tests `dataflow-lowering-proc.mlir`, `dataflow-proc-fsm.mlir` |
| C3 | Unified timing validation | [x] | TimingValidation validates task timing vs TDCC; tests `dataflow-timing-validation*.mlir` |
| C4 | Stall propagation to task FSMs | [x] | FSM rules marked with stall.gated; test `dataflow-stall-propagation.mlir` |
| C5 | Automatic LS/LI mode inference | [x] | TDCC infers tdcc.requires_li_mode/static_timing; test `dataflow-mode-inference.mlir` |
| C6 | Nested dataflow support | [deferred] | Parser rejects (requires parent=module); flatten manually for now |
| C7 | PyCMT2 task control builders | [x] | `dataflow_builders.py`: seq/par/if_/static_repeat/enable |

**Compatibility Scenarios:**

| Scenario | Status | Notes |
|----------|--------|-------|
| Static tasks (single-cycle) | [x] | Current implementation |
| Static tasks (multi-cycle, fixed timing) | [x] | C1-C2 complete; FSM-gated token production |
| Dynamic tasks (while loops) | [x] | C5 auto-infers LI mode for tasks with while loops |
| Mixed LS/LI regions | [x] | Stall controller + C4 complete; FSM rules stall-gated |
| Tasks calling external modules with timing | [x] | C3 complete; timing validated against TDCC |

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

Per-branch FSM infrastructure complete with branch-specific FSM widths.

| Task | Status | Notes |
|------|--------|-------|
| Handle nested par | [x] | Branch FSM width fix; test `proc-complex-par.mlir` |
| Fix branch FSM width overflow | [x] | Branch FSMs now use width calculated from `num_states` |
| Add proc_testbench tests for complex par | [x] | 5 test cases in `proc-complex-par.mlir` |
| Test with nested control in branches | [x] | par with static_repeat, if tested; tests in `proc-complex-par.mlir` |

**Bug Fixed:** ProcStmtToAction was using main FSM width for branch FSM operations, causing APInt overflow when branch FSMs had more states. Fix: track `branchFsmWidths` map and create branch-specific register modules.

#### Interpreter Cleanup

| Task | Status | Notes |
|------|--------|-------|
| Remove TDCC-based code | [x] | No TDCC code in interpreter - uses plugin architecture |

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
| ECMT2-Guide.md (C++ API) | [x] | 378 lines, comprehensive C++ API guide |
| Debugging.md | [x] | 395 lines, covers interpreter, cmt2-dbg, RTL simulation |
| Add more code examples | [~] | Examples exist but could be expanded |
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

0. ~~**Token operations converted to basic pass-through** (2026-01-16)~~
   - **RESOLVED (2026-01-18):** TokenRTLGen pass now generates proper storage modules
   - Phase 4b complete: `cmt2-token-rtl-gen` creates ShiftReg/FIFO instances
   - Token operations rewritten to storage module calls (write/valid/peek)
   - Status: [x] Completed

1. ~~**Dataflow simulation timeout** (2026-01-18)~~ - **FIXED**
   - **Symptom:** RTL generation succeeds but simulation hangs waiting for pipeline output
   - **Root causes identified and fixed:**
     a. [x] Duplicate storage instances - DataflowLowering marked dataflow as lowered but didn't erase it
     b. [x] Dataflow return values not wired to outputs - final rule now has result types and returns values
     c. [x] Rules with results not getting output port wiring - Cmt2ToFIRRTL now wires rule results
     d. [x] TokenRTLGen type-based lookup for fork/join - explicit storage indices fix multiple same-type tokens
   - **Fixes applied:**
     - `DataflowLowering.cpp`: Erase ProcDataflowOp after lowering; detect final task; add result types; return computed values; annotate rules with `dataflow.input_storage_indices`
     - `Cmt2ToFIRRTL.cpp`: Add rule results to `needsResultWires` check; wire rule result outputs
     - `TokenRTLGen.cpp`: Use explicit storage indices for token argument mapping
   - **Verified:** Simple pipeline test shows correct fork-join result (130 = 3*10 + 100)
   - Status: [x] **FIXED**

### Recently Fixed Issues (2026-01-18)

10. ~~**DataflowLowering external value capture** (2026-01-18)~~
    - **Symptom:** "value defined outside the region" MLIR verifier error
    - **Root cause:** `DataflowLowering.cpp` didn't capture dataflow arguments used inside tasks
    - **Fix:** Added code to collect external values (dataflow block arguments) used in task bodies and add them as rule arguments with proper IRMapping during cloning
    - **Files:** `lib/Dialect/Cmt2/Transforms/DataflowLowering.cpp`
    - Status: [x] Fixed

11. ~~**Python DataflowTaskOp block arguments** (2026-01-18)~~
    - **Symptom:** Task body incorrectly created block arguments for token types
    - **Root cause:** `DataflowTaskOp` is NOT `IsolatedFromAbove`, so tokens should reference operands not block args
    - **Fix:** Modified `dataflow_builders.py` to create empty block and map tokens to task operands
    - **Files:** `lib/Bindings/Python/pycmt2/dataflow_builders.py`
    - Status: [x] Fixed

12. ~~**Port index tracking for rules in Cmt2ToFIRRTL** (2026-01-18)~~
    - **Symptom:** "connect has invalid flow: start_enable has source flow" error
    - **Root cause:** `connectOutputPorts()` didn't account for rule arg/result ports when tracking port indices
    - **Fix:** Added handling in `connectOutputPorts()` to skip rule ports (they have ports but no external ready/enable signals)
    - **Files:** `lib/Conversion/Cmt2ToFIRRTL/Cmt2ToFIRRTL.cpp`
    - Status: [x] Fixed

13. ~~**Circular dependency between ECMT2 and Transforms** (2026-01-18)~~
    - **Symptom:** Linker errors for `ModuleLibrary` symbols when building
    - **Root cause:** ECMT2 and Transforms both needed ModuleLibrary, creating circular dependency
    - **Fix:** Created new `CIRCTCmt2ModuleLib` library in `lib/Dialect/Cmt2/ModuleLib/`
    - **Files:** `lib/Dialect/Cmt2/ModuleLib/CMakeLists.txt`, `lib/Dialect/Cmt2/CMakeLists.txt`, `lib/Dialect/Cmt2/ECMT2/CMakeLists.txt`, `lib/Dialect/Cmt2/Transforms/CMakeLists.txt`
    - Status: [x] Fixed

14. ~~**Missing dataflow passes in emit_verilog() pipeline** (2026-01-18)~~
    - **Symptom:** Dataflow constructs not lowered, generated RTL had no pipeline logic
    - **Root cause:** `circuit.py` `emit_verilog()` didn't include dataflow/token passes
    - **Fix:** Added `cmt2-dataflow-lowering`, `cmt2-token-lowering`, `cmt2-token-rtl-gen` to pass pipeline
    - **Files:** `lib/Bindings/Python/pycmt2/circuit.py`
    - Status: [x] Fixed

15. ~~**Duplicate storage instances from dataflow** (2026-01-18)~~
    - **Symptom:** Two ShiftReg instances created for one token (e.g., `__tok_0` and `__tok_1`)
    - **Root cause:** `DataflowLowering` marked dataflow as lowered but didn't erase it, so `TokenRTLGen` processed both rules and dataflow tasks
    - **Fix:** Modified `DataflowLowering.cpp` to erase `ProcDataflowOp` after creating rules
    - **Files:** `lib/Dialect/Cmt2/Transforms/DataflowLowering.cpp`
    - Status: [x] Fixed

16. ~~**Dataflow return values not wired to outputs** (2026-01-18)~~
    - **Symptom:** Final task computes result but it's not exposed as module output
    - **Root cause:** `DataflowReturnOp` handler created empty `ReturnOp`, final rule had no result types
    - **Fix:**
      - Added `isFinalTask` detection (checks for `DataflowReturnOp` terminator)
      - Final rule gets result types from dataflow's function type
      - `ReturnOp` now includes the computed values
      - `Cmt2ToFIRRTL` creates result wires and wires them to output ports for rules with results
    - **Files:** `lib/Dialect/Cmt2/Transforms/DataflowLowering.cpp`, `lib/Conversion/Cmt2ToFIRRTL/Cmt2ToFIRRTL.cpp`
    - Status: [x] Fixed

17. ~~**TokenRTLGen type-based token mapping fails for fork/join** (2026-01-18)~~
    - **Symptom:** Join task checks wrong storage valid signals (e.g., `__tok_3` instead of `__tok_1` and `__tok_2`)
    - **Root cause:** `tokenTypeToStorage_` map only stores one storage per token type; when multiple tasks produce same-type tokens (fork/join), all map to the last registered
    - **Fix:**
      - `DataflowLowering` now tracks token SSA values → storage index mapping
      - Rules annotated with `dataflow.input_storage_indices` attribute
      - `TokenRTLGen` uses explicit indices when available, falls back to type-based only for non-dataflow rules
    - **Files:** `lib/Dialect/Cmt2/Transforms/DataflowLowering.cpp`, `lib/Dialect/Cmt2/Transforms/TokenRTLGen.cpp`
    - **Verified:** Fork-join pipeline produces correct result (130 = (10+100) + (10*2) for input 10)
    - Status: [x] Fixed

1. **Type widths in arithmetic**: Some operations produce wider results than expected
   - Workaround: Use `bits()` to truncate

2. **Empty static_step**: Steps with no operations may cause issues
   - Workaround: Add at least one operation

3. ~~**Dataflow task result types** (2026-01-18)~~
   - RESOLVED: Testbench was monitoring wrong signals
   - The dataflow pipeline has separate input (`fork_join_pipeline_source_data_in`)
     and output (`fork_join_pipeline_finalize_result_0`) ports
   - Fixed testbench to drive dataflow input directly and read output after pipeline latency
   - Status: [x] Fixed

4. ~~**Parallel block register write visibility** (2026-01-16)~~
   - RESOLVED: RTL simulation confirms complex parallel blocks work correctly
   - Both simple parallel (par.enable) and complex parallel (par with nested seq)
     correctly propagate register writes to subsequent stages
   - Verified with par_write_debug.py test case - flag written in branch 0 is
     visible to check_flag step (result=42 as expected)
   - Status: [x] Working as designed

5. ~~**Nested module proc_rule scheduling** (2026-01-16)~~
   - RESOLVED: RTL simulation confirms nested module proc_rules work correctly
   - Parent module calls submodule.start() which sets busy_reg
   - Submodule's proc_rule correctly fires and executes its control flow
   - Verified with nested_proc_debug.py test case - Calculator submodule
     correctly produces result=42 (input 21 * 2)
   - Status: [x] Working as designed

6. ~~**if_ lacks condition region** (2026-01-16)~~
   - RESOLVED: Added `ProcCondIfOp` with condition region support
   - `if_()` in PyCMT2 now accepts both Signal (backward compatible) and callable
     condition functions (like `while_()`)
   - Callable form creates `cmt2.proc.cond_if` with condition region supporting
     `cmt2.call` ops for runtime-dependent conditions
   - Example: `with ctrl.if_(lambda b: b.lt(b.call(counter, "read"), b.const(10, 32))) as if_:`
   - Implementation: `ProcCondIfOp`, `ProcCondIfYieldOp` ops; TDCC and ProcStmtToAction updated
   - Test: `examples/PyCMT2/test_cond_if.py`
   - Status: [x] Completed

7. ~~**Submodule proc_rule scheduling in complex flows** (2026-01-16)~~
   - RESOLVED: RTL simulation confirms submodule proc_rules fire correctly
   - When parent calls submodule.start() from a static step within proc_rule,
     the submodule's proc_rule fires correctly
   - Key insight: Parent must include explicit wait loop (while_) for submodule
     completion since rule interleaving is cycle-based
   - Test: `examples/PyCMT2/test_submodule_proc_step.py` - result=42 as expected
   - Status: [x] Working as designed

8. ~~**Complex parallel blocks with multiple register writes** (2026-01-16)~~
   - RESOLVED: RTL simulation confirms complex parallel blocks work correctly
   - Registers written in parallel branches are visible to subsequent steps
   - Test: `examples/PyCMT2/par_write_debug.py` with custom testbench
   - Result=42 confirms flag written in branch 0 was visible to check_flag step
   - Status: [x] Working as designed

---

## Test Coverage

| Category | Files | Status |
|----------|-------|--------|
| Basic ops | 8 | Pass |
| Procedural | 14 | Pass |
| Timing | 8 | Pass |
| Integration | 6 | Pass |
| PyCMT2 | 6 | Pass |
| Token/Dataflow | 5 | Pass |
| Dataflow-Proc | 7 | Pass |
| **Total** | **54** | **100%** |

**New Tests (Phase 7):**
- `test/Dialect/Cmt2/dataflow-proc-compat.mlir` - Proc control ops in DataflowTaskOp

**New Examples (Phase 7):**
- `examples/PyCMT2/dataflow_proc_control.py` - PyCMT2 dataflow with proc control
- `examples/PyCMT2/comprehensive_example.py` - All major PyCMT2 features with full E2E RTL simulation

---

## PyCMT2 E2E Simulation Status

**Verified Working (2026-01-18):**

| Example | Tests/Result | Notes |
|---------|--------------|-------|
| `comprehensive_example.py` | 6/6 PASS | All major features |
| `comprehensive_dataflow_example.py` | 5/5 PASS | Fork-join pipeline |
| `gcd.py` | 8/8 PASS | GCD algorithm |
| `alu.py` | 6/6 PASS | Multi-cycle ALU |
| `while_loop_example.py` | PASS | While loop counter |
| `memory_proc.py` | PASS | Memory accumulator |
| `proc_par_test.py` | PASS | Parallel execution |
| `proc_pipeline.py` | PASS | Pipeline with FIFO |
| `dynamic_pipeline_fifo.py` | PASS | Dynamic FIFO pipeline |
| `systolic.py` | PASS | 2x2 matrix multiply |
| `test_submodule_proc_step.py` | PASS (42) | Submodule proc_rule |
| `test_cond_if.py` | PASS | Conditional regions |
| `proc_testbench.py` | 18/18 PASS | Multiple proc control tests |
| `division_pipeline.py` | MLIR OK | Pipeline MLIR generation |
| `dataflow_forkjoin.py` | MLIR OK | Fork-join MLIR generation |
| `proc.py` | Verilog OK | Procedural control |

**Recently Fixed Examples (2026-01-18):**

| Example | Fix Applied |
|---------|-------------|
| `static_proc.py` | Changed to regular `method()` (timing attrs only for proc_method) |
| `dataflow_proc_control.py` | Fixed `static_step(latency, name)` argument order |

**Examples with Potential API Issues:**

| Example | Issue |
|---------|-------|
| `pipeline_fifo_testbench.py` | May need testing - `and_()` usage |

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
