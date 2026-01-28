#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Clock Domain and Reset Support for CMT2.

This module provides clock domain abstractions, reset configurations,
and clock domain crossing (CDC) primitives for multi-clock designs.

Example:
    from cmt2 import ClockDomain, ResetDomain
    from cmt2.stl import Reg
    
    # Define clock domains
    clk_core = ClockDomain("clk_core", frequency=250.0)
    clk_io = ClockDomain("clk_io", frequency=100.0)
    
    # Create registers with specific clock/reset domains
    reg = Reg(
        UInt(32),
        init=0,
        clock_domain=clk_core,
        reset_domain="rst_n",
        reset_type="async_low"
    )
    
    # Cross clock domains
    signal_io = cdc_cross(
        signal_core,
        from_clk=clk_core,
        to_clk=clk_io,
        method="2flop"
    )
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional, Union, Any, Dict, List, Set


class ResetType(Enum):
    """Reset type enumeration.
    
    Defines the behavior and polarity of reset signals.
    """
    SYNC_HIGH = "sync_high"
    SYNC_LOW = "sync_low"
    ASYNC_HIGH = "async_high"
    ASYNC_LOW = "async_low"
    
    @property
    def is_sync(self) -> bool:
        """Returns True if this is a synchronous reset."""
        return self in (ResetType.SYNC_HIGH, ResetType.SYNC_LOW)
    
    @property
    def is_async(self) -> bool:
        """Returns True if this is an asynchronous reset."""
        return self in (ResetType.ASYNC_HIGH, ResetType.ASYNC_LOW)
    
    @property
    def is_active_high(self) -> bool:
        """Returns True if reset is active high."""
        return self in (ResetType.SYNC_HIGH, ResetType.ASYNC_HIGH)
    
    @property
    def is_active_low(self) -> bool:
        """Returns True if reset is active low."""
        return self in (ResetType.SYNC_LOW, ResetType.ASYNC_LOW)


class CDCMethod(Enum):
    """Clock Domain Crossing method enumeration.
    
    Defines the synchronization method for crossing clock domains.
    """
    TWO_FLOP = "2flop"
    HANDSHAKE = "handshake"
    ASYNC_FIFO = "async_fifo"


@dataclass(frozen=True)
class ClockDomain:
    """Named clock domain for hardware designs.
    
    A clock domain represents a distinct clock source with its own
    frequency and phase characteristics. Signals within the same
    clock domain can be directly connected without synchronization.
    
    Attributes:
        name: Unique identifier for this clock domain.
        frequency: Clock frequency in MHz (optional, for documentation/analysis).
        phase: Clock phase offset in degrees (default 0).
        duty_cycle: Clock duty cycle as ratio (0.0-1.0, default 0.5).
        
    Example:
        >>> clk_core = ClockDomain("clk_core", frequency=250.0)
        >>> clk_io = ClockDomain("clk_io", frequency=100.0, duty_cycle=0.4)
        >>> 
        >>> # Use in register declaration
        >>> reg = Reg(UInt(32), clock_domain=clk_core)
    """
    name: str
    frequency: Optional[float] = None
    phase: float = 0.0
    duty_cycle: float = 0.5
    
    def __post_init__(self):
        # Validate duty cycle
        if not 0.0 < self.duty_cycle < 1.0:
            raise ValueError(f"duty_cycle must be between 0 and 1, got {self.duty_cycle}")
        
        # Validate frequency if provided
        if self.frequency is not None and self.frequency <= 0:
            raise ValueError(f"frequency must be positive, got {self.frequency}")
    
    def __hash__(self) -> int:
        return hash(self.name)
    
    def __eq__(self, other: object) -> bool:
        if not isinstance(other, ClockDomain):
            return NotImplemented
        return self.name == other.name
    
    def __repr__(self) -> str:
        freq_str = f"{self.frequency} MHz" if self.frequency else "unspecified"
        return f"ClockDomain({self.name!r}, {freq_str})"
    
    @property
    def period_ns(self) -> Optional[float]:
        """Return clock period in nanoseconds.
        
        Returns:
            The period in nanoseconds, or None if frequency is not set.
        """
        if self.frequency is None:
            return None
        return 1000.0 / self.frequency
    
    def can_safely_cross_to(self, other: ClockDomain) -> bool:
        """Check if safe to cross from this domain to another.
        
        Currently this is a conservative check. In practice, safe CDC
        depends on the relationship between the clocks (synchronous vs
        asynchronous) and the frequencies involved.
        
        Args:
            other: The target clock domain.
            
        Returns:
            True if crossing is considered safe without special handling.
        """
        # Same domain is always safe
        if self == other:
            return True
        
        # Different domains always require CDC (conservative)
        return False


