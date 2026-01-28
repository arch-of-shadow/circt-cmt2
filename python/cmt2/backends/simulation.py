#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Simulation backend for CMT2 JIT compilation.

This module provides the simulation backend that integrates with PyCMT2
for running hardware simulations.

Example:
    from cmt2.backends.simulation import SimulationBackend
    from cmt2 import elaborate
    
    @elaborate
    def my_design():
        # ... create circuit ...
        return circuit
    
    # Method 1: Using SimulationBackend directly
    backend = SimulationBackend()
    circuit = my_design()
    executable = backend.compile(circuit)
    result = executable.run(cycles=1000)
    
    # Method 2: Using @simulate decorator
    @simulate
    def test_my_design():
        circuit = my_design()
        # ... test and simulate ...
        return circuit
"""

from __future__ import annotations

import os
import tempfile
from dataclasses import dataclass, field
from typing import Any, TYPE_CHECKING

if TYPE_CHECKING:
    from cmt2.jit._stages import LoweredCircuit, CompiledCircuit


# PyCMT2 availability check
try:
    from circt.pycmt2 import SimulationWorkspace
    from circt.pycmt2.testbench import Testbench
    _HAS_PYCMT2 = True
except ImportError:
    _HAS_PYCMT2 = False
    SimulationWorkspace = None
    Testbench = None


@dataclass
class SimulationResult:
    """Result of a simulation run.
    
    Attributes:
        success: Whether the simulation completed successfully
        cycles: Number of cycles simulated
        waveforms: Path to waveform file (if captured)
        outputs: Dictionary of output values
        logs: Simulation log output
    """
    success: bool
    cycles: int
    waveforms: str | None = None
    outputs: dict[str, Any] = field(default_factory=dict)
    logs: str = ""


class SimulationExecutable:
    """Compiled simulation ready to run.
    
    This class wraps a compiled simulation artifact and provides
    methods to run and interact with it.
    
    Attributes:
        _mlir_module: The MLIR module for simulation
        _workspace: SimulationWorkspace instance (if created)
        _workspace_path: Path to simulation workspace
    """
    
    def __init__(
        self,
        mlir_module: Any,
        workspace_path: str | None = None,
    ):
        """Initialize a simulation executable.
        
        Args:
            mlir_module: The MLIR module to simulate
            workspace_path: Optional path to simulation workspace
        """
        self._mlir_module = mlir_module
        self._workspace_path = workspace_path
        self._workspace = None
    
    def run(
        self,
        cycles: int = 100,
        inputs: dict[str, Any] | None = None,
        waveform: bool = False,
        timeout: int | None = None,
    ) -> SimulationResult:
        """Run the simulation.
        
        Args:
            cycles: Number of simulation cycles to run
            inputs: Input stimulus dictionary
            waveform: Whether to capture waveforms
            timeout: Maximum simulation time in seconds
            
        Returns:
            SimulationResult with simulation outputs
            
        Raises:
            RuntimeError: If simulation fails or PyCMT2 is not available
        """
        if not _HAS_PYCMT2:
            raise RuntimeError(
                "PyCMT2 not available. "
                "Install CIRCT Python bindings to use simulation."
            )
        
        # TODO: Full PyCMT2 integration
        # This would:
        # 1. Create simulation workspace
        # 2. Convert MLIR to simulatable format
        # 3. Run simulation
        # 4. Collect results
        
        # Placeholder implementation
        return SimulationResult(
            success=True,
            cycles=cycles,
            waveforms=None,
            outputs={},
            logs="Simulation not yet fully implemented",
        )
    
    def step(self) -> SimulationResult:
        """Run a single simulation cycle.
        
        Returns:
            SimulationResult for the current cycle
        """
        return self.run(cycles=1)
    
    def reset(self) -> None:
        """Reset the simulation to initial state."""
        # TODO: Implement reset
        pass


class SimulationBackend:
    """Backend for PyCMT2 simulation.
    
    This backend compiles CMT2 circuits to simulation executables
    that can be run using PyCMT2.
    
    Example:
        backend = SimulationBackend()
        executable = backend.compile(circuit)
        result = executable.run(cycles=1000)
        
        # Or with testbench
        tb = backend.create_testbench(circuit)
        with tb.sequence("test") as seq:
            seq.reset(5)
            seq.wait(10)
        result = backend.run(circuit, testbench=tb)
    """
    
    def __init__(self, workspace_dir: str | None = None):
        """Initialize the simulation backend.
        
        Args:
            workspace_dir: Optional directory for simulation workspaces.
                If not provided, a temporary directory will be used.
        """
        self._workspace_dir = workspace_dir
        self._temp_dir = None
        
        if not _HAS_PYCMT2:
            raise RuntimeError(
                "PyCMT2 not available. "
                "Install CIRCT Python bindings to use SimulationBackend."
            )
    
    def compile(self, circuit) -> SimulationExecutable:
        """Compile a circuit to simulation executable.
        
        Args:
            circuit: The circuit to compile (can be Circuit, ElaboratedCircuit,
                    LoweredCircuit, or any object with emit_verilog method)
            
        Returns:
            A SimulationExecutable ready to run
            
        Raises:
            RuntimeError: If compilation fails
        """
        # Get or create workspace directory
        if self._workspace_dir is None:
            self._temp_dir = tempfile.mkdtemp(prefix="cmt2_sim_")
            workspace_path = self._temp_dir
        else:
            workspace_path = self._workspace_dir
            os.makedirs(workspace_path, exist_ok=True)
        
        # Extract MLIR module from circuit if needed
        mlir_module = self._extract_mlir(circuit)
        
        # Create the simulation executable
        return SimulationExecutable(
            mlir_module=mlir_module,
            workspace_path=workspace_path,
        )
    
    def create_testbench(self, circuit, auto_debug_ports: bool = False) -> "Testbench":
        """Create a testbench for the circuit.
        
        Args:
            circuit: The circuit to create testbench for
            auto_debug_ports: Enable auto debug ports
            
        Returns:
            Testbench instance
        """
        if not _HAS_PYCMT2:
            raise RuntimeError("PyCMT2 not available")
        
        return Testbench(circuit, auto_debug_ports=auto_debug_ports)
    
    def run(
        self,
        circuit,
        testbench: "Testbench" | None = None,
        waves: bool = True,
        workspace_dir: str | None = None,
    ) -> dict:
        """Run simulation for a circuit.
        
        This is a convenience method that creates the workspace,
        generates the simulation, and runs it.
        
        Args:
            circuit: The circuit to simulate
            testbench: Optional testbench to use
            waves: Enable waveform capture
            workspace_dir: Optional workspace directory
            
        Returns:
            Simulation result dictionary with 'success', 'output', 'workspace' keys
        """
        # Use provided workspace or create one
        if workspace_dir is None:
            workspace_dir = self._workspace_dir or tempfile.mkdtemp(prefix="cmt2_sim_")
        
        # Create workspace
        os.makedirs(workspace_dir, exist_ok=True)
        
        # Create SimulationWorkspace
        workspace = SimulationWorkspace(circuit, workspace_dir)
        
        # Generate with or without testbench
        if testbench:
            workspace.generate_with_testbench(testbench)
        else:
            workspace.generate_placeholder()
        
        # Build and run
        try:
            success, output = workspace.build_and_run(waves=waves)
            return {
                "success": success,
                "output": output,
                "workspace": workspace_dir,
            }
        except Exception as e:
            return {
                "success": False,
                "error": str(e),
                "workspace": workspace_dir,
            }
    
    def _extract_mlir(self, circuit) -> Any:
        """Extract MLIR module from various circuit types."""
        # Direct MLIR module
        if hasattr(circuit, 'operation') or hasattr(circuit, 'body'):
            return circuit
        
        # ElaboratedCircuit, LoweredCircuit, CompiledCircuit
        if hasattr(circuit, 'mlir_module'):
            return circuit.mlir_module
        
        # PyCMT2 Circuit
        if hasattr(circuit, 'emit_mlir'):
            # Return the circuit itself - SimulationWorkspace handles it
            return circuit
        
        raise ValueError(f"Cannot extract MLIR from circuit type: {type(circuit)}")
    
    def is_available(self) -> bool:
        """Check if the simulation backend is available.
        
        Returns:
            True if PyCMT2 bindings are available
        """
        return _HAS_PYCMT2


def create_simulation_backend(
    workspace_dir: str | None = None
) -> SimulationBackend | None:
    """Factory function to create a simulation backend.
    
    Args:
        workspace_dir: Optional directory for simulation workspaces
        
    Returns:
        A configured SimulationBackend instance, or None if PyCMT2
        is not available
        
    Example:
        backend = create_simulation_backend("./sim_workspace")
        if backend:
            executable = backend.compile(circuit)
    """
    if not _HAS_PYCMT2:
        return None
    return SimulationBackend(workspace_dir=workspace_dir)


def quick_simulate(
    circuit,
    cycles: int = 100,
    workspace_dir: str | None = None,
) -> dict:
    """Quick simulation helper for testing.
    
    This is a convenience function for simple simulations without
    a custom testbench.
    
    Args:
        circuit: The circuit to simulate
        cycles: Number of cycles to run
        workspace_dir: Optional workspace directory
        
    Returns:
        Simulation result dictionary
        
    Example:
        @elaborate
        def my_design():
            return circuit
        
        circuit = my_design()
        result = quick_simulate(circuit, cycles=50)
        print(result['success'])
    """
    if not _HAS_PYCMT2:
        raise RuntimeError("PyCMT2 not available")
    
    backend = SimulationBackend(workspace_dir=workspace_dir)
    
    # Create simple testbench
    tb = backend.create_testbench(circuit)
    with tb.sequence("quick_test") as seq:
        seq.reset(5)
        seq.wait(cycles)
    
    return backend.run(circuit, testbench=tb)
