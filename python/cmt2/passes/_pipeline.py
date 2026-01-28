#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 JIT pass pipeline.

Implements Task 3.5 from `docs/Cmt2/future/Implementation-Plan.md`.

The pipeline is *staged*:

  Elaborated -> Lowered -> Compiled

Each stage can be run independently to support caching and inspection.
The pipeline is also configurable by:
  - target: simulation | verilog | fpga
  - optimization level: 0..3

This module is intentionally self-contained and uses *lazy imports* so that it
can be imported in environments where the CIRCT Python bindings are not
available. Pass execution requires the bindings (`circt.passmanager`).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Iterable, Sequence


class PassPipelineError(RuntimeError):
    """Raised when the pass pipeline cannot be built or executed."""


class PipelineStage(str, Enum):
    """Compilation stages in the CMT2 JIT pipeline."""

    ELABORATED = "elaborated"
    LOWERED = "lowered"
    COMPILED = "compiled"

    @staticmethod
    def parse(value: str) -> "PipelineStage":
        v = value.strip().lower()
        if v in ("elab", "elaborated"):
            return PipelineStage.ELABORATED
        if v in ("lower", "lowered"):
            return PipelineStage.LOWERED
        if v in ("comp", "compiled"):
            return PipelineStage.COMPILED
        raise ValueError(f"Unsupported stage: {value!r}")


class PipelineTarget(str, Enum):
    """Compilation targets supported by the pipeline."""

    SIMULATION = "simulation"
    VERILOG = "verilog"
    FPGA = "fpga"

    @staticmethod
    def parse(value: str) -> "PipelineTarget":
        v = value.strip().lower()
        if v in ("sim", "simulation", "verilator"):
            return PipelineTarget.SIMULATION
        if v in ("sv", "systemverilog", "verilog"):
            return PipelineTarget.VERILOG
        if v in ("fpga",):
            return PipelineTarget.FPGA
        raise ValueError(f"Unsupported target: {value!r}")


@dataclass(frozen=True, slots=True)
class PassPipelineConfig:
    """Configuration for building and running the pass pipeline."""

    target: PipelineTarget = PipelineTarget.VERILOG
    optimization_level: int = 2

    # Extra passes to append at each stage. These are raw pass pipeline fragments
    # (e.g. "canonicalize" or "cmt2.circuit(cmt2-inline-modules)").
    extra_elaborated: tuple[str, ...] = ()
    extra_lowered: tuple[str, ...] = ()
    extra_compiled: tuple[str, ...] = ()

    # FPGA-specific: vendor passes are appended in the compiled stage. Keep empty
    # by default since vendor tooling varies and these passes may not exist.
    fpga_vendor_passes: tuple[str, ...] = ()

    # If true, returns a detailed per-stage description including derived passes.
    # This does not affect pass execution.
    verbose_describe: bool = False

    def __post_init__(self) -> None:
        if not (0 <= self.optimization_level <= 3):
            raise ValueError(
                f"optimization_level must be 0..3, got {self.optimization_level}"
            )


def _require_circt_bindings() -> Any:
    """Import CIRCT passmanager lazily, raising a helpful error on failure."""
    try:
        from circt.passmanager import PassManager  # type: ignore

        return PassManager
    except Exception as e:  # pragma: no cover (depends on external install)
        raise PassPipelineError(
            "CIRCT Python bindings are required to run passes. "
            "Set PYTHONPATH to include circt_core (e.g. build/tools/circt/python_packages/circt_core)."
        ) from e


def _infer_mlir_context(mlir_module: Any) -> Any:
    """Best-effort inference of an MLIR context from a module-like object."""
    ctx = getattr(mlir_module, "context", None)
    if ctx is not None:
        return ctx
    op = getattr(mlir_module, "operation", None)
    if op is not None:
        ctx = getattr(op, "context", None)
        if ctx is not None:
            return ctx
    raise PassPipelineError(
        "Unable to infer MLIR context from mlir_module; "
        "expected circt.ir.Module (or an object with .context or .operation.context)."
    )


