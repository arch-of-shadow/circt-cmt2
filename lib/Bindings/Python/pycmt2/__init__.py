#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
PyCMT2: High-level Python EDSL for the CMT2 dialect.

This module provides a Pythonic API for building CMT2 hardware designs
using context managers, strong typing, and object references.

Example:
    from pycmt2 import Circuit, UInt

    circuit = Circuit("Counter")

    with circuit.module("Counter") as mod:
        clk = mod.clock()
        rst = mod.reset()

        count_reg = mod.instance(Reg(32, init=0))

        with mod.rule("increment") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                val = b.call(count_reg, count_reg.read)
                b.call(count_reg, count_reg.write, val + b.const(1, 32))

    print(circuit.emit_mlir())
"""

from .types import (
    Cmt2Type,
    UInt,
    SInt,
    ClockType,
    ResetType,
    AsyncResetType,
    Bundle,
    Vector,
    Clock,
    Reset,
    AsyncReset,
    Bool,
    SyncToken,
)

from .signals import Signal

from .circuit import Circuit

from .module import ModuleBuilder

from .refs import MethodRef, ValueRef, StepRef, RuleRef, Instance, InterfaceDecl

from .interface import InterfaceBuilder, InterfaceDefBuilder

from .simulation import SimulationWorkspace

from .testbench import Testbench, TestSequence

from .stl import (
    Reg,
    Wire,
    FIFO,
    FIFO1Push,
    FIFO1Pull,
    FIFO2I,
    ShiftReg,
    Memory,
    get_stl_rtl_files,
    add_stl_rtl_to_workspace,
    clear_stl_registry,
)

from .location import PythonLocation, get_python_location, LocationTracker

from .external_module import ExternalModuleBuilder, ExternalModuleInstance

from .diagnostics import (
    DiagnosticLevel,
    Diagnostic,
    DiagnosticHandler,
    emit_error,
    emit_warning,
    emit_info,
    emit_debug,
    type_mismatch_error,
    undefined_reference_error,
    scheduling_conflict_warning,
    format_diagnostic_with_source,
)

from .dataflow_builders import (
    DataflowBuilder,
    TaskBuilder,
    Token,
    pipeline_dataflow,
)

from .pipeline_builders import (
    Pipeline,
    ForkJoinPipeline,
    PipelineStage,
)

from .timing import (
    TimingInterval,
    timing_interval,
    single_cycle,
    pipeline_timing,
    interleaved_timing,
    total_latency,
    validate_timing,
    timing_to_attr_tuple,
    timing_list_to_tuples,
    arg_timing,
    result_timing,
    IMMEDIATE,
)

__all__ = [
    # Types
    "Cmt2Type",
    "UInt",
    "SInt",
    "ClockType",
    "ResetType",
    "AsyncResetType",
    "Bundle",
    "Vector",
    "Clock",
    "Reset",
    "AsyncReset",
    "Bool",
    "SyncToken",
    # Signals
    "Signal",
    # Builders
    "Circuit",
    "ModuleBuilder",
    "InterfaceBuilder",
    "InterfaceDefBuilder",
    # Dataflow
    "DataflowBuilder",
    "TaskBuilder",
    "Token",
    "pipeline_dataflow",
    # Pipeline shortcuts
    "Pipeline",
    "ForkJoinPipeline",
    "PipelineStage",
    # Timing helpers
    "TimingInterval",
    "timing_interval",
    "single_cycle",
    "pipeline_timing",
    "interleaved_timing",
    "total_latency",
    "validate_timing",
    "timing_to_attr_tuple",
    "timing_list_to_tuples",
    "arg_timing",
    "result_timing",
    "IMMEDIATE",
    # References
    "MethodRef",
    "ValueRef",
    "StepRef",
    "RuleRef",
    "Instance",
    "InterfaceDecl",
    # Simulation
    "SimulationWorkspace",
    # Testbench
    "Testbench",
    "TestSequence",
    # STL Components
    "Reg",
    "Wire",
    "FIFO",
    "FIFO1Push",
    "FIFO1Pull",
    "FIFO2I",
    "ShiftReg",
    "Memory",
    "get_stl_rtl_files",
    "add_stl_rtl_to_workspace",
    "clear_stl_registry",
    # Source Location Tracing
    "PythonLocation",
    "get_python_location",
    "LocationTracker",
    # External Modules
    "ExternalModuleBuilder",
    "ExternalModuleInstance",
    # Diagnostics
    "DiagnosticLevel",
    "Diagnostic",
    "DiagnosticHandler",
    "emit_error",
    "emit_warning",
    "emit_info",
    "emit_debug",
    "type_mismatch_error",
    "undefined_reference_error",
    "scheduling_conflict_warning",
    "format_diagnostic_with_source",
]
