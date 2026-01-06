# CMT2 vs Calyx: Design Comparison

Comparison of CMT2's procedural layer with Calyx's control compilation.

---

## Overview

| Aspect | CMT2 | Calyx |
|--------|------|-------|
| **Purpose** | GAA with multi-cycle control | HLS intermediate language |
| **Abstraction** | Rule/method-centric | Group-centric |
| **Timing** | Cycle-precise with attributes | Port annotations |
| **Backend** | FIRRTL → Verilog | Direct Verilog |

---

## Timing Systems

### Calyx Port Annotations

```futil
primitive std_mult_pipe[WIDTH](
    @clk clk: 1,
    @reset reset: 1,
    @write_together(1) @interval(3) @go go: 1,
    @write_together(1) @data left: WIDTH,
    @write_together(1) @data right: WIDTH,
) -> (
    @stable out: WIDTH,
    @done done: 1
);
```

| Attribute | Meaning |
|-----------|---------|
| `@go(n)` | Go signal with optional latency |
| `@done(n)` | Done signal at cycle n |
| `@interval(n)` | Initiation interval |
| `@stable` | Output is latched |
| `@data` | Pure data port |

### CMT2 Timing Attributes

```mlir
cmt2.bind.method @multiply static<4> : (...) -> (...) {
    arg_port_timing = [#cmt2.port<data, 0>],
    result_port_timing = [#cmt2.port<data, 4>],
    interval = #cmt2.interval<2>
}
```

| Attribute | Meaning |
|-----------|---------|
| `#cmt2.timing<[s,e]>` | Cycle interval [s,e) |
| `#cmt2.latency<n>` | Port latency |
| `#cmt2.interval<n>` | Initiation interval |
| `#cmt2.port<kind,lat>` | Port type with latency |

### Call-Site Timing

**Calyx** (inside groups):
```futil
static<4> group multiply {
  mult.left = %[0:1] ? x;     // Cycle 0
  mult.right = %[0:1] ? y;    // Cycle 0
  ans.in = %3 ? mult.out;     // Cycle 3
}
```

**CMT2** (on calls):
```mlir
cmt2.call @mult @multiply(%a, %b) {
    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
    result_timing = [#cmt2.timing<[4, 5]>]
} : ...
```

---

## Compilation Architecture

Both use a two-phase approach:

```
Phase 1: Static Compilation
  • Process static control first
  • Generate FSM for timing
  • Wrap as dynamic-compatible unit

Phase 2: Dynamic Compilation (TDCC)
  • Handle all control uniformly
  • Both original dynamic and wrapped static
```

### CMT2 Pipeline

```
┌─────────────────────────────────────────────────────┐
│  Static Compilation Phase                           │
│  TimingInference → TimingValidation                │
│  → StaticFSMAllocation → CompileStatic             │
└─────────────────────────────────────────────────────┘
                        │
    CompileStatic: static_step → wrapper step
    (internal FSM + done signal)
                        │
                        ▼
┌─────────────────────────────────────────────────────┐
│  Dynamic Compilation Phase                          │
│  CompileInvoke → TDCC → ProcStmtToAction → ProcToGAA│
└─────────────────────────────────────────────────────┘
                        │
                        ▼
                   FIRRTL → Verilog
```

### Calyx Pipeline

```
┌─────────────────────────────────────────────────────┐
│  Static Compilation Phase                           │
│  StaticInference → StaticPromotion → StaticInliner │
│  → StaticFSMAllocation → CompileStatic             │
└─────────────────────────────────────────────────────┘
                        │
    CompileStatic: static group → dynamic wrapper
                        │
                        ▼
┌─────────────────────────────────────────────────────┐
│  Dynamic Compilation Phase                          │
│  TopDownCompileControl (TDCC)                      │
└─────────────────────────────────────────────────────┘
                        │
                        ▼
                    Verilog
```

---

## Feature Comparison

| Feature | CMT2 | Calyx |
|---------|------|-------|
| Method latency | `static<n>` | `static<n>` |
| Port timing | `#cmt2.port<kind,lat>` | `@go`, `@done`, `@stable` |
| Call timing | `arg_timing`, `result_timing` | `%[i:j]` guards |
| Timing validation | `cmt2-timing-validation` | Multi-stage |
| Initiation interval | `#cmt2.interval<n>` | `@interval(n)` |
| FSM encoding | Binary/one-hot | Configurable |
| FSM sharing | Graph coloring | Graph coloring |

---

## Implementation Status

| Component | CMT2 | Calyx |
|-----------|------|-------|
| Static timing passes | ✅ | ✅ |
| Timing inference | ✅ | ✅ |
| Timing validation | ✅ | ✅ |
| CompileStatic wrapper | ✅ | ✅ |
| TDCC | ✅ | ✅ |
| FSM generation | ✅ | ✅ |
| Early-reset optimization | ✅ | ✅ |

---

## Key Differences

### 1. Abstraction Level

**CMT2**: Rule-based with ready-enable protocol
- Rules are top-level scheduling units
- Methods expose interfaces
- Scheduling resolves conflicts

**Calyx**: Group-based with go-done protocol
- Groups are execution units
- Control tree schedules groups
- No conflict resolution (user responsibility)

### 2. Backend

**CMT2**: Lowers to FIRRTL
- Leverages FIRRTL optimizations
- Reuses FIRRTL toolchain

**Calyx**: Direct Verilog generation
- Complete control over output
- No intermediate IR

### 3. Scheduling

**CMT2**: Automatic conflict detection
- Compiler builds conflict matrix
- Rules scheduled based on conflicts

**Calyx**: Explicit control
- User specifies control tree
- No automatic scheduling

---

## Summary

CMT2 adopts Calyx's timing system with adaptations for GAA semantics:

1. **Timing attributes** on operations instead of ports
2. **Call-site timing** for precise scheduling
3. **Two-phase compilation** (static then dynamic)
4. **FIRRTL backend** for integration

This combines GAA's rule-based abstraction with Calyx's cycle-precise timing.

---

## See Also

- [MultiCycle.md](MultiCycle.md) - User guide
- [Cmt2Proc-Design.md](Cmt2Proc-Design.md) - Procedural design
- [Passes.md](Passes.md) - Pass reference