@dataclass(frozen=True)
class ResetDomain:
    """Reset domain configuration.
    
    A reset domain groups signals that share a common reset behavior.
    This allows for coordinated reset sequencing across the design.
    
    Attributes:
        name: Name of the reset signal.
        reset_type: Type of reset (sync/async, active high/low).
        clock_domain: The associated clock domain for synchronous resets.
        
    Example:
        >>> rst_core = ResetDomain("rst_core", ResetType.ASYNC_LOW, clk_core)
        >>> 
        >>> # Use in register declaration
        >>> reg = Reg(UInt(32), reset_domain=rst_core)
    """
    name: str
    reset_type: ResetType = ResetType.ASYNC_LOW
    clock_domain: Optional[ClockDomain] = None
    
    def __post_init__(self):
        # Validate: synchronous reset requires a clock domain
        if self.reset_type.is_sync and self.clock_domain is None:
            raise ValueError(
                f"Synchronous reset '{self.name}' requires a clock_domain. "
                "Use ClockDomain parameter or specify as async reset."
            )
    
    def __hash__(self) -> int:
        return hash((self.name, self.reset_type))
    
    def __eq__(self, other: object) -> bool:
        if not isinstance(other, ResetDomain):
            return NotImplemented
        return self.name == other.name and self.reset_type == other.reset_type
    
    def __repr__(self) -> str:
        return f"ResetDomain({self.name!r}, {self.reset_type.value})"


# =============================================================================
# Clock Domain Registry
# =============================================================================

class _ClockDomainRegistry:
    """Internal registry for tracking clock domains in a design.
    
    This registry maintains a global view of all clock domains and
    their relationships for CDC analysis and validation.
    """
    
    def __init__(self):
        self._domains: Dict[str, ClockDomain] = {}
        self._crossings: List[CDCCrossing] = []
    
    def register(self, domain: ClockDomain) -> None:
        """Register a clock domain.
        
        Args:
            domain: The clock domain to register.
            
        Raises:
            ValueError: If a domain with the same name already exists
                       but has different parameters.
        """
        if domain.name in self._domains:
            existing = self._domains[domain.name]
            if existing != domain:
                raise ValueError(
                    f"Clock domain '{domain.name}' already registered with "
                    f"different parameters: existing={existing}, new={domain}"
                )
        else:
            self._domains[domain.name] = domain
    
    def get(self, name: str) -> Optional[ClockDomain]:
        """Get a registered clock domain by name."""
        return self._domains.get(name)
    
    def add_crossing(self, crossing: CDCCrossing) -> None:
        """Add a CDC crossing to the registry."""
        self._crossings.append(crossing)
        # Register both domains involved
        self.register(crossing.from_domain)
        self.register(crossing.to_domain)
    
    def get_crossings(self) -> List[CDCCrossing]:
        """Get all registered CDC crossings."""
        return self._crossings.copy()
    
    def clear(self) -> None:
        """Clear all registered domains and crossings."""
        self._domains.clear()
        self._crossings.clear()


# Global registry instance
_domain_registry = _ClockDomainRegistry()


def get_domain_registry() -> _ClockDomainRegistry:
    """Get the global clock domain registry."""
    return _domain_registry


def reset_domain_registry() -> None:
    """Reset the global clock domain registry.
    
    Call this between designs or during testing to ensure
    a clean state.
    """
    _domain_registry.clear()


# =============================================================================
# Clock Domain Crossing (CDC) Support
# =============================================================================

