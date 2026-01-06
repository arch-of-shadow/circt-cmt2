# CMT2 Dialect Rationale

Design rationale for the CMT2 dialect, following [MLIR Rationale](https://mlir.llvm.org/docs/Rationale/) conventions.

---

## Introduction

CMT2 implements **Guarded Atomic Actions (GAA)** semantics for hardware design in MLIR. The dialect provides:

- **One-Rule-At-A-Time (ORAAT)** execution model
- **Ready-Enable** hardware contract
- **Automatic scheduling** with conflict detection
- **Multi-cycle operations** with FSM generation
- **Cycle-precise timing** for static control

---

## Core Concepts

### Guarded Atomic Actions (GAA)

GAA provides a state-transition based RTL design paradigm where:

1. **Rules** define atomic state transitions with boolean guards
2. **Methods** expose module interfaces with ready-enable protocol
3. **Values** provide read-only access to internal state
4. **Scheduling** resolves conflicts between concurrent rules

### One-Rule-At-A-Time (ORAAT)

The ORAAT semantics means:
- In each cycle, pick a rule whose guard is true
- Execute that rule atomically
- Commit results before the next cycle

This eliminates multi-write issues: rules can write to the same port, but won't be enabled together due to scheduling constraints.

### Hardware Contract

CMT2 uses the **ready-enable** contract:
- `ready`: Port can be operated in current state
- `enable`: Trigger state mutation when asserted

For each method:
- `ready = AND(all_port_ready_signals, explicit_guard)`
- `enable`: Asserted by scheduler when rule/method fires

---

## Operations

### Top-Level

| Operation | Description |
|-----------|-------------|
| `cmt2.circuit` | Container for modules |
| `cmt2.module` | CMT2 module with rules, methods, values |
| `cmt2.module.extern.firrtl` | External FIRRTL module binding |

### Function-Like

| Operation | Description |
|-----------|-------------|
| `cmt2.rule` | Rule with guard and body regions |
| `cmt2.method` | Action method with ready-enable |
| `cmt2.value` | Read-only value method |

Each function-like operation has two regions:
1. **Guard region**: Returns boolean condition
2. **Body region**: Contains actions when guard is true

```mlir
cmt2.rule @increment () -> () {
  // Guard region
  %ready = firrtl.constant 1 : !firrtl.uint<1>
  cmt2.return %ready : !firrtl.uint<1>
} {
  // Body region
  %val = cmt2.call @reg @read() : () -> !firrtl.uint<32>
  %one = firrtl.constant 1 : !firrtl.uint<32>
  %new = firrtl.add %val, %one : ...
  cmt2.call @reg @write(%new) : ...
  cmt2.return
}
```

### Instance and Call

| Operation | Description |
|-----------|-------------|
| `cmt2.instance` | Module instantiation |
| `cmt2.call` | Method/value invocation |

### Procedural Control

| Operation | Description |
|-----------|-------------|
| `cmt2.proc.step` | Dynamic step with done signal |
| `cmt2.proc.static_step` | Static step with fixed latency |
| `cmt2.proc.seq` | Sequential composition |
| `cmt2.proc.par` | Parallel composition |
| `cmt2.proc.if` | Dynamic conditional |
| `cmt2.proc.while` | Dynamic loop |
| `cmt2.proc.static_if` | Static conditional |
| `cmt2.proc.static_repeat` | Static loop |
| `cmt2.proc.enable` | Enable a step |
| `cmt2.proc.rule` | Multi-cycle rule |
| `cmt2.proc.method` | Multi-cycle method |

### External Binding

| Operation | Description |
|-----------|-------------|
| `cmt2.bind.bare` | Bind clock/reset |
| `cmt2.bind.value` | Bind value method |
| `cmt2.bind.method` | Bind action method |

### Interface

| Operation | Description |
|-----------|-------------|
| `cmt2.interface` | Interface definition |
| `cmt2.interface.def` | Interface binding |
| `cmt2.interface.decl` | Interface placeholder |

---

## Interfaces (MLIR Op Interfaces)

### Cmt2ModuleLike

Common abstraction for module-like operations (`ModuleOp`, `ExtModuleFirrtlOp`).

**Methods:**
- `lookupFunctionLike(StringAttr)`: Look up rule/method/value by name

### Cmt2FunctionLike

Abstraction for callable operations with two-region design.

**Methods:**
- `functionName()`, `functionNameAttr()`: Get function name
- `getFunctionKind()`: Returns Rule, Method, or Value
- `getCallableRegion()`: Get body region
- `getArgumentTypes()`, `getResultTypes()`: Type accessors
- `isExternal()`: True for bind ops (no regions)

**FunctionKind enum:**
- `Rule`: For `RuleOp`, `ProcRuleOp`
- `Method`: For `MethodOp`, `ProcMethodOp`, `BindMethodOp`
- `Value`: For `ValueOp`, `BindValueOp`

---

## Scheduling

### Conflict Matrix

The scheduler analyzes method calls to build relationships:

| Relation | Symbol | Meaning |
|----------|--------|---------|
| Conflict | `r0 / r1` | Cannot execute together |
| ConflictFree | `r0 <> r1` | Can execute in any order |
| SequenceBefore | `r0 < r1` | r0 must precede r1 |

### Scheduling Algorithm

1. **Bottom-up analysis**: Gather method call relationships from instances
2. **Conflict propagation**: Build conflict matrix for rules
3. **PLA generation**: Create scheduling logic
4. **Enable generation**: Compute enable signals from guards and conflicts

---

## Timing System

### Timing Attributes

| Attribute | Description |
|-----------|-------------|
| `#cmt2.timing<[s,e]>` | Half-open cycle interval |
| `#cmt2.latency<n>` | Port latency in cycles |
| `#cmt2.interval<n>` | Initiation interval |
| `#cmt2.port<kind,lat>` | Port timing (Go/Done/Data/Stable) |

### Call-Site Timing

```mlir
cmt2.call @mem @read(%addr) {
    arg_timing = [#cmt2.timing<[0, 1]>],
    result_timing = [#cmt2.timing<[2, 3]>]
} : ...
```

### Method Timing

```mlir
cmt2.method @multiply (...) -> (...) attributes {
    static_latency = 4 : i64,
    interval = #cmt2.interval<2>
} { ... }
```

---

## Compilation Pipeline

```
CMT2 IR
  → cmt2-compile-invoke        // Lower invoke to steps
  → cmt2-tdcc                   // Generate FSM for control
  → cmt2-static-inference      // Infer latencies
  → cmt2-static-promotion      // Promote to static
  → cmt2-timing-inference      // Infer timing
  → cmt2-timing-validation     // Validate constraints
  → cmt2-static-fsm-allocation // Allocate FSM states
  → cmt2-compile-static        // Generate FSM hardware
  → cmt2-proc-stmt-to-action   // Convert to action rules
  → cmt2-proc-to-gaa           // Final GAA conversion
  → cmt2-to-firrtl             // Convert to FIRRTL
  → firrtl-to-verilog          // Generate Verilog
```

---

## Design Decisions

### Two-Region Functions

Unlike MLIR's single-region `FunctionOpInterface`, CMT2 uses two regions:
- **Guard**: Boolean condition for execution
- **Body**: Actions when guard is true

This enables:
- Implicit ready signal computation
- Guard-body sharing of arguments
- Clear separation of control and data

### Ready-Enable vs Ready-Valid

CMT2 uses ready-enable (not ready-valid):
- `ready`: Can operate
- `enable`: Do operate

This matches GAA semantics where:
- Guards compute `ready`
- Scheduler generates `enable`

### Static vs Dynamic Control

CMT2 supports both:
- **Dynamic**: Runtime-determined completion (`proc.step`, `proc.while`)
- **Static**: Compile-time known latency (`proc.static_step`, `proc.static_repeat`)

Static control enables:
- Cycle-precise timing
- FSM optimization
- Pipelining

### FIRRTL Backend

CMT2 lowers to FIRRTL (not directly to Verilog):
- Leverages FIRRTL optimization passes
- Reuses FIRRTL-to-Verilog pipeline
- Enables FIRRTL ecosystem integration

---

## References

1. Arvind et al., "A Synthesizable Subset of System Verilog", 2010
2. Nikhil, "Bluespec System Verilog: Efficient, Correct RTL from High Level Specifications", 2004
3. Nigam et al., "Calyx: A Language for Hardware Accelerator Generators", 2020

---

## See Also

- [Concepts.md](Concepts.md) - GAA concepts for users
- [Operations.md](Operations.md) - Complete operation reference
- [Passes.md](Passes.md) - Transformation passes
- [MultiCycle.md](MultiCycle.md) - Multi-cycle operations
