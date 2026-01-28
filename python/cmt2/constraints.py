#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Timing Constraints for CMT2 Hardware Designs.

This module provides timing constraint definitions for synthesis and implementation.
Constraints can be specified during circuit elaboration and exported to SDC/XDC
format for use with FPGA/ASIC tools (Vivado, Quartus, Design Compiler, etc.).

Example:
    import cmt2
    from cmt2 import Circuit
    from cmt2.constraints import ClockConstraint, ConstraintSet

    @cmt2.elaborate
    def design(target_freq: Annotated[str, cmt2.static]):
        circuit = Circuit("Top")
        
        # Define clock constraint
        circuit.clock(
            name="clk",
            frequency="250MHz",  # or period_ns=4.0
            duty_cycle=0.5,
            jitter=0.1
        )
        
        # Define I/O delays
        circuit.input_delay(
            port="data_in",
            clock="clk",
            min_delay=0.5,
            max_delay=2.0
        )
        
        circuit.output_delay(
            port="data_out",
            clock="clk",
            min_delay=0.5,
            max_delay=2.0
        )
        
        # Define false paths
        circuit.set_false_path(
            from_ports="reset",
            to_ports="clk"
        )
        
        return circuit
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional, Union, List, Dict, Any, Set


# =============================================================================
# Clock Constraints
# =============================================================================

class ClockWaveform:
    """Clock waveform specification.
    
    Defines the duty cycle and phase characteristics of a clock.
    
    Attributes:
        duty_cycle: Duty cycle as a ratio (0.0 to 1.0, default 0.5).
        phase_deg: Phase offset in degrees (default 0).
        
    Example:
        >>> waveform = ClockWaveform(duty_cycle=0.4, phase_deg=90)
    """
    
    def __init__(self, duty_cycle: float = 0.5, phase_deg: float = 0.0):
        """Initialize clock waveform.
        
        Args:
            duty_cycle: Duty cycle as a ratio (0.0 to 1.0).
            phase_deg: Phase offset in degrees.
            
        Raises:
            ValueError: If duty_cycle is not in valid range.
        """
        if not 0.0 < duty_cycle < 1.0:
            raise ValueError(f"duty_cycle must be between 0 and 1, got {duty_cycle}")
        self.duty_cycle = duty_cycle
        self.phase_deg = phase_deg
    
    def __repr__(self) -> str:
        return f"ClockWaveform(duty_cycle={self.duty_cycle}, phase_deg={self.phase_deg})"


@dataclass
class ClockConstraint:
    """Clock timing constraint.
    
    Represents a clock definition with period, waveform, and jitter information.
    Maps to the SDC `create_clock` command.
    
    Attributes:
        name: Clock name (also used as port/pin name if source not specified).
        period_ns: Clock period in nanoseconds.
        waveform: Optional clock waveform (duty cycle and phase).
        jitter_ns: Clock jitter in nanoseconds (optional).
        source: Source port/pin for the clock (default: uses name).
        comment: Optional comment for the constraint.
        
    Example:
        >>> # Create clock from frequency string
        >>> clk = ClockConstraint.from_frequency("clk", "250MHz")
        >>> 
        >>> # Create clock with explicit period
        >>> clk = ClockConstraint("clk", period_ns=4.0)
        >>> 
        >>> # Create clock with custom waveform
        >>> clk = ClockConstraint(
        ...     "clk",
        ...     period_ns=4.0,
        ...     waveform=ClockWaveform(duty_cycle=0.4)
        ... )
    """
    name: str
    period_ns: float
    waveform: Optional[ClockWaveform] = None
    jitter_ns: Optional[float] = None
    source: Optional[str] = None
    comment: Optional[str] = None
    
    def __post_init__(self):
        """Validate clock constraint parameters."""
        if self.period_ns <= 0:
            raise ValueError(f"period_ns must be positive, got {self.period_ns}")
        if self.jitter_ns is not None and self.jitter_ns < 0:
            raise ValueError(f"jitter_ns must be non-negative, got {self.jitter_ns}")
    
    @property
    def frequency_mhz(self) -> float:
        """Return clock frequency in MHz."""
        return 1000.0 / self.period_ns
    
    @classmethod
    def from_frequency(
        cls,
        name: str,
        frequency: Union[str, float],
        duty_cycle: float = 0.5,
        jitter_ns: Optional[float] = None,
        source: Optional[str] = None,
        comment: Optional[str] = None
    ) -> ClockConstraint:
        """Create a clock constraint from a frequency specification.
        
        Args:
            name: Clock name.
            frequency: Frequency as string (e.g., "250MHz", "100 MHz") 
                      or float (MHz).
            duty_cycle: Clock duty cycle (0.0 to 1.0, default 0.5).
            jitter_ns: Clock jitter in nanoseconds (optional).
            source: Source port/pin for the clock.
            comment: Optional comment.
            
        Returns:
            A ClockConstraint instance.
            
        Raises:
            ValueError: If frequency format is invalid.
            
        Example:
            >>> clk = ClockConstraint.from_frequency("clk", "250MHz")
            >>> clk = ClockConstraint.from_frequency("clk", 250.0)
            >>> clk = ClockConstraint.from_frequency("clk", "100 MHz")
        """
        freq_mhz = _parse_frequency(frequency)
        period_ns = 1000.0 / freq_mhz
        waveform = ClockWaveform(duty_cycle=duty_cycle)
        
        return cls(
            name=name,
            period_ns=period_ns,
            waveform=waveform,
            jitter_ns=jitter_ns,
            source=source,
            comment=comment
        )
    
    def get_source(self) -> str:
        """Get the clock source (port/pin name)."""
        return self.source if self.source else self.name
    
    def __repr__(self) -> str:
        freq_str = f"{self.frequency_mhz:.2f}MHz"
        return f"ClockConstraint({self.name!r}, {freq_str})"


