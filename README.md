<p align="center"><img src="docs/includes/img/circt-logo.svg"/></p>

# circt-cmt2

This repository is a CIRCT fork focused on the **Cmt2** dialect and the **PyCMT2** Python EDSL/tooling. It targets cycle-accurate hardware construction using **Guarded Atomic Actions (GAA)** / **One-Rule-At-A-Time (ORAAT)** semantics, plus token-based **dataflow/pipeline** modeling and end-to-end simulation workflows.

Start here:
- Docs landing page: `docs/Cmt2/_index.md`
- Feature tour: `docs/Cmt2/features/Concepts.md`

## What’s in this repo (Cmt2-specific)

- **Cmt2 IR + lowering**: rules/methods/values, proc control, multi-cycle timing, dataflow tasks/tokens, conversion to RTL.
  - Proc: `docs/Cmt2/features/Proc.md`
  - Multi-cycle: `docs/Cmt2/features/MultiCycle.md`
  - Dataflow: `docs/Cmt2/features/Dataflow.md`
  - Lowering: `docs/Cmt2/features/Lowering.md`
- **Debugging & simulation**
  - C++ interpreter/debugger tool: `build/bin/cmt2-dbg` (see `docs/Cmt2/guides/Debugging.md`)
  - PyCMT2 simulation workspaces (Verilator): `docs/Cmt2/features/Interpreter.md`
- **STL / ModuleLibrary** (Reg/Wire/Memory, extern modules): `docs/Cmt2/features/STL.md`
- **Examples**
  - Quick examples list: `docs/Cmt2/examples/Examples.md`
  - PyCMT2 E2E runner: `examples/PyCMT2/run_examples.py`
  - Banked-memory tiled GEMM (dataflow load/compute/store): `examples/PyCMT2/banked_gemm_dataflow.py`

## Build (CIRCT + Cmt2)

This is a standard CIRCT build with an `llvm` submodule.

```sh
# From repo root
git submodule update --init --recursive

cmake -G Ninja llvm/llvm -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_TARGETS_TO_BUILD=host \
  -DLLVM_ENABLE_PROJECTS=mlir \
  -DLLVM_EXTERNAL_PROJECTS=circt \
  -DLLVM_EXTERNAL_CIRCT_SOURCE_DIR=$PWD

ninja -C build check-circt
```

## Quick sanity: run PyCMT2 E2E examples

```sh
PYTHONPATH=build/tools/circt/python_packages/circt_core \
  python3 examples/PyCMT2/run_examples.py --keep-going
```

## Upstream CIRCT

This fork is based on CIRCT (MLIR/LLVM). For general CIRCT information, see:
- CIRCT upstream: `https://github.com/llvm/circt`
- CIRCT docs (in-tree): `docs/`
