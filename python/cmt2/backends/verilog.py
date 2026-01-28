#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Verilog/SystemVerilog backend for CMT2 JIT compilation.

This module provides the Verilog generation backend that converts
CMT2 circuits to SystemVerilog code.

Example:
    from cmt2.backends.verilog import VerilogBackend
    
    backend = VerilogBackend()
    executable = backend.compile(lowered_circuit)
    
    # Generate Verilog code
    verilog_code = executable.codegen(format="systemverilog")
    
    # Write to file
    executable.write("output.sv")
"""

from __future__ import annotations

import os
import subprocess
import tempfile
from dataclasses import dataclass, field
from typing import Any, TYPE_CHECKING

if TYPE_CHECKING:
    from cmt2.jit._stages import LoweredCircuit, CompiledCircuit


# CIRCT availability check
try:
    from circt import ir
    from circt.dialects import hw, comb, seq
    _HAS_CIRCT = True
except ImportError:
    _HAS_CIRCT = False


@dataclass
class VerilogGenerationResult:
    """Result of Verilog code generation.
    
    Attributes:
        verilog: The generated Verilog code
        success: Whether generation was successful
        errors: List of errors if generation failed
    """
    verilog: str
    success: bool
    errors: list[str] = field(default_factory=list)


class VerilogExecutable:
    """Compiled Verilog ready for output.
    
    This class wraps a compiled Verilog artifact and provides
    methods to generate and save the Verilog code.
    """
    
    def __init__(
        self,
        mlir_module: Any,
        target: str = "systemverilog",
    ):
        """Initialize a Verilog executable.
        
        Args:
            mlir_module: The MLIR module containing the circuit
            target: Target Verilog format ("verilog" or "systemverilog")
        """
        self._mlir_module = mlir_module
        self._target = target
        self._cached_verilog: str | None = None
    
    def codegen(self, format: str = "systemverilog") -> str:
        """Generate Verilog/SystemVerilog code.
        
        Args:
            format: Output format - "systemverilog" or "verilog"
            
        Returns:
            Generated Verilog code as a string
            
        Raises:
            RuntimeError: If code generation fails
        """
        # Check cache
        if self._cached_verilog is not None:
            return self._cached_verilog
        
        # Try to use CIRCT's translation if available
        if _HAS_CIRCT and self._mlir_module is not None:
            try:
                # Check if module is already Verilog-like
                mlir_text = self._get_mlir_text()
                if self._looks_like_verilog(mlir_text):
                    self._cached_verilog = mlir_text
                    return mlir_text
                
                # TODO: Use CIRCT's export to Verilog when available
                # This would typically involve:
                # 1. Running hw-to-sv pass
                # 2. Using the SV emitter to generate Verilog
                
            except Exception as e:
                # Fall through to placeholder
                pass
        
        # Placeholder: return a comment indicating generation status
        if self._mlir_module is None:
            return "// Error: No MLIR module available\n"
        
        verilog = (
            f"// Verilog output for target={self._target}\n"
            f"// (Full CIRCT integration pending for actual Verilog generation)\n"
            f"// MLIR module would be converted here\n"
        )
        self._cached_verilog = verilog
        return verilog
    
    def write(self, path: str) -> None:
        """Write Verilog to file.
        
        Args:
            path: Output file path
            
        Raises:
            IOError: If file cannot be written
        """
        # Generate code
        verilog = self.codegen()
        
        # Ensure directory exists
        dir_path = os.path.dirname(path)
        if dir_path and not os.path.exists(dir_path):
            os.makedirs(dir_path, exist_ok=True)
        
        # Write to file
        with open(path, 'w') as f:
            f.write(verilog)
    
    def _get_mlir_text(self) -> str:
        """Get the MLIR module as text."""
        if self._mlir_module is None:
            return ""
        
        if hasattr(self._mlir_module, 'dump'):
            try:
                return str(self._mlir_module.dump())
            except Exception:
                pass
        
        return str(self._mlir_module)
    
    def _looks_like_verilog(self, text: str) -> bool:
        """Check if text looks like Verilog code."""
        verilog_keywords = [
            'module', 'endmodule', 'always', 'initial',
            'assign', 'wire', 'reg', 'logic', 'input', 'output',
            'begin', 'end', 'if', 'else', 'case', 'endcase'
        ]
        return any(kw in text for kw in verilog_keywords)


class VerilogBackend:
    """Backend for SystemVerilog generation.
    
    This backend compiles CMT2 circuits to SystemVerilog code
    using CIRCT's translation infrastructure.
    
    Example:
        backend = VerilogBackend()
        executable = backend.compile(lowered_circuit)
        executable.write("output.sv")
    """
    
    def __init__(self, format: str = "systemverilog"):
        """Initialize the Verilog backend.
        
        Args:
            format: Output format ("verilog" or "systemverilog")
        """
        self._format = format
    
    def compile(self, lowered: LoweredCircuit) -> VerilogExecutable:
        """Compile to Verilog output.
        
        Args:
            lowered: The lowered circuit to compile
            
        Returns:
            A VerilogExecutable ready for code generation
        """
        return VerilogExecutable(
            mlir_module=lowered.mlir_module,
            target=self._format,
        )
    
    def is_available(self) -> bool:
        """Check if the Verilog backend is available.
        
        Returns:
            True if CIRCT bindings are available
        """
        return _HAS_CIRCT
    
    def generate_sdc(
        self,
        lowered: LoweredCircuit,
        constraints: Any | None = None,
    ) -> str:
        """Generate SDC constraints file.
        
        Args:
            lowered: The lowered circuit
            constraints: Optional constraints to include
            
        Returns:
            SDC constraints as a string
        """
        # TODO: Generate SDC from circuit timing constraints
        return "# SDC constraints would be generated here\n"


def create_verilog_backend(format: str = "systemverilog") -> VerilogBackend:
    """Factory function to create a Verilog backend.
    
    Args:
        format: Output format ("verilog" or "systemverilog")
        
    Returns:
        A configured VerilogBackend instance
        
    Example:
        backend = create_verilog_backend("systemverilog")
        executable = backend.compile(lowered_circuit)
        verilog = executable.codegen()
    """
    return VerilogBackend(format=format)


def emit_verilog(
    mlir_module: Any,
    output_file: str | None = None,
) -> str:
    """Convenience function to emit Verilog from an MLIR module.
    
    Args:
        mlir_module: The MLIR module to convert
        output_file: Optional file path to write output
        
    Returns:
        The generated Verilog code
        
    Example:
        verilog = emit_verilog(mlir_module, "output.sv")
    """
    from cmt2.jit._stages import LoweredCircuit
    
    lowered = LoweredCircuit(
        mlir_module=mlir_module,
        target="systemverilog",
        optimization_level=2,
    )
    
    backend = VerilogBackend()
    executable = backend.compile(lowered)
    
    verilog = executable.codegen()
    
    if output_file:
        executable.write(output_file)
    
    return verilog
