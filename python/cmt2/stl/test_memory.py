#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Tests for memory abstractions in CMT2 STL."""

import unittest
from cmt2.types import UInt, SInt, Bits
from cmt2.stl import (
    Memory,
    SRAM,
    ROM,
    MemoryError,
    create_single_port_sram,
    create_dual_port_sram,
    create_rom,
)


class TestSRAM(unittest.TestCase):
    """Test cases for SRAM memory."""
    
    def test_single_port_sram_creation(self):
        """Test creating a single-port SRAM."""
        mem = SRAM(data_type=UInt(32), depth=1024, ports=["read_write"])
        
        self.assertEqual(mem.data_type, UInt(32))
        self.assertEqual(mem.depth, 1024)
        self.assertEqual(mem.ports, ["read_write"])
        self.assertEqual(mem.addr_width, 10)  # log2(1024)
        self.assertEqual(mem.data_width, 32)
        self.assertTrue(mem.is_single_port)
        self.assertFalse(mem.is_dual_port)
        self.assertTrue(mem.has_read_port)
        self.assertTrue(mem.has_write_port)
    
    def test_dual_port_sram_creation(self):
        """Test creating a dual-port SRAM."""
        mem = SRAM(data_type=UInt(32), depth=1024, ports=["read", "write"])
        
        self.assertEqual(mem.ports, ["read", "write"])
        self.assertFalse(mem.is_single_port)
        self.assertTrue(mem.is_dual_port)
        self.assertTrue(mem.has_read_port)
        self.assertTrue(mem.has_write_port)
    
    def test_sram_with_clock_domain(self):
        """Test SRAM with clock domain."""
        mem = SRAM(
            data_type=UInt(32),
            depth=1024,
            ports=["read_write"],
            clock_domain="clk_core"
        )
        
        self.assertEqual(mem.clock_domain, "clk_core")
    
    def test_sram_invalid_depth(self):
        """Test SRAM with invalid depth."""
        with self.assertRaises(MemoryError):
            SRAM(data_type=UInt(32), depth=0)
        
        with self.assertRaises(MemoryError):
            SRAM(data_type=UInt(32), depth=-1)
    
    def test_sram_invalid_ports(self):
        """Test SRAM with invalid port configuration."""
        with self.assertRaises(MemoryError):
            SRAM(data_type=UInt(32), depth=1024, ports=["invalid"])
        
        with self.assertRaises(MemoryError):
            SRAM(data_type=UInt(32), depth=1024, ports=["read", "read"])  # Duplicate
    
    def test_sram_factory_functions(self):
        """Test SRAM factory functions."""
        sp_mem = create_single_port_sram(UInt(32), depth=1024)
        self.assertTrue(sp_mem.is_single_port)
        
        dp_mem = create_dual_port_sram(UInt(32), depth=1024)
        self.assertTrue(dp_mem.is_dual_port)
    
    def test_sram_addr_width_calculation(self):
        """Test address width calculation."""
        # Power of 2
        mem1 = SRAM(data_type=UInt(32), depth=1024)
        self.assertEqual(mem1.addr_width, 10)
        
        # Non-power of 2
        mem2 = SRAM(data_type=UInt(32), depth=1000)
        self.assertEqual(mem2.addr_width, 10)  # ceil(log2(1000)) = 10
        
        # Depth of 1
        mem3 = SRAM(data_type=UInt(32), depth=1)
        self.assertEqual(mem3.addr_width, 0)  # log2(1) = 0