def _clone_mlir_module(mlir_module: Any) -> Any:
    """Clone an MLIR module without a print/parse round-trip.

    Uses Operation.clone() similarly to `lib/Bindings/Python/pycmt2/circuit.py`.
    """
    try:
        from circt.ir import Module as MlirModule, Location, InsertionPoint  # type: ignore
    except Exception as e:  # pragma: no cover
        raise PassPipelineError(
            "CIRCT Python bindings are required to clone MLIR modules."
        ) from e

    ctx = _infer_mlir_context(mlir_module)
    loc = Location.unknown(ctx)

    new_module = MlirModule.create(loc)
    body = getattr(mlir_module, "body", None)
    if body is None:
        raise PassPipelineError("mlir_module has no .body; cannot clone safely.")

    with InsertionPoint(new_module.body):
        for op in body:
            op.operation.clone()
    return new_module


def _csv(items: Iterable[str]) -> str:
    return ",".join([i for i in items if i])


class PassPipeline:
    """Build and run the CMT2 staged pass pipeline."""

    def __init__(self, config: PassPipelineConfig | None = None):
        self.config = config or PassPipelineConfig()

    # -------------------------------------------------------------------------
    # Public API
    # -------------------------------------------------------------------------

    def pipeline_for_stage(self, stage: PipelineStage | str) -> str:
        """Return the textual MLIR pass pipeline for a given stage."""
        if isinstance(stage, str):
            stage = PipelineStage.parse(stage)
        if stage == PipelineStage.ELABORATED:
            return self._build_elaborated_pipeline()
        if stage == PipelineStage.LOWERED:
            return self._build_lowered_pipeline()
        if stage == PipelineStage.COMPILED:
            return self._build_compiled_pipeline()
        raise ValueError(f"Unknown stage: {stage}")

    def run_stage(
        self, mlir_module: Any, stage: PipelineStage | str, *, clone: bool = True
    ) -> Any:
        """Run a single stage on an MLIR module and return the transformed module."""
        PassManager = _require_circt_bindings()
        if isinstance(stage, str):
            stage = PipelineStage.parse(stage)
        module_to_run = _clone_mlir_module(mlir_module) if clone else mlir_module
        ctx = _infer_mlir_context(module_to_run)
        pipeline = self.pipeline_for_stage(stage)
        pm = PassManager.parse(pipeline, context=ctx)
        pm.run(module_to_run.operation)
        return module_to_run

    def run_all(self, mlir_module: Any, *, clone: bool = True) -> Any:
        """Run Elaborated->Lowered->Compiled stages sequentially."""
        m = self.run_stage(mlir_module, PipelineStage.ELABORATED, clone=clone)
        m = self.run_stage(m, PipelineStage.LOWERED, clone=False)
        m = self.run_stage(m, PipelineStage.COMPILED, clone=False)
        return m

    def export_systemverilog(self, mlir_module: Any, *, clone: bool = True) -> str:
        """Run the full pipeline and return SystemVerilog as a string.

        This requires `circt.export_verilog`.
        """
        m = self.run_all(mlir_module, clone=clone)
        try:
            from circt import export_verilog  # type: ignore
            import io

            out = io.StringIO()
            export_verilog(m, out)
            return out.getvalue()
        except Exception as e:  # pragma: no cover
            raise PassPipelineError(
                "Failed to export SystemVerilog. Ensure circt.export_verilog is available."
            ) from e

    def describe(self) -> dict[str, Any]:
        """Return a structured description of the configured pipeline."""
        cfg = self.config
        return {
            "target": cfg.target.value,
            "optimization_level": cfg.optimization_level,
            "stages": {
                PipelineStage.ELABORATED.value: self._describe_elaborated(),
                PipelineStage.LOWERED.value: self._describe_lowered(),
                PipelineStage.COMPILED.value: self._describe_compiled(),
            },
        }

    # -------------------------------------------------------------------------
    # Stage construction
    # -------------------------------------------------------------------------

    def _build_elaborated_pipeline(self) -> str:
        cfg = self.config
        o = cfg.optimization_level

        # Conceptual groups (Task 3.5): constant_folding, dead_code_elimination.
        # Use standard MLIR passes at module scope.
        passes: list[str] = []
        if o >= 1:
            passes += ["canonicalize", "cse"]
        if o >= 2:
            passes += ["sccp", "canonicalize", "symbol-dce"]
        if o >= 3:
            passes += ["cse", "symbol-dce"]

        passes += list(cfg.extra_elaborated)

        return f"builtin.module({_csv(passes)})"

    def _build_lowered_pipeline(self) -> str:
        cfg = self.config
        o = cfg.optimization_level

        # Conceptual groups (Task 3.5): cmt2-canonicalize, cmt2-inline, proc-lowering.
        # CMT2 dialect passes are anchored on cmt2.circuit.
        cmt2_passes: list[str] = []

        # "cmt2-inline" (as used in the plan) corresponds to these real passes:
        # - cmt2-inline-private-funcs
        # - cmt2-inline-modules
        cmt2_passes.append("cmt2-inline-private-funcs")
        if o >= 1:
            cmt2_passes.append("cmt2-inline-modules")

        # "proc-lowering"
        cmt2_passes += [
            "cmt2-compile-invoke",
            "cmt2-tdcc",
            "cmt2-proc-stmt-to-action",
            "cmt2-proc-to-gaa",
        ]

        # "cmt2-canonicalize" (conceptual) => standard canonicalization/CSE runs.
        if o >= 1:
            cmt2_passes += ["canonicalize", "cse"]
        if o >= 2:
            cmt2_passes += ["canonicalize"]

        # Module-level cleanup after CMT2 passes.
        module_passes: list[str] = []
        if o >= 2:
            module_passes += ["symbol-dce"]

        # Allow users to append raw fragments; these may include nesting.
        module_passes += list(cfg.extra_lowered)

        parts = [f"cmt2.circuit({_csv(cmt2_passes)})"]
        parts += module_passes
        return f"builtin.module({_csv(parts)})"

    def _build_compiled_pipeline(self) -> str:
        cfg = self.config
        o = cfg.optimization_level

        # After Lowered stage, compile to target-dependent forms.
        module_passes: list[str] = []

        # CMT2 -> FIRRTL conversion is a module pass.
        module_passes.append("lower-cmt2-to-firrtl")

        # FIRRTL prep (mirrors `pycmt2.circuit.emit_verilog`).
        # Keep this at all opt levels since downstream lowering often expects it.
        module_passes.append("firrtl.circuit(firrtl-infer-resets,firrtl-lower-types)")
        module_passes.append("any(any(firrtl-expand-whens))")

        # FIRRTL -> HW/SV.
        module_passes.append("lower-firrtl-to-hw")

        if cfg.target in (PipelineTarget.VERILOG, PipelineTarget.FPGA):
            module_passes.append("lower-seq-to-sv")
            if o >= 2:
                # Optional extra SV canonicalization at the HW module level.
                module_passes.append("hw.module(lower-hw-to-sv)")

        if cfg.target == PipelineTarget.FPGA and cfg.fpga_vendor_passes:
            module_passes += list(cfg.fpga_vendor_passes)

        module_passes += list(cfg.extra_compiled)
        return f"builtin.module({_csv(module_passes)})"

    # -------------------------------------------------------------------------
    # Descriptions (docs + debugging)
    # -------------------------------------------------------------------------

    def _describe_elaborated(self) -> dict[str, Any]:
        cfg = self.config
        return {
            "conceptual": ["constant_folding", "dead_code_elimination"],
            "pipeline": self._build_elaborated_pipeline()
            if cfg.verbose_describe
            else None,
        }

    def _describe_lowered(self) -> dict[str, Any]:
        cfg = self.config
        return {
            "conceptual": ["cmt2-canonicalize", "cmt2-inline", "proc-lowering"],
            "pipeline": self._build_lowered_pipeline() if cfg.verbose_describe else None,
            "notes": [
                "cmt2-inline maps to cmt2-inline-private-funcs (+ cmt2-inline-modules at O>=1).",
                "cmt2-canonicalize maps to canonicalize/cse runs within cmt2.circuit.",
                "proc-lowering maps to cmt2-compile-invoke,cmt2-tdcc,cmt2-proc-stmt-to-action,cmt2-proc-to-gaa.",
            ]
            if cfg.verbose_describe
            else None,
        }

    def _describe_compiled(self) -> dict[str, Any]:
        cfg = self.config
        return {
            "target": cfg.target.value,
            "pipeline": self._build_compiled_pipeline() if cfg.verbose_describe else None,
            "notes": [
                "Implementation-Plan names differ slightly from actual registered pass names "
                "(e.g. lower-firrtl-to-hw, lower-hw-to-sv)."
            ]
            if cfg.verbose_describe
            else None,
        }
