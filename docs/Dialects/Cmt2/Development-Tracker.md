# CMT2 Development Tracker

Consolidated tracker for ongoing work and TODOs.

**Last Updated:** 2026-01-06

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
| PyCMT2 | **100%** | Full API, STL, simulation |
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
- [x] STL components (Reg, FIFO, Memory)
- [x] External module bindings
- [x] Timing parameters (static_latency, interval)
- [x] SimulationWorkspace
- [x] Source location tracking

### Testing
- [x] 39 MLIR tests (100% passing)
- [x] End-to-end simulation examples
- [x] Timing validation tests

---

## Remaining Work

### Documentation (Priority: Medium)

| Task | Status | Notes |
|------|--------|-------|
| ECMT2-Guide.md (C++ API) | [ ] | Port from ecmt2-EDSL.md |
| Update Debugging.md | [~] | Add interpreter details |
| Add more code examples | [ ] | In each guide |
| API reference generation | [ ] | From docstrings |

### Features (Priority: Low)

| Task | Status | Notes |
|------|--------|-------|
| Shorthand `{timing = n}` syntax | [ ] | Expands to `[n, n+1]` |
| Timing dependency graph | [ ] | For complex inference |
| Memory timing integration | [ ] | Memory with read latency |

### Tooling (Priority: Low)

| Task | Status | Notes |
|------|--------|-------|
| cmt2-dbg command-line tool | [~] | Basic framework exists |
| Waveform annotation | [ ] | FSM state visualization |
| IDE integration | [ ] | VSCode extension |

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

### 2026-01-06
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