@dataclass
class CDCCrossing:
    """Represents a clock domain crossing.
    
    This class tracks a signal crossing from one clock domain to
    another, including the synchronization method used.
    
    Attributes:
        signal: The signal being crossed (name or reference).
        from_domain: Source clock domain.
        to_domain: Destination clock domain.
        method: CDC synchronization method.
        depth: For FIFO-based CDC, the FIFO depth.
    """
    signal: str
    from_domain: ClockDomain
    to_domain: ClockDomain
    method: CDCMethod
    depth: Optional[int] = None
    
    def __post_init__(self):
        # Validate FIFO depth for async_fifo method
        if self.method == CDCMethod.ASYNC_FIFO and self.depth is None:
            raise ValueError("ASYNC_FIFO CDC method requires a depth parameter")


class CDCSynchronizer:
    """Factory for creating CDC synchronizer components.
    
    This class provides methods to create various CDC primitives
    for safely crossing signals between clock domains.
    """
    
    @staticmethod
    def two_flop(
        signal: Any,
        from_clk: ClockDomain,
        to_clk: ClockDomain,
        name: Optional[str] = None
    ) -> Any:
        """Create a 2-flop synchronizer for single-bit signals.
        
        This is the most basic CDC primitive, suitable for:
        - Single-bit control signals
        - Level signals (not pulses)
        - Low-frequency crossing
        
        Args:
            signal: The signal to synchronize.
            from_clk: Source clock domain.
            to_clk: Destination clock domain.
            name: Optional name for the synchronizer instance.
            
        Returns:
            The synchronized signal in the destination clock domain.
            
        Example:
            >>> clk_fast = ClockDomain("clk_fast", 500.0)
            >>> clk_slow = ClockDomain("clk_slow", 100.0)
            >>> control_sync = CDCSynchronizer.two_flop(
            ...     control_signal, clk_fast, clk_slow, "control_sync"
            ... )
        """
        # Register the crossing
        signal_name = name or f"cdc_{from_clk.name}_to_{to_clk.name}"
        crossing = CDCCrossing(
            signal=signal_name,
            from_domain=from_clk,
            to_domain=to_clk,
            method=CDCMethod.TWO_FLOP
        )
        _domain_registry.add_crossing(crossing)
        
        # Return a placeholder - actual implementation would create
        # the synchronizer hardware
        return _CDCSignalProxy(signal, to_clk, crossing)
    
    @staticmethod
    def handshake(
        data: Any,
        from_clk: ClockDomain,
        to_clk: ClockDomain,
        name: Optional[str] = None
    ) -> Any:
        """Create a handshake-based CDC for multi-bit data.
        
        Uses a full handshake protocol (request/acknowledge) to safely
        transfer data across clock domains. Suitable for:
        - Multi-bit data buses
        - Occasional transfers (not streaming)
        - When latency is not critical
        
        Args:
            data: The data signal to transfer.
            from_clk: Source clock domain.
            to_clk: Destination clock domain.
            name: Optional name for the synchronizer instance.
            
        Returns:
            The transferred data in the destination clock domain.
            
        Example:
            >>> data_sync = CDCSynchronizer.handshake(
            ...     data_bus, clk_core, clk_io, "data_sync"
            ... )
        """
        signal_name = name or f"cdc_handshake_{from_clk.name}_to_{to_clk.name}"
        crossing = CDCCrossing(
            signal=signal_name,
            from_domain=from_clk,
            to_domain=to_clk,
            method=CDCMethod.HANDSHAKE
        )
        _domain_registry.add_crossing(crossing)
        
        return _CDCSignalProxy(data, to_clk, crossing)
    
    @staticmethod
    def async_fifo(
        write_clk: ClockDomain,
        read_clk: ClockDomain,
        data_type: Any,
        depth: int,
        name: Optional[str] = None
    ) -> "CDCFifo":
        """Create an asynchronous FIFO for streaming data CDC.
        
        Args:
            write_clk: Clock domain for write operations.
            read_clk: Clock domain for read operations.
            data_type: Type of data stored in the FIFO.
            depth: FIFO depth (must be power of 2 for efficiency).
            name: Optional name for the FIFO instance.
            
        Returns:
            A CDCFifo instance for cross-clock streaming.
            
        Example:
            >>> fifo = CDCSynchronizer.async_fifo(
            ...     clk_core, clk_io, UInt(32), 16, "crossing_fifo"
            ... )
        """
        return CDCFifo(write_clk, read_clk, data_type, depth, name)


