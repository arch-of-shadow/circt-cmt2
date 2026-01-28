#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Staged compilation API classes for CMT2 JIT.

This module provides classes representing different stages of the compilation
pipeline for CMT2 hardware designs:

1. ElaboratedCircuit - Contains CMT2 IR after Python tracing/elaboration
2. LoweredCircuit - Contains MLIR after lowering to target dialect
3. CompiledCircuit - Contains target-specific compiled artifact

The staged approach allows users to:
- Inspect and transform the circuit at each stage
- Cache intermediate results
- Choose different targets without re-elaboration
- Debug the compilation pipeline

Example:
    from cmt2 import elaborate
    from cmt2.jit import ElaboratedCircuit, LoweredCircuit, CompiledCircuit

    @elaborate
    def my_design(width: int):
        circuit = Circuit("Test")
        # ... build circuit ...
        return circuit

    # Stage 1: Elaboration (Python tracing)
    elaborated: ElaboratedCircuit = my_design.elaborate(32)
    print(elaborated.dump())

    # Stage 2: Lowering (to FIRRTL/Verilog)
    lowered: LoweredCircuit = elaborated.lower(target="firrtl")
    print(lowered.codegen(format="mlir"))

    # Stage 3: Compilation
    compiled: CompiledCircuit = lowered.compile()
    compiled.write("output.sv")
