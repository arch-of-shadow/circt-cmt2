#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 Standard Template Library (STL).

This module provides standard hardware components with clock domain
and reset support, including registers, wires, FIFOs, and memories.

Example:
    from cmt2 import ClockDomain
    from cmt2.stl import Reg, RegArray
    from cmt2.types import UInt
    
    # Create clock domain
    clk_core = ClockDomain("clk_core", frequency=250.0)
    
    # Create register with clock domain
    reg = Reg(
        UInt(32),
        init=0,
        clock_domain=clk_core,
        reset_type="async_low"
    )
    
    # Create register array
    regfile = RegArray(UInt(32), depth=8, clock_domain=clk_core)
"""

from cmt2.stl._reg import (
    Reg,
    RegConfig,
    RegArray,
    RegError,
    create_reg_bank,
    check_clock_domain_match,
)

from cmt2.stl._memory import (
    Memory,
    SRAM,
    ROM,
    MemoryError,
    create_single_port_sram,
    create_dual_port_sram,
    create_rom,
)

__all__ = [
    # Register components
    "Reg",
    "RegConfig",
    "RegArray",
    "RegError",
    
    # Memory components
    "Memory",
    "SRAM",
    "ROM",
    "MemoryError",
    
    # Memory factory functions
    "create_single_port_sram",
    "create_dual_port_sram",
    "create_rom",
    
    # Register utility functions
    "create_reg_bank",
    "check_clock_domain_match",
]

# Try to re-export from native pycmt2 if available
try:
    from circt.pycmt2 import (
        Wire,
        FIFO,
        FIFO1Push,
        FIFO1Pull,
        FIFO2I,
        ShiftReg,
    )
    
    __all__.extend([
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
