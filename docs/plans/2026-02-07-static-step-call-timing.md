# Static-Step Multicycle Call Timing (call\_timing + call\_ty)

## Motivation

The current `cmt2.proc.static_step` lowering interprets `arg_timing`/`result_timing` as an **enable window**, causing a `cmt2.call` to be re-issued in every cycle of the window during procedural lowering. This is incorrect for “start once, complete later” multicycle calls and breaks the intended “value validity” meaning of timing attributes.

This design restores paper-like semantics:

- `call_timing`: when the call **starts** (i.e., the callee samples inputs / sees the start pulse).
- `arg_timing`: when each argument must be **valid**.
- `result_timing`: when each result is **valid/captured**.
- A multicycle call is issued **once**, and its result is observed later; it is *not* repeatedly invoked.

## IR Changes

### `cmt2.call` new attributes

- `call_timing: #cmt2.timing<[s, e)>` (optional; only meaningful inside `cmt2.proc.static_step`)
  - Defaults to `#cmt2.timing<[0, 1)>` if omitted.
  - **Restriction (initial):** must be a single-cycle interval (`e == s + 1`).

- `call_ty: "Enable" | "GetRes"` (optional; used on lowered/cloned calls outside `static_step`)
  - Absent means legacy behavior (treated like `"Enable"` for method calls).

## Semantics

For a `cmt2.call` inside `cmt2.proc.static_step`:

- **Start:** the call starts at `call_timing.start` (one cycle).
- **Arguments:** each argument must be valid at the start cycle.
  - **Restriction (initial):** every `arg_timing[i]` must equal `call_timing` (no implicit “hold regs”).
- **Results:** results become valid at `result_timing.start`.
  - **Restriction (initial):** all results must share the same single-cycle `result_timing`.

Method latency validation uses the call start:

- If the callee’s declared static latency is `L`, then results must not be captured before
  `call_timing.start + L`.
  - **Restriction (initial):** require equality (`result_timing.start == call_timing.start + L`)
    unless/until we model “stable-until-next-call” explicitly.

## Lowering Strategy (FSM construction)

Static-step bodies are cloned into per-FSM-state rules. For multicycle calls, we split the behavior
across states *without re-issuing* the call:

- At local state `call_timing.start`: clone the call with `call_ty="Enable"` (drives enable=1).
- At local state `result_timing.start`: clone the call with `call_ty="GetRes"` (drives enable=0,
  but reads/matches result ports to SSA values).

This requires `StaticFSMAllocation` to annotate per-state call actions (Enable/GetRes) rather than
marking a whole “active window”.

## Caveat: cross-cycle SSA dependencies

Because per-cycle lowering clones ops into separate rules, **SSA values cannot implicitly flow
across cycles**. Any value produced in one scheduled cycle and used by a call in a different cycle
requires explicit state (e.g., a `Reg`).

We keep an explicit diagnostic for this pattern and document it as a current restriction; future
work may introduce automatic promotion/insertion of state elements.

## Deliverables

- Update passes: `TimingInference`, `TimingValidation`, `StaticFSMAllocation`, `CompileStatic`,
  `ProcStmtToAction`, `Cmt2ToFIRRTL`.
- Update docs: `docs/Cmt2/features/MultiCycle.md`, `docs/Cmt2/reference/Attributes.md`.
- Replace `examples/PyCMT2/static_step_call_timing_window.py` with a correct, simulatable example
  showing “start once, result later”.

