#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 Backend Generators.

This subpackage provides backend code generators for CMT2 designs:
- Constraint file generators (SDC, XDC)
- Simulation backend (PyCMT2 integration)
- Verilog/SystemVerilog generation

Example:
    from cmt2.backends import (
        SDCGenerator, XDCGenerator,
        SimulationBackend, VerilogBackend
    )
    from cmt2.constraints import ConstraintSet
    
    # Constraints
    constraints = ConstraintSet()
    sdc = SDCGenerator().generate(constraints)
    
    # Simulation
    sim_backend = SimulationBackend()
    sim = sim_backend.compile(lowered_circuit)
    result = sim.run(cycles=1000)
    
    # Verilog
    v_backend = VerilogBackend()
    v_exec = v_backend.compile(lowered_circuit)
    v_exec.write("output.sv")
"""

from __future__ import annotations

# Import constraint generators
from ._sdc_generator import (
    ConstraintGenerator,
    SDCGenerator,
    XDCGenerator,
    generate_sdc,
    generate_xdc,
)

# Import simulation backend
try:
    from .simulation import (
        SimulationBackend,
        SimulationExecutable,
        SimulationResult,
        create_simulation_backend,
    )
    _HAS_SIMULATION = True
except ImportError:
    _HAS_SIMULATION = False

# Import Verilog backend
try:
    from .verilog import (
        VerilogBackend,
        VerilogExecutable,
        VerilogGenerationResult,
        create_verilog_backend,
        emit_verilog,
    )
    _HAS_VERILOG = True
except ImportError:
    _HAS_VERILOG = False

__all__ = [
    # Constraint generators
    "ConstraintGenerator",
    "SDCGenerator",
    "XDCGenerator",
    "generate_sdc",
    "generate_xdc",
]

# Add simulation exports if available
if _HAS_SIMULATION:
    __all__.extend([
        "SimulationBackend",
        "SimulationExecutable",
        "SimulationResult",
        "create_simulation_backend",
    ])

# Add Verilog exports if available
if _HAS_VERILOG:
    __all__.extend([
        "VerilogBackend",
        "VerilogExecutable",
        "VerilogGenerationResult",
        "create_verilog_backend",
        "emit_verilog",
    ])