@dataclass
class ClockGroup:
    """Clock group constraint.
    
    Defines a group of clocks that are logically or physically exclusive.
    Maps to SDC `set_clock_groups` command.
    
    Attributes:
        name: Name of this clock group.
        clocks: List of clock names in this group.
        group_type: Type of exclusivity (logically_exclusive, physically_exclusive, asynchronous).
        
    Example:
        >>> group1 = ClockGroup("group1", ["clk_a", "clk_b"], "asynchronous")
        >>> group2 = ClockGroup("group2", ["clk_c"], "asynchronous")
    """
    name: str
    clocks: List[str] = field(default_factory=list)
    group_type: str = "asynchronous"
    
    def __post_init__(self):
        valid_types = ("logically_exclusive", "physically_exclusive", "asynchronous")
        if self.group_type not in valid_types:
            raise ValueError(f"group_type must be one of {valid_types}, got {self.group_type}")


@dataclass
class ClockLatency:
    """Clock latency constraint.
    
    Specifies the latency of a clock network.
    Maps to SDC `set_clock_latency` command.
    
    Attributes:
        clock: Clock name.
        latency_ns: Latency in nanoseconds.
        is_early: If True, specifies early latency; else late latency.
        is_source: If True, specifies source latency; else network latency.
    """
    clock: str
    latency_ns: float
    is_early: bool = False
    is_source: bool = False


@dataclass
class ClockUncertainty:
    """Clock uncertainty constraint.
    
    Specifies the uncertainty (skew) between clocks.
    Maps to SDC `set_clock_uncertainty` command.
    
    Attributes:
        uncertainty_ns: Uncertainty in nanoseconds.
        from_clock: Source clock (optional, for inter-clock uncertainty).
        to_clock: Destination clock (optional, for inter-clock uncertainty).
        setup: If True, applies to setup analysis.
        hold: If True, applies to hold analysis.
    """
    uncertainty_ns: float
    from_clock: Optional[str] = None
    to_clock: Optional[str] = None
    setup: bool = True
    hold: bool = True


# =============================================================================
# I/O Constraints
# =============================================================================

@dataclass
class InputDelayConstraint:
    """Input delay constraint.
    
    Specifies the delay from a clock edge to the arrival of data at an input port.
    Maps to SDC `set_input_delay` command.
    
    Attributes:
        port: Input port name (can include wildcards like "data_*").
        clock: Reference clock name.
        min_delay: Minimum delay in nanoseconds.
        max_delay: Maximum delay in nanoseconds.
        clock_fall: If True, delay is relative to falling clock edge.
        add_delay: If True, add to existing delay instead of replacing.
        
    Example:
        >>> delay = InputDelayConstraint(
        ...     port="data_in",
        ...     clock="clk",
        ...     min_delay=0.5,
        ...     max_delay=2.0
        ... )
    """
    port: str
    clock: str
    min_delay: float
    max_delay: float
    clock_fall: bool = False
    add_delay: bool = False
    
    def __post_init__(self):
        if self.min_delay < 0 or self.max_delay < 0:
            raise ValueError("Delays must be non-negative")
        if self.min_delay > self.max_delay:
            raise ValueError(f"min_delay ({self.min_delay}) must be <= max_delay ({self.max_delay})")


