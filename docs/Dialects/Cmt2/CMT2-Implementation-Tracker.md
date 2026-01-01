# CMT2 Improvements Implementation Tracker

This document tracks the implementation progress of the CMT2 improvements outlined in [CMT2-Improvements-Plan.md](./CMT2-Improvements-Plan.md).

**Last Updated:** 2026-01-02

---

## Status Legend

- [ ] Not started
- [x] Completed
- [~] In progress
- [!] Blocked

---

## Phase 1: Core Fixes

### 1.1 GAA Backpressure (Section 1)

**Key Insight:** GAA semantics naturally handle backpressure - the rule that sets a step's `done` signal automatically depends on all method calls' successful firing.

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Add `collectStepCalls()` utility | `Transforms/CallInfo.cpp` | [x] | Collect all method calls within a step |
| High | Verify GAA lowering includes method readies in rule guard | `Transforms/ProcStmtToAction.cpp` | [x] | Ensure rule guard ANDs method ready signals |
| High | Add static step validation | `Transforms/TDCC.cpp` | [x] | Warn on conflicting methods in static steps |
| Medium | Block static promotion for conflicting methods | `Transforms/TDCC.cpp` | [x] | Prevent promotion when conflicts exist |
| Medium | Add warning diagnostics infrastructure | `Transforms/Diagnostics.cpp` | [x] | Using MLIR emitWarning/emitRemark |
| High | Add tests for backpressure scenarios | `test/Dialect/Cmt2/proc-backpressure.mlir` | [x] | Dynamic, static, promotion tests |

**Subtasks:**

- [x] 1.1.1 Implement `collectStepCalls()` in CallInfo.cpp
- [x] 1.1.2 Add `getConcurrentFunctions()` helper
- [x] 1.1.3 Implement `validateStaticStep()` in TDCC.cpp
- [x] 1.1.4 Implement `canPromoteToStatic()` in TDCC.cpp
- [x] 1.1.5 Add test: dynamic step with method calls (verify guard includes ready)
- [x] 1.1.6 Add test: static step with conflicting methods (expect warning)
- [x] 1.1.7 Add test: blocked static promotion (expect remark)

---

### 1.2 PyCMT2 End-to-End Flow (Section 2)

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Add `emit_firrtl()` method | `pycmt2/circuit.py` | [x] | Run CMT2-to-FIRRTL pass |
| High | Add `emit_verilog()` method | `pycmt2/circuit.py` | [x] | Full pipeline to Verilog |
| High | Create `stl.py` with Reg, FIFO, Memory | `pycmt2/stl.py` | [x] | Python wrappers for STL modules |
| High | Add PassManager integration | `pycmt2/circuit.py` | [x] | Integrated in emit methods |
| Medium | Update examples to be minimal | `examples/PyCMT2/*.py` | [x] | Remove boilerplate |
| Medium | Add GCD end-to-end example | `examples/PyCMT2/gcd_example.py` | [x] | Multi-cycle procedural example |
| Medium | Add type-safe RuleRef for precedence | `pycmt2/refs.py`, `pycmt2/module.py` | [x] | No string-based references |
| Medium | Add proc conflict example | `examples/PyCMT2/proc_conflict_example.py` | [x] | proc.rule vs rule priority |

**Subtasks:**

- [x] 1.2.1 Implement `Circuit.emit_mlir()`
- [x] 1.2.2 Implement `Circuit.emit_firrtl()` with pass pipeline
- [x] 1.2.3 Implement `Circuit.emit_verilog()` with full pipeline
- [x] 1.2.4 Implement `Reg` class in stl.py
- [x] 1.2.5 Implement `FIFO` class in stl.py
- [x] 1.2.6 Implement `Memory` class in stl.py (+ Wire)
- [x] 1.2.7 Create simplified counter_example.py
- [x] 1.2.8 Create GCD example with procedural control
- [x] 1.2.9 Add integration test for end-to-end flow
- [x] 1.2.10 Add `RuleRef` class for type-safe rule references
- [x] 1.2.11 Add `RuleBuilder.ref()` and `ProcRuleBuilder.ref()` methods
- [x] 1.2.12 Update `ModuleBuilder.precedence()` to accept `RuleRef` objects
- [x] 1.2.13 Create proc_conflict_example.py demonstrating precedence

---

## Phase 2: Location Tracing (Section 3)

