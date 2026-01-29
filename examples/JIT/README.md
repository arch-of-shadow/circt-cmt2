# Cmt2 JIT Examples

This folder contains **JIT-only** examples. JIT is a thin syntax layer
stacked on PyCMT2:

- JIT: `cmt2.jit` (rules/methods/values ergonomics)
- PyCMT2: `circt.pycmt2` (builders, codegen, SimulationWorkspace, Testbench)

## Quick run

From repo root:

```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/counter_minimal.py --emit mlir
```

## Examples

- `examples/JIT/counter_minimal.py`: minimal counter (`Reg` + one rule + one value)
- `examples/JIT/fifo.py`: FIFO producer/consumer rules (guarded by `fifo.full/empty`)

## End-to-end (simulation + testbench)

This folder contains a **JIT reimplementation** of every E2E script in
`examples/PyCMT2/` (same designs/testbenches, but using `cmt2.jit` for module
elaboration).

Run the full suite:

```bash
PYTHONPATH=build/tools/circt/python_packages/circt_core:python \
  python3 examples/JIT/run_examples.py --keep-going
```

Note: these examples generate `sim_*`, `*_workspace`, and `*_sim` folders under
`examples/JIT/` (gitignored).