@dataclass
class OutputDelayConstraint:
    """Output delay constraint.
    
    Specifies the delay from an output port to the next sequential element.
    Maps to SDC `set_output_delay` command.
    
    Attributes:
        port: Output port name (can include wildcards).
        clock: Reference clock name.
        min_delay: Minimum delay in nanoseconds.
        max_delay: Maximum delay in nanoseconds.
        clock_fall: If True, delay is relative to falling clock edge.
        add_delay: If True, add to existing delay instead of replacing.
        
    Example:
        >>> delay = OutputDelayConstraint(
        ...     port="data_out",
        ...     clock="clk",
        ...     min_delay=0.5,
        ...     max_delay=2.0
        ... )
    """
    port: str
    clock: str
    min_delay: float
    max_delay: float
    clock_fall: bool = False
    add_delay: bool = False
    
    def __post_init__(self):
        if self.min_delay < 0 or self.max_delay < 0:
            raise ValueError("Delays must be non-negative")
        if self.min_delay > self.max_delay:
            raise ValueError(f"min_delay ({self.min_delay}) must be <= max_delay ({self.max_delay})")


# =============================================================================
# Path Constraints
# =============================================================================

@dataclass
class FalsePathConstraint:
    """False path constraint.
    
    Identifies paths that should not be analyzed for timing.
    Maps to SDC `set_false_path` command.
    
    Attributes:
        from_ports: Source ports/pins (can include wildcards).
        to_ports: Destination ports/pins (can include wildcards).
        through: Intermediate points (optional).
        comment: Optional comment.
        
    Example:
        >>> fp = FalsePathConstraint(
        ...     from_ports="reset",
        ...     to_ports="*"
        ... )
        >>> fp = FalsePathConstraint(
        ...     from_ports="test_mode",
        ...     to_ports="alu/*"
        ... )
    """
    from_ports: Optional[str] = None
    to_ports: Optional[str] = None
    through: Optional[Union[str, List[str]]] = None
    comment: Optional[str] = None
    
    def __post_init__(self):
        if self.from_ports is None and self.to_ports is None and self.through is None:
            raise ValueError("At least one of from_ports, to_ports, or through must be specified")


@dataclass
class MulticyclePathConstraint:
    """Multicycle path constraint.
    
    Specifies paths that require multiple clock cycles.
    Maps to SDC `set_multicycle_path` command.
    
    Attributes:
        setup: Number of clock cycles for setup analysis.
        hold: Number of clock cycles for hold analysis.
        from_ports: Source ports/pins (optional).
        to_ports: Destination ports/pins (optional).
        through: Intermediate points (optional).
        start: If True, multiplier applies to launch clock.
        end: If True, multiplier applies to latch clock.
        comment: Optional comment.
        
    Example:
        >>> mcp = MulticyclePathConstraint(
        ...     setup=2,
        ...     hold=1,
        ...     from_ports="multiplier/*",
        ...     to_ports="accumulator/*"
        ... )
    """
    setup: int
    hold: Optional[int] = None
    from_ports: Optional[str] = None
    to_ports: Optional[str] = None
    through: Optional[Union[str, List[str]]] = None
    start: bool = False
    end: bool = True
    comment: Optional[str] = None
    
    def __post_init__(self):
        if self.setup < 1:
            raise ValueError("setup must be >= 1")
        if self.hold is not None and self.hold < 0:
            raise ValueError("hold must be >= 0")


@dataclass
class MaxDelayConstraint:
    """Maximum delay constraint.
    
    Specifies an explicit maximum delay for a path.
    Maps to SDC `set_max_delay` command.
    
    Attributes:
        delay_ns: Maximum delay in nanoseconds.
        from_ports: Source ports/pins (optional).
        to_ports: Destination ports/pins (optional).
        through: Intermediate points (optional).
        comment: Optional comment.
    """
    delay_ns: float
    from_ports: Optional[str] = None
    to_ports: Optional[str] = None
    through: Optional[Union[str, List[str]]] = None
    comment: Optional[str] = None
    
    def __post_init__(self):
        if self.delay_ns < 0:
            raise ValueError("delay_ns must be non-negative")