### 2.1 Python Source Location Capture

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Create `PythonLocation` class | `pycmt2/location.py` | [x] | Capture filename, line, function |
| High | Update all builders to capture location | `pycmt2/*.py` | [x] | RuleBuilder, MethodBuilder, etc. |
| Medium | Add `PythonSourceLocAttr` to TableGen | `Cmt2Attrs.td` | [x] | N/A - Using standard FileLineColLoc works |

**Subtasks:**

- [x] 2.1.1 Implement `get_python_location()` function
- [x] 2.1.2 Implement `PythonLocation.to_mlir_location()`
- [x] 2.1.3 Update `RuleBuilder` to capture location
- [x] 2.1.4 Update `MethodBuilder` to capture location
- [x] 2.1.5 Update `ValueBuilder` to capture location
- [x] 2.1.6 Update `StepBuilder` to capture location

### 2.2 Location Preservation Through Passes

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Preserve locations in `ProcStmtToAction` | `Transforms/ProcStmtToAction.cpp` | [x] | Locations flow through |
| High | Preserve locations in `Cmt2ToFIRRTL` | `Conversion/Cmt2ToFIRRTL.cpp` | [x] | Propagate to FIRRTL ops |
| Medium | Enhanced error reporting | All passes | [x] | Extract Python loc from FusedLoc |

**Subtasks:**

- [x] 2.2.1 Create `FusedLoc` when cloning step bodies
- [x] 2.2.2 Preserve locations in FSM generation
- [x] 2.2.3 Propagate locations to FIRRTL operations
- [x] 2.2.4 Implement `reportConversionError()` with source trace
- [x] 2.2.5 Add test for location preservation through pipeline (verified in Verilog output)

---

## Phase 3: Debugging Infrastructure (Section 4)

### 3.1 RTL Simulation Workspace

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Create `SimulationWorkspace` class | `pycmt2/simulation.py` | [x] | Directory structure generation |
| High | Implement placeholder testbench generation | `pycmt2/simulation.py` | [x] | Verilator C++ template |
| Medium | Generate Makefile for simulation | `pycmt2/simulation.py` | [x] | Build automation |

**Subtasks:**

- [x] 3.1.1 Implement `SimulationWorkspace.__init__()`
- [x] 3.1.2 Implement `_generate_rtl()` - emit Verilog to rtl/
- [x] 3.1.3 Implement `_generate_placeholder_testbench()` - C++ template
- [x] 3.1.4 Implement `_generate_makefile()` - Verilator build
- [x] 3.1.5 Add `generate_placeholder()` entry point
- [x] 3.1.6 Test workspace generation

### 3.2 Testbench DSL

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| Medium | Create `Testbench` DSL | `pycmt2/testbench.py` | [x] | Python testbench description |
| Medium | Add testbench IR operations | `Cmt2Ops.td` | [x] | N/A - Python DSL generates C++ directly |
| Medium | Implement testbench-to-C++ lowering | `pycmt2/testbench.py` | [x] | Generate C++ directly from Python |

**Subtasks:**

- [x] 3.2.1 Implement `Testbench` class
- [x] 3.2.2 Implement `TestSequence` class
- [x] 3.2.3 Implement test operations (reset, wait, drive, expect, call_method, wait_ready)
- [x] 3.2.4 Define testbench IR operations in TableGen (N/A - Python DSL generates C++ directly)
- [x] 3.2.5 Implement `generate_with_testbench()` in SimulationWorkspace
- [x] 3.2.6 Test DSL-generated testbenches

### 3.3 GAA-Centric Interpreter (cmt2-dbg)

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| High | Create `cmt2-dbg` tool directory | `tools/cmt2-dbg/` | [x] | Tool skeleton |
| High | Implement interpreter core | `tools/cmt2-dbg/Interpreter.cpp` | [x] | State, scheduler, execution |
| Medium | Add CLI interface | `tools/cmt2-dbg/cmt2-dbg.cpp` | [x] | REPL commands |
| Medium | Add precedence-based scheduling | `tools/cmt2-dbg/Interpreter.cpp` | [x] | Parses module `precedence` attr |
| Medium | Add proc.rule support | `tools/cmt2-dbg/Interpreter.cpp` | [x] | FSM state, step execution |
| Medium | Add file input support | `tools/cmt2-dbg/cmt2-dbg.cpp` | [x] | stdin pipe, --script option |
| Medium | Add Python interpreter | `pycmt2/interpreter.py` | [x] | Pure Python GAA interpreter |
| Low | Add Python bindings for debugger | `pycmt2/debugger.py` | [x] | N/A - Python interpreter provides this |

**Subtasks:**

