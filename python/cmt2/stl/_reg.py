#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Register (Reg) component with clock and reset domain support.

This module provides the Reg class for creating hardware registers
with configurable clock domains, reset domains, and reset types.

Example:
    from cmt2 import ClockDomain, ResetDomain
    from cmt2.stl import Reg
    from cmt2.types import UInt
    
    # Basic register
    reg = Reg(UInt(32), init=0)
    
    # Register with specific clock domain
    clk_core = ClockDomain("clk_core", frequency=250.0)
    reg = Reg(UInt(32), init=0, clock_domain=clk_core)
    
    # Register with async active-low reset
    reg = Reg(
        UInt(32),
        init=0,
        clock_domain=clk_core,
        reset_domain="rst_n",
        reset_type="async_low"
    )
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional, Union, Any, Dict, List

try:
    from circt.pycmt2 import Reg as _NativeReg
except ImportError:
    _NativeReg = None

from cmt2._clock_domain import (
    ClockDomain,
    ResetDomain,
    ResetType,
    parse_reset_type,
    CLK_CORE,
    RST_CORE,
)
from cmt2.types import SignalType, UInt


class RegError(Exception):
    """Exception raised for register-related errors."""
    pass


@dataclass
class RegConfig:
    """Configuration for a register.
    
    This dataclass encapsulates all register configuration parameters
    for clean separation between initialization and instantiation.
    
    Attributes:
        data_type: The signal type of the register (e.g., UInt(32)).
        init: Initial value (default 0).
        clock_domain: Clock domain for the register.
        reset_domain: Reset domain or reset signal name.
        reset_type: Type of reset (sync/async, active high/low).
        enable: Optional enable signal name.
        name: Optional instance name.
    """
    data_type: SignalType
    init: int = 0
    clock_domain: ClockDomain = field(default_factory=lambda: CLK_CORE)
    reset_domain: Union[str, ResetDomain, None] = None
    reset_type: Union[str, ResetType] = "async_low"
    enable: Optional[str] = None
    name: Optional[str] = None
    
    def __post_init__(self):
        # Validate data type
        if not isinstance(self.data_type, SignalType):
            raise RegError(
                f"data_type must be a SignalType (UInt, SInt, Bits), "
                f"got {type(self.data_type).__name__}"
            )
        
        # Validate init value fits in data type
        max_val = (1 << self.data_type.width) - 1
        if self.data_type.is_signed:
            min_val = -(1 << (self.data_type.width - 1))
            max_val = (1 << (self.data_type.width - 1)) - 1
            if not (min_val <= self.init <= max_val):
                raise RegError(
                    f"init value {self.init} out of range for "
                    f"{self.data_type} (min={min_val}, max={max_val})"
                )
        else:
            if not (0 <= self.init <= max_val):
                raise RegError(
                    f"init value {self.init} out of range for "
                    f"{self.data_type} (max={max_val})"
                )
        
        # Parse reset type
        self._parsed_reset_type = parse_reset_type(self.reset_type)
        
        # Convert reset_domain string to ResetDomain object
        if isinstance(self.reset_domain, str):
            self._reset_domain_obj = ResetDomain(
                name=self.reset_domain,
                reset_type=self._parsed_reset_type,
                clock_domain=self.clock_domain if self._parsed_reset_type.is_sync else None
            )
        elif self.reset_domain is None:
            # Create default reset domain based on clock domain
            reset_name = f"rst_{self.clock_domain.name}"
            self._reset_domain_obj = ResetDomain(
                name=reset_name,
                reset_type=self._parsed_reset_type,
                clock_domain=self.clock_domain if self._parsed_reset_type.is_sync else None
            )
        else:
            self._reset_domain_obj = self.reset_domain
    
    @property
    def parsed_reset_type(self) -> ResetType:
        """Get the parsed ResetType enum value."""
        return self._parsed_reset_type
    
    @property
    def reset_domain_obj(self) -> ResetDomain:
        """Get the ResetDomain object."""
        return self._reset_domain_obj
    
    def get_reset_info(self) -> Dict[str, Any]:
        """Get reset information as a dictionary.
        
        Returns:
            Dictionary with reset configuration details.
        """
        return {
            "name": self._reset_domain_obj.name,
            "type": self._parsed_reset_type.value,
            "is_sync": self._parsed_reset_type.is_sync,
            "is_async": self._parsed_reset_type.is_async,
            "active_high": self._parsed_reset_type.is_active_high,
            "active_low": self._parsed_reset_type.is_active_low,
            "clock_domain": self.clock_domain.name if self._parsed_reset_type.is_sync else None,
        }


