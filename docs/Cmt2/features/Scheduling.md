# Scheduling: Conflicts, Precedence, and ORAAT

Cmt2 is based on **Guarded Atomic Actions (GAA)** with a **One-Rule-At-A-Time
(ORAAT)** execution model. In each cycle, the scheduler selects which enabled
rule(s) (and compatible method calls) can fire while preserving atomicity and
user-declared scheduling constraints.

---

## Conflicts vs Ordering (Concepts)

Two operations can relate in three common ways:

```
conflict:          A <> B    (cannot happen in same cycle)
ordered:           A <  B    (A must be before B if both occur)
conflict-free:     A || B    (can happen together)
```

Where “operations” here includes rules and method/value calls that touch shared
state (e.g., the same register, FIFO, or memory port).

In practice, conflicts/ordering often come from:
- STL/ModuleLibrary extern bindings (declared scheduling relationships)
- Explicit user constraints (precedence chains)
- Compiler-inferred hazards during lowering/scheduling

---

## Precedence (Priority)

When multiple rules are enabled and mutually exclusive (conflicting), **precedence**
defines the priority order:

```
precedence chain:  rule_a > rule_b > rule_c
```

Meaning: if `rule_a` can fire, it blocks lower-priority conflicting rules.

### PyCMT2 surface

Precedence is typically expressed via the module builder (e.g., `mod.precedence(...)`).

### MLIR surface

At the IR level, precedence is carried as a module attribute (a list of chains)
that is consumed by scheduling/lowering.

---

## How to Validate (Manual, Evidence-Based)

Use these commands as “extract checks” when reviewing precedence/scheduling claims:

- Find precedence users in the code:
  - `rg -n "precedence" lib/Dialect/Cmt2 include/circt/Dialect/Cmt2`
- Find the precedence attribute/printing in IR:
  - `rg -n "precedence" include/circt/Dialect/Cmt2 | rg -n "\\.td|\\.h"`
- Find scheduler/conflict analysis entry points:
  - `rg -n "Conflict|conflict" lib/Dialect/Cmt2`

---

## Related Reading

- Core model: `docs/Cmt2/features/Concepts.md`
- Proc control: `docs/Cmt2/features/Proc.md`
- Operation reference: `docs/Cmt2/reference/Operations.md`

