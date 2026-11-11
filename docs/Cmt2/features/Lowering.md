# Lowering Overview: Proc, Dataflow, and RTL Generation

This page summarizes how high-level Cmt2 constructs (proc control and dataflow)
lower into RTL-ready IR and ultimately SystemVerilog.

It is release-facing: it focuses on **what is implemented** and where to find it
in the codebase (not long design history).

---

## Big Picture

```
PyCMT2 / ECMT2
   |
   v
Cmt2 IR (rules/methods/values + proc + dataflow)
   |
   v
Lowering passes (proc/dataflow/timing)
   |
   v
FIRRTL / HW / SV
   |
   v
SystemVerilog (plus ModuleLibrary RTL as needed for simulation)
```

---

## Proc Control Lowering

Proc control constructs (seq/par/if/while/static_step/…) describe a controller.
Lowering materializes control state (often FSM-like) and wires enables/guards to
the datapath calls.

Manual “extract” commands:

- Find proc transforms:
  - `rg -n \"Proc\" lib/Dialect/Cmt2/Transforms | head`
- Find proc ops:
  - `rg -n \"Proc\" include/circt/Dialect/Cmt2 | rg -n \"Ops\\.td|\\.h\"`

Related user docs:
- `docs/Cmt2/features/Proc.md`
- `docs/Cmt2/features/MultiCycle.md`

---

## Dataflow Lowering

Dataflow constructs (`proc.dataflow`, task/token ops) express decoupled pipelines.
Lowering connects task scheduling, token readiness, and (when needed) queueing.

Manual “extract” commands:

- Find dataflow/token transforms:
  - `rg -n \"Token\" lib/Dialect/Cmt2/Transforms | head`
  - `rg -n \"Dataflow\" lib/Dialect/Cmt2/Transforms | head`

Related user docs:
- `docs/Cmt2/features/Dataflow.md`

---

## Timing (Static Latency / Interval)

Cycle-precise timing contracts are validated and used to allocate control states
and to gate calls/results at the correct cycles.

Manual “extract” commands:

- Find timing analysis/passes:
  - `rg -n \"Timing\" lib/Dialect/Cmt2/Analysis lib/Dialect/Cmt2/Transforms | head`

Related user docs:
- `docs/Cmt2/reference/Attributes.md`
- `docs/Cmt2/features/MultiCycle.md`

---

## SV Generation and ModuleLibrary RTL

The emitted SystemVerilog may reference ModuleLibrary-backed extern modules.
Simulation workspaces should include the referenced RTL modules (exact variants).

Manual “extract” commands:

- Find SimulationWorkspace module injection:
  - `rg -n \"SimulationWorkspace\" -S lib/Bindings/Python/pycmt2`
- Find ModuleLibrary references:
  - `rg -n \"ModuleLibrary\" -S lib include`

