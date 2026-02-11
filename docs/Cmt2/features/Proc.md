# Proc Control (Multi-cycle Control Flow)

Cmt2 supports **multi-cycle control flow** via *proc control*: structured sequencing
and synchronization that lowers to cycle-accurate hardware (typically an FSM plus
token/handshake wiring).

This page is a user-facing tour of proc control, with pointers to the underlying
IR and passes.

---

## Mental Model

Think of a proc rule as describing a **controller**:

```
            +---------------------------+
clk/rst --->|  proc controller (FSM)    |----> enables/calls to datapath
            |  - seq/par/if/while       |
            |  - dynamic + static steps |
            +---------------------------+
```

- **Dynamic steps** complete when an explicit `done` condition becomes true.
- **Static steps** complete after a fixed number of cycles (optionally pipelined).

Proc control composes steps with:
- `seq`: do A then B
- `par`: do A and B concurrently (join when both complete)
- `if`: conditional execution
- `while`: looping until a condition becomes false
- `static_repeat`: fixed-iteration loops for static schedules

---

## Dynamic vs Static

### Dynamic steps (variable latency)

Use dynamic steps when completion depends on runtime state (e.g., FIFO not empty).

```
step wait_for_data:
  done = !fifo_empty
  body: data = fifo.dequeue()
```

Key property: the controller may stay in the same “step state” for multiple
cycles until `done` is asserted.

### Static steps (fixed latency)

Use static steps when latency is a compile-time contract:

```
static_step latency=4:
  body: issue op
  (completes after 4 cycles)
```

Static steps are the foundation for **cycle-precise pipelines**:

- `static_latency`: cycles from start to completion
- `interval`: initiation interval (start a new iteration every N cycles)

See `docs/Cmt2/features/MultiCycle.md` and `docs/Cmt2/reference/Attributes.md`.

---

## How It Lowers (High Level)

Proc control is represented in Cmt2 IR and then lowered through a pipeline of
passes into:

1. A controller (FSM) that tracks which control node is active.
2. Enable/ready wiring for method calls, steps, and dataflow tokens.
3. Optional debug ports (rule-firing, etc.) for simulation/debugging.

```
      proc control tree                  lowered hardware shape

    seq( load, compute, store )     ->   FSM state: LOAD/COMPUTE/STORE
                                          + enables into datapath
```

Implementation pointers: `docs/Cmt2/features/Lowering.md`.

---

## Where to Look in IR / Passes

- User guide for multi-cycle + proc/dataflow surface:
  - `docs/Cmt2/features/MultiCycle.md`
- Operation reference:
  - `docs/Cmt2/reference/Operations.md`
- Pass reference:
  - `docs/Cmt2/reference/Passes.md`
- Timing implementation index:
  - `docs/Cmt2/features/Lowering.md`

---

## Validation (Examples)

Run these for confidence:

- Dynamic proc control: `examples/PyCMT2/proc.py`
- Static proc control + pipelining: `examples/PyCMT2/static_proc.py`

Curated runner (recommended):

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/run_examples.py
```