class Reg:
    """Hardware register with clock and reset domain support.
    
    The Reg class represents a sequential element (flip-flop) with
    configurable clock domain, reset behavior, and initial value.
    
    Attributes:
        config: The RegConfig containing all register parameters.
        
    Example:
        >>> # Simple register
        >>> reg = Reg(UInt(32), init=0)
        >>> 
        >>> # Register with explicit clock domain
        >>> clk_core = ClockDomain("clk_core", 250.0)
        >>> reg = Reg(UInt(32), init=0xDEADBEEF, clock_domain=clk_core)
        >>> 
        >>> # Register with synchronous reset
        >>> reg = Reg(
        ...     UInt(16),
        ...     init=0,
        ...     clock_domain=clk_core,
        ...     reset_domain="rst_core",
        ...     reset_type="sync_high"
        ... )
    """
    
    def __init__(
        self,
        data_type: SignalType,
        init: int = 0,
        clock_domain: Optional[ClockDomain] = None,
        reset_domain: Union[str, ResetDomain, None] = None,
        reset_type: Union[str, ResetType] = "async_low",
        enable: Optional[str] = None,
        name: Optional[str] = None,
    ):
        """Initialize a register.
        
        Args:
            data_type: The signal type (UInt, SInt, Bits).
            init: Initial value at reset (default 0).
            clock_domain: Clock domain for this register. Defaults to CLK_CORE.
            reset_domain: Reset signal name or ResetDomain object.
            reset_type: Reset behavior - "sync_high", "sync_low", 
                       "async_high", or "async_low" (default).
            enable: Optional enable signal name.
            name: Optional instance name.
            
        Raises:
            RegError: If configuration is invalid.
            
        Example:
            >>> reg = Reg(UInt(8), init=0xFF, reset_type="async_low")
        """
        # Use default clock domain if not specified
        if clock_domain is None:
            clock_domain = CLK_CORE
        
        self._config = RegConfig(
            data_type=data_type,
            init=init,
            clock_domain=clock_domain,
            reset_domain=reset_domain,
            reset_type=reset_type,
            enable=enable,
            name=name,
        )
        
        self._native_reg: Any = None
        
        # Register the clock domain in the global registry
        from cmt2._clock_domain import get_domain_registry
        get_domain_registry().register(clock_domain)
    
    @classmethod
    def create(
        cls,
        circuit: Any,
        width: int,
        init: int = 0,
        clock_domain: Optional[ClockDomain] = None,
        reset_domain: Union[str, ResetDomain, None] = None,
        reset_type: Union[str, ResetType] = "async_low",
        name: Optional[str] = None,
    ) -> "Reg":
        """Factory method for creating a register compatible with Circuit API.
        
        This method provides compatibility with the existing Circuit.module().instance()
        API pattern used in CMT2 designs.
        
        Args:
            circuit: The Circuit object (for compatibility, not used directly).
            width: Bit width of the register.
            init: Initial value (default 0).
            clock_domain: Clock domain for this register.
            reset_domain: Reset domain or signal name.
            reset_type: Reset type string or enum.
            name: Optional instance name.
            
        Returns:
            A new Reg instance.
            
        Example:
            >>> circuit = Circuit("Test")
            >>> with circuit.module("Top") as m:
            ...     reg = m.instance(Reg.create(circuit, 32), "my_reg")
        """
        return cls(
            data_type=UInt(width),
            init=init,
            clock_domain=clock_domain,
            reset_domain=reset_domain,
            reset_type=reset_type,
            name=name,
        )
    
    @property
    def config(self) -> RegConfig:
        """Get the register configuration."""
        return self._config
    
    @property
    def data_type(self) -> SignalType:
        """Get the data type of this register."""
        return self._config.data_type
    
    @property
    def width(self) -> int:
        """Get the bit width of this register."""
        return self._config.data_type.width
    
    @property
    def init(self) -> int:
        """Get the initial/reset value."""
        return self._config.init
    
    @property
    def clock_domain(self) -> ClockDomain:
        """Get the clock domain."""
        return self._config.clock_domain
    
    @property
    def reset_domain(self) -> ResetDomain:
        """Get the reset domain."""
        return self._config.reset_domain_obj
    
    @property
    def reset_type(self) -> ResetType:
        """Get the reset type."""
        return self._config.parsed_reset_type
    
    @property
    def has_sync_reset(self) -> bool:
        """Returns True if register uses synchronous reset."""
        return self._config.parsed_reset_type.is_sync
    
    @property
    def has_async_reset(self) -> bool:
        """Returns True if register uses asynchronous reset."""
        return self._config.parsed_reset_type.is_async
    
    @property
    def is_active_high_reset(self) -> bool:
        """Returns True if reset is active high."""
        return self._config.parsed_reset_type.is_active_high
    
    @property
    def is_active_low_reset(self) -> bool:
        """Returns True if reset is active low."""
        return self._config.parsed_reset_type.is_active_low
    
    def get_clock_info(self) -> Dict[str, Any]:
        """Get clock domain information.
        
        Returns:
            Dictionary with clock domain details.
        """
        return {
            "clock_name": self._config.clock_domain.name,
            "clock_frequency": self._config.clock_domain.frequency,
            "reset_name": self._config.reset_domain_obj.name,
            "reset_type": self._config.parsed_reset_type.value,
            "is_sync_reset": self._config.parsed_reset_type.is_sync,
        }
    
    def validate_clock_compatibility(self, other: "Reg") -> bool:
        """Check if this register is in the same clock domain as another.
        
        Args:
            other: Another Reg instance to compare with.
            
        Returns:
            True if both registers are in the same clock domain.
            
        Raises:
            RegError: If registers are in different clock domains.
        """
        if self.clock_domain != other.clock_domain:
            raise RegError(
                f"Clock domain mismatch: '{self._config.name or 'unnamed'}' "
                f"is in domain '{self.clock_domain.name}' but "
                f"'{other._config.name or 'unnamed'}' is in domain "
                f"'{other.clock_domain.name}'. "
                f"Use cdc_cross() for signals crossing clock domains."
            )
        return True
    
    def __repr__(self) -> str:
        name_str = f"'{self._config.name}'" if self._config.name else "unnamed"
        return (
            f"Reg({name_str}, {self.data_type}, "
            f"clk={self.clock_domain.name}, "
            f"rst={self.reset_type.value})"
        )


