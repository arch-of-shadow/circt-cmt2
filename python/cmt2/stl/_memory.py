#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Memory Abstractions for CMT2.

This module provides hardware memory primitives including SRAM (single-port and
dual-port configurations) and ROM (read-only memory with initialization).

Example:
    import cmt2
    from cmt2 import UInt
    from cmt2.stl import SRAM, ROM

    # Single-port SRAM
    mem = cmt2.SRAM(
        data_type=UInt(32),
        depth=1024,
        ports=["read_write"]
    )

    # Read operation (context manager)
    with mem.read(addr) as data:
        result = data

    # Write operation
    mem.write(addr, data, enable=write_enable)

    # ROM with initialization
    rom = cmt2.ROM(
        data_type=UInt(32),
        depth=256,
        init=[0, 1, 2, 3, ...]
    )

    with rom.read(addr) as data:
        result = data
"""

from __future__ import annotations

import math
from abc import ABC, abstractmethod
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Iterator, List, Optional, Union

if TYPE_CHECKING:
    from cmt2.types._core import SignalType


class MemoryError(Exception):
    """Exception raised for memory-related errors in CMT2."""
    pass


@dataclass
class Memory(ABC):
    """Base class for all memory components.
    
    Memories are hardware primitives that store data and provide read/write
    access through various port configurations.
    
    Attributes:
        data_type: The SignalType of data stored in memory (e.g., UInt(32)).
        depth: Number of entries in the memory (must be positive).
        clock_domain: Optional name of the clock domain this memory belongs to.
        
    Example:
        >>> mem = SRAM(data_type=UInt(32), depth=1024)
        >>> mem.addr_width
        10
        >>> mem.data_width
        32
    """
    
    data_type: SignalType
    depth: int
    clock_domain: Optional[str] = None
    
    def __post_init__(self):
        if not isinstance(self.depth, int):
            raise MemoryError(f"Depth must be an integer, got {type(self.depth).__name__}")
        if self.depth <= 0:
            raise MemoryError(f"Depth must be positive, got {self.depth}")
        if self.depth & (self.depth - 1) != 0:
            # Non-power-of-2 depths are allowed but may be less efficient
            pass
    
    @property
    def addr_width(self) -> int:
        """Calculate the address width needed for this memory depth.
        
        Returns:
            Number of bits needed to address all entries.
        """
        return math.ceil(math.log2(self.depth))
    
    @property
    def data_width(self) -> int:
        """Get the data width in bits.
        
        Returns:
            The bit width of the data type.
        """
        return self.data_type.width
    
    @abstractmethod
    def read(self, addr: Union[int, 'Signal']) -> ReadContext:
        """Read from memory at the given address.
        
        Args:
            addr: The address to read from (integer or signal).
            
        Returns:
            A context manager that yields the data signal.
        """
        raise NotImplementedError
    
    def _validate_addr(self, addr: Union[int, 'Signal']) -> None:
        """Validate that an address is within bounds.
        
        Args:
            addr: The address to validate.
            
        Raises:
            MemoryError: If the address is out of bounds.
        """
        if isinstance(addr, int):
            if addr < 0 or addr >= self.depth:
                raise MemoryError(
                    f"Address {addr} out of bounds for memory with depth {self.depth}"
                )


class ReadContext:
    """Context manager for memory read operations.
    
    This context manager provides access to the data signal from a memory read.
    The read operation is performed when entering the context, and the data
    signal is available within the with-block.
    
    Example:
        with mem.read(addr) as data:
            # data is a Signal containing the read value
            result = data + 1
    """
    
    def __init__(
        self,
        memory: Memory,
        addr: Union[int, 'Signal'],
        data_signal: Optional['Signal'] = None
    ):
        """Initialize the read context.
        
        Args:
            memory: The memory being read from.
            addr: The address being read.
            data_signal: Optional pre-created data signal.
        """
        self._memory = memory
        self._addr = addr
        self._data_signal = data_signal
        self._entered = False
    
    def __enter__(self) -> 'Signal':
        """Enter the read context and return the data signal.
        
        Returns:
            The signal containing the read data.
        """
        self._entered = True
        self._memory._validate_addr(self._addr)
        
        # In a real implementation, this would generate the read operation
        # For now, we return a placeholder signal or the pre-created one
        if self._data_signal is None:
            # This would typically call into the builder to create a read operation
            from cmt2.types._core import Bits
            # Create a placeholder signal with the correct type
            # In real implementation, this comes from the IR builder
            self._data_signal = self._create_read_signal()
        
        return self._data_signal
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        """Exit the read context."""
        self._entered = False
        return False
    
    def _create_read_signal(self) -> 'Signal':
        """Create a signal representing the read data.
        
        In a real implementation, this would generate IR operations.
        For now, we return a placeholder.
        
        Returns:
            A signal representing the read data.
        """
        # Placeholder - in real implementation this creates IR
        # For now return a mock signal object
        return _MockSignal(self._memory.data_type, f"read_{self._addr}")


class _MockSignal:
    """Mock signal for development/testing when IR builder is not available."""
    
    def __init__(self, signal_type: 'SignalType', name: str = ""):
        self._type = signal_type
        self._name = name
    
    @property
    def type(self) -> 'SignalType':
        return self._type
    
    def __repr__(self) -> str:
        return f"Signal({self._type}, '{self._name}')"


@dataclass
class SRAM(Memory):
    """SRAM (Static Random Access Memory) with configurable ports.
    
    SRAM supports different port configurations:
    - Single-port: ["read_write"] - one port for both read and write
    - Dual-port: ["read", "write"] - separate read and write ports
    - Read-only: ["read"] - ROM-like behavior but with write capability
    
    Attributes:
        data_type: The SignalType of data stored in memory.
        depth: Number of entries in the memory.
        ports: List of port names (e.g., ["read_write"] or ["read", "write"]).
        clock_domain: Optional name of the clock domain.
        
    Example:
        # Single-port SRAM
        mem = SRAM(data_type=UInt(32), depth=1024, ports=["read_write"])
        
        # Dual-port SRAM
        mem = SRAM(data_type=UInt(32), depth=1024, ports=["read", "write"])
        
        # Read operation
        with mem.read(addr) as data:
            result = data
        
        # Write operation
        mem.write(addr, data, enable=write_enable)
    """
    
    ports: List[str] = field(default_factory=lambda: ["read_write"])
    
    # Valid port configurations
    VALID_PORT_CONFIGS = {
        ("read_write",),  # Single-port
        ("read", "write"),  # Dual-port
        ("read",),  # Read-only (ROM-like)
        ("write",),  # Write-only (specialized)
    }
    
    def __post_init__(self):
        super().__post_init__()
        
        # Validate port configuration
        port_tuple = tuple(self.ports)
        if port_tuple not in self.VALID_PORT_CONFIGS:
            raise MemoryError(
                f"Invalid port configuration: {self.ports}. "
                f"Valid configurations: {self.VALID_PORT_CONFIGS}"
            )
        
        # Check for duplicate ports
        if len(self.ports) != len(set(self.ports)):
            raise MemoryError(f"Duplicate ports in configuration: {self.ports}")
    
    @property
    def is_single_port(self) -> bool:
        """Check if this is a single-port SRAM."""
        return self.ports == ["read_write"]
    
    @property
    def is_dual_port(self) -> bool:
        """Check if this is a dual-port SRAM (separate read/write)."""
        return "read" in self.ports and "write" in self.ports
    
    @property
    def has_read_port(self) -> bool:
        """Check if SRAM has a read port."""
        return "read" in self.ports or "read_write" in self.ports
    
    @property
    def has_write_port(self) -> bool:
        """Check if SRAM has a write port."""
        return "write" in self.ports or "read_write" in self.ports
    
    def read(self, addr: Union[int, 'Signal']) -> ReadContext:
        """Read from SRAM at the given address.
        
        For single-port SRAM, this asserts the read control signals.
        For dual-port SRAM, this uses the dedicated read port.
        
        Args:
            addr: The address to read from (integer or signal).
            
        Returns:
            A ReadContext that yields the data signal.
            
        Raises:
            MemoryError: If the SRAM doesn't have a read port.
            
        Example:
            with mem.read(addr) as data:
                # Use the read data
                result = data + 1
        """
        if not self.has_read_port:
            raise MemoryError("SRAM does not have a read port")
        
        return ReadContext(self, addr)
    
    def write(
        self,
        addr: Union[int, 'Signal'],
        data: Union[int, 'Signal'],
        enable: Union[bool, 'Signal'] = True
    ) -> None:
        """Write data to SRAM at the given address.
        
        Args:
            addr: The address to write to.
            data: The data to write.
            enable: Optional write enable signal (default True).
            
        Raises:
            MemoryError: If the SRAM doesn't have a write port.
            
        Example:
            # Unconditional write
            mem.write(addr, data)
            
            # Conditional write
            mem.write(addr, data, enable=write_enable)
        """
        if not self.has_write_port:
            raise MemoryError("SRAM does not have a write port")
        
        self._validate_addr(addr)
        
        # In a real implementation, this would generate IR for the write operation
        # The enable signal controls whether the write actually happens
        self._generate_write_ir(addr, data, enable)
    
    def _generate_write_ir(
        self,
        addr: Union[int, 'Signal'],
        data: Union[int, 'Signal'],
        enable: Union[bool, 'Signal']
    ) -> None:
        """Generate IR for a write operation.
        
        In a real implementation, this would call into the IR builder to
        generate the appropriate memory write operation.
        
        Args:
            addr: The write address.
            data: The data to write.
            enable: The write enable signal.
        """
        # Placeholder - in real implementation this generates IR
        # For example:
        # builder = get_current_builder()
        # builder.create_mem_write(self, addr, data, enable)
        pass


@dataclass
class ROM(Memory):
    """ROM (Read-Only Memory) with initialization.
    
    ROM is initialized with data and can only be read from. It has no write
    ports and the initialization data is fixed at elaboration time.
    
    Attributes:
        data_type: The SignalType of data stored in ROM.
        depth: Number of entries in the ROM.
        init: List of initial values for each entry.
        clock_domain: Optional name of the clock domain.
        
    Example:
        # ROM initialized with values
        rom = ROM(
            data_type=UInt(32),
            depth=256,
            init=[i * 2 for i in range(256)]  # Even numbers
        )
        
        # Read operation
        with rom.read(addr) as data:
            result = data
    """
    
    init: List[int] = field(default_factory=list)
    
    def __post_init__(self):
        super().__post_init__()
        
        # Validate initialization data
        if len(self.init) > self.depth:
            raise MemoryError(
                f"Initialization data has {len(self.init)} entries, "
                f"but ROM depth is only {self.depth}"
            )
        
        # Validate that init values fit in data_type
        max_val = (1 << self.data_type.width) - 1
        if self.data_type.is_signed:
            max_val = (1 << (self.data_type.width - 1)) - 1
            min_val = -(1 << (self.data_type.width - 1))
        else:
            max_val = (1 << self.data_type.width) - 1
            min_val = 0
        
        for i, val in enumerate(self.init):
            if val < min_val or val > max_val:
                raise MemoryError(
                    f"Initialization value {val} at index {i} out of range "
                    f"for {self.data_type} (range: {min_val} to {max_val})"
                )
    
    def read(self, addr: Union[int, 'Signal']) -> ReadContext:
        """Read from ROM at the given address.
        
        ROM reads are combinational - the data is available immediately based
        on the address (like a lookup table).
        
        Args:
            addr: The address to read from.
            
        Returns:
            A ReadContext that yields the data signal.
            
        Example:
            with rom.read(addr) as data:
                # Use the ROM data
                result = data
        """
        return ReadContext(self, addr)
    
    def get_init_value(self, addr: int) -> int:
        """Get the initialization value at a specific address.
        
        Args:
            addr: The address to look up.
            
        Returns:
            The initialization value at that address, or 0 if not initialized.
        """
        if addr < 0 or addr >= len(self.init):
            return 0
        return self.init[addr]


# Type alias for backward compatibility with existing code
MemoryPort = str


def create_single_port_sram(
    data_type: 'SignalType',
    depth: int,
    clock_domain: Optional[str] = None
) -> SRAM:
    """Create a single-port SRAM.
    
    This is a convenience factory function for creating single-port SRAMs.
    
    Args:
        data_type: The SignalType of data stored in memory.
        depth: Number of entries in the memory.
        clock_domain: Optional name of the clock domain.
        
    Returns:
        An SRAM instance with single read-write port.
        
    Example:
        mem = create_single_port_sram(UInt(32), depth=1024)
    """
    return SRAM(
        data_type=data_type,
        depth=depth,
        ports=["read_write"],
        clock_domain=clock_domain
    )


def create_dual_port_sram(
    data_type: 'SignalType',
    depth: int,
    clock_domain: Optional[str] = None
) -> SRAM:
    """Create a dual-port SRAM with separate read and write ports.
    
    This is a convenience factory function for creating dual-port SRAMs.
    Dual-port SRAMs allow concurrent reads and writes (to different addresses).
    
    Args:
        data_type: The SignalType of data stored in memory.
        depth: Number of entries in the memory.
        clock_domain: Optional name of the clock domain.
        
    Returns:
        An SRAM instance with separate read and write ports.
        
    Example:
        mem = create_dual_port_sram(UInt(32), depth=1024)
        
        # Concurrent read and write
        with mem.read(addr_a) as data_a:
            result = data_a
        mem.write(addr_b, data_b, enable=we_b)
    """
    return SRAM(
        data_type=data_type,
        depth=depth,
        ports=["read", "write"],
        clock_domain=clock_domain
    )


def create_rom(
    data_type: 'SignalType',
    init: List[int],
    clock_domain: Optional[str] = None
) -> ROM:
    """Create a ROM with the given initialization data.
    
    This is a convenience factory function for creating ROMs.
    The depth is inferred from the initialization data.
    
    Args:
        data_type: The SignalType of data stored in ROM.
        init: List of initialization values.
        clock_domain: Optional name of the clock domain.
        
    Returns:
        A ROM instance initialized with the given data.
        
    Example:
        rom = create_rom(UInt(32), init=[0, 1, 2, 3, 4])
    """
    return ROM(
        data_type=data_type,
        depth=len(init),
        init=init,
        clock_domain=clock_domain
    )
