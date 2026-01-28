#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Unit tests for CMT2 timing constraints module.

Run with: python -m pytest python/cmt2/test_constraints.py -v
"""

import pytest
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
    _parse_frequency,
)
from .backends._sdc_generator import SDCGenerator, XDCGenerator


# =============================================================================
# ClockWaveform Tests
# =============================================================================

class TestClockWaveform:
    def test_default_waveform(self):
        wf = ClockWaveform()
        assert wf.duty_cycle == 0.5
        assert wf.phase_deg == 0.0
    
    def test_custom_waveform(self):
        wf = ClockWaveform(duty_cycle=0.4, phase_deg=90.0)
        assert wf.duty_cycle == 0.4
        assert wf.phase_deg == 90.0
    
    def test_invalid_duty_cycle(self):
        with pytest.raises(ValueError):
            ClockWaveform(duty_cycle=1.5)
        with pytest.raises(ValueError):
            ClockWaveform(duty_cycle=0.0)
        with pytest.raises(ValueError):
            ClockWaveform(duty_cycle=-0.1)


# =============================================================================
# ClockConstraint Tests
# =============================================================================

class TestClockConstraint:
    def test_create_from_period(self):
        clk = ClockConstraint("clk", period_ns=4.0)
        assert clk.name == "clk"
        assert clk.period_ns == 4.0
        assert abs(clk.frequency_mhz - 250.0) < 0.001
    
    def test_create_from_frequency_string(self):
        clk = ClockConstraint.from_frequency("clk", "250MHz")
        assert clk.name == "clk"
        assert abs(clk.period_ns - 4.0) < 0.001
    
    def test_create_from_frequency_float(self):
        clk = ClockConstraint.from_frequency("clk", 250.0)
        assert clk.name == "clk"
        assert abs(clk.period_ns - 4.0) < 0.001
    
    def test_custom_waveform(self):
        wf = ClockWaveform(duty_cycle=0.4)
        clk = ClockConstraint("clk", period_ns=4.0, waveform=wf)
        assert clk.waveform.duty_cycle == 0.4
    
    def test_jitter(self):
        clk = ClockConstraint("clk", period_ns=4.0, jitter_ns=0.1)
        assert clk.jitter_ns == 0.1
    
    def test_source_override(self):
        clk = ClockConstraint("clk", period_ns=4.0, source="clk_pin")
        assert clk.get_source() == "clk_pin"
    
    def test_invalid_period(self):
        with pytest.raises(ValueError):
            ClockConstraint("clk", period_ns=0.0)
        with pytest.raises(ValueError):
            ClockConstraint("clk", period_ns=-1.0)
    
    def test_invalid_jitter(self):
        with pytest.raises(ValueError):
            ClockConstraint("clk", period_ns=4.0, jitter_ns=-0.1)


# =============================================================================
# Frequency Parsing Tests
# =============================================================================

class TestParseFrequency:
    def test_mhz_string(self):
        assert _parse_frequency("250MHz") == 250.0
        assert _parse_frequency("250mhz") == 250.0
        assert _parse_frequency("250 MHz") == 250.0
    
    def test_ghz_string(self):
        assert _parse_frequency("1GHz") == 1000.0
        assert _parse_frequency("1.5GHz") == 1500.0
    
    def test_khz_string(self):
        assert _parse_frequency("1000kHz") == 1.0
    
    def test_hz_string(self):
        assert _parse_frequency("250000000Hz") == 250.0
    
    def test_float_input(self):
        assert _parse_frequency(250.0) == 250.0
        assert _parse_frequency(100) == 100.0
    
    def test_invalid_format(self):
        with pytest.raises(ValueError):
            _parse_frequency("invalid")
        with pytest.raises(ValueError):
            _parse_frequency("abcMHz")


# =============================================================================
# I/O Constraint Tests
# =============================================================================

class TestInputDelayConstraint:
    def test_valid_delay(self):
        delay = InputDelayConstraint(
            port="data_in",
            clock="clk",
            min_delay=0.5,
            max_delay=2.0
        )
        assert delay.port == "data_in"
        assert delay.clock == "clk"
        assert delay.min_delay == 0.5
        assert delay.max_delay == 2.0
    
    def test_invalid_delays(self):
        with pytest.raises(ValueError):
            InputDelayConstraint("data", "clk", -1.0, 2.0)
        with pytest.raises(ValueError):
            InputDelayConstraint("data", "clk", 0.5, -1.0)
        with pytest.raises(ValueError):
            InputDelayConstraint("data", "clk", 2.0, 0.5)  # min > max


class TestOutputDelayConstraint:
    def test_valid_delay(self):
        delay = OutputDelayConstraint(
            port="data_out",
            clock="clk",
            min_delay=0.5,
            max_delay=2.0
        )
        assert delay.port == "data_out"
        assert delay.min_delay == 0.5
        assert delay.max_delay == 2.0


# =============================================================================
# Path Constraint Tests
# =============================================================================

class TestFalsePathConstraint:
    def test_from_to(self):
        fp = FalsePathConstraint(from_ports="reset", to_ports="*")
        assert fp.from_ports == "reset"
        assert fp.to_ports == "*"
    
    def test_through_only(self):
        fp = FalsePathConstraint(through="test_mode")
        assert fp.through == "test_mode"
    
    def test_empty_constraint(self):
        with pytest.raises(ValueError):
            FalsePathConstraint()


class TestMulticyclePathConstraint:
    def test_basic_setup(self):
        mcp = MulticyclePathConstraint(setup=2)
        assert mcp.setup == 2
        assert mcp.hold is None
    
    def test_setup_and_hold(self):
        mcp = MulticyclePathConstraint(setup=2, hold=1)
        assert mcp.setup == 2
        assert mcp.hold == 1
    
    def test_with_ports(self):
        mcp = MulticyclePathConstraint(
            setup=2,
            from_ports="src/*",
            to_ports="dst/*"
        )
        assert mcp.from_ports == "src/*"
        assert mcp.to_ports == "dst/*"
    
    def test_invalid_setup(self):
        with pytest.raises(ValueError):
            MulticyclePathConstraint(setup=0)
        with pytest.raises(ValueError):
            MulticyclePathConstraint(setup=-1)
    
    def test_invalid_hold(self):
        with pytest.raises(ValueError):
            MulticyclePathConstraint(setup=2, hold=-1)


# =============================================================================
# Physical Constraint Tests
# =============================================================================

class TestIOStandardConstraint:
    def test_basic(self):
        io = IOStandardConstraint("data_in", "LVCMOS33")
        assert io.port == "data_in"
        assert io.standard == "LVCMOS33"


class TestDriveStrengthConstraint:
    def test_valid(self):
        drive = DriveStrengthConstraint("data_out", 12)
        assert drive.port == "data_out"
        assert drive.strength == 12
    
    def test_invalid(self):
        with pytest.raises(ValueError):
            DriveStrengthConstraint("data_out", 0)
        with pytest.raises(ValueError):
            DriveStrengthConstraint("data_out", -1)


class TestLocationConstraint:
    def test_basic(self):
        loc = LocationConstraint("clk", "A12")
        assert loc.port == "clk"
        assert loc.pin == "A12"


# =============================================================================
# ConstraintSet Tests
# =============================================================================

class TestConstraintSet:
    def test_empty(self):
        cs = ConstraintSet()
        assert cs.is_empty()
        assert len(cs.clocks) == 0
    
    def test_add_clock(self):
        cs = ConstraintSet()
        clk = ClockConstraint.from_frequency("clk", "250MHz")
        cs.add_clock(clk)
        assert len(cs.clocks) == 1
        assert not cs.is_empty()
    
    def test_clock_helper(self):
        cs = ConstraintSet()
        cs.clock(name="clk", frequency="250MHz")
        assert len(cs.clocks) == 1
        assert cs.clocks[0].name == "clk"
    
    def test_clock_helper_with_period(self):
        cs = ConstraintSet()
        cs.clock(name="clk", period_ns=4.0)
        assert len(cs.clocks) == 1
        assert abs(cs.clocks[0].period_ns - 4.0) < 0.001
    
    def test_input_delay_helper(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.input_delay("data_in", "clk", 0.5, 2.0)
        assert len(cs.input_delays) == 1
        assert cs.input_delays[0].port == "data_in"
    
    def test_output_delay_helper(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.output_delay("data_out", "clk", 0.5, 2.0)
        assert len(cs.output_delays) == 1
    
    def test_false_path_helper(self):
        cs = ConstraintSet()
        cs.set_false_path(from_ports="reset", to_ports="*")
        assert len(cs.false_paths) == 1
    
    def test_multicycle_path_helper(self):
        cs = ConstraintSet()
        cs.set_multicycle_path(setup=2, hold=1, from_ports="src", to_ports="dst")
        assert len(cs.multicycle_paths) == 1
        assert cs.multicycle_paths[0].setup == 2
    
    def test_io_standard_helper(self):
        cs = ConstraintSet()
        cs.set_io_standard("data_in", "LVCMOS33")
        assert len(cs.io_standards) == 1
    
    def test_drive_strength_helper(self):
        cs = ConstraintSet()
        cs.set_drive_strength("data_out", 12)
        assert len(cs.drive_strengths) == 1
    
    def test_location_helper(self):
        cs = ConstraintSet()
        cs.set_location("clk", "A12")
        assert len(cs.locations) == 1
    
    def test_repr(self):
        cs = ConstraintSet()
        assert "empty" in repr(cs)
        
        cs.clock("clk", frequency="250MHz")
        assert "1 clocks" in repr(cs)
    
    def test_to_sdc(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.input_delay("data_in", "clk", 0.5, 2.0)
        
        sdc = cs.to_sdc()
        assert "create_clock" in sdc
        assert "set_input_delay" in sdc
    
    def test_to_xdc(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.set_io_standard("data_in", "LVCMOS33")
        
        xdc = cs.to_xdc()
        assert "create_clock" in xdc
        assert "set_property IOSTANDARD" in xdc


# =============================================================================
# SDC Generator Tests
# =============================================================================

class TestSDCGenerator:
    def test_generate_clock(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "create_clock -name clk -period 4.000 [get_ports clk]" in sdc
    
    def test_generate_clock_with_waveform(self):
        cs = ConstraintSet()
        cs.clock("clk", period_ns=10.0, duty_cycle=0.4)
        
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "-waveform" in sdc
    
    def test_generate_input_delay(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.input_delay("data_in", "clk", 0.5, 2.0)
        
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "set_input_delay -min" in sdc
        assert "set_input_delay -max" in sdc
        assert "0.500" in sdc
        assert "2.000" in sdc
    
    def test_generate_output_delay(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.output_delay("data_out", "clk", 0.5, 2.0)
        
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "set_output_delay -min" in sdc
        assert "set_output_delay -max" in sdc
    
    def test_generate_false_path(self):
        cs = ConstraintSet()
        cs.set_false_path(from_ports="reset", to_ports="*")
        
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "set_false_path -from [get_ports reset] -to [get_ports *]" in sdc
    
    def test_generate_multicycle_path(self):
        cs = ConstraintSet()
        cs.set_multicycle_path(setup=2, hold=1, from_ports="src", to_ports="dst")
        
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "set_multicycle_path 2 -end" in sdc
        assert "set_multicycle_path 1 -hold" in sdc
    
    def test_escape_special_chars(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.input_delay("data[0]", "clk", 0.5, 2.0)
        
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "data\\[0\\]" in sdc
    
    def test_empty_constraints(self):
        cs = ConstraintSet()
        gen = SDCGenerator(include_timestamp=False)
        sdc = gen.generate(cs)
        
        assert "# Timing Constraints" in sdc


# =============================================================================
# XDC Generator Tests
# =============================================================================

class TestXDCGenerator:
    def test_generate_io_standard(self):
        cs = ConstraintSet()
        cs.set_io_standard("data_in", "LVCMOS33")
        
        gen = XDCGenerator(include_timestamp=False)
        xdc = gen.generate(cs)
        
        assert "set_property IOSTANDARD LVCMOS33 [get_ports data_in]" in xdc
    
    def test_generate_drive_strength(self):
        cs = ConstraintSet()
        cs.set_drive_strength("data_out", 12)
        
        gen = XDCGenerator(include_timestamp=False)
        xdc = gen.generate(cs)
        
        assert "set_property DRIVE 12 [get_ports data_out]" in xdc
    
    def test_generate_location(self):
        cs = ConstraintSet()
        cs.set_location("clk", "A12")
        
        gen = XDCGenerator(include_timestamp=False)
        xdc = gen.generate(cs)
        
        assert "set_property PACKAGE_PIN A12 [get_ports clk]" in xdc
    
    def test_includes_sdc_content(self):
        cs = ConstraintSet()
        cs.clock("clk", frequency="250MHz")
        cs.set_io_standard("data_in", "LVCMOS33")
        
        gen = XDCGenerator(include_timestamp=False)
        xdc = gen.generate(cs)
        
        assert "create_clock" in xdc  # From SDC
        assert "set_property IOSTANDARD" in xdc  # XDC-specific


# =============================================================================
# Integration Tests
# =============================================================================

class TestConstraintIntegration:
    def test_full_design_constraints(self):
        """Test a realistic set of constraints for a complete design."""
        cs = ConstraintSet()
        
        # Clocks
        cs.clock("clk_sys", frequency="250MHz")
        cs.clock("clk_io", frequency="100MHz")
        
        # I/O Delays
        cs.input_delay("data_in[*]", "clk_sys", 0.5, 2.0)
        cs.output_delay("data_out[*]", "clk_sys", 0.5, 2.0)
        
        # False paths
        cs.set_false_path(from_ports="rst_n", to_ports="*")
        cs.set_false_path(from_ports="test_en", to_ports="*")
        
        # Multicycle paths
        cs.set_multicycle_path(
            setup=2, hold=1,
            from_ports="multiplier/*",
            to_ports="accumulator/*"
        )
        
        # Physical constraints
        cs.set_io_standard("clk_sys", "LVDS")
        cs.set_io_standard("data_in[*]", "LVCMOS33")
        cs.set_io_standard("data_out[*]", "LVCMOS33")
        cs.set_drive_strength("data_out[*]", 12)
        cs.set_location("clk_sys", "A12")
        
        # Generate files
        sdc = cs.to_sdc()
        xdc = cs.to_xdc()
        
        # Verify SDC
        assert "create_clock -name clk_sys -period 4.000" in sdc
        assert "create_clock -name clk_io -period 10.000" in sdc
        assert "set_false_path" in sdc
        assert "set_multicycle_path" in sdc
        
        # Verify XDC includes physical constraints
        assert "set_property IOSTANDARD LVDS" in xdc
        assert "set_property PACKAGE_PIN A12" in xdc


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