# =============================================================================
# Register Array for Vector/Memory Operations
# =============================================================================

class RegArray:
    """Array of registers with common clock/reset domains.
    
    Useful for creating register files, shift registers, or parallel
    register banks that share clock and reset characteristics.
    
    Attributes:
        regs: List of Reg instances.
        
    Example:
        >>> # 8-entry register file
        >>> regfile = RegArray(
        ...     data_type=UInt(32),
        ...     depth=8,
        ...     init=0,
        ...     clock_domain=clk_core
        ... )
    """
    
    def __init__(
        self,
        data_type: SignalType,
        depth: int,
        init: int = 0,
        clock_domain: Optional[ClockDomain] = None,
        reset_domain: Union[str, ResetDomain, None] = None,
        reset_type: Union[str, ResetType] = "async_low",
        name_prefix: Optional[str] = None,
    ):
        """Initialize a register array.
        
        Args:
            data_type: Type for each register element.
            depth: Number of registers in the array.
            init: Initial value for all registers.
            clock_domain: Shared clock domain.
            reset_domain: Shared reset domain.
            reset_type: Shared reset type.
            name_prefix: Prefix for register names.
            
        Raises:
            RegError: If depth is not positive.
        """
        if depth <= 0:
            raise RegError(f"RegArray depth must be positive, got {depth}")
        
        self._depth = depth
        self._data_type = data_type
        self._clock_domain = clock_domain or CLK_CORE
        
        # Create registers
        self._regs: List[Reg] = []
        for i in range(depth):
            name = f"{name_prefix}_{i}" if name_prefix else None
            reg = Reg(
                data_type=data_type,
                init=init,
                clock_domain=clock_domain,
                reset_domain=reset_domain,
                reset_type=reset_type,
                name=name,
            )
            self._regs.append(reg)
    
    def __getitem__(self, index: int) -> Reg:
        """Get a register by index.
        
        Args:
            index: Register index (0 to depth-1).
            
        Returns:
            The Reg at the specified index.
            
        Raises:
            IndexError: If index is out of bounds.
        """
        if not 0 <= index < self._depth:
            raise IndexError(
                f"RegArray index {index} out of range [0, {self._depth})"
            )
        return self._regs[index]
    
    def __len__(self) -> int:
        """Get the number of registers."""
        return self._depth
    
    def __iter__(self):
        """Iterate over registers."""
        return iter(self._regs)
    
    @property
    def depth(self) -> int:
        """Get the array depth."""
        return self._depth
    
    @property
    def clock_domain(self) -> ClockDomain:
        """Get the shared clock domain."""
        return self._clock_domain
    
    def validate_all_same_domain(self) -> bool:
        """Verify all registers are in the same clock domain.
        
        Returns:
            True if all registers share the same clock domain.
        """
        for i, reg in enumerate(self._regs[1:], 1):
            if reg.clock_domain != self._clock_domain:
                raise RegError(
                    f"Register {i} clock domain mismatch: "
                    f"expected {self._clock_domain.name}, "
                    f"got {reg.clock_domain.name}"
                )
        return True


