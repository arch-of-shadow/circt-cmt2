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
| Documentation | **~80%** | Needs updates |

---

## Remaining Work

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
| **Total** | **39** | **100%** |

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
| [tmp/PrecedenceHandling.md](tmp/PrecedenceHandling.md) | FSM rule precedence design |
| [tmp/ProcInterpreterDesign.md](tmp/ProcInterpreterDesign.md) | Direct proc interpretation |
| [tmp/InterpreterModularization-Design.md](tmp/InterpreterModularization-Design.md) | Plugin architecture |
| [tmp/PyCMT2-STL-Reimplementation.md](tmp/PyCMT2-STL-Reimplementation.md) | STL component design |