- [x] 3.3.1 Create tool directory and CMakeLists.txt
- [x] 3.3.2 Implement `Cmt2Interpreter` class skeleton
- [x] 3.3.3 Implement `reset()` - initialize state
- [x] 3.3.4 Implement `evaluateGuards()` - evaluate rule guards
- [x] 3.3.5 Implement `resolveConflicts()` - use scheduler with precedence
- [x] 3.3.6 Implement `executeRule()` - execute rule body
- [x] 3.3.7 Implement `step()` - single cycle execution
- [x] 3.3.8 Implement `run()` - run until breakpoint
- [x] 3.3.9 Implement breakpoint support
- [x] 3.3.10 Implement state inspection (readRegister, etc.)
- [x] 3.3.11 Implement CLI REPL
- [x] 3.3.12 Add trace output
- [x] 3.3.13 Test interpreter with counter example
- [x] 3.3.14 Add precedence-based conflict resolution (parses `precedence` attribute)
- [x] 3.3.15 Add proc.rule/proc.step support (FSM state tracking, step execution)
- [x] 3.3.16 Add FIRRTL operations (shr, pad, shl, etc.)
- [x] 3.3.17 Add file input support (stdin redirect, skip comments, no prompts)
- [x] 3.3.18 Create Python interpreter (`pycmt2/interpreter.py`)
- [x] 3.3.19 Add `Interpreter` class with GAA semantics
- [x] 3.3.20 Add callback system for guards and bodies (`register_guard()`, `register_body()`)
- [x] 3.3.21 Add breakpoint support (rule, cycle, condition)
- [x] 3.3.22 Add tracing support
- [x] 3.3.23 Add `Circuit.interpreter()` integration method
- [x] 3.3.24 Create interpreter example (`interpreter_example.py`)

---

## Phase 4: Polish

### 4.1 Documentation & Tests

| Priority | Task | File | Status | Notes |
|----------|------|------|--------|-------|
| Medium | Add comprehensive unit tests | `test/Dialect/Cmt2/` | [x] | Backpressure, location tests added |
| Medium | Add integration tests | `integration_test/` | [x] | pycmt2_e2e.py comprehensive |
| Medium | Update documentation | `docs/Dialects/Cmt2/` | [x] | PyCmt2-Design.md consolidated |
| Low | Example refinement | `examples/PyCMT2/` | [x] | 8 examples created |

**Subtasks:**

- [x] 4.1.1 Add unit tests for backpressure handling (proc-backpressure.mlir)
- [x] 4.1.2 Add unit tests for location preservation (location-preservation.mlir, pycmt2_location_test.py)
- [x] 4.1.3 Add integration tests for emit_verilog()
- [x] 4.1.4 Add integration tests for SimulationWorkspace
- [x] 4.1.5 Add interpreter tests (proc-conflict-test.mlir with script)
- [x] 4.1.6 Update ecmt2-EDSL.md with new APIs (added procedural operations: ProcStep, ProcStaticStep, ControlBuilder, ProcRule, ProcMethod)
- [x] 4.1.7 Update PyCMT2-Design.md (consolidated from v1 and v2)
- [x] 4.1.8 Add debugging documentation (docs/Dialects/Cmt2/Debugging.md)

---

## Summary

| Phase | Section | Tasks | Completed | Progress |
|-------|---------|-------|-----------|----------|
| 1 | GAA Backpressure | 13 | 13 | 100% |
| 1 | PyCMT2 E2E | 19 | 19 | 100% |
| 2 | Location Tracing | 11 | 11 | 100% |
| 3 | Simulation | 6 | 6 | 100% |
| 3 | Testbench DSL | 6 | 6 | 100% |
| 3 | Interpreter (C++) | 17 | 17 | 100% |
| 3 | Interpreter (Python) | 7 | 7 | 100% |
| 4 | Documentation | 8 | 8 | 100% |
| **Total** | | **87** | **87** | **100%** |

---

## Notes

### Dependencies

1. **Phase 1.1 (Backpressure)** depends on existing ConflictMatrixAnalysis infrastructure
2. **Phase 1.2 (E2E)** depends on existing CMT2 passes working correctly
3. **Phase 2 (Location)** can proceed independently
4. **Phase 3.1 (Simulation)** depends on Phase 1.2 (emit_verilog)
5. **Phase 3.3 (Interpreter)** depends on Phase 1.1 (scheduler analysis)

### Blockers

- None currently identified

### Recent Changes