class _CDCSignalProxy:
    """Proxy object representing a signal that has crossed clock domains.
    
    This proxy maintains metadata about the CDC crossing while
    providing access to the signal value.
    """
    
    def __init__(self, original: Any, new_domain: ClockDomain, crossing: CDCCrossing):
        self._original = original
        self._domain = new_domain
        self._crossing = crossing
    
    @property
    def clock_domain(self) -> ClockDomain:
        """Get the clock domain of this signal (after CDC)."""
        return self._domain
    
    @property
    def crossing_info(self) -> CDCCrossing:
        """Get information about the CDC crossing."""
        return self._crossing
    
    def __repr__(self) -> str:
        return f"CDCSignal({self._crossing.signal}, domain={self._domain.name})"


class CDCFifo:
    """Asynchronous FIFO for clock domain crossing.
    
    Provides a FIFO-based CDC solution for streaming data between
    asynchronous clock domains. Uses dual-port memory with separate
    read and write pointers synchronized across domains.
    
    Attributes:
        write_clock: Clock domain for write operations.
        read_clock: Clock domain for read operations.
        data_type: Type of data elements.
        depth: FIFO capacity.
        
    Example:
        >>> fifo = CDCFifo(clk_core, clk_io, UInt(32), 16)
        >>> 
        >>> # In write clock domain
        >>> with m.rule("write") as r:
        ...     with r.guard() as g:
        ...         g.returns(fifo.not_full())
        ...     with r.body() as body:
        ...         body.call(fifo, "enq", data)
        >>> 
        >>> # In read clock domain
        >>> with m.rule("read") as r:
        ...     with r.guard() as g:
        ...         g.returns(fifo.not_empty())
        ...     with r.body() as body:
        ...         data = body.call(fifo, "deq")
    """
    
    def __init__(
        self,
        write_clock: ClockDomain,
        read_clock: ClockDomain,
        data_type: Any,
        depth: int,
        name: Optional[str] = None
    ):
        if depth <= 0:
            raise ValueError(f"FIFO depth must be positive, got {depth}")
        
        self.write_clock = write_clock
        self.read_clock = read_clock
        self.data_type = data_type
        self.depth = depth
        self.name = name or f"cdc_fifo_{write_clock.name}_to_{read_clock.name}"
        
        # Register the crossing
        crossing = CDCCrossing(
            signal=self.name,
            from_domain=write_clock,
            to_domain=read_clock,
            method=CDCMethod.ASYNC_FIFO,
            depth=depth
        )
        _domain_registry.add_crossing(crossing)
        self._crossing = crossing
    
    def __repr__(self) -> str:
        return f"CDCFifo({self.name}, depth={self.depth})"


# =============================================================================
# Convenience Functions
# =============================================================================

def cdc_cross(
    signal: Any,
    from_clk: ClockDomain,
    to_clk: ClockDomain,
    method: str = "2flop",
    name: Optional[str] = None
) -> Any:
    """Cross a signal from one clock domain to another.
    
    This is a convenience function that creates the appropriate CDC
    primitive based on the method specified.
    
    Args:
        signal: The signal to cross.
        from_clk: Source clock domain.
        to_clk: Destination clock domain.
        method: CDC method - "2flop", "handshake", or "fifo".
        name: Optional name for the CDC instance.
        
    Returns:
        The signal in the destination clock domain.
        
    Raises:
        ValueError: If an unsupported method is specified.
        
    Example:
        >>> clk_fast = ClockDomain("clk_fast", 500.0)
        >>> clk_slow = ClockDomain("clk_slow", 100.0)
        >>> 
        >>> # 2-flop synchronizer for control signal
        >>> ctrl_sync = cdc_cross(ctrl, clk_fast, clk_slow, "2flop")
        >>> 
        >>> # Handshake for data
        >>> data_sync = cdc_cross(data, clk_fast, clk_slow, "handshake")
    """
    if from_clk == to_clk:
        # No CDC needed for same domain
        return signal
    
    method_enum = CDCMethod(method)
    
    if method_enum == CDCMethod.TWO_FLOP:
        return CDCSynchronizer.two_flop(signal, from_clk, to_clk, name)
    elif method_enum == CDCMethod.HANDSHAKE:
        return CDCSynchronizer.handshake(signal, from_clk, to_clk, name)
    elif method_enum == CDCMethod.ASYNC_FIFO:
        raise ValueError(
            "Use CDCSynchronizer.async_fifo() or CDCFifo class directly "
            "for FIFO-based CDC. cdc_cross() with method='fifo' requires "
            "additional parameters (data_type, depth)."
        )
    else:
        raise ValueError(f"Unsupported CDC method: {method}")