class TestROM(unittest.TestCase):
    """Test cases for ROM memory."""
    
    def test_rom_creation(self):
        """Test creating a ROM."""
        init_data = [i * 2 for i in range(256)]
        rom = ROM(data_type=UInt(32), depth=256, init=init_data)
        
        self.assertEqual(rom.data_type, UInt(32))
        self.assertEqual(rom.depth, 256)
        self.assertEqual(rom.init, init_data)
    
    def test_rom_get_init_value(self):
        """Test getting initialization values."""
        init_data = [10, 20, 30, 40]
        rom = ROM(data_type=UInt(32), depth=4, init=init_data)
        
        self.assertEqual(rom.get_init_value(0), 10)
        self.assertEqual(rom.get_init_value(1), 20)
        self.assertEqual(rom.get_init_value(2), 30)
        self.assertEqual(rom.get_init_value(3), 40)
        self.assertEqual(rom.get_init_value(100), 0)  # Out of bounds
    
    def test_rom_init_validation(self):
        """Test ROM initialization validation."""
        # Too many init values
        with self.assertRaises(MemoryError):
            ROM(data_type=UInt(32), depth=4, init=[1, 2, 3, 4, 5])
        
        # Value out of range for data type
        with self.assertRaises(MemoryError):
            ROM(data_type=UInt(8), depth=4, init=[256])  # 256 > 255
    
    def test_rom_signed_validation(self):
        """Test ROM with signed data type."""
        # Valid signed values
        rom = ROM(data_type=SInt(8), depth=4, init=[-128, 0, 127, -1])
        self.assertEqual(rom.get_init_value(0), -128)
        self.assertEqual(rom.get_init_value(2), 127)
        
        # Invalid signed value
        with self.assertRaises(MemoryError):
            ROM(data_type=SInt(8), depth=4, init=[128])  # 128 > 127
        
        with self.assertRaises(MemoryError):
            ROM(data_type=SInt(8), depth=4, init=[-129])  # -129 < -128
    
    def test_rom_factory_function(self):
        """Test ROM factory function."""
        init_data = [i for i in range(100)]
        rom = create_rom(UInt(32), init=init_data)
        
        self.assertEqual(rom.depth, 100)
        self.assertEqual(rom.init, init_data)


class TestMemoryReadContext(unittest.TestCase):
    """Test cases for memory read operations."""
    
    def test_sram_read_context(self):
        """Test SRAM read context manager."""
        mem = SRAM(data_type=UInt(32), depth=1024, ports=["read_write"])
        
        # Read with integer address
        with mem.read(0) as data:
            self.assertIsNotNone(data)
        
        # Read at specific address
        with mem.read(100) as data:
            pass
    
    def test_rom_read_context(self):
        """Test ROM read context manager."""
        rom = ROM(data_type=UInt(32), depth=256, init=[i for i in range(256)])
        
        with rom.read(0) as data:
            self.assertIsNotNone(data)
    
    def test_read_addr_validation(self):
        """Test address validation during read."""
        mem = SRAM(data_type=UInt(32), depth=1024, ports=["read_write"])
        
        # Valid addresses
        with mem.read(0) as data:
            pass
        with mem.read(1023) as data:
            pass
        
        # Invalid addresses
        with self.assertRaises(MemoryError):
            with mem.read(1024) as data:
                pass
        
        with self.assertRaises(MemoryError):
            with mem.read(-1) as data:
                pass


class TestMemoryInheritance(unittest.TestCase):
    """Test memory class inheritance."""
    
    def test_memory_is_abstract(self):
        """Test that Memory is an abstract base class."""
        with self.assertRaises(TypeError):
            Memory(data_type=UInt(32), depth=1024)
    
    def test_sram_is_memory(self):
        """Test that SRAM inherits from Memory."""
        mem = SRAM(data_type=UInt(32), depth=1024)
        self.assertIsInstance(mem, Memory)
    
    def test_rom_is_memory(self):
        """Test that ROM inherits from Memory."""
        rom = ROM(data_type=UInt(32), depth=256, init=[])
        self.assertIsInstance(rom, Memory)


class TestMemoryWithDifferentTypes(unittest.TestCase):
    """Test memory with different data types."""
    
    def test_sram_with_bits(self):
        """Test SRAM with Bits type."""
        mem = SRAM(data_type=Bits(64), depth=1024)
        self.assertEqual(mem.data_width, 64)
    
    def test_sram_with_sint(self):
        """Test SRAM with SInt type."""
        mem = SRAM(data_type=SInt(16), depth=1024)
        self.assertEqual(mem.data_width, 16)
    
    def test_rom_with_different_widths(self):
        """Test ROM with different data widths."""
        rom8 = ROM(data_type=UInt(8), depth=16, init=[i for i in range(16)])
        self.assertEqual(rom8.data_width, 8)
        
        rom64 = ROM(data_type=UInt(64), depth=8, init=[i for i in range(8)])
        self.assertEqual(rom64.data_width, 64)


if __name__ == "__main__":
    unittest.main()
