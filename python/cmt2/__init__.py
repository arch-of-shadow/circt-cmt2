#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
CMT2 Python Frontend - JIT API

This module provides the JIT-enabled Python frontend for CMT2 hardware design,
including hardware control flow constructs and staged compilation.

Example:
    import cmt2
    from cmt2 import Circuit, UInt
    from cmt2.stl import Reg

    @cmt2.elaborate
    def counter_design(max_count: int):
        circuit = Circuit("Counter")
        
        with circuit.module("Counter") as m:
            clk, rst = m.clock(), m.reset()
            count = m.instance(Reg.create(circuit, 32), "count", clk=clk, rst=rst)
            
            with m.rule("increment") as r:
                with r.guard() as g:
                    val = g.call(count, "read")
                    at_limit = g.eq(val, g.const(max_count, 32))
                    g.returns(g.not_(at_limit))
                with r.body() as body:
                    val = body.call(count, "read")
                    body.call(count, "write", body.add(val, body.const(1, 32)))
        
        return circuit
"""

from __future__ import annotations

__version__ = "0.1.0"

# Timing Constraints API
from . import constraints
from .constraints import (
    ConstraintSet,
    ClockConstraint,
    ClockWaveform,
    ClockGroup,
    ClockLatency,
    ClockUncertainty,
    InputDelayConstraint,
    OutputDelayConstraint,
    FalsePathConstraint,
    MulticyclePathConstraint,
    MaxDelayConstraint,
    IOStandardConstraint,
    DriveStrengthConstraint,
    LocationConstraint,
    IOBankConstraint,
)

# JIT API - Staged compilation decorators
from .jit import (
    elaborate,
    simulate,
    static,
)

# Control Flow API - Hardware control flow constructs
from .control_flow import (
    when,
    otherwise,
    switch,
    case,
    unroll,
    ControlFlowWarning,
)

# Clock Domain API - Clock and reset domain support
from ._clock_domain import (
    ClockDomain,
    ResetDomain,
    ResetType,
    CDCMethod,
    CDCFifo,
    CDCSynchronizer,
    cdc_cross,
    parse_reset_type,
    validate_clock_domains,
    # Common clock domain presets
    CLK_CORE,
    CLK_IO,
    CLK_PERIPHERAL,
    CLK_DDR,
    CLK_USB,
    RST_CORE,
    RST_IO,
    RST_N_CORE,
    RST_N_IO,
)

__all__ = [
    # Timing Constraints
    "constraints",
    "ConstraintSet",
    "ClockConstraint",
    "ClockWaveform",
    "ClockGroup",
    "ClockLatency",
    "ClockUncertainty",
    "InputDelayConstraint",
    "OutputDelayConstraint",
    "FalsePathConstraint",
    "MulticyclePathConstraint",
    "MaxDelayConstraint",
    "IOStandardConstraint",
    "DriveStrengthConstraint",
    "LocationConstraint",
    "IOBankConstraint",
    # JIT Decorators
    "elaborate",
    "simulate",
    # Static argument marker
    "static",
    # Control Flow
    "when",
    "otherwise", 
    "switch",
    "case",
    "unroll",
    "ControlFlowWarning",
    # Clock Domain
    "ClockDomain",
    "ResetDomain",
    "ResetType",
    "CDCMethod",
    "CDCFifo",
    "CDCSynchronizer",
    "cdc_cross",
    "parse_reset_type",
    "validate_clock_domains",
    # Clock Domain Presets
    "CLK_CORE",
    "CLK_IO",
    "CLK_PERIPHERAL",
    "CLK_DDR",
    "CLK_USB",
    "RST_CORE",
    "RST_IO",
    "RST_N_CORE",
    "RST_N_IO",
]

# Re-export from pycmt2 for convenience
try:
    from circt.pycmt2 import (
        Circuit,
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
        Signal,
        ModuleBuilder,
        SimulationWorkspace,
        Reg,
        Wire,
        FIFO,
        FIFO1Push,
        FIFO1Pull,
        FIFO2I,
        ShiftReg,
        Memory as _Pycmt2Memory,
    )

    __all__.extend([
        "Circuit",
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
        "Signal",
        "ModuleBuilder",
        "SimulationWorkspace",
        "Reg",
        "Wire",
        "FIFO",
        "FIFO1Push",
        "FIFO1Pull",
        "FIFO2I",
        "ShiftReg",
    ])
except ImportError:
    # circt.pycmt2 may not be available during development
    pass

# Note: Memory classes (SRAM, ROM) should be imported from cmt2.stl directly
# to avoid circular import issues during package initialization.
# Example: from cmt2.stl import SRAM, ROM

# PyCMT2 Integration (optional - requires CIRCT bindings)
try:
    from .pycmt2_integration import (
        CircuitBuilder,
        JITSimulationRunner,
        is_pycmt2_available,
        get_pycmt2_version,
    )
    _HAS_PYCMT2_INTEGRATION = True
    
    __all__.extend([
        "CircuitBuilder",
        "JITSimulationRunner",
        "is_pycmt2_available",
        "get_pycmt2_version",
    ])
except ImportError:
    _HAS_PYCMT2_INTEGRATION = False

# JIT v2 - Thin layer on PyCMT2
try:
    from . import jit_v2
    _HAS_JIT_V2 = True
    
    __all__.append("jit_v2")
except ImportError:
    _HAS_JIT_V2 = False

# JIT v3 - Zero-boilerplate API (recommended for new designs)
try:
    from . import jit_v3
    _HAS_JIT_V3 = True
    
    __all__.append("jit_v3")
except ImportError:
    _HAS_JIT_V3 = False