- 2025-12-31: Initial tracker created from design document
- 2025-12-31: Renamed "group" to "step" throughout codebase
- 2025-12-31: Updated design documents with backpressure understanding
- 2025-12-31: Implemented `SimulationWorkspace` class in `pycmt2/simulation.py`
- 2025-12-31: Implemented `Testbench` DSL in `pycmt2/testbench.py`
- 2025-12-31: Added test operations: reset, wait, drive, expect, call_method, wait_ready, wait_condition, print, comment
- 2025-12-31: Added cocotb testbench generation support
- 2025-12-31: **Phase 1.1 Complete:** Added `collectStepCalls()`, `getConcurrentFunctions()`, `validateStaticSteps()`, `canPromoteToStatic()` to CallInfo.cpp and TDCC.cpp
- 2025-12-31: **Phase 1.2 Complete:** Added `emit_firrtl()`, `emit_verilog()` to Circuit; created STL wrappers (Reg, FIFO, Memory, Wire); simplified counter_example.py; created gcd_example.py; added pycmt2_e2e.py integration test
- 2025-12-31: **Phase 2 Progress:** Implemented Python source location tracing in `pycmt2/location.py`; all builders now capture Python source locations; locations preserved through passes and visible in Verilog output comments
- 2025-12-31: **External Module Fix:** Added proper `BindBareOp` creation in Python EDSL for clock/reset ports; external modules now generate complete CMT2 IR with block arguments and bind.bare operations
- 2025-12-31: **GCD Example Working:** Full end-to-end flow from PyCMT2 → CMT2 MLIR → FIRRTL → Verilog now working correctly with external register modules
- 2025-12-31: **Simulation Tests Passing:** Created `test_gcd_simulation.py` and `test_proc_simulation.py` with Verilator testbenches; both pass all tests. Fixed `add()` in builders.py to truncate result (FIRRTL width rule). Created `counter_reg_example.py` and rewrote `proc_example.py` as MultiCycleALU with working Verilog generation.
- 2025-12-31: **STL Module Factory:** Rewrote `pycmt2/stl.py` with factory pattern (`Reg.create(circuit, width)`, `Wire.create(circuit, width)`); added auto-generated RTL implementations; added STL RTL registry with `get_stl_rtl_files()`, `add_stl_rtl_to_workspace()`, `clear_stl_registry()`; updated `SimulationWorkspace` to auto-add STL RTL files; updated test examples to use STL factories (removed verbose manual external module definitions); both GCD and ALU simulations pass.
- 2025-12-31: **Proc Comprehensive Example:** Created `proc_comprehensive_example.py` demonstrating all procedural control features: dynamic steps (explicit done), static steps (fixed latency), proc.seq, proc.par, proc.if, proc.while, nested control, proc rules, and regular rules/methods/values. Validates proc-related passes (compile-invoke, TDCC, proc-stmt-to-action, proc-to-gaa). All constructs successfully generate FIRRTL and Verilog.
- 2025-12-31: **Simulation Workspace Example:** Created `simulation_workspace_example.py` demonstrating `SimulationWorkspace.generate_placeholder()` for generating Verilator simulation environments. Shows generated directory structure (rtl/, tb/, build/, waves/), auto-generated STL RTL files, placeholder testbench template, and Makefile.
- 2026-01-01: **PyCMT2 Runner Script:** Created `scripts/pycmt2.sh` helper script for running PyCMT2 examples. Features: `--list` to show available examples, `-i` for interactive Python with pycmt2 loaded, `-v` for verbose mode, `-b` for custom build directory. Auto-sets PYTHONPATH and working directory.
- 2026-01-01: **Documentation Consolidation:** Merged `PyCmt2-Design.md` and `PyCmt2-Design-v2.md` into a single concise `PyCmt2-Design.md`. Removed code-heavy implementation details in favor of API reference and usage examples.
- 2026-01-01: **Integration Tests Updated:** Rewrote `pycmt2_e2e.py` with comprehensive tests covering: basic circuit creation, STL factory pattern (Reg, Wire), module with instances, FIRRTL emission, Verilog emission, SimulationWorkspace generation, Testbench DSL, methods with arguments, expression operations, and source location tracking. All 10 tests pass.
- 2026-01-01: **ECMT2 Procedural Ops Documented:** Added documentation for procedural operations to `ecmt2-EDSL.md`: ProcStep, ProcStaticStep, ControlBuilder (seq, par, ifThen, ifThenElse, whileLoop, enable, invoke), ProcRule, ProcMethod. Includes GCD example with procedural control flow.
- 2026-01-01: **Phase 2.2.4 Complete - Multi-Level Diagnostics:** Implemented comprehensive diagnostic system with ERROR, WARNING, INFO, DEBUG levels. Created `Diagnostics.h/cpp` with `Cmt2Diagnostic` builder class, Python location extraction from FusedLoc, and helper functions (`reportConversionError`, `reportTypeMismatch`, `reportMissingDefinition`, `reportSchedulingConflict`). Integrated diagnostics into TDCC.cpp, ProcStmtToAction.cpp, and ModuleInliner.cpp passes. Added Python bindings in `pycmt2/diagnostics.py` with `DiagnosticHandler` context manager. Created comprehensive `diagnostics_example.py` demonstrating all diagnostic features. Phase 2 (Location Tracing) now 100% complete.
- 2026-01-01: **Phase 3.3 Complete - GAA Interpreter (cmt2-dbg):** Implemented `cmt2-dbg` tool with REPL interface for debugging CMT2 circuits. Core components: `Cmt2Interpreter` class with GAA semantics (ORAAT execution), `evaluateGuards()`, `resolveConflicts()`, `step()`, `run()`, register state management, breakpoints (rule fire, register write, cycle), and execution tracing. CLI commands: step, run, reset, state, rules, regs, reg/set, fire, break/bpc/bpw, list/delete, trace on/off, history, help, quit. Supports FIRRTL, HW, Comb, and CMT2 dialects. Phase 3 (Debugging Infrastructure) now mostly complete (only Python debugger bindings remain).
- 2026-01-01: **Interpreter Precedence Support:** Added precedence-based conflict resolution to `resolveConflicts()`. Parses module `precedence` attribute (e.g., `{precedence = [[@div_by_2, @incr_loop]]}`) to determine rule priority. Higher-priority rules block lower-priority rules when both are enabled.
- 2026-01-01: **Interpreter Proc Support:** Added support for `proc.rule` and `proc.step` operations. Tracks FSM state per proc.rule, executes step bodies, handles `proc.step_done` signals. Implemented `initializeProcConstructs()`, `executeProcStep()`, `isProcRuleEnabled()`, `executeProcRuleStep()`.
- 2026-01-01: **Interpreter FIRRTL Ops:** Added handlers for `firrtl.shr`, `firrtl.dshr`, `firrtl.shl`, `firrtl.pad` operations in `executeOp()`.
- 2026-01-01: **Interpreter File Input:** Updated REPL to detect stdin pipe vs terminal using `isatty()`. When reading from file: no prompts displayed, comments (lines starting with `#`) skipped, empty lines skipped. Supports both `< script.txt` and `--script=script.txt`.
- 2026-01-01: **PyCMT2 Type-Safe Precedence:** Added `RuleRef` class to `pycmt2/refs.py` for type-safe rule references. Added `ref()` methods to `RuleBuilder` and `ProcRuleBuilder`. Updated `ModuleBuilder.precedence()` to accept `RuleRef` objects instead of strings, with clear error messages for incorrect usage.
- 2026-01-01: **PyCMT2 shr Fix:** Fixed `RegionBuilder.shr()` in `pycmt2/builders.py` to generate correct FIRRTL result type. Uses `Operation.create()` with explicit result type since Python bindings' `ShrPrimOp` doesn't properly infer the narrower width.
- 2026-01-01: **Proc Conflict Example:** Created `examples/PyCMT2/proc_conflict_example.py` demonstrating conflict resolution between `proc.rule` (incr_loop) and regular `rule` (div_by_2) with precedence. Includes interpreter test script `proc_conflict_script.txt`.
- 2026-01-02: **Python Interpreter for PyCMT2:** Implemented pure Python interpreter in `pycmt2/interpreter.py` for direct simulation from Python. Features: `Interpreter` class with GAA semantics (ORAAT execution), precedence-based conflict resolution, callback system for guards (`register_guard()`) and bodies (`register_body()`), breakpoints (rule fire, cycle, condition), tracing with `CycleTrace`, state inspection (`get_register()`, `set_register()`). Integrated via `Circuit.interpreter()` method. Created `interpreter_example.py` demonstrating callback-based simulation with proper guard/body implementations.
- 2026-01-02: **All Tasks Complete (100%):** Added location preservation tests (`location-preservation.mlir`, `pycmt2_location_test.py`). Created comprehensive debugging documentation (`docs/Dialects/Cmt2/Debugging.md`) covering Python interpreter, cmt2-dbg, RTL simulation, and debugging workflow. Marked optional TableGen tasks (PythonSourceLocAttr, testbench IR ops) as N/A since existing solutions work. All 87 tasks now complete.
