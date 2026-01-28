#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""SDC and XDC Constraint File Generators.

This module provides generators for Synopsys Design Constraints (SDC) and
Xilinx Design Constraints (XDC) files. These constraint formats are used
by FPGA and ASIC synthesis/implementation tools.

Supported formats:
- SDC: Industry standard timing constraints (Vivado, Quartus, DC, etc.)
- XDC: Xilinx-specific format including physical constraints

Example:
    from cmt2.constraints import ConstraintSet, ClockConstraint
    from cmt2.backends._sdc_generator import SDCGenerator, XDCGenerator
    
    # Create constraint set
    constraints = ConstraintSet()
    constraints.add_clock(ClockConstraint.from_frequency("clk", "250MHz"))
    
    # Generate SDC
    sdc_gen = SDCGenerator()
    sdc_content = sdc_gen.generate(constraints)
    
    # Generate XDC
    xdc_gen = XDCGenerator()
    xdc_content = xdc_gen.generate(constraints)
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import TYPE_CHECKING, List, Optional
from datetime import datetime

if TYPE_CHECKING:
    from ..constraints import (
        ConstraintSet,
        ClockConstraint,
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


# =============================================================================
# Base Generator
# =============================================================================

class ConstraintGenerator(ABC):
    """Base class for constraint file generators.
    
    Provides common functionality for generating constraint files in
    various formats (SDC, XDC, etc.).
    
    Attributes:
        header_comment: Optional header comment for the generated file.
        include_timestamp: Whether to include generation timestamp.
    """
    
    def __init__(
        self,
        header_comment: Optional[str] = None,
        include_timestamp: bool = True
    ):
        """Initialize the constraint generator.
        
        Args:
            header_comment: Optional comment to include in the file header.
            include_timestamp: Whether to include generation timestamp.
        """
        self.header_comment = header_comment
        self.include_timestamp = include_timestamp
    
    @abstractmethod
    def generate(self, constraints: ConstraintSet) -> str:
        """Generate constraint file content.
        
        Args:
            constraints: The constraint set to export.
            
        Returns:
            Constraint file content as a string.
        """
        pass
    
    def _generate_header(self) -> str:
        """Generate file header with comments."""
        lines = ["#" * 70]
        lines.append("# Timing Constraints")
        lines.append("#")
        
        if self.header_comment:
            lines.append(f"# {self.header_comment}")
            lines.append("#")
        
        if self.include_timestamp:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            lines.append(f"# Generated: {timestamp}")
        
        lines.append("#" * 70)
        lines.append("")
        
        return "\n".join(lines)
    
    def _escape_sdc_string(self, s: str) -> str:
        """Escape special characters in SDC strings.
        
        Args:
            s: String to escape.
            
        Returns:
            Escaped string.
        """
        # Escape braces and special characters
        return s.replace("[", "\\[").replace("]", "\\]")


# =============================================================================
# SDC Generator
# =============================================================================

class SDCGenerator(ConstraintGenerator):
    """Generator for SDC (Synopsys Design Constraints) files.
    
    SDC is the industry standard format for timing constraints, supported by
    most synthesis and implementation tools including:
    - Xilinx Vivado
    - Intel Quartus Prime
    - Synopsys Design Compiler
    - Cadence Genus
    
    Example:
        >>> from cmt2.constraints import ConstraintSet, ClockConstraint
        >>> constraints = ConstraintSet()
        >>> constraints.add_clock(ClockConstraint.from_frequency("clk", "250MHz"))
        >>> 
        >>> generator = SDCGenerator(header_comment="My Design Constraints")
        >>> sdc_content = generator.generate(constraints)
        >>> print(sdc_content)
        ########################################################################
        # Timing Constraints
        # My Design Constraints
        # Generated: 2025-01-28 10:30:00
        ########################################################################
        
        # Clock Constraints
        create_clock -name clk -period 4.000 [get_ports clk]
    """
    
    def generate(self, constraints: ConstraintSet) -> str:
        """Generate SDC file content from constraint set.
        
        Args:
            constraints: The constraint set to export.
            
        Returns:
            SDC file content as a string.
        """
        lines: List[str] = []
        
        # Header
        lines.append(self._generate_header())
        
        # Clock constraints
        if constraints.clocks:
            lines.append("# " + "=" * 68)
            lines.append("# Clock Constraints")
            lines.append("# " + "=" * 68)
            lines.append("")
            for clock in constraints.clocks:
                lines.append(self._format_clock(clock))
            lines.append("")
        
        # Clock groups
        if constraints.clock_groups:
            lines.append("# " + "=" * 68)
            lines.append("# Clock Groups")
            lines.append("# " + "=" * 68)
            lines.append("")
            for group in constraints.clock_groups:
                lines.append(self._format_clock_group(group))
            lines.append("")
        
        # Clock latencies
        if constraints.clock_latencies:
            lines.append("# " + "=" * 68)
            lines.append("# Clock Latency")
            lines.append("# " + "=" * 68)
            lines.append("")
            for latency in constraints.clock_latencies:
                lines.append(self._format_clock_latency(latency))
            lines.append("")
        
        # Clock uncertainties
        if constraints.clock_uncertainties:
            lines.append("# " + "=" * 68)
            lines.append("# Clock Uncertainty")
            lines.append("# " + "=" * 68)
            lines.append("")
            for uncertainty in constraints.clock_uncertainties:
                lines.append(self._format_clock_uncertainty(uncertainty))
            lines.append("")
        
        # Input delays
        if constraints.input_delays:
            lines.append("# " + "=" * 68)
            lines.append("# Input Delay Constraints")
            lines.append("# " + "=" * 68)
            lines.append("")
            for delay in constraints.input_delays:
                lines.append(self._format_input_delay(delay))
            lines.append("")
        
        # Output delays
        if constraints.output_delays:
            lines.append("# " + "=" * 68)
            lines.append("# Output Delay Constraints")
            lines.append("# " + "=" * 68)
            lines.append("")
            for delay in constraints.output_delays:
                lines.append(self._format_output_delay(delay))
            lines.append("")
        
        # False paths
        if constraints.false_paths:
            lines.append("# " + "=" * 68)
            lines.append("# False Path Constraints")
            lines.append("# " + "=" * 68)
            lines.append("")
            for path in constraints.false_paths:
                lines.append(self._format_false_path(path))
            lines.append("")
        
        # Multicycle paths
        if constraints.multicycle_paths:
            lines.append("# " + "=" * 68)
            lines.append("# Multicycle Path Constraints")
            lines.append("# " + "=" * 68)
            lines.append("")
            for path in constraints.multicycle_paths:
                lines.append(self._format_multicycle_path(path))
            lines.append("")
        
        # Max delays
        if constraints.max_delays:
            lines.append("# " + "=" * 68)
            lines.append("# Max Delay Constraints")
            lines.append("# " + "=" * 68)
            lines.append("")
            for delay in constraints.max_delays:
                lines.append(self._format_max_delay(delay))
            lines.append("")
        
        return "\n".join(lines)
    
    def _format_clock(self, clock: ClockConstraint) -> str:
        """Format a clock constraint as SDC command.
        
        Args:
            clock: The clock constraint.
            
        Returns:
            SDC create_clock command.
        """
        parts = ["create_clock"]
        
        # Clock name
        parts.append(f"-name {clock.name}")
        
        # Period
        parts.append(f"-period {clock.period_ns:.3f}")
        
        # Waveform (if non-default)
        if clock.waveform and (
            clock.waveform.duty_cycle != 0.5 or clock.waveform.phase_deg != 0.0
        ):
            rise = clock.waveform.phase_deg / 360.0 * clock.period_ns
            high_time = clock.waveform.duty_cycle * clock.period_ns
            fall = (rise + high_time) % clock.period_ns
            parts.append(f"-waveform {{{rise:.3f} {fall:.3f}}}")
        
        # Source port/pin
        source = self._escape_sdc_string(clock.get_source())
        parts.append(f"[get_ports {source}]")
        
        result = " ".join(parts)
        
        # Add comment if present
        if clock.comment:
            result += f" ;# {clock.comment}"
        
        return result
    
    def _format_clock_group(self, group: ClockGroup) -> str:
        """Format a clock group constraint as SDC command.
        
        Args:
            group: The clock group constraint.
            
        Returns:
            SDC set_clock_groups command.
        """
        parts = ["set_clock_groups"]
        parts.append(f"-{group.group_type}")
        
        # Group name as comment
        parts.append(f"-group {{ {' '.join(group.clocks)} }} ;# {group.name}")
        
        return " ".join(parts)
    
    def _format_clock_latency(self, latency: ClockLatency) -> str:
        """Format a clock latency constraint as SDC command.
        
        Args:
            latency: The clock latency constraint.
            
        Returns:
            SDC set_clock_latency command.
        """
        parts = ["set_clock_latency"]
        
        if latency.is_source:
            parts.append("-source")
        
        if latency.is_early:
            parts.append("-early")
        else:
            parts.append("-late")
        
        parts.append(f"{latency.latency_ns:.3f}")
        parts.append(f"[get_clocks {latency.clock}]")
        
        return " ".join(parts)
    
    def _format_clock_uncertainty(self, uncertainty: ClockUncertainty) -> str:
        """Format a clock uncertainty constraint as SDC command.
        
        Args:
            uncertainty: The clock uncertainty constraint.
            
        Returns:
            SDC set_clock_uncertainty command.
        """
        parts = ["set_clock_uncertainty"]
        
        if uncertainty.from_clock or uncertainty.to_clock:
            if uncertainty.from_clock:
                parts.append(f"-from [get_clocks {uncertainty.from_clock}]")
            if uncertainty.to_clock:
                parts.append(f"-to [get_clocks {uncertainty.to_clock}]")
        
        if not uncertainty.setup:
            parts.append("-hold")
        elif not uncertainty.hold:
            parts.append("-setup")
        
        parts.append(f"{uncertainty.uncertainty_ns:.3f}")
        
        return " ".join(parts)
    
    def _format_input_delay(self, delay: InputDelayConstraint) -> str:
        """Format an input delay constraint as SDC command.
        
        Args:
            delay: The input delay constraint.
            
        Returns:
            SDC set_input_delay command.
        """
        lines = []
        port = self._escape_sdc_string(delay.port)
        
        # Min delay
        parts_min = ["set_input_delay"]
        parts_min.append("-min")
        if delay.clock_fall:
            parts_min.append("-clock_fall")
        if delay.add_delay:
            parts_min.append("-add_delay")
        parts_min.append(f"-clock [get_clocks {delay.clock}]")
        parts_min.append(f"{delay.min_delay:.3f}")
        parts_min.append(f"[get_ports {port}]")
        lines.append(" ".join(parts_min))
        
        # Max delay
        parts_max = ["set_input_delay"]
        parts_max.append("-max")
        if delay.clock_fall:
            parts_max.append("-clock_fall")
        if delay.add_delay:
            parts_max.append("-add_delay")
        parts_max.append(f"-clock [get_clocks {delay.clock}]")
        parts_max.append(f"{delay.max_delay:.3f}")
        parts_max.append(f"[get_ports {port}]")
        lines.append(" ".join(parts_max))
        
        return "\n".join(lines)
    
    def _format_output_delay(self, delay: OutputDelayConstraint) -> str:
        """Format an output delay constraint as SDC command.
        
        Args:
            delay: The output delay constraint.
            
        Returns:
            SDC set_output_delay command.
        """
        lines = []
        port = self._escape_sdc_string(delay.port)
        
        # Min delay
        parts_min = ["set_output_delay"]
        parts_min.append("-min")
        if delay.clock_fall:
            parts_min.append("-clock_fall")
        if delay.add_delay:
            parts_min.append("-add_delay")
        parts_min.append(f"-clock [get_clocks {delay.clock}]")
        parts_min.append(f"{delay.min_delay:.3f}")
        parts_min.append(f"[get_ports {port}]")
        lines.append(" ".join(parts_min))
        
        # Max delay
        parts_max = ["set_output_delay"]
        parts_max.append("-max")
        if delay.clock_fall:
            parts_max.append("-clock_fall")
        if delay.add_delay:
            parts_max.append("-add_delay")
        parts_max.append(f"-clock [get_clocks {delay.clock}]")
        parts_max.append(f"{delay.max_delay:.3f}")
        parts_max.append(f"[get_ports {port}]")
        lines.append(" ".join(parts_max))
        
        return "\n".join(lines)
    
    def _format_false_path(self, path: FalsePathConstraint) -> str:
        """Format a false path constraint as SDC command.
        
        Args:
            path: The false path constraint.
            
        Returns:
            SDC set_false_path command.
        """
        parts = ["set_false_path"]
        
        if path.from_ports:
            from_esc = self._escape_sdc_string(path.from_ports)
            parts.append(f"-from [get_ports {from_esc}]")
        
        if path.to_ports:
            to_esc = self._escape_sdc_string(path.to_ports)
            parts.append(f"-to [get_ports {to_esc}]")
        
        if path.through:
            if isinstance(path.through, list):
                for t in path.through:
                    t_esc = self._escape_sdc_string(t)
                    parts.append(f"-through [get_pins {t_esc}]")
            else:
                t_esc = self._escape_sdc_string(path.through)
                parts.append(f"-through [get_pins {t_esc}]")
        
        result = " ".join(parts)
        
        if path.comment:
            result += f" ;# {path.comment}"
        
        return result
    
    def _format_multicycle_path(self, path: MulticyclePathConstraint) -> str:
        """Format a multicycle path constraint as SDC command.
        
        Args:
            path: The multicycle path constraint.
            
        Returns:
            SDC set_multicycle_path command.
        """
        lines = []
        
        # Setup
        parts_setup = ["set_multicycle_path"]
        parts_setup.append(f"{path.setup}")
        
        if path.start:
            parts_setup.append("-start")
        if path.end:
            parts_setup.append("-end")
        
        if path.from_ports:
            from_esc = self._escape_sdc_string(path.from_ports)
            parts_setup.append(f"-from [get_pins {from_esc}]")
        
        if path.to_ports:
            to_esc = self._escape_sdc_string(path.to_ports)
            parts_setup.append(f"-to [get_pins {to_esc}]")
        
        if path.through:
            if isinstance(path.through, list):
                for t in path.through:
                    t_esc = self._escape_sdc_string(t)
                    parts_setup.append(f"-through [get_pins {t_esc}]")
            else:
                t_esc = self._escape_sdc_string(path.through)
                parts_setup.append(f"-through [get_pins {t_esc}]")
        
        lines.append(" ".join(parts_setup))
        
        # Hold (if specified)
        if path.hold is not None:
            parts_hold = ["set_multicycle_path"]
            parts_hold.append(f"{path.hold}")
            parts_hold.append("-hold")
            
            if path.start:
                parts_hold.append("-start")
            if path.end:
                parts_hold.append("-end")
            
            if path.from_ports:
                from_esc = self._escape_sdc_string(path.from_ports)
                parts_hold.append(f"-from [get_pins {from_esc}]")
            
            if path.to_ports:
                to_esc = self._escape_sdc_string(path.to_ports)
                parts_hold.append(f"-to [get_pins {to_esc}]")
            
            if path.through:
                if isinstance(path.through, list):
                    for t in path.through:
                        t_esc = self._escape_sdc_string(t)
                        parts_hold.append(f"-through [get_pins {t_esc}]")
                else:
                    t_esc = self._escape_sdc_string(path.through)
                    parts_hold.append(f"-through [get_pins {t_esc}]")
            
            lines.append(" ".join(parts_hold))
        
        result = "\n".join(lines)
        
        if path.comment:
            result += f"\n# {path.comment}"
        
        return result
    
    def _format_max_delay(self, delay: MaxDelayConstraint) -> str:
        """Format a max delay constraint as SDC command.
        
        Args:
            delay: The max delay constraint.
            
        Returns:
            SDC set_max_delay command.
        """
        parts = ["set_max_delay"]
        parts.append(f"{delay.delay_ns:.3f}")
        
        if delay.from_ports:
            from_esc = self._escape_sdc_string(delay.from_ports)
            parts.append(f"-from [get_pins {from_esc}]")
        
        if delay.to_ports:
            to_esc = self._escape_sdc_string(delay.to_ports)
            parts.append(f"-to [get_pins {to_esc}]")
        
        if delay.through:
            if isinstance(delay.through, list):
                for t in delay.through:
                    t_esc = self._escape_sdc_string(t)
                    parts.append(f"-through [get_pins {t_esc}]")
            else:
                t_esc = self._escape_sdc_string(delay.through)
                parts.append(f"-through [get_pins {t_esc}]")
        
        result = " ".join(parts)
        
        if delay.comment:
            result += f" ;# {delay.comment}"
        
        return result


# =============================================================================
# XDC Generator
# =============================================================================

class XDCGenerator(SDCGenerator):
    """Generator for XDC (Xilinx Design Constraints) files.
    
    XDC extends SDC with physical constraints specific to Xilinx FPGAs,
    including I/O standards, pin locations, and drive strengths.
    
    Supported tools:
    - Xilinx Vivado
    - Xilinx ISE (with some limitations)
    
    Example:
        >>> from cmt2.constraints import (
        ...     ConstraintSet, ClockConstraint, IOStandardConstraint
        ... )
        >>> constraints = ConstraintSet()
        >>> constraints.add_clock(ClockConstraint.from_frequency("clk", "250MHz"))
        >>> constraints.add_io_standard(IOStandardConstraint("data_in", "LVCMOS33"))
        >>> 
        >>> generator = XDCGenerator(header_comment="Xilinx FPGA Constraints")
        >>> xdc_content = generator.generate(constraints)
    """
    
    def generate(self, constraints: ConstraintSet) -> str:
        """Generate XDC file content from constraint set.
        
        This generates all SDC constraints plus XDC-specific physical
        constraints (I/O standards, pin locations, etc.).
        
        Args:
            constraints: The constraint set to export.
            
        Returns:
            XDC file content as a string.
        """
        # Start with SDC content
        lines = SDCGenerator.generate(self, constraints).split("\n")
        
        # Add XDC-specific sections
        xdc_sections = []
        
        # I/O Standards
        if constraints.io_standards:
            xdc_sections.append("")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("# I/O Standard Constraints")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("")
            for io in constraints.io_standards:
                xdc_sections.append(self._format_io_standard(io))
        
        # Drive Strengths
        if constraints.drive_strengths:
            xdc_sections.append("")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("# Drive Strength Constraints")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("")
            for drive in constraints.drive_strengths:
                xdc_sections.append(self._format_drive_strength(drive))
        
        # Pin Locations
        if constraints.locations:
            xdc_sections.append("")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("# Pin Location Constraints")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("")
            for loc in constraints.locations:
                xdc_sections.append(self._format_location(loc))
        
        # I/O Banks
        if constraints.io_banks:
            xdc_sections.append("")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("# I/O Bank Voltage Constraints")
            xdc_sections.append("# " + "=" * 68)
            xdc_sections.append("")
            for bank in constraints.io_banks:
                xdc_sections.append(self._format_io_bank(bank))
        
        return "\n".join(lines + xdc_sections)
    
    def _format_io_standard(self, io: IOStandardConstraint) -> str:
        """Format an I/O standard constraint as XDC command.
        
        Args:
            io: The I/O standard constraint.
            
        Returns:
            XDC set_property IOSTANDARD command.
        """
        port = self._escape_sdc_string(io.port)
        return f'set_property IOSTANDARD {io.standard} [get_ports {port}]'
    
    def _format_drive_strength(self, drive: DriveStrengthConstraint) -> str:
        """Format a drive strength constraint as XDC command.
        
        Args:
            drive: The drive strength constraint.
            
        Returns:
            XDC set_property DRIVE command.
        """
        port = self._escape_sdc_string(drive.port)
        return f'set_property DRIVE {drive.strength} [get_ports {port}]'
    
    def _format_location(self, loc: LocationConstraint) -> str:
        """Format a location constraint as XDC command.
        
        Args:
            loc: The location constraint.
            
        Returns:
            XDC set_property PACKAGE_PIN command.
        """
        port = self._escape_sdc_string(loc.port)
        return f'set_property PACKAGE_PIN {loc.pin} [get_ports {port}]'
    
    def _format_io_bank(self, bank: IOBankConstraint) -> str:
        """Format an I/O bank constraint as XDC command.
        
        Args:
            bank: The I/O bank constraint.
            
        Returns:
            XDC set_property command for bank voltage.
        """
        return f'# Bank {bank.bank}: {bank.voltage}'


# =============================================================================
# Factory Functions
# =============================================================================

def generate_sdc(
    constraints: ConstraintSet,
    header_comment: Optional[str] = None,
    include_timestamp: bool = True
) -> str:
    """Generate SDC content from a constraint set.
    
    Convenience function for one-shot SDC generation.
    
    Args:
        constraints: The constraint set to export.
        header_comment: Optional comment for the file header.
        include_timestamp: Whether to include generation timestamp.
        
    Returns:
        SDC file content as a string.
        
    Example:
        >>> from cmt2.constraints import ConstraintSet, ClockConstraint
        >>> constraints = ConstraintSet()
        >>> constraints.add_clock(ClockConstraint.from_frequency("clk", "250MHz"))
        >>> sdc = generate_sdc(constraints, header_comment="My Design")
    """
    generator = SDCGenerator(
        header_comment=header_comment,
        include_timestamp=include_timestamp
    )
    return generator.generate(constraints)


def generate_xdc(
    constraints: ConstraintSet,
    header_comment: Optional[str] = None,
    include_timestamp: bool = True
) -> str:
    """Generate XDC content from a constraint set.
    
    Convenience function for one-shot XDC generation.
    
    Args:
        constraints: The constraint set to export.
        header_comment: Optional comment for the file header.
        include_timestamp: Whether to include generation timestamp.
        
    Returns:
        XDC file content as a string.
        
    Example:
        >>> from cmt2.constraints import (
        ...     ConstraintSet, ClockConstraint, LocationConstraint
        ... )
        >>> constraints = ConstraintSet()
        >>> constraints.add_clock(ClockConstraint.from_frequency("clk", "250MHz"))
        >>> constraints.add_location(LocationConstraint("clk", "A12"))
        >>> xdc = generate_xdc(constraints, header_comment="Xilinx Design")
    """
    generator = XDCGenerator(
        header_comment=header_comment,
        include_timestamp=include_timestamp
    )
    return generator.generate(constraints)


__all__ = [
    "ConstraintGenerator",
    "SDCGenerator",
    "XDCGenerator",
    "generate_sdc",
    "generate_xdc",
]
