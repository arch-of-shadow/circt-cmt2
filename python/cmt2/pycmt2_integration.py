#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""PyCMT2 integration for CMT2 JIT.

This module provides the bridge between the CMT2 JIT system and PyCMT2,
enabling full circuit creation and simulation through the JIT API.

Example:
    from cmt2 import elaborate
    from cmt2.pycmt2_integration import CircuitBuilder
    
    @elaborate
    def counter_design(width: int = 32):
        builder = CircuitBuilder("Counter")
        
        with builder.module("Counter") as m:
            clk = m.clock()
            rst = m.reset()
            
            count = m.instance_reg(32, "count", clk=clk, rst=rst)
            
            with m.rule("increment"):
                with m.guard() as g:
                    g.always()
                with m.body() as b:
                    val = b.call(count, "read")
                    b.call(count, "write", val + 1)
        
        return builder.circuit
"""

from __future__ import annotations

from typing import Any, TYPE_CHECKING

# PyCMT2 availability check
try:
    from circt.pycmt2 import Circuit, UInt, SInt, ClockType, ResetType
    from circt.pycmt2.stl import Reg, FIFO, Wire
    from circt.pycmt2.simulation import SimulationWorkspace
    from circt.pycmt2.testbench import Testbench
    _HAS_PYCMT2 = True
except ImportError:
    _HAS_PYCMT2 = False


class CircuitBuilder:
    """Builder for creating PyCMT2 circuits through JIT.
    
    This class wraps PyCMT2's Circuit and provides a convenient API
    for building hardware designs programmatically.
    
    Example:
        builder = CircuitBuilder("MyDesign")
        
        with builder.module("Top") as m:
            clk = m.clock()
            rst = m.reset()
            
            # Create registers, FIFOs, etc.
            reg = m.instance_reg(32, "my_reg", clk=clk, rst=rst)
        
        circuit = builder.circuit
        print(circuit.emit_mlir())
    """
    
    def __init__(self, name: str):
        """Initialize a circuit builder.
        
        Args:
            name: Name of the circuit
            
        Raises:
            RuntimeError: If PyCMT2 is not available
        """
        if not _HAS_PYCMT2:
            raise RuntimeError(
                "PyCMT2 not available. "
                "Install CIRCT Python bindings to use CircuitBuilder."
            )
        
        self._circuit = Circuit(name)
        self._current_module = None
    
    @property
    def circuit(self) -> "Circuit":
        """Get the built circuit."""
        return self._circuit
    
    def module(self, name: str):
        """Create a module context.
        
        Args:
            name: Name of the module
            
        Returns:
            ModuleContext for building the module
        """
        return ModuleContext(self._circuit, name)
    
    def emit_mlir(self) -> str:
        """Emit the circuit as MLIR.
        
        Returns:
            MLIR representation of the circuit
        """
        return self._circuit.emit_mlir()
    
    def emit_firrtl(self) -> str:
        """Emit the circuit as FIRRTL.
        
        Returns:
            FIRRTL representation of the circuit
        """
        return self._circuit.emit_firrtl()
    
    def emit_verilog(self) -> str:
        """Emit the circuit as Verilog.
        
        Returns:
            Verilog representation of the circuit
        """
        return self._circuit.emit_verilog()


class ModuleContext:
    """Context for building a module within a circuit.
    
    This wraps PyCMT2's module builder and provides convenient methods
    for creating module contents.
    """
    
    def __init__(self, circuit: "Circuit", name: str):
        self._circuit = circuit
        self._name = name
        self._module = None
    
    def __enter__(self):
        self._context = self._circuit.module(self._name)
        self._module = self._context.__enter__()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        return self._context.__exit__(exc_type, exc_val, exc_tb)
    
    # Port creation shortcuts
    def clock(self, name: str = "clk"):
        """Create a clock input port."""
        return self._module.clock(name)
    
    def reset(self, name: str = "rst"):
        """Create a reset input port."""
        return self._module.reset(name)
    
    def input(self, name: str, dtype):
        """Create an input port."""
        return self._module.input(name, dtype)
    
    def output(self, name: str, dtype):
        """Create an output port."""
        return self._module.output(name, dtype)
    
    # Instance creation
    def instance(self, module, name: str, **kwargs):
        """Create a module instance."""
        return self._module.instance(module, name, **kwargs)
    
    def instance_reg(self, width: int, name: str, clk=None, rst=None, init: int = 0):
        """Create a register instance.
        
        Args:
            width: Bit width of the register
            name: Instance name
            clk: Clock signal (optional)
            rst: Reset signal (optional)
            init: Initial value
            
        Returns:
            Register instance
        """
        reg = Reg.create(self._circuit, width, init=init)
        kwargs = {"clk": clk, "rst": rst}
        kwargs = {k: v for k, v in kwargs.items() if v is not None}
        return self._module.instance(reg, name, **kwargs)
    
    def instance_fifo(self, data_type, depth: int, name: str, clk=None, rst=None):
        """Create a FIFO instance.
        
        Args:
            data_type: Data type for FIFO entries
            depth: FIFO depth
            name: Instance name
            clk: Clock signal (optional)
            rst: Reset signal (optional)
            
        Returns:
            FIFO instance
        """
        fifo = FIFO.create(self._circuit, data_type, depth)
        kwargs = {"clk": clk, "rst": rst}
        kwargs = {k: v for k, v in kwargs.items() if v is not None}
        return self._module.instance(fifo, name, **kwargs)
    
    # Rule and method creation
    def rule(self, name: str):
        """Create a rule context.
        
        Args:
            name: Rule name
            
        Returns:
            RuleContext
        """
        return RuleContext(self._module, name)
    
    def value(self, name: str, returns: list):
        """Create a value method.
        
        Args:
            name: Method name
            returns: List of return types
            
        Returns:
            ValueContext
        """
        return ValueContext(self._module, name, returns)
    
    def method(self, name: str, args: list = None, returns: list = None):
        """Create an action method.
        
        Args:
            name: Method name
            args: List of argument types (optional)
            returns: List of return types (optional)
            
        Returns:
            MethodContext
        """
        return MethodContext(self._module, name, args or [], returns or [])


class RuleContext:
    """Context for building a rule."""
    
    def __init__(self, module, name: str):
        self._module = module
        self._name = name
        self._rule = None
    
    def __enter__(self):
        self._context = self._module.rule(self._name)
        self._rule = self._context.__enter__()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        return self._context.__exit__(exc_type, exc_val, exc_tb)
    
    def guard(self):
        """Get the guard context."""
        return self._rule.guard()
    
    def body(self):
        """Get the body context."""
        return self._rule.body()


class ValueContext:
    """Context for building a value method."""
    
    def __init__(self, module, name: str, returns: list):
        self._module = module
        self._name = name
        self._returns = returns
        self._value = None
    
    def __enter__(self):
        self._context = self._module.value(self._name, returns=self._returns)
        self._value = self._context.__enter__()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        return self._context.__exit__(exc_type, exc_val, exc_tb)
    
    def guard(self):
        """Get the guard context."""
        return self._value.guard()
    
    def body(self):
        """Get the body context."""
        return self._value.body()


class MethodContext:
    """Context for building an action method."""
    
    def __init__(self, module, name: str, args: list, returns: list):
        self._module = module
        self._name = name
        self._args = args
        self._returns = returns
        self._method = None
    
    def __enter__(self):
        self._context = self._module.method(self._name, args=self._args, returns=self._returns)
        self._method = self._context.__enter__()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        return self._context.__exit__(exc_type, exc_val, exc_tb)
    
    def guard(self):
        """Get the guard context."""
        return self._method.guard()
    
    def body(self):
        """Get the body context."""
        return self._method.body()


class JITSimulationRunner:
    """Run simulations for JIT-compiled circuits.
    
    This class integrates with PyCMT2's SimulationWorkspace to provide
    end-to-end simulation for JIT designs.
    
    Example:
        runner = JITSimulationRunner(circuit, "./sim")
        
        # Create testbench
        tb = runner.create_testbench()
        with tb.sequence("test") as seq:
            seq.reset(5)
            seq.wait(10)
        
        # Run simulation
        result = runner.run(testbench=tb)
        print(result.success)
    """
    
    def __init__(
        self,
        circuit: "Circuit",
        workspace_dir: str,
        top_module: str | None = None,
        debug_ports: bool = False,
    ):
        """Initialize the simulation runner.
        
        Args:
            circuit: The circuit to simulate
            workspace_dir: Directory for simulation workspace
            top_module: Optional top module name
            debug_ports: Enable debug ports
        """
        if not _HAS_PYCMT2:
            raise RuntimeError("PyCMT2 not available")
        
        self._circuit = circuit
        self._workspace_dir = workspace_dir
        self._workspace = SimulationWorkspace(
            circuit, workspace_dir, top_module, debug_ports
        )
    
    def create_testbench(self, auto_debug_ports: bool = False) -> "Testbench":
        """Create a testbench for the circuit.
        
        Args:
            auto_debug_ports: Enable auto debug ports
            
        Returns:
            Testbench instance
        """
        return Testbench(self._circuit, auto_debug_ports=auto_debug_ports)
    
    def generate(self, testbench: "Testbench" | None = None) -> None:
        """Generate the simulation workspace.
        
        Args:
            testbench: Optional testbench to include
        """
        if testbench:
            self._workspace.generate_with_testbench(testbench)
        else:
            self._workspace.generate_placeholder()
    
    def run(
        self,
        testbench: "Testbench" | None = None,
        waves: bool = True,
        build_args: list[str] | None = None,
    ) -> dict:
        """Run the simulation.
        
        Args:
            testbench: Optional testbench to use
            waves: Enable waveform capture
            build_args: Additional build arguments
            
        Returns:
            Simulation result dictionary
        """
        # Generate workspace
        self.generate(testbench)
        
        # Build and run
        try:
            success, output = self._workspace.build_and_run(
                waves=waves,
                build_args=build_args or []
            )
            return {
                "success": success,
                "output": output,
                "workspace": self._workspace_dir,
            }
        except Exception as e:
            return {
                "success": False,
                "error": str(e),
                "workspace": self._workspace_dir,
            }
    
    def build(self, args: list[str] | None = None) -> tuple[bool, str]:
        """Build the simulation without running.
        
        Args:
            args: Additional build arguments
            
        Returns:
            (success, output) tuple
        """
        self._workspace.generate_placeholder()
        return self._workspace.build(args or [])


def is_pycmt2_available() -> bool:
    """Check if PyCMT2 is available.
    
    Returns:
        True if PyCMT2 bindings are available
    """
    return _HAS_PYCMT2


def get_pycmt2_version() -> str | None:
    """Get PyCMT2 version if available.
    
    Returns:
        Version string or None
    """
    if not _HAS_PYCMT2:
        return None
    
    try:
        import circt
        return getattr(circt, "__version__", "unknown")
    except Exception:
        return "unknown"