# =============================================================================
# Physical Constraints (XDC-specific)
# =============================================================================

@dataclass
class IOStandardConstraint:
    """I/O standard constraint.
    
    Specifies the electrical standard for an I/O port.
    Maps to XDC `set_property IOSTANDARD` command.
    
    Attributes:
        port: Port name (can include wildcards).
        standard: I/O standard name (e.g., "LVCMOS33", "LVDS", "HSTL_I").
        
    Example:
        >>> io = IOStandardConstraint("data_in", "LVCMOS33")
        >>> io = IOStandardConstraint("clk", "LVDS")
    """
    port: str
    standard: str


@dataclass
class DriveStrengthConstraint:
    """Drive strength constraint.
    
    Specifies the output drive strength for a port.
    Maps to XDC `set_property DRIVE` command.
    
    Attributes:
        port: Output port name.
        strength: Drive strength (typically in mA, e.g., 4, 8, 12, 16).
        
    Example:
        >>> drive = DriveStrengthConstraint("data_out", 12)
    """
    port: str
    strength: int
    
    def __post_init__(self):
        if self.strength <= 0:
            raise ValueError("strength must be positive")


@dataclass
class LocationConstraint:
    """Pin/package location constraint.
    
    Specifies the physical location for a port.
    Maps to XDC `set_property PACKAGE_PIN` or `set_property LOC` command.
    
    Attributes:
        port: Port name.
        pin: Pin location (e.g., "A12", "Y23").
        
    Example:
        >>> loc = LocationConstraint("clk", "A12")
    """
    port: str
    pin: str


@dataclass
class IOBankConstraint:
    """I/O bank voltage constraint.
    
    Specifies the voltage for an I/O bank.
    Maps to XDC `set_property IOBANK` or voltage constraints.
    
    Attributes:
        bank: I/O bank number.
        voltage: Bank voltage (e.g., "3.3V", "1.8V").
    """
    bank: int
    voltage: str


# =============================================================================
# Constraint Set
# =============================================================================

