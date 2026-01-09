# CMT2 Development Tracker

Consolidated tracker for ongoing work and TODOs.

**Last Updated:** 2026-01-09

---

## Status Legend

- [x] Completed
- [~] In progress
- [ ] Not started
- [!] Blocked

---

## Implementation Status Summary

| Area | Status | Notes |
|------|--------|-------|
| Core Infrastructure | **100%** | GAA, scheduling, lowering |
| Multi-Cycle Operations | **100%** | Steps, control flow, timing |
| PyCMT2 | **100%** | STL reimplemented, ModuleLibrary integration complete |
| Documentation | **~80%** | Reorganized |
| ECMT2 (C++) | **90%** | Stable, low-level API |

---

## Completed Features

### Core GAA Infrastructure
- [x] Rules, methods, values with guards
- [x] Scheduling constraints (conflict, sequence-before, conflict-free)
- [x] Automatic conflict detection
- [x] FIRRTL conversion
- [x] Verilog generation

### Multi-Cycle Operations
- [x] Dynamic steps with done signals
- [x] Static steps with fixed latency
- [x] Sequential composition (seq)
- [x] Parallel composition (par)
- [x] Conditional (if, static_if)
- [x] Loops (while, static_repeat)
- [x] TDCC FSM generation
- [x] CompileStatic wrapper transformation
- [x] Timing attributes (TimingIntervalAttr, LatencyAttr, IntervalAttr)
- [x] Method signature timing
- [x] Call-site timing guards
- [x] TimingInference pass
- [x] TimingValidation pass
- [x] StaticFSMAllocation pass
- [x] FSM encoding (binary, one-hot)

### PyCMT2 Python Frontend
- [x] Circuit and ModuleBuilder
- [x] Rule/Method/Value builders
- [x] Procedural control builders
- [x] STL components (Reg, Wire, WireDefault, FIFO1Push, FIFO1Pull, FIFO2I, Memory)
- [x] External module bindings
- [x] Timing parameters (static_latency, interval)
- [x] SimulationWorkspace (needs ModuleLibrary integration like ECMT2)
- [x] Source location tracking

### Testing
- [x] 39 MLIR tests (100% passing)
- [x] End-to-end simulation examples
- [x] Timing validation tests

---

## Remaining Work

### Documentation (Priority: Medium)

| # | Task | Status | Notes |
|---|------|--------|-------|
| D1 | ECMT2-Guide.md (C++ API) | [ ] | Port from ecmt2-EDSL.md |
| D2 | Update Debugging.md | [~] | Add interpreter details |
| D3 | Add more code examples | [ ] | In each guide |
| D4 | API reference generation | [ ] | From docstrings |

### Features (Priority: Low)

| # | Task | Status | Notes |
|---|------|--------|-------|
| F1 | Shorthand `{timing = n}` syntax | [ ] | Expands to `[n, n+1]` |
| F2 | Timing dependency graph | [ ] | For complex inference |
| F3 | Memory timing integration | [ ] | Memory with read latency |

### Interpreter Guard Evaluation Fix (Priority: High) - COMPLETE

| # | Task | Status | Notes |
|---|------|--------|-------|
| IG1 | On-demand guard evaluation | [x] | Evaluate guards when rule is scheduled, not upfront |
| IG2 | Wire value propagation | [x] | Wire writes from fired rules visible to later guards |
| IG3 | Bypass logic support | [x] | Enable `!full \| deqed` pattern to work correctly |

