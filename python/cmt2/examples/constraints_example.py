#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Example: Timing Constraints for CMT2 Designs.

This example demonstrates how to define timing constraints for CMT2 hardware
designs and export them to SDC/XDC format for use with FPGA/ASIC tools.

Usage:
    python constraints_example.py

Output:
    - design.sdc: SDC constraints file
    - design.xdc: XDC constraints file (includes physical constraints)
"""

import sys
from pathlib import Path

# Add the parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from cmt2.constraints import ConstraintSet


def create_processor_constraints() -> ConstraintSet:
    """Create timing constraints for a processor design.
    
    This example shows constraints for a typical processor with:
    - System clock at 250MHz
    - I/O clock at 100MHz
    - Multiple I/O interfaces with different standards
    - False paths for reset and test signals
    - Multicycle paths for multiplier/accumulator
    
    Returns:
        ConstraintSet with all constraints defined.
    """
    cs = ConstraintSet()
    
    # ========================================================================
    # Clock Definitions
    # ========================================================================
    
    print("Defining clocks...")
    
    # System clock - 250MHz
    cs.clock(
        name="clk_sys",
        frequency="250MHz",
        duty_cycle=0.5,
        comment="Main system clock"
    )
    
    # I/O clock - 100MHz
    cs.clock(
        name="clk_io",
        frequency="100MHz",
        duty_cycle=0.5,
        comment="I/O interface clock"
    )
    
    # DDR clock - 800MHz (with phase shift for center-aligned data)
    cs.clock(
        name="clk_ddr",
        frequency="800MHz",
        duty_cycle=0.5,
        comment="DDR memory interface clock"
    )
    
    # ========================================================================
    # Input Delay Constraints
    # ========================================================================
    
    print("Defining input delays...")
    
    # Data input from external device
    cs.input_delay(
        port="data_in[*]",
        clock="clk_sys",
        min_delay=0.5,   # Fastest arrival
        max_delay=2.0,   # Slowest arrival
        comment="External device data input"
    )
    
    # Control signals
    cs.input_delay(
        port="cmd_valid",
        clock="clk_sys",
        min_delay=0.2,
        max_delay=1.5
    )
    
    cs.input_delay(
        port="cmd_data[*]",
        clock="clk_sys",
        min_delay=0.5,
        max_delay=2.0
    )
    
    # I/O clock domain signals
    cs.input_delay(
        port="io_data_in[*]",
        clock="clk_io",
        min_delay=1.0,
        max_delay=4.0
    )
    
    # ========================================================================
    # Output Delay Constraints
    # ========================================================================
    
    print("Defining output delays...")
    
    # Data output to external device
    cs.output_delay(
        port="data_out[*]",
        clock="clk_sys",
        min_delay=0.5,
        max_delay=2.0,
        comment="External device data output"
    )
    
    # Response signals
    cs.output_delay(
        port="resp_ready",
        clock="clk_sys",
        min_delay=0.2,
        max_delay=1.5
    )
    
    cs.output_delay(
        port="resp_data[*]",
        clock="clk_sys",
        min_delay=0.5,
        max_delay=2.0
    )
    
    # I/O clock domain outputs
    cs.output_delay(
        port="io_data_out[*]",
        clock="clk_io",
        min_delay=1.0,
        max_delay=4.0
    )
    
    # ========================================================================
    # False Path Constraints
    # ========================================================================
    
    print("Defining false paths...")
    
    # Reset signal - asynchronous, no timing check needed
    cs.set_false_path(
        from_ports="rst_n",
        to_ports="*",
        comment="Asynchronous reset - no timing check"
    )
    
    # Test mode signals - only active during testing
    cs.set_false_path(
        from_ports="test_en",
        to_ports="*",
        comment="Test mode - not active during normal operation"
    )
    
    cs.set_false_path(
        from_ports="scan_in[*]",
        to_ports="*",
        comment="Scan chain input - test only"
    )
    
    cs.set_false_path(
        from_ports="*",
        to_ports="scan_out[*]",
        comment="Scan chain output - test only"
    )
    
    # Configuration registers - set at power-up only
    cs.set_false_path(
        from_ports="cfg_*",
        to_ports="*",
        comment="Configuration registers - static after power-up"
    )
    
    # ========================================================================
    # Multicycle Path Constraints
    # ========================================================================
    
    print("Defining multicycle paths...")
    
    # Multiplier to accumulator - takes 2 cycles
    cs.set_multicycle_path(
        setup=2,
        hold=1,
        from_ports="multiplier/*",
        to_ports="accumulator/*",
        comment="Multiplier result available after 2 cycles"
    )
    
    # Divider unit - takes 4 cycles
    cs.set_multicycle_path(
        setup=4,
        hold=3,
        from_ports="divider/*",
        to_ports="div_result/*",
        comment="Division result available after 4 cycles"
    )
    
    # Wide shifter - takes 2 cycles
    cs.set_multicycle_path(
        setup=2,
        hold=1,
        from_ports="shifter/in*",
        to_ports="shifter/out*",
        comment="Barrel shifter takes 2 cycles"
    )
    
    # ========================================================================
    # Physical Constraints (XDC-specific)
    # ========================================================================
    
    print("Defining physical constraints...")
    
    # I/O Standards
    # System clock uses LVDS for high-speed signaling
    cs.set_io_standard("clk_sys", "LVDS")
    cs.set_io_standard("clk_io", "LVCMOS33")
    cs.set_io_standard("clk_ddr", "DIFF_SSTL15")
    
    # Data I/O uses LVCMOS 3.3V
    cs.set_io_standard("data_in[*]", "LVCMOS33")
    cs.set_io_standard("data_out[*]", "LVCMOS33")
    cs.set_io_standard("cmd_*", "LVCMOS33")
    cs.set_io_standard("resp_*", "LVCMOS33")
    
    # DDR interface uses SSTL15
    cs.set_io_standard("ddr_*", "SSTL15")
    
    # Drive Strengths
    cs.set_drive_strength("data_out[*]", 12)
    cs.set_drive_strength("resp_*", 8)
    cs.set_drive_strength("io_data_out[*]", 12)
    
    # Pin Locations
    cs.set_location("clk_sys", "A12")
    cs.set_location("clk_io", "B14")
    cs.set_location("clk_ddr", "C16")
    cs.set_location("rst_n", "D18")
    
    return cs


def create_simple_constraints() -> ConstraintSet:
    """Create simple constraints for a basic design.
    
    This is a minimal example showing only essential constraints.
    
    Returns:
        ConstraintSet with basic constraints.
    """
    cs = ConstraintSet()
    
    # Single clock at 100MHz
    cs.clock(name="clk", frequency="100MHz")
    
    # Input delay
    cs.input_delay(
        port="data_in",
        clock="clk",
        min_delay=0.5,
        max_delay=2.0
    )
    
    # Output delay
    cs.output_delay(
        port="data_out",
        clock="clk",
        min_delay=0.5,
        max_delay=2.0
    )
    
    # False path for reset
    cs.set_false_path(from_ports="reset", to_ports="*")
    
    return cs


def main():
    """Main entry point."""
    print("=" * 70)
    print("CMT2 Timing Constraints Example")
    print("=" * 70)
    print()
    
    # Example 1: Simple constraints
    print("Example 1: Simple Design Constraints")
    print("-" * 70)
    
    simple_cs = create_simple_constraints()
    print(f"ConstraintSet: {simple_cs}")
    print()
    
    # Generate SDC
    simple_sdc = simple_cs.to_sdc()
    print("Generated SDC:")
    print(simple_sdc)
    print()
    
    # Example 2: Processor constraints
    print("=" * 70)
    print("Example 2: Processor Design Constraints")
    print("-" * 70)
    
    processor_cs = create_processor_constraints()
    print(f"ConstraintSet: {processor_cs}")
    print()
    
    # Generate SDC
    processor_sdc = processor_cs.to_sdc()
    
    # Write to file
    output_dir = Path(__file__).parent
    sdc_file = output_dir / "design.sdc"
    xdc_file = output_dir / "design.xdc"
    
    with open(sdc_file, "w") as f:
        f.write(processor_sdc)
    print(f"SDC constraints written to: {sdc_file}")
    
    # Generate XDC
    processor_xdc = processor_cs.to_xdc()
    
    with open(xdc_file, "w") as f:
        f.write(processor_xdc)
    print(f"XDC constraints written to: {xdc_file}")
    print()
    
    # Show snippets of the generated files
    print("SDC Snippet (first 30 lines):")
    print("-" * 70)
    for i, line in enumerate(processor_sdc.split("\n")[:30]):
        print(line)
    print("...")
    print()
    
    print("XDC Physical Constraints Section:")
    print("-" * 70)
    lines = processor_xdc.split("\n")
    in_physical = False
    for line in lines:
        if "I/O Standard Constraints" in line:
            in_physical = True
        if in_physical:
            print(line)
    print()
    
    print("=" * 70)
    print("Example completed successfully!")
    print("=" * 70)


if __name__ == "__main__":
    main()