class ConstraintSet:
    """Collection of timing and physical constraints for a design.
    
    The ConstraintSet holds all constraints defined for a circuit and provides
    methods to export them to various formats (SDC, XDC).
    
    Attributes:
        clocks: List of clock constraints.
        clock_groups: List of clock group constraints.
        clock_latencies: List of clock latency constraints.
        clock_uncertainties: List of clock uncertainty constraints.
        input_delays: List of input delay constraints.
        output_delays: List of output delay constraints.
        false_paths: List of false path constraints.
        multicycle_paths: List of multicycle path constraints.
        max_delays: List of max delay constraints.
        io_standards: List of I/O standard constraints.
        drive_strengths: List of drive strength constraints.
        locations: List of location constraints.
        io_banks: List of I/O bank constraints.
        
    Example:
        >>> constraints = ConstraintSet()
        >>> 
        >>> # Add clock constraint
        >>> constraints.add_clock(ClockConstraint.from_frequency("clk", "250MHz"))
        >>> 
        >>> # Add I/O constraints
        >>> constraints.add_input_delay(InputDelayConstraint(
        ...     port="data_in",
        ...     clock="clk",
        ...     min_delay=0.5,
        ...     max_delay=2.0
        ... ))
        >>> 
        >>> # Export to SDC
        >>> sdc_content = constraints.to_sdc()
        >>> 
        >>> # Export to XDC
        >>> xdc_content = constraints.to_xdc()
    """
    
    def __init__(self):
        """Initialize an empty constraint set."""
        self.clocks: List[ClockConstraint] = []
        self.clock_groups: List[ClockGroup] = []
        self.clock_latencies: List[ClockLatency] = []
        self.clock_uncertainties: List[ClockUncertainty] = []
        self.input_delays: List[InputDelayConstraint] = []
        self.output_delays: List[OutputDelayConstraint] = []
        self.false_paths: List[FalsePathConstraint] = []
        self.multicycle_paths: List[MulticyclePathConstraint] = []
        self.max_delays: List[MaxDelayConstraint] = []
        self.io_standards: List[IOStandardConstraint] = []
        self.drive_strengths: List[DriveStrengthConstraint] = []
        self.locations: List[LocationConstraint] = []
        self.io_banks: List[IOBankConstraint] = []
    
    # -------------------------------------------------------------------------
    # Clock Constraints
    # -------------------------------------------------------------------------
    
    def add_clock(self, clock: ClockConstraint) -> None:
        """Add a clock constraint.
        
        Args:
            clock: The clock constraint to add.
        """
        self.clocks.append(clock)
    
    def clock(
        self,
        name: str,
        frequency: Optional[Union[str, float]] = None,
        period_ns: Optional[float] = None,
        duty_cycle: float = 0.5,
        jitter: Optional[float] = None,
        source: Optional[str] = None,
        comment: Optional[str] = None
    ) -> ClockConstraint:
        """Create and add a clock constraint.
        
        Args:
            name: Clock name.
            frequency: Frequency as string (e.g., "250MHz") or float (MHz).
            period_ns: Period in nanoseconds (alternative to frequency).
            duty_cycle: Clock duty cycle (0.0 to 1.0, default 0.5).
            jitter: Clock jitter in nanoseconds (optional).
            source: Source port/pin (default: uses name).
            comment: Optional comment.
            
        Returns:
            The created ClockConstraint.
            
        Raises:
            ValueError: If neither frequency nor period_ns is specified.
        """
        if frequency is not None:
            clock = ClockConstraint.from_frequency(
                name=name,
                frequency=frequency,
                duty_cycle=duty_cycle,
                jitter_ns=jitter,
                source=source,
                comment=comment
            )
        elif period_ns is not None:
            waveform = ClockWaveform(duty_cycle=duty_cycle)
            clock = ClockConstraint(
                name=name,
                period_ns=period_ns,
                waveform=waveform,
                jitter_ns=jitter,
                source=source,
                comment=comment
            )
        else:
            raise ValueError("Either frequency or period_ns must be specified")
        
        self.add_clock(clock)
        return clock
    
    def add_clock_group(self, group: ClockGroup) -> None:
        """Add a clock group constraint."""
        self.clock_groups.append(group)
    
    def add_clock_latency(self, latency: ClockLatency) -> None:
        """Add a clock latency constraint."""
        self.clock_latencies.append(latency)
    
    def add_clock_uncertainty(self, uncertainty: ClockUncertainty) -> None:
        """Add a clock uncertainty constraint."""
        self.clock_uncertainties.append(uncertainty)
    
    # -------------------------------------------------------------------------
    # I/O Constraints
    # -------------------------------------------------------------------------
    
    def add_input_delay(self, delay: InputDelayConstraint) -> None:
        """Add an input delay constraint."""
        self.input_delays.append(delay)
    
    def input_delay(
        self,
        port: str,
        clock: str,
        min_delay: float,
        max_delay: float,
        clock_fall: bool = False,
        add_delay: bool = False,
        comment: Optional[str] = None
    ) -> InputDelayConstraint:
        """Create and add an input delay constraint.
        
        Args:
            port: Input port name.
            clock: Reference clock name.
            min_delay: Minimum delay in nanoseconds.
            max_delay: Maximum delay in nanoseconds.
            clock_fall: If True, delay relative to falling clock edge.
            add_delay: If True, add to existing delay.
            comment: Optional comment (for documentation only).
            
        Returns:
            The created InputDelayConstraint.
        """
        delay = InputDelayConstraint(
            port=port,
            clock=clock,
            min_delay=min_delay,
            max_delay=max_delay,
            clock_fall=clock_fall,
            add_delay=add_delay
        )
        self.add_input_delay(delay)
        return delay
    
    def add_output_delay(self, delay: OutputDelayConstraint) -> None:
        """Add an output delay constraint."""
        self.output_delays.append(delay)
    
    def output_delay(
        self,
        port: str,
        clock: str,
        min_delay: float,
        max_delay: float,
        clock_fall: bool = False,
        add_delay: bool = False,
        comment: Optional[str] = None
    ) -> OutputDelayConstraint:
        """Create and add an output delay constraint.
        
        Args:
            port: Output port name.
            clock: Reference clock name.
            min_delay: Minimum delay in nanoseconds.
            max_delay: Maximum delay in nanoseconds.
            clock_fall: If True, delay relative to falling clock edge.
            add_delay: If True, add to existing delay.
            comment: Optional comment (for documentation only).
            
        Returns:
            The created OutputDelayConstraint.
        """
        delay = OutputDelayConstraint(
            port=port,
            clock=clock,
            min_delay=min_delay,
            max_delay=max_delay,
            clock_fall=clock_fall,
            add_delay=add_delay
        )
        self.add_output_delay(delay)
        return delay
    
    # -------------------------------------------------------------------------
    # Path Constraints
    # -------------------------------------------------------------------------
    
    def add_false_path(self, path: FalsePathConstraint) -> None:
        """Add a false path constraint."""
        self.false_paths.append(path)
    
    def set_false_path(
        self,
        from_ports: Optional[str] = None,
        to_ports: Optional[str] = None,
        through: Optional[Union[str, List[str]]] = None,
        comment: Optional[str] = None
    ) -> FalsePathConstraint:
        """Create and add a false path constraint.
        
        Args:
            from_ports: Source ports/pins (can include wildcards).
            to_ports: Destination ports/pins (can include wildcards).
            through: Intermediate points (optional).
            comment: Optional comment.
            
        Returns:
            The created FalsePathConstraint.
        """
        path = FalsePathConstraint(
            from_ports=from_ports,
            to_ports=to_ports,
            through=through,
            comment=comment
        )
        self.add_false_path(path)
        return path
    
    def add_multicycle_path(self, path: MulticyclePathConstraint) -> None:
        """Add a multicycle path constraint."""
        self.multicycle_paths.append(path)
    
    def set_multicycle_path(
        self,
        setup: int,
        hold: Optional[int] = None,
        from_ports: Optional[str] = None,
        to_ports: Optional[str] = None,
        through: Optional[Union[str, List[str]]] = None,
        start: bool = False,
        end: bool = True,
        comment: Optional[str] = None
    ) -> MulticyclePathConstraint:
        """Create and add a multicycle path constraint.
        
        Args:
            setup: Number of clock cycles for setup analysis.
            hold: Number of clock cycles for hold analysis (optional).
            from_ports: Source ports/pins (can include wildcards).
            to_ports: Destination ports/pins (can include wildcards).
            through: Intermediate points (optional).
            start: If True, multiplier applies to launch clock.
            end: If True, multiplier applies to latch clock.
            comment: Optional comment.
            
        Returns:
            The created MulticyclePathConstraint.
        """
        path = MulticyclePathConstraint(
            setup=setup,
            hold=hold,
            from_ports=from_ports,
            to_ports=to_ports,
            through=through,
            start=start,
            end=end,
            comment=comment
        )
        self.add_multicycle_path(path)
        return path
    
    def add_max_delay(self, delay: MaxDelayConstraint) -> None:
        """Add a max delay constraint."""
        self.max_delays.append(delay)
    
    # -------------------------------------------------------------------------
    # Physical Constraints
    # -------------------------------------------------------------------------
    
    def add_io_standard(self, io: IOStandardConstraint) -> None:
        """Add an I/O standard constraint."""
        self.io_standards.append(io)
    
    def set_io_standard(self, port: str, standard: str) -> IOStandardConstraint:
        """Create and add an I/O standard constraint.
        
        Args:
            port: Port name (can include wildcards).
            standard: I/O standard name (e.g., "LVCMOS33", "LVDS").
            
        Returns:
            The created IOStandardConstraint.
        """
        io = IOStandardConstraint(port=port, standard=standard)
        self.add_io_standard(io)
        return io
    
    def add_drive_strength(self, drive: DriveStrengthConstraint) -> None:
        """Add a drive strength constraint."""
        self.drive_strengths.append(drive)
    
    def set_drive_strength(self, port: str, strength: int) -> DriveStrengthConstraint:
        """Create and add a drive strength constraint.
        
        Args:
            port: Output port name.
            strength: Drive strength in mA.
            
        Returns:
            The created DriveStrengthConstraint.
        """
        drive = DriveStrengthConstraint(port=port, strength=strength)
        self.add_drive_strength(drive)
        return drive
    
    def add_location(self, loc: LocationConstraint) -> None:
        """Add a location constraint."""
        self.locations.append(loc)
    
    def set_location(self, port: str, pin: str) -> LocationConstraint:
        """Create and add a location constraint.
        
        Args:
            port: Port name.
            pin: Pin location (e.g., "A12").
            
        Returns:
            The created LocationConstraint.
        """
        loc = LocationConstraint(port=port, pin=pin)
        self.add_location(loc)
        return loc
    
    def add_io_bank(self, bank: IOBankConstraint) -> None:
        """Add an I/O bank constraint."""
        self.io_banks.append(bank)
    
    # -------------------------------------------------------------------------
    # Export Methods
    # -------------------------------------------------------------------------
    
    def to_sdc(self) -> str:
        """Export constraints to SDC (Synopsys Design Constraints) format.
        
        SDC is the industry standard timing constraint format supported by
        most FPGA and ASIC tools (Vivado, Quartus Prime, Design Compiler, etc.).
        
        Returns:
            SDC file content as a string.
        """
        from .backends._sdc_generator import SDCGenerator
        return SDCGenerator().generate(self)
    
    def to_xdc(self) -> str:
        """Export constraints to XDC (Xilinx Design Constraints) format.
        
        XDC is Xilinx's constraint format that extends SDC with physical
        constraints (I/O standards, pin locations, etc.).
        
        Returns:
            XDC file content as a string.
        """
        from .backends._sdc_generator import XDCGenerator
        return XDCGenerator().generate(self)
    
    def is_empty(self) -> bool:
        """Check if the constraint set is empty.
        
        Returns:
            True if no constraints have been added.
        """
        return not any([
            self.clocks,
            self.clock_groups,
            self.clock_latencies,
            self.clock_uncertainties,
            self.input_delays,
            self.output_delays,
            self.false_paths,
            self.multicycle_paths,
            self.max_delays,
            self.io_standards,
            self.drive_strengths,
            self.locations,
            self.io_banks,
        ])
    
    def __repr__(self) -> str:
        counts = []
        if self.clocks:
            counts.append(f"{len(self.clocks)} clocks")
        if self.input_delays:
            counts.append(f"{len(self.input_delays)} input delays")
        if self.output_delays:
            counts.append(f"{len(self.output_delays)} output delays")
        if self.false_paths:
            counts.append(f"{len(self.false_paths)} false paths")
        if self.multicycle_paths:
            counts.append(f"{len(self.multicycle_paths)} multicycle paths")
        if not counts:
            counts.append("empty")
        return f"ConstraintSet({', '.join(counts)})"