# =============================================================================
# Utility Functions
# =============================================================================

def create_reg_bank(
    data_type: SignalType,
    count: int,
    clock_domain: Optional[ClockDomain] = None,
    reset_type: str = "async_low",
    name_prefix: str = "reg",
) -> List[Reg]:
    """Create a bank of registers with common configuration.
    
    Args:
        data_type: Type for all registers.
        count: Number of registers to create.
        clock_domain: Shared clock domain.
        reset_type: Shared reset type.
        name_prefix: Prefix for register names.
        
    Returns:
        List of Reg instances.
        
    Example:
        >>> regs = create_reg_bank(UInt(32), 4, name_prefix="pipeline")
        >>> # Creates pipeline_0, pipeline_1, pipeline_2, pipeline_3
    """
    regs = []
    for i in range(count):
        name = f"{name_prefix}_{i}"
        reg = Reg(
            data_type=data_type,
            clock_domain=clock_domain,
            reset_type=reset_type,
            name=name,
        )
        regs.append(reg)
    return regs


def check_clock_domain_match(regs: List[Reg]) -> Optional[ClockDomain]:
    """Check that all registers are in the same clock domain.
    
    Args:
        regs: List of Reg instances to check.
        
    Returns:
        The common clock domain if all match, None if list is empty.
        
    Raises:
        RegError: If registers are in different clock domains.
    """
    if not regs:
        return None
    
    first_domain = regs[0].clock_domain
    for i, reg in enumerate(regs[1:], 1):
        if reg.clock_domain != first_domain:
            raise RegError(
                f"Clock domain mismatch at index {i}: "
                f"'{reg._config.name or 'unnamed'}' is in '{reg.clock_domain.name}' "
                f"but expected '{first_domain.name}'"
            )
    
    return first_domain
