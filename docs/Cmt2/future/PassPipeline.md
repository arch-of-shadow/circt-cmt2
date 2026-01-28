# CMT2 JIT Pass Pipeline

**Status:** Implemented (Task 3.5)  
**Last Updated:** 2026-01-28  

This document defines the **CMT2 JIT staged pass pipeline** and the public
configuration surface for selecting targets and optimization levels.

Implementation lives in `python/cmt2/passes/_pipeline.py`.

---

## Goals

- Provide a **staged** compilation pipeline:
  - `Elaborated` → `Lowered` → `Compiled`
- Make pass execution **configurable** by:
  - target (`simulation`, `verilog`, `fpga`)
  - optimization level (`O0`..`O3`)
- Keep pass scheduling **transparent**:
  - expose a string pipeline for debugging
  - document conceptual vs. real pass names

---

## Stages

### Stage 1: Elaborated

**Input:** MLIR module produced by tracing/elaboration (CMT2 dialect present).  
**Output:** Still CMT2, but simplified enough to reduce downstream work.

Conceptual passes:
- `constant_folding`
- `dead_code_elimination`

Implementation notes:
- Uses standard MLIR passes (module-scope) such as `canonicalize`, `cse`, `sccp`,
  and `symbol-dce` depending on `O` level.

### Stage 2: Lowered

**Input:** Elaborated-stage output.  
**Output:** CMT2 IR with proc control lowered to plain GAA rules, plus inlining.

Conceptual passes:
- `cmt2-canonicalize`
- `cmt2-inline`
- `proc-lowering`

Mapping to real (registered) passes:
- `cmt2-inline` (conceptual) →
  - `cmt2-inline-private-funcs`
  - `cmt2-inline-modules` (enabled at `O>=1`)
- `proc-lowering` →
  - `cmt2-compile-invoke`
  - `cmt2-tdcc`
  - `cmt2-proc-stmt-to-action`
  - `cmt2-proc-to-gaa`
- `cmt2-canonicalize` (conceptual) →
  - `canonicalize` / `cse` runs anchored on `cmt2.circuit(...)`

### Stage 3: Compiled (target-dependent)

**Input:** Lowered-stage output (still CMT2 dialect).  
**Output:** Target-ready MLIR and/or exported artifact.

All targets run:
- `lower-cmt2-to-firrtl`
- FIRRTL prep:
  - `firrtl.circuit(firrtl-infer-resets,firrtl-lower-types)`
  - `any(any(firrtl-expand-whens))`
- `lower-firrtl-to-hw`

Target-specific:
- `simulation`:
  - stops after HW lowering (backend execution handled elsewhere)
- `verilog`:
  - `lower-seq-to-sv`
  - optional `hw.module(lower-hw-to-sv)` at `O>=2`
  - export to SystemVerilog via `circt.export_verilog`
- `fpga`:
  - same as `verilog`
  - optional vendor passes appended (configurable; defaults empty)

Note on naming:
- The implementation plan used names like `firrtl-lower-to-hw` and `hw-to-sv`.
  In CIRCT these are registered as `lower-firrtl-to-hw` and `lower-hw-to-sv`.

---

## Optimization Levels (O0–O3)

Optimization levels control **how aggressively** we run general-purpose cleanup
passes (canonicalization/CSE/SCCP/DCE) and optional SV lowering steps.

### O0 (minimum)
- Run only required lowering/infrastructure passes.
- Avoid extra cleanup except what is necessary for correctness.

### O1 (basic)
- Enable canonicalization/CSE passes to simplify the IR.
- Enable module inlining (`cmt2-inline-modules`).

### O2 (default)
- Add SCCP-based constant folding and DCE at the Elaborated stage.
- Add additional cleanup after Lowered stage.
- Enable optional `hw.module(lower-hw-to-sv)` in the Compiled stage.

### O3 (aggressive)
- More repeated cleanup passes.
- Intended for final-quality codegen (may increase compile time).

---

## Python API

The pipeline is exposed via `cmt2.passes`.

```python
from cmt2.passes import PassPipeline, PassPipelineConfig, PipelineTarget

# Build a pipeline for verilog at O2 (default)
pipe = PassPipeline(PassPipelineConfig(
    target=PipelineTarget.VERILOG,
    optimization_level=2,
))

# Run a single stage
mlir_after_lowered = pipe.run_stage(mlir_module, stage="lowered")

# Run all stages and export SystemVerilog
sv = pipe.export_systemverilog(mlir_module)
```

### Debugging and introspection

Set `verbose_describe=True` to include the full derived pipeline strings:

```python
info = PassPipeline(PassPipelineConfig(
    target=PipelineTarget.SIMULATION,
    optimization_level=3,
    verbose_describe=True,
)).describe()
print(info["stages"]["compiled"]["pipeline"])
```

---

## Relationship to `jit/_stages.py`

The staged pipeline is designed to be used by (or alongside) the staged API
classes (`ElaboratedCircuit`, `LoweredCircuit`, `CompiledCircuit`) in
`python/cmt2/jit/_stages.py`. The pipeline module itself is independent and can
run directly on a `circt.ir.Module`.