# =============================================================================
# Utility Functions
# =============================================================================

def _parse_frequency(frequency: Union[str, float]) -> float:
    """Parse a frequency specification into MHz.
    
    Args:
        frequency: Frequency as string (e.g., "250MHz", "100 MHz", "1GHz")
                  or float (interpreted as MHz).
                  
    Returns:
        Frequency in MHz.
        
    Raises:
        ValueError: If frequency format is invalid.
        
    Example:
        >>> _parse_frequency("250MHz")
        250.0
        >>> _parse_frequency("1.5 GHz")
        1500.0
        >>> _parse_frequency(100.0)
        100.0
    """
    if isinstance(frequency, (int, float)):
        return float(frequency)
    
    freq_str = frequency.strip().lower()
    
    # Remove spaces
    freq_str = freq_str.replace(" ", "")
    
    # Extract numeric value and unit
    if freq_str.endswith("mhz"):
        value = freq_str[:-3]
        multiplier = 1.0
    elif freq_str.endswith("ghz"):
        value = freq_str[:-3]
        multiplier = 1000.0
    elif freq_str.endswith("khz"):
        value = freq_str[:-3]
        multiplier = 0.001
    elif freq_str.endswith("hz"):
        value = freq_str[:-2]
        multiplier = 0.000001
    else:
        # Assume MHz if no unit
        try:
            return float(freq_str)
        except ValueError:
            raise ValueError(f"Invalid frequency format: {frequency!r}")
    
    try:
        return float(value) * multiplier
    except ValueError:
        raise ValueError(f"Invalid frequency value: {frequency!r}")


# Make ConstraintSet available at module level for imports
__all__ = [
    # Constraint set
    "ConstraintSet",
    # Clock constraints
    "ClockConstraint",
    "ClockWaveform",
    "ClockGroup",
    "ClockLatency",
    "ClockUncertainty",
    # I/O constraints
    "InputDelayConstraint",
    "OutputDelayConstraint",
    # Path constraints
    "FalsePathConstraint",
    "MulticyclePathConstraint",
    "MaxDelayConstraint",
    # Physical constraints
    "IOStandardConstraint",
    "DriveStrengthConstraint",
    "LocationConstraint",
    "IOBankConstraint",
    # Utility
    "_parse_frequency",
]