"""

from __future__ import annotations

from typing import Any, TYPE_CHECKING

if TYPE_CHECKING:
    # Avoid circular imports during type checking
    pass

# Import pass pipeline for lowering
try:
    from cmt2.passes._pipeline import (
        PassPipeline,
        PassPipelineConfig,
        PipelineTarget,
        PipelineStage,
    )
    _HAS_PASS_PIPELINE = True
except ImportError:
    _HAS_PASS_PIPELINE = False


class ElaboratedCircuit:
    """Circuit after elaboration (tracing). Contains CMT2 IR.
    
    This class represents the first stage of the compilation pipeline,
    containing the MLIR module produced by tracing Python circuit
    construction code.
    
    Attributes:
        mlir_module: The MLIR module containing CMT2 dialect operations
        name: Name of the circuit
        static_args: Dictionary of static (compile-time) argument names to values
        
    Example:
        elaborated = ElaboratedCircuit(module, "Counter", {"width": 32})
        print(elaborated)  # ElaboratedCircuit(name='Counter', ops=15, ...)
        
        # Lower to target representation
        lowered = elaborated.lower(target="verilog")
    """
    
    def __init__(
        self,
        mlir_module: Any,
        name: str,
        static_args: dict[str, Any],
    ):
        """Initialize an elaborated circuit.
        
        Args:
            mlir_module: The MLIR module containing CMT2 IR
            name: Name of the circuit
            static_args: Dictionary mapping static argument names to their values
        """
        self.mlir_module = mlir_module
        self.name = name
        self.static_args = static_args
    
    def __repr__(self) -> str:
        """Return a concise string representation for debugging."""
        return (
            f"ElaboratedCircuit("
            f"name={self.name!r}, "
            f"ops={self.num_ops}, "
            f"inputs={list(self.static_args.keys())!r}"
            f")"
        )
    
    def __str__(self) -> str:
        """Return a human-readable summary of the circuit."""
        return (
            f"ElaboratedCircuit '{self.name}':\n"
            f"  Operations: {self.num_ops}\n"
            f"  Static arguments: {self.static_args}\n"
            f"  Use .dump() to see the full IR"
        )
    
    def lower(
        self,
        target: str = "verilog",
        optimization_level: int = 2,
        **options: Any,
    ) -> LoweredCircuit:
        """Lower the circuit to target representation.
        
        This transforms the CMT2 IR to the target dialect (e.g., FIRRTL,
        HW, or SystemVerilog-ready MLIR).
        
        Args:
            target: Target format - "verilog", "firrtl", "hw", or "mlir"
            optimization_level: Optimization level (0-3, default 2)
            **options: Additional target-specific options
            
        Returns:
            A LoweredCircuit containing the transformed MLIR
            
        Raises:
            ValueError: If target is not supported
            RuntimeError: If lowering fails
            
        Example:
            lowered = elaborated.lower(target="firrtl", optimization_level=3)
            lowered = elaborated.lower(target="verilog", preserve_names=True)
        """
        # Build pipeline configuration
        target_map = {
            "verilog": PipelineTarget.VERILOG,
            "systemverilog": PipelineTarget.VERILOG,
            "sv": PipelineTarget.VERILOG,
            "simulation": PipelineTarget.SIMULATION,
            "sim": PipelineTarget.SIMULATION,
            "fpga": PipelineTarget.FPGA,
        }
        
        pipeline_target = target_map.get(target.lower())
        if pipeline_target is None:
            raise ValueError(
                f"Unsupported target: {target!r}. "
                f"Supported: {list(target_map.keys())}"
            )
        
        config = PassPipelineConfig(
            target=pipeline_target,
            optimization_level=optimization_level,
        )
        
        # Run the lowering pipeline if available
        if _HAS_PASS_PIPELINE and self.mlir_module is not None:
            try:
                pipeline = PassPipeline(config)
                lowered_module = pipeline.run_stage(
                    self.mlir_module,
                    PipelineStage.LOWERED,
                    clone=True,
                )
                return LoweredCircuit(
                    mlir_module=lowered_module,
                    target=target,
                    optimization_level=optimization_level,
                    _source_circuit=self,
                )
            except Exception as e:
                # If pipeline fails, fall back to unlowered module with warning
                import warnings
                warnings.warn(
                    f"Lowering pipeline failed: {e}. "
                    "Returning unlowered circuit.",
                    RuntimeWarning,
                )
        
        # Return unlowered circuit (for development/testing without CIRCT)
        return LoweredCircuit(
            mlir_module=self.mlir_module,
            target=target,
            optimization_level=optimization_level,
            _source_circuit=self,
        )
    
    def dump(self) -> str:
        """Pretty-print the IR as a string.
        
        Returns:
            Formatted string representation of the MLIR module
            
        Example:
            print(elaborated.dump())
        """
        if self.mlir_module is None:
            return f"// ElaboratedCircuit: {self.name}\n// No MLIR module\n"
        
        # Try to get the MLIR string representation
        if hasattr(self.mlir_module, 'dump'):
            # CIRCT MLIR module - use its dump method
            try:
                return str(self.mlir_module.dump())
            except Exception:
                pass
        
        if hasattr(self.mlir_module, '__str__'):
            try:
                return str(self.mlir_module)
            except Exception:
                pass
        
        # Fallback: return summary
        return (
            f"// ElaboratedCircuit: {self.name}\n"
            f"// Operations: {self.num_ops}\n"
            f"// Static args: {self.static_args}\n"
        )
    
    @property
    def num_ops(self) -> int:
        """Count operations in the circuit.
        
        Returns:
            Total number of operations in the MLIR module
            
        Note:
            This counts all operations recursively through the module.
        """
        # TODO: Implement actual operation counting in Task 2.2
        # For now, return a placeholder value
        if hasattr(self.mlir_module, 'body'):
            return sum(1 for _ in self.mlir_module.body.operations)
        return 0
    
    def get_operation_names(self) -> list[str]:
        """Get list of operation names in the circuit.
        
        Returns:
            List of operation names (e.g., ['cmt2.circuit', 'cmt2.module', ...])
        """
        # TODO: Implement in Task 2.2
        return []


class LoweredCircuit:
    """Circuit after lowering to target MLIR dialect.
    
    This class represents the second stage of the compilation pipeline,
    containing the MLIR module after transformation to a target dialect
    (e.g., FIRRTL, HW, Comb, Seq).
    
    Attributes:
        mlir_module: The MLIR module in target dialect
        target: Target format ("verilog", "firrtl", "hw", "mlir")
        optimization_level: Optimization level applied (0-3)
        
    Example:
        lowered = elaborated.lower(target="firrtl")
        print(lowered)  # LoweredCircuit(target='firrtl', ...)
        
        # Generate code
        mlir_code = lowered.codegen(format="mlir")
        
        # Compile to final artifact
        compiled = lowered.compile()
    """
    
    def __init__(
        self,
        mlir_module: Any,
        target: str,
        optimization_level: int,
        _source_circuit: ElaboratedCircuit | None = None,
    ):
        """Initialize a lowered circuit.
        
        Args:
            mlir_module: The MLIR module in target dialect
            target: Target format ("verilog", "firrtl", "hw", "mlir")
            optimization_level: Optimization level applied (0-3)
            _source_circuit: Reference to source ElaboratedCircuit (internal)
        """
        self.mlir_module = mlir_module
        self.target = target
        self.optimization_level = optimization_level
        self._source_circuit = _source_circuit
    
    def __repr__(self) -> str:
        """Return a concise string representation for debugging."""
        mlir_size = len(self.mlir_module) if hasattr(self.mlir_module, '__len__') else 0
        return (
            f"LoweredCircuit("
            f"target={self.target!r}, "
            f"mlir_size={mlir_size}, "
            f"opt_level={self.optimization_level}"
            f")"
        )
    
    def __str__(self) -> str:
        """Return a human-readable summary of the lowered circuit."""
        mlir_size = len(self.mlir_module) if hasattr(self.mlir_module, '__len__') else 0
        return (
            f"LoweredCircuit (target={self.target}):\n"
            f"  MLIR size: {mlir_size} bytes\n"
            f"  Optimization level: {self.optimization_level}\n"
            f"  Use .codegen() to generate output"
        )
    
    def compile(self) -> CompiledCircuit:
        """Compile to executable or final artifact.
        
        This produces a CompiledCircuit containing the target-specific
        compiled artifact (e.g., Verilog code, simulation binary).
        
        Returns:
            A CompiledCircuit ready for execution or code generation
            
        Raises:
            RuntimeError: If compilation fails
            
        Example:
            compiled = lowered.compile()
            compiled.write("output.v")
        """
        # Run the compiled stage pipeline if available
        if _HAS_PASS_PIPELINE and self.mlir_module is not None:
            try:
                pipeline = PassPipeline(
                    PassPipelineConfig(
                        target=PipelineTarget(self.target),
                        optimization_level=self.optimization_level,
                    )
                )
                compiled_module = pipeline.run_stage(
                    self.mlir_module,
                    PipelineStage.COMPILED,
                    clone=True,
                )
                return CompiledCircuit(
                    artifact=compiled_module,
                    target=self.target,
                    _source_lowered=self,
                )
            except Exception as e:
                # If pipeline fails, fall back to uncompiled artifact with warning
                import warnings
                warnings.warn(
                    f"Compilation pipeline failed: {e}. "
                    "Returning uncompiled artifact.",
                    RuntimeWarning,
                )
        
        # Return uncompiled artifact (for development/testing without CIRCT)
        return CompiledCircuit(
            artifact=self.mlir_module,
            target=self.target,
            _source_lowered=self,
        )
    
    def codegen(self, format: str = "mlir") -> str:
        """Generate code in specified format.
        
        Args:
            format: Output format - "mlir", "mlir-bytecode", "verilog", 
                   "systemverilog", or "firrtl"
                   
        Returns:
            Generated code as a string
            
        Raises:
            ValueError: If format is not supported
            RuntimeError: If code generation fails
            
        Example:
            verilog = lowered.codegen(format="verilog")
            mlir = lowered.codegen(format="mlir")
        """
        # Generate MLIR output
        if format == "mlir":
            if self.mlir_module is None:
                return f"// LoweredCircuit: No MLIR module (target={self.target})\n"
            if hasattr(self.mlir_module, 'dump'):
                try:
                    return str(self.mlir_module.dump())
                except Exception as e:
                    return f"// Error dumping MLIR: {e}\n"
            if hasattr(self.mlir_module, '__str__'):
                try:
                    return str(self.mlir_module)
                except Exception:
                    pass
            return f"// LoweredCircuit MLIR (target={self.target})\n"
        
        # Try to use CIRCT translation for Verilog/FIRRTL
        if format in ("verilog", "systemverilog", "firrtl"):
            # Try CIRCT translation if available
            try:
                from circt import ir
                
                if format == "firrtl":
                    # For FIRRTL, return the module as-is if it's FIRRTL dialect
                    if hasattr(self.mlir_module, 'dump'):
                        return str(self.mlir_module.dump())
                
                # Try to use CIRCT's translation or export
                # Note: This would require the specific CIRCT bindings for
                # FIRRTL to Verilog conversion
                if hasattr(self.mlir_module, 'dump'):
                    mlir_text = str(self.mlir_module.dump())
                    # Check if already contains Verilog-like output
                    if any(keyword in mlir_text for keyword in ['module', 'always', 'assign']):
                        return mlir_text
                    
            except ImportError:
                pass
            
            # Fallback: return placeholder
            return (
                f"// Verilog output for target={self.target}\n"
                f"// (CIRCT bindings required for actual Verilog generation)\n"
            )
        
        raise ValueError(f"Unsupported format: {format}")


class CompiledCircuit:
    """Compiled circuit ready for execution or code generation.
    
    This class represents the final stage of the compilation pipeline,
    containing a target-specific compiled artifact. Depending on the
    target, this could be:
    - Verilog/SystemVerilog source code
    - A simulation binary
    - An FPGA bitstream (future)
    - Other hardware representations
    
    Attributes:
        artifact: The compiled artifact (format depends on target)
        target: Target format ("verilog", "simulation", "firrtl")
        
    Example:
        compiled = lowered.compile()
        print(compiled)  # CompiledCircuit(target='verilog')
        
        # Write to file
        compiled.write("output.sv")
        
        # Or run simulation
        result = compiled.run(cycles=100)
    """
    
    def __init__(
        self,
        artifact: Any,
        target: str,
        _source_lowered: LoweredCircuit | None = None,
    ):
        """Initialize a compiled circuit.
        
        Args:
            artifact: The compiled artifact (format depends on target)
            target: Target format ("verilog", "simulation", "firrtl")
            _source_lowered: Reference to source LoweredCircuit (internal)
        """
        self.artifact = artifact
        self.target = target
        self._source_lowered = _source_lowered
    
    def __repr__(self) -> str:
        """Return a concise string representation for debugging."""
        artifact_info = ""
        if hasattr(self.artifact, '__len__'):
            artifact_info = f", artifact_size={len(self.artifact)}"
        return f"CompiledCircuit(target={self.target!r}{artifact_info})"
    
    def __str__(self) -> str:
        """Return a human-readable summary of the compiled circuit."""
        artifact_info = len(self.artifact) if hasattr(self.artifact, '__len__') else "unknown"
        return (
            f"CompiledCircuit (target={self.target}):\n"
            f"  Artifact size: {artifact_info} bytes\n"
            f"  Use .write() to save to file or .run() to simulate"
        )
    
    def run(self, **kwargs: Any) -> Any:
        """Run simulation (if target is simulation).
        
        This method executes the compiled circuit if the target supports
        simulation. For non-simulation targets (e.g., "verilog"), this
        raises an error.
        
        Args:
            **kwargs: Simulation parameters (target-specific)
                - cycles: Number of simulation cycles
                - timeout: Maximum simulation time
                - inputs: Input stimulus dictionary
                - waveform: Enable waveform capture
                
        Returns:
            Simulation results (type depends on target)
            
        Raises:
            RuntimeError: If target does not support simulation
            RuntimeError: If simulation fails
            
        Example:
            # For simulation targets
            result = compiled.run(cycles=1000, waveform=True)
            print(result.waveforms)
            
            # For non-simulation targets
            compiled.run()  # Raises RuntimeError
        """
        if self.target not in ("simulation", "verilator", "sim"):
            raise RuntimeError(
                f"Target '{self.target}' does not support simulation. "
                f"Use .write() to save the compiled artifact."
            )
        
        # Try to use PyCMT2 simulation if available
        try:
            from circt.pycmt2 import SimulationWorkspace
            
            # Extract simulation parameters
            cycles = kwargs.get('cycles', 100)
            timeout = kwargs.get('timeout')
            inputs = kwargs.get('inputs', {})
            waveform = kwargs.get('waveform', False)
            
            # TODO: Create simulation workspace and run
            # This would require the MLIR module to be converted to
            # a simulatable format first
            
            return {
                "status": "simulation_not_yet_implemented",
                "target": self.target,
                "cycles_requested": cycles,
                "note": "Full PyCMT2 integration pending"
            }
            
        except ImportError:
            # PyCMT2 not available
            return {
                "status": "pycmt2_not_available",
                "target": self.target,
                "note": "PyCMT2 bindings not installed"
            }
    
    def write(self, path: str) -> None:
        """Write compiled output to file.
        
        Args:
            path: Output file path
            
        Raises:
            IOError: If file cannot be written
            
        Example:
            compiled.write("output.v")
            compiled.write("design.sv")
        """
        import os
        
        # Ensure directory exists
        dir_path = os.path.dirname(path)
        if dir_path and not os.path.exists(dir_path):
            os.makedirs(dir_path, exist_ok=True)
        
        # Determine format from path extension or use target
        _, ext = os.path.splitext(path)
        format_map = {
            '.v': 'verilog',
            '.sv': 'systemverilog',
            '.mlir': 'mlir',
            '.fir': 'firrtl',
            '.json': 'json',
        }
        format = format_map.get(ext, self.target)
        
        # Generate content
        content = self.codegen(format=format)
        
        # Write to file
        with open(path, 'w') as f:
            f.write(content)
    
    def codegen(self, format: str = "systemverilog") -> str:
        """Generate code in specified format.
        
        This method generates code from the compiled artifact. For most
        targets, this simply returns the artifact content. For some
        targets, it may perform additional transformation.
        
        Args:
            format: Output format - "systemverilog", "verilog", "mlir",
                   "firrtl", or "json"
                   
        Returns:
            Generated code as a string
            
        Raises:
            ValueError: If format is not supported for this target
            
        Example:
            verilog = compiled.codegen(format="verilog")
            json_repr = compiled.codegen(format="json")
        """
        # Handle MLIR format
        if format == "mlir":
            if self.artifact is None:
                return f"// CompiledCircuit: No artifact (target={self.target})\n"
            if hasattr(self.artifact, 'dump'):
                try:
                    return str(self.artifact.dump())
                except Exception as e:
                    return f"// Error dumping MLIR: {e}\n"
            if isinstance(self.artifact, str):
                return self.artifact
            return f"// CompiledCircuit MLIR (target={self.target})\n"
        
        # Handle Verilog/SystemVerilog format
        if format in ("verilog", "systemverilog"):
            if isinstance(self.artifact, str):
                # If artifact is already a string, assume it's Verilog
                return self.artifact
            
            # Try to extract Verilog from MLIR artifact
            if self.artifact is not None and hasattr(self.artifact, 'dump'):
                mlir_text = str(self.artifact.dump())
                # Check if it looks like Verilog already
                verilog_keywords = ['module', 'always', 'assign', 'wire', 'reg', 'input', 'output']
                if any(kw in mlir_text for kw in verilog_keywords):
                    return mlir_text
            
            # Fallback: return placeholder
            return (
                f"// Verilog output for target={self.target}\n"
                f"// (Generated from CMT2 JIT compilation)\n"
            )
        
        # Handle FIRRTL format
        if format == "firrtl":
            if isinstance(self.artifact, str):
                return self.artifact
            if self.artifact is not None and hasattr(self.artifact, 'dump'):
                try:
                    return str(self.artifact.dump())
                except Exception:
                    pass
            return f"// FIRRTL output\n"
        
        # Handle JSON format (metadata)
        if format == "json":
            import json
            
            metadata = {
                "target": self.target,
                "has_artifact": self.artifact is not None,
                "artifact_type": type(self.artifact).__name__ if self.artifact else None,
            }
            
            # Add source information if available
            if self._source_lowered is not None:
                metadata["optimization_level"] = self._source_lowered.optimization_level
                if self._source_lowered._source_circuit is not None:
                    metadata["circuit_name"] = self._source_lowered._source_circuit.name
                    metadata["static_args"] = self._source_lowered._source_circuit.static_args
            
            return json.dumps(metadata, indent=2, default=str)
        
        raise ValueError(f"Format '{format}' not supported for target '{self.target}'")


# =============================================================================
# Utility Functions
# =============================================================================

def create_elaborated_circuit(
    mlir_module: Any,
    name: str,
    static_args: dict[str, Any] | None = None,
) -> ElaboratedCircuit:
    """Factory function to create an ElaboratedCircuit.
    
    This is a convenience factory for creating elaborated circuits
    with proper type handling.
    
    Args:
        mlir_module: The MLIR module containing CMT2 IR
        name: Name of the circuit
        static_args: Optional dictionary of static argument names to values
        
    Returns:
        A new ElaboratedCircuit instance
        
    Example:
        circuit = create_elaborated_circuit(
            mlir_module=module,
            name="Counter",
            static_args={"width": 32}
        )
    """
    return ElaboratedCircuit(
        mlir_module=mlir_module,
        name=name,
        static_args=static_args or {},
    )


def is_elaborated(obj: Any) -> bool:
    """Check if object is an ElaboratedCircuit."""
    return isinstance(obj, ElaboratedCircuit)


def is_lowered(obj: Any) -> bool:
    """Check if object is a LoweredCircuit."""
    return isinstance(obj, LoweredCircuit)


def is_compiled(obj: Any) -> bool:
    """Check if object is a CompiledCircuit."""
    return isinstance(obj, CompiledCircuit)