def validate_clock_domains() -> List[str]:
    """Validate all registered CDC crossings.
    
    This function checks for potential issues in clock domain crossings:
    - Missing synchronizers
    - Unsafe crossings
    - Frequency ratio issues
    
    Returns:
        A list of warning/error messages (empty if no issues).
    """
    warnings: List[str] = []
    crossings = _domain_registry.get_crossings()
    
    for crossing in crossings:
        from_freq = crossing.from_domain.frequency
        to_freq = crossing.to_domain.frequency
        
        # Check for missing frequency information
        if from_freq is None or to_freq is None:
            warnings.append(
                f"CDC crossing '{crossing.signal}' missing frequency info "
                f"for domains {crossing.from_domain.name} -> {crossing.to_domain.name}"
            )
            continue
        
        # Check for potentially unsafe fast-to-slow crossing
        if from_freq > to_freq * 2 and crossing.method == CDCMethod.TWO_FLOP:
            warnings.append(
                f"Unsafe CDC: '{crossing.signal}' crosses from {from_freq} MHz "
                f"to {to_freq} MHz using 2-flop synchronizer. "
                f"Consider using handshake or FIFO for fast-to-slow crossing."
            )
    
    return warnings


# =============================================================================
# Common Clock Domain Presets
# =============================================================================

# Common clock domains for convenience
CLK_CORE = ClockDomain("clk_core", frequency=250.0)
CLK_IO = ClockDomain("clk_io", frequency=100.0)
CLK_PERIPHERAL = ClockDomain("clk_peripheral", frequency=50.0)
CLK_DDR = ClockDomain("clk_ddr", frequency=800.0)
CLK_USB = ClockDomain("clk_usb", frequency=60.0)

# Common reset domains
RST_CORE = ResetDomain("rst_core", ResetType.ASYNC_LOW, CLK_CORE)
RST_IO = ResetDomain("rst_io", ResetType.ASYNC_LOW, CLK_IO)
RST_N_CORE = ResetDomain("rst_n_core", ResetType.ASYNC_LOW, CLK_CORE)
RST_N_IO = ResetDomain("rst_n_io", ResetType.ASYNC_LOW, CLK_IO)


# =============================================================================
# Reset Type Parsing Utility
# =============================================================================

def parse_reset_type(reset_type: Union[str, ResetType]) -> ResetType:
    """Parse a reset type from string or ResetType enum.
    
    Args:
        reset_type: Reset type as string ("sync_high", "async_low", etc.)
                   or ResetType enum value.
                   
    Returns:
        The ResetType enum value.
        
    Raises:
        ValueError: If the reset type string is not recognized.
        
    Example:
        >>> parse_reset_type("async_low")
        ResetType.ASYNC_LOW
        >>> parse_reset_type(ResetType.SYNC_HIGH)
        ResetType.SYNC_HIGH
    """
    if isinstance(reset_type, ResetType):
        return reset_type
    
    try:
        return ResetType(reset_type.lower())
    except ValueError:
        valid_values = [rt.value for rt in ResetType]
        raise ValueError(
            f"Invalid reset_type '{reset_type}'. "
            f"Must be one of: {', '.join(valid_values)}"
        )
