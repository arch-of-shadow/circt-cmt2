# Interpreter (`cmt2-dbg`): Coverage and Extensibility

`cmt2-dbg` is a **GAA-level** debugger/interpreter for stepping Cmt2 circuits
cycle-by-cycle. It is useful for understanding rule firing, precedence, and
control flow, but it is not a replacement for RTL simulation.

---

## What It Interprets

At a high level:

```
   Cmt2 IR (rules/methods/values)  --->  cmt2-dbg (cycle stepping)
   Generated SV                    --->  Verilator (cycle-accurate RTL)
```

- `cmt2-dbg` interprets **Cmt2 semantics** (rule guards, atomic bodies, proc
  control constructs that are represented in the IR).
- For **external modules**, `cmt2-dbg` only has semantics if an interpreter
  implementation exists for that external module.

If you need fidelity for ModuleLibrary-backed blocks (FIFO/memory variants),
prefer **RTL simulation** via `docs/Cmt2/guides/Debugging.md`.

---

## External Modules: When Do You Need an Interpreter?

Only truly **external** modules need interpreter semantics:

- If a component is emitted as regular Cmt2/FIRRTL/SV in the design, it will be
  executed naturally by RTL simulation.
- If a component is emitted as an *extern reference* (no body), then:
  - RTL simulation needs the RTL definition to exist in the workspace build, and
  - `cmt2-dbg` needs an interpreter implementation to model it at the IR level.

---

## Extensibility (Where to Look)

Interpreter support is implemented as a set of handlers/plugins in the C++ code.
To audit what’s supported, use commands like:

- `rg -n \"registerCMT2Handlers\" lib/Dialect/Cmt2/Interpreter`
- `rg -n \"OpHandler\" lib/Dialect/Cmt2/Interpreter`
- `rg -n \"ControlFlowPlugin\" lib/Dialect/Cmt2/Interpreter`

---

## Practical Guidance

- Use `cmt2-dbg` for:
  - rule enabling/firing debugging
  - precedence/conflict debugging
  - quick “what state changed?” inspection
- Use RTL simulation for:
  - timing behavior and multi-cycle pipelines
  - correctness of ModuleLibrary-backed memories/FIFOs
  - integration behavior with external RTL blocks

