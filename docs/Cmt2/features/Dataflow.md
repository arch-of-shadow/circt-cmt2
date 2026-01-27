# Dataflow Tasks (Decoupled Pipelines)

Cmt2 supports **dataflow-style pipelines** via `proc.dataflow` and task/token
operations. The goal is to make it easy to build **decoupled** load/compute/store
pipelines where stages can be latency-sensitive (fixed timing) or latency-insensitive
(backpressure-aware).

---

## Two Styles: LS vs LI

### Latency-Sensitive (LS)

Use LS when the timing between stages is fixed and known:

```
stage A  --(token @ cycle k)-->  stage B  --(token @ cycle k+N)--> stage C
          (implemented as shift registers / fixed delays)
```

Pros: compact hardware, easy to reason about cycle timing.

### Latency-Insensitive (LI)

Use LI when stages are decoupled and may stall independently:

```
stage A  -->  FIFO/queue  -->  stage B  -->  FIFO/queue  -->  stage C
        (valid/ready + backpressure)
```

Pros: robust to variable latency and backpressure, natural for memory systems.

---

## Tokens and Tasks (Mental Model)

Dataflow is expressed in terms of **tasks** and **tokens**:

- A **task** runs when its input tokens are available.
- A **token** represents “permission/data is ready” and can be created, joined,
  or forwarded between tasks.

```
      token_in
         |
         v
     +--------+    token_out
     |  task  |-------->
     +--------+
```

In practice, tasks typically:
- read operands / consume tokens
- perform work (possibly multi-cycle)
- produce output tokens and/or data

---

## Where It Lives (Docs / IR)

- Multi-cycle guide (proc + dataflow surface): `docs/Cmt2/features/MultiCycle.md`
- Operation reference (task/token ops): `docs/Cmt2/reference/Operations.md`

If you need precise timing annotations/contracts, also see:
- `docs/Cmt2/reference/Attributes.md`
- `docs/Cmt2/features/Lowering.md`

---

## Validation (Examples)

- Comprehensive dataflow: `examples/PyCMT2/comprehensive_dataflow_example.py`
- Fork/join patterns: `examples/PyCMT2/dataflow_forkjoin.py`
- Banked-memory GEMM (decoupled stages): `examples/PyCMT2/banked_gemm_dataflow.py`

Curated runner (recommended):

```bash
cd build
PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/run_examples.py
```