**Implementation (2026-01-09):**
- Refactored `step()` function to evaluate guards on-demand in precedence order
- Wire values written by earlier-fired rules are now visible to later guard evaluations
- Relaxed strict ORAAT to allow multiple top-level rules to fire when bypass logic enables them
- Dynamic pipeline now completes in 7 cycles (matches RTL's 8 cycles after reset)
- All 39 Cmt2 tests pass

### Debugging & Simulation (Priority: Medium)

| # | Task | Status | Notes |
|---|------|--------|-------|
| DS1 | AddRuleFiringPort pass | [ ] | Optional pass for debugging mode - adds output ports that indicate when each rule fires |
| DS2 | Testbench DSL | [ ] | Python DSL for defining testbenches (see Debugging.md) |
| DS3 | JIT Python frontend for PyCMT2 | [ ] | Better interactive/incremental compilation for Python frontend |

### Interpreter Modularization (Priority: High) - COMPLETE

The `cmt2-dbg` interpreter has been fully modularized with plugin-based architecture.

**Design Document:** `docs/Dialects/Cmt2/tmp/InterpreterModularization-Design.md`

| # | Task | Status | Notes |
|---|------|--------|-------|
| I1 | Phase 0: Module Interpreter | [x] | ModuleInterpreter, ModuleInterpreterRegistry (Reg, FIFO, Memory) |
| I2 | Phase 1: Core Infrastructure | [x] | StateManager (observer pattern), OpHandlerRegistry (TypeID dispatch) |
| I3 | Phase 2: Control Flow Plugins | [x] | DynamicControlPlugin, StaticControlPlugin with timing validation |
| I4 | Phase 3: Integration | [x] | Plugins integrated into Cmt2Interpreter, `setPluginExecution()` API |
| I5 | Phase 4: Timing Validation | [x] | StaticControlPlugin validates arg_timing/result_timing at runtime |
| I6 | Phase 5: Scheduler Plugins | [x] | ORAATScheduler, AnnotationScheduler, PriorityScheduler |

**Completed Infrastructure:**
- `include/circt/Dialect/Cmt2/Interpreter/Types.h` - Shared types (InterpValue, RegisterState, FSMState, etc.)
- `include/circt/Dialect/Cmt2/Interpreter/StateManager.h` - Centralized state with observer pattern
- `include/circt/Dialect/Cmt2/Interpreter/OpHandlerRegistry.h` - Extensible operation dispatch
- `include/circt/Dialect/Cmt2/Interpreter/ControlFlowPlugin.h` - Plugin interface + implementations
- `include/circt/Dialect/Cmt2/Interpreter/Scheduler.h` - Scheduler plugin interface + implementations
- `lib/Dialect/Cmt2/Interpreter/*.cpp` - All implementations

**New APIs in Cmt2Interpreter:**
- `setPluginExecution(bool)` - Enable/disable plugin-based execution
- `getStateManager()` - Access centralized state manager
- `getStaticControlPlugin()` - Access timing validation
- `setScheduler(std::unique_ptr<Scheduler>)` - Set custom scheduler
- `getSchedulerName()` - Get current scheduler name

**Available Schedulers:**
- `ORAATScheduler` - One Rule At A Time (default GAA semantics)
- `AnnotationScheduler` - Uses conflict/conflictFree/sequenceBefore attributes for parallel firing
- `PriorityScheduler` - Priority-based parallel firing with conflict awareness

**Remaining Work:**
- Full migration from legacy execution path to plugin-based path
- Testing with interpretation examples

### Interpreter Scheduler Integration (Priority: High) - COMPLETE

The `step()` function has been fully refactored to use the Scheduler infrastructure.

| # | Task | Status | Notes |
|---|------|--------|-------|
| SI1 | Use scheduler for top-level rule selection | [x] | Uses `ORAATScheduler::getPriority()` for priority ordering |
| SI2 | Remove duplicate precedence parsing (top-level) | [x] | Top-level rules use scheduler |
| SI3 | Maintain nested module conflict checking | [x] | `conflictMatrixAnalysis_` used for per-instance conflicts |
| SI4 | Support on-demand guard evaluation with scheduler | [x] | Guards evaluated in scheduler-determined order with wire propagation |
| SI5 | Per-instance schedulers for nested modules | [x] | Each nested CMT2 module instance gets its own ORAATScheduler |

**Implementation (2026-01-09):**
- Top-level rules use `ORAATScheduler::getPriority()` instead of ad-hoc precedence parsing
- `AnnotationScheduler::conflicts()` used for explicit conflict annotations
- `conflictMatrixAnalysis_` used for rule-to-rule conflicts in top module
- Per-instance schedulers created for each nested CMT2 module (stored in `instanceSchedulers_`)
- All ad-hoc precedence parsing removed - both top-level and nested modules use schedulers
- RTTI-free implementation using `scheduler_->getName()` for type detection
- All 39 Cmt2 tests pass
- All 14 interpretation tests pass
- Dynamic pipeline completes in 7 cycles (matches RTL's 8 cycles after reset)

### PyCMT2 STL Reimplementation (Priority: High) - COMPLETE

The PyCMT2 STL has been reimplemented to match ECMT2's architecture.

**Design Document:** `docs/Dialects/Cmt2/tmp/PyCMT2-STL-Reimplementation.md`

**Changes Made:**
- FIFOs (FIFO1Push, FIFO1Pull, FIFO2I) are now CMT2 modules built from Reg and Wire primitives
- WireDefault added as CMT2 module helper
- Memory implemented with sync (1-cycle read) and async (0-cycle read) variants
- Inline RTL generation removed - RTL comes from ModuleLibrary
- ModuleLibrary Python integration added (`pycmt2/module_library.py`)
- External modules now correctly bind to FIRRTL module names from ModuleLibrary
- Test file: `examples/PyCMT2/stl_test.py` validates all components (9 tests pass)

**ModuleLibrary Integration:**
- `module_library.py` - Python equivalent of C++ `ModuleLibrary.cpp`
- Finds project root to locate `lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml`
- Calls `build.sh` scripts to generate FIRRTL from Chisel sources
- `ExternalModuleBuilder.set_firrtl_module_name()` sets the target FIRRTL module
- Example: `Reg.create(circuit, 32)` creates `cmt2.module.extern.firrtl @FIRRTLReg_32 : @Reg_width32_init0`

| # | Task | Status | Notes |
|---|------|--------|-------|
| S1 | Phase 1: Remove inline RTL generation | [x] | `_generate_*_rtl()` functions removed |
| S2 | Phase 1: Add ModuleLibrary integration | [x] | External modules bind to ModuleLibrary |
| S3 | Phase 2: Reimplement FIFO1Push as CMT2 module | [x] | Matches STLLibrary.cpp |
| S4 | Phase 2: Reimplement FIFO1Pull as CMT2 module | [x] | Matches STLLibrary.cpp |
| S5 | Phase 2: Reimplement FIFO2I as CMT2 module | [x] | Matches STLLibrary.cpp |
| S6 | Phase 2: Add WireDefault CMT2 module | [x] | Helper for FIFOs |
| S7 | Phase 3: Implement Memory.create_1r1w_sync() | [x] | Mem1r1w1c binding |
| S8 | Phase 3: Implement Memory.create_1r1w_async() | [x] | Mem1r1w0c binding |
| S9 | Phase 4: Update SimulationWorkspace | [x] | Legacy functions deprecated |
| S10 | Phase 4: Add tests and examples | [x] | `stl_test.py` (9 tests pass) |

### Tooling (Priority: Low)

| # | Task | Status | Notes |
|---|------|--------|-------|
| T1 | cmt2-dbg command-line tool | [~] | Basic framework exists, needs modularization |
| T2 | Waveform annotation | [ ] | FSM state visualization |
| T3 | IDE integration | [ ] | VSCode extension |

---

## Known Issues

### Minor Issues

1. **Type widths in arithmetic**: Some operations produce wider results than expected
   - Workaround: Use `bits()` to truncate

2. **Empty static_step**: Steps with no operations may cause issues
   - Workaround: Add at least one operation

### Documentation Issues

1. Some old docs reference outdated syntax
2. Examples in old docs may not compile

---

## Test Coverage

| Category | Files | Status |
|----------|-------|--------|
| Basic ops | 8 | Pass |
| Procedural | 12 | Pass |
| Timing | 8 | Pass |
| Integration | 6 | Pass |
| PyCMT2 | 5 | Pass |
| **Total** | **39** | **100%** |

---

## Performance Notes

### Compile Time

- Small designs (<100 rules): <1s
- Medium designs (100-1000 rules): 1-10s
- Large designs: May need optimization

### Generated Hardware

- FSM encoding: One-hot for ≤8 states, binary otherwise
- Early-reset optimization: Enabled by default

---

## Contributing

### Adding New Features

1. Add operation to `Cmt2Ops.td`
2. Implement in `Cmt2Ops.cpp`
3. Add pass if needed in `Transforms/`
4. Add Python binding if needed
5. Add tests in `test/Dialect/Cmt2/`
6. Update documentation

### Running Tests

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

## Historical Trackers (Archived)

The following documents are now archived but kept for reference:

| Document | Content | Status |
|----------|---------|--------|
| CyclePreciseTimingImplementation.md | Timing implementation details | Merged into MultiCycle.md |
| ProcLoweringFixes-Tracker.md | Procedural lowering fixes | 100% complete |
| CMT2-Improvements-Plan.md | Original improvement plan | Complete |
| CMT2-Implementation-Tracker.md | Original tracker | Merged here |

---

## Changelog

### 2026-01-09

- **Interpreter Scheduler Integration COMPLETE** - Refactored `step()` to use Scheduler infrastructure
  - SI1: Top-level rules use `ORAATScheduler::getPriority()` instead of ad-hoc precedence parsing
  - SI2: `AnnotationScheduler::conflicts()` used for explicit conflict annotations
  - SI3: `conflictMatrixAnalysis_` used for top module rule-to-rule conflicts
  - SI4: Nested modules maintain per-instance conflict checking via `conflictMatrixAnalysis_`
  - SI5: Per-instance schedulers for nested CMT2 modules (stored in `instanceSchedulers_`)
  - All ad-hoc precedence parsing eliminated - both top-level and nested modules use schedulers
  - RTTI-free implementation using `scheduler_->getName()` for type detection
  - All 39 Cmt2 tests pass, all 14 interpretation tests pass
- **Interpreter Guard Evaluation Fix COMPLETE** - On-demand guard evaluation with wire propagation
  - IG1: Guards are now evaluated on-demand in precedence order (not upfront)
  - IG2: Wire values written by earlier-fired rules are visible to later guard evaluations
  - IG3: Bypass logic (`!full | deqed`) now works correctly
  - Relaxed strict ORAAT to allow multiple top-level rules to fire when bypass enables them
  - Dynamic pipeline achieves 7 cycles (matches RTL's 8 cycles after reset)
  - All 39 Cmt2 tests pass
- **Interpreter method guard propagation** - Added `evaluateMethodGuards()` to propagate method guards to rule guards
  - Matches lowering pipeline behavior where method guards are ANDed with rule guards
  - Fixed FIRRTL type width handling in interpreter (was defaulting to 32-bit for `!firrtl.uint<1>`)
  - Added `getTypeWidth()` helper for FIRRTL type width extraction
- **cmt2-dbg script execution fix** - Script mode now exits properly after completion
- **Added Debugging & Simulation tracking section** (Medium Priority):
  - DS1: AddRuleFiringPort pass - optional pass for debugging mode that adds rule firing output ports
  - DS2: Testbench DSL - Python DSL for defining testbenches
  - DS3: JIT Python frontend for PyCMT2 - better interactive/incremental compilation

### 2026-01-07

- **Interpreter modularization complete (Phases 0-5)** - Plugin-based architecture for cmt2-dbg
  - Phase 0: ModuleInterpreter and ModuleInterpreterRegistry for external modules (Reg, FIFO, Memory)
  - Phase 1: StateManager with observer pattern, OpHandlerRegistry with TypeID-based dispatch
  - Phase 2: ControlFlowPlugin interface with DynamicControlPlugin and StaticControlPlugin
  - Phase 3: Integration into Cmt2Interpreter with `setPluginExecution()` API
  - Phase 4: StaticControlPlugin validates arg_timing/result_timing at runtime
  - Phase 5: Scheduler plugins - ORAATScheduler, AnnotationScheduler, PriorityScheduler
  - New files: Types.h, StateManager.h/cpp, OpHandlerRegistry.h/cpp, ControlFlowPlugin.h/cpp, Scheduler.h/cpp
  - New APIs: `setScheduler()`, `getSchedulerName()` for pluggable conflict resolution
  - All 39 Cmt2 tests pass
- **PyCMT2 ModuleLibrary integration complete** - External modules now properly bind to FIRRTL modules
  - Added `module_library.py` - Python equivalent of C++ `ModuleLibrary.cpp`
  - Parses `manifest.yaml`, calls `build.sh` scripts to generate FIRRTL from Chisel
  - Added `ExternalModuleBuilder.set_firrtl_module_name()` to set target FIRRTL module
  - Updated `Reg`, `Wire`, `Memory` to use ModuleLibrary for FIRRTL module names
  - External modules correctly reference parameterized FIRRTL names (e.g., `@Reg_width32_init0`)
  - PyCMT2 status updated from ~95% to 100%
- **PyCMT2 STL reimplementation complete** - All STL components now match ECMT2 architecture
  - FIFOs (FIFO1Push, FIFO1Pull, FIFO2I) reimplemented as CMT2 modules built from Reg/Wire primitives
  - WireDefault added as helper CMT2 module
  - Memory implemented with sync and async variants
  - Removed inline RTL generation - now uses ModuleLibrary
  - Fixed `call()` method in builders.py to support CMT2 module instance type lookups
  - Added `precedence()` support for MethodRef and ValueRef (not just RuleRef)
  - Test file: `examples/PyCMT2/stl_test.py` (9 tests, all passing)

### 2026-01-06

- **Interpreter modularization design** - Created design document for refactoring cmt2-dbg
  - Design: `docs/Dialects/Cmt2/tmp/InterpreterModularization-Design.md`
  - Goal: Extensible architecture with plugin support for static control
- Reorganized documentation structure
- Completed PyCMT2 timing attributes
- Added C API for timing attributes
- All 39 tests passing

### 2026-01-05

- CompileStatic wrapper transformation complete
- Procedural lowering pipeline complete

### 2026-01-04

- Test suite modernization (100% pass rate)
- Timing passes implementation

### 2026-01-03

- Timing attributes and analysis
- TimingInference and TimingValidation passes

### 2026-01-02

- PyCMT2 static control builders
- End-to-end simulation examples
