#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 Pass Pipeline and Transformations.

This subpackage provides pass pipeline management and transformation utilities
for CMT2 JIT compilation:
- PassPipeline: Configure and run optimization passes
- Stage transforms: Elaborated -> Lowered -> Compiled
- Target-specific pipelines: simulation, verilog, fpga

Example:
    from cmt2.passes import PassPipeline, OptimizationLevel
    
    # Create pipeline with optimization level
    pipeline = PassPipeline(OptimizationLevel.O2)
    
    # Run on elaborated circuit
    lowered = pipeline.lower(elaborated, target="verilog")
    compiled = pipeline.compile(lowered, target="simulation")
"""

from __future__ import annotations

from ._pipeline import (
    PassPipeline,
    OptimizationLevel,
    PassStage,
    PipelineConfig,
    TargetPipeline,
    get_default_pipeline,
)

__all__ = [
    "PassPipeline",
    "OptimizationLevel",
    "PassStage",
    "PipelineConfig",
    "TargetPipeline",
    "get_default_pipeline",
]
