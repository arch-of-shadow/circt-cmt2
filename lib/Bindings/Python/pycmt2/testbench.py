#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Testbench DSL for PyCMT2 designs.

This module provides a Python DSL for describing testbenches that can be
compiled to C++ (Verilator) or Python (cocotb) testbenches.

Example:
    from pycmt2 import Circuit
    from pycmt2.testbench import Testbench
    from pycmt2.simulation import SimulationWorkspace

    circuit = Circuit("Counter")
    # ... build circuit ...

    tb = Testbench(circuit)

    with tb.sequence("basic_count") as seq:
        seq.reset(5)
        seq.wait(10)
        seq.expect("count", 10)

    with tb.sequence("stress_test") as seq:
        seq.reset(5)
        for i in range(100):
            seq.wait(1)
            seq.expect("count", i + 1)

    # Generate simulation workspace with testbench
    ws = SimulationWorkspace(circuit, "./sim")
    ws.generate_with_testbench(tb)
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from contextlib import contextmanager
from dataclasses import dataclass
from typing import TYPE_CHECKING, Iterator, Any

if TYPE_CHECKING:
    from .circuit import Circuit


# Test operation types

class TestOp(ABC):
    """Base class for testbench operations."""

    @abstractmethod
    def to_cpp(self) -> str:
        """Generate C++ code for this operation."""
        pass

    @abstractmethod
    def to_python(self) -> str:
        """Generate Python (cocotb) code for this operation."""
        pass


@dataclass
class ResetOp(TestOp):
    """Assert reset for N cycles."""
    cycles: int

    def to_cpp(self) -> str:
        return f"reset({self.cycles});"

    def to_python(self) -> str:
        return f"await reset_dut(dut, {self.cycles})"


@dataclass
class WaitOp(TestOp):
    """Wait for N clock cycles."""
    cycles: int

    def to_cpp(self) -> str:
        return f"wait_cycles({self.cycles});"

    def to_python(self) -> str:
        return f"await ClockCycles(dut.clk, {self.cycles})"


@dataclass
class EvalOp(TestOp):
    """Evaluate combinational logic without advancing time."""

    def to_cpp(self) -> str:
        return "dut->eval();"

    def to_python(self) -> str:
        # Best-effort placeholder (this backend is currently not used in-tree).
        return "# eval()"


@dataclass
class DriveOp(TestOp):
    """Drive a value to an input port."""
    port: str
    value: int | str

    def to_cpp(self) -> str:
        return f"dut->{self.port} = {self.value};"

    def to_python(self) -> str:
        return f"dut.{self.port}.value = {self.value}"


@dataclass
class ExpectOp(TestOp):
    """Assert expected value on output port."""
    port: str
    value: int | str
    message: str | None = None

    def to_cpp(self) -> str:
        return f'expect("{self.port}", dut->{self.port}, {self.value});'

    def to_python(self) -> str:
        msg = self.message or f"{self.port} mismatch"
        return f'assert dut.{self.port}.value == {self.value}, "{msg}"'


@dataclass
class CallMethodOp(TestOp):
    """Call a method on an instance (drive enable, check ready)."""
    instance: str
    method: str
    args: tuple[Any, ...]

    def to_cpp(self) -> str:
        lines = [
            f"// Call {self.instance}.{self.method}",
            f"dut->{self.instance}_{self.method}_enable = 1;",
        ]
        for i, arg in enumerate(self.args):
            lines.append(f"dut->{self.instance}_{self.method}_arg{i} = {arg};")
        lines.append("tick();")
        lines.append(f"dut->{self.instance}_{self.method}_enable = 0;")
        return "\n".join(lines)

    def to_python(self) -> str:
        lines = [
            f"# Call {self.instance}.{self.method}",
            f"dut.{self.instance}_{self.method}_enable.value = 1",
        ]
        for i, arg in enumerate(self.args):
            lines.append(f"dut.{self.instance}_{self.method}_arg{i}.value = {arg}")
        lines.append("await RisingEdge(dut.clk)")
        lines.append(f"dut.{self.instance}_{self.method}_enable.value = 0")
        return "\n".join(lines)


@dataclass
class WaitReadyOp(TestOp):
    """Wait until a method is ready."""
    instance: str
    method: str

    def to_cpp(self) -> str:
        return f"while (!dut->{self.instance}_{self.method}_ready) tick();"

    def to_python(self) -> str:
        return f"while not dut.{self.instance}_{self.method}_ready.value: await RisingEdge(dut.clk)"


@dataclass
class WaitConditionOp(TestOp):
    """Wait until a condition is true."""
    condition: str  # C++ expression
    timeout: int = 1000

    def to_cpp(self) -> str:
        return f"""\
{{
    int timeout = {self.timeout};
    while (!({self.condition}) && timeout-- > 0) tick();
    if (timeout <= 0) {{
        std::cerr << "TIMEOUT waiting for: {self.condition}" << std::endl;
        check_passed = false;
    }}
}}"""

    def to_python(self) -> str:
        return f"""\
for _ in range({self.timeout}):
    if {self.condition}:
        break
    await RisingEdge(dut.clk)
else:
    assert False, "TIMEOUT waiting for: {self.condition}"
"""


@dataclass
class PrintOp(TestOp):
    """Print a message during simulation."""
    message: str
    values: tuple[str, ...] = ()

    def to_cpp(self) -> str:
        if not self.values:
            return f'std::cout << "{self.message}" << std::endl;'

        # Cast to uint64_t so that small integer types (e.g. uint8_t) print as
        # numbers rather than characters.
        pieces = [f'std::cout << "{self.message}: "']
        for i, v in enumerate(self.values):
            if i != 0:
                pieces.append(' << " "')
            pieces.append(f" << (uint64_t)dut->{v}")
        pieces.append(" << std::endl;")
        return "".join(pieces)

    def to_python(self) -> str:
        if self.values:
            value_strs = ", ".join(f"dut.{v}.value" for v in self.values)
            return f'print(f"{self.message}: {{{value_strs}}}")'
        return f'print("{self.message}")'


@dataclass
class CommentOp(TestOp):
    """Add a comment in the generated testbench."""
    text: str

    def to_cpp(self) -> str:
        return f"// {self.text}"

    def to_python(self) -> str:
        return f"# {self.text}"


@dataclass
class RecordCycleOp(TestOp):
    """Record the current cycle number for timing verification."""
    label: str

    def to_cpp(self) -> str:
        return f"uint64_t cycle_{self.label} = cycle_count;"

    def to_python(self) -> str:
        return f"cycle_{self.label} = cocotb.utils.get_sim_time('ns') // 10  # Approx cycles"


@dataclass
class PrintCycleDiffOp(TestOp):
    """Print the difference between two recorded cycle counts."""
    start_label: str
    end_label: str
    message: str

    def to_cpp(self) -> str:
        return f'std::cout << "{self.message}: " << (cycle_{self.end_label} - cycle_{self.start_label}) << " cycles" << std::endl;'

    def to_python(self) -> str:
        return f'print(f"{self.message}: {{cycle_{self.end_label} - cycle_{self.start_label}}} cycles")'


@dataclass
class DebugPortCheckOp(TestOp):
    """Check debug firing port for a rule."""
    rule_name: str
    expected_fired: bool = True

    def to_cpp(self) -> str:
        port_name = f"dbg_{self.rule_name}_firing"
        expected = "true" if self.expected_fired else "false"
        return f'expect("{port_name}", dut->{port_name}, {expected});'

    def to_python(self) -> str:
        port_name = f"dbg_{self.rule_name}_firing"
        expected = "True" if self.expected_fired else "False"
        return f'assert dut.{port_name}.value == {expected}, "{self.rule_name} firing mismatch"'


@dataclass
class DebugPortPrintOp(TestOp):
    """Print debug firing port status for a rule."""
    rule_name: str

    def to_cpp(self) -> str:
        port_name = f"dbg_{self.rule_name}_firing"
        return f'std::cout << "{self.rule_name} fired: " << (dut->{port_name} ? "yes" : "no") << std::endl;'

    def to_python(self) -> str:
        port_name = f"dbg_{self.rule_name}_firing"
        return f'print(f"{self.rule_name} fired: {{\\"yes\\" if dut.{port_name}.value else \\"no\\"}}")'


@dataclass
class DebugPortAllOp(TestOp):
    """Print all debug firing ports."""
    rule_names: tuple[str, ...]

    def to_cpp(self) -> str:
        lines = ['std::cout << "=== Rule Firing Status ===" << std::endl;']
        for rule in self.rule_names:
            port_name = f"dbg_{rule}_firing"
            lines.append(f'std::cout << "  {rule}: " << (dut->{port_name} ? "FIRED" : "-") << std::endl;')
        return "\n".join(lines)

    def to_python(self) -> str:
        lines = ['print("=== Rule Firing Status ===")']
        for rule in self.rule_names:
            port_name = f"dbg_{rule}_firing"
            lines.append(f'print(f"  {rule}: {{\\"FIRED\\" if dut.{port_name}.value else \\"-\\"}}")')
        return "\n".join(lines)


class TestSequence:
    """A sequence of test operations.

    TestSequence provides a fluent API for building test sequences
    that can be compiled to C++ or Python testbenches.

    Example:
        with tb.sequence("basic") as seq:
            seq.reset(5)
            seq.drive("input_a", 42)
            seq.wait(10)
            seq.expect("output", 84)
    """

    def __init__(self, name: str):
        self.name = name
        self._ops: list[TestOp] = []

    def reset(self, cycles: int = 5) -> TestSequence:
        """Assert reset for N cycles.

        Args:
            cycles: Number of cycles to hold reset high.

        Returns:
            self for chaining.
        """
        self._ops.append(ResetOp(cycles))
        return self

    def wait(self, cycles: int) -> TestSequence:
        """Wait for N clock cycles.

        Args:
            cycles: Number of cycles to wait.

        Returns:
            self for chaining.
        """
        self._ops.append(WaitOp(cycles))
        return self

    def eval(self) -> TestSequence:
        """Evaluate combinational logic without a clock edge."""
        self._ops.append(EvalOp())
        return self

    def drive(self, port: str, value: int | str) -> TestSequence:
        """Drive a value to an input port.

        Args:
            port: Name of the input port.
            value: Value to drive.

        Returns:
            self for chaining.
        """
        self._ops.append(DriveOp(port, value))
        return self

    def expect(
        self, port: str, value: int | str, message: str | None = None
    ) -> TestSequence:
        """Assert expected value on output port.

        Args:
            port: Name of the output port.
            value: Expected value.
            message: Optional error message on failure.

        Returns:
            self for chaining.
        """
        self._ops.append(ExpectOp(port, value, message))
        return self

    def call_method(
        self, instance: str, method: str, *args: Any
    ) -> TestSequence:
        """Call a method on an instance.

        This drives the method's enable signal high for one cycle
        and provides the arguments.

        Args:
            instance: Name of the instance.
            method: Name of the method to call.
            *args: Arguments to pass to the method.

        Returns:
            self for chaining.
        """
        self._ops.append(CallMethodOp(instance, method, args))
        return self

    def call_interface(
        self,
        interface_decl: Any,
        func: Any,
        *args: Any,
        ready: int | str = 1,
        results: tuple[int | str, ...] | None = None,
        timeout: int = 1000,
        advance_cycle: bool = True,
    ) -> TestSequence:
        """Expect an *outgoing* interface call from the DUT this cycle.

        InterfaceDecl ports are oriented as:
          - args/enable: Out (driven by DUT)
          - results/ready: In (driven by testbench)

        This helper drives `ready` (+ optional `results`), waits for `enable`
        (methods only), then checks the outgoing args.
        """
        decl = getattr(interface_decl, "_decl", interface_decl)
        decl_name = getattr(decl, "name", None) or getattr(decl, "_name", None)
        iface = getattr(decl, "interface", None)
        if not isinstance(decl_name, str) or iface is None:
            raise TypeError("call_interface expects an InterfaceDecl/InterfaceRef")

        # Apply optional custom prefix used by lowering.
        prefix = f"{decl_name}_"
        op = getattr(decl, "_op", None)
        attrs = getattr(op, "attributes", None)
        if attrs is not None:
            try:
                prefix_attr = attrs["prefix"]
            except Exception:
                prefix_attr = None
            if prefix_attr is not None:
                prefix = prefix_attr.value

        func_name = (
            getattr(func, "name", None)
            or getattr(func, "__name__", None)
            or func
        )
        if not isinstance(func_name, str):
            raise TypeError("call_interface expects `func` to be str-like or function-like")

        fn = iface.get_function(func_name)
        if fn is None:
            raise KeyError(f"Interface '{iface.name}' has no function '{func_name}'")

        fn_prefix = f"{prefix}{func_name}_"

        # Ready is always an input port.
        self.drive(f"{fn_prefix}ready", ready)

        # Results are input ports (optional; used by value or returned methods).
        if results is not None:
            for i, v in enumerate(results):
                self.drive(f"{fn_prefix}res{i}", v)

        # Ensure combinational outputs reflect the new drives before we sample them.
        self.eval()

        # Methods have an enable output we can wait for.
        if func_name in getattr(iface, "_methods", {}):
            self.wait_condition(f"dut->{fn_prefix}enable", timeout=timeout)

        # Check outgoing args.
        arg_types = getattr(fn, "_arg_types", [])
        if arg_types and isinstance(arg_types[0], tuple):
            arg_names = [n for n, _ in arg_types]
        else:
            arg_names = [f"arg{i}" for i in range(len(args))]

        for name, val in zip(arg_names, args):
            self.expect(f"{fn_prefix}{name}", val)

        if advance_cycle:
            self.wait(1)

        return self

    def wait_ready(self, instance: str, method: str) -> TestSequence:
        """Wait until a method is ready.

        Args:
            instance: Name of the instance.
            method: Name of the method.

        Returns:
            self for chaining.
        """
        self._ops.append(WaitReadyOp(instance, method))
        return self

    def wait_condition(
        self, condition: str, timeout: int = 1000
    ) -> TestSequence:
        """Wait until a condition is true.

        Args:
            condition: C++ boolean expression to wait for.
            timeout: Maximum cycles to wait.

        Returns:
            self for chaining.
        """
        self._ops.append(WaitConditionOp(condition, timeout))
        return self

    def print(self, message: str, *values: str) -> TestSequence:
        """Print a message during simulation.

        Args:
            message: Message to print.
            *values: Signal names to include in output.

        Returns:
            self for chaining.
        """
        self._ops.append(PrintOp(message, values))
        return self

    def comment(self, text: str) -> TestSequence:
        """Add a comment in the generated testbench.

        Args:
            text: Comment text.

        Returns:
            self for chaining.
        """
        self._ops.append(CommentOp(text))
        return self

    def record_cycle(self, label: str) -> TestSequence:
        """Record the current cycle number for timing verification.

        Use with print_cycle_diff() to measure elapsed cycles.

        Args:
            label: Label for this cycle recording (used in generated variable name).

        Returns:
            self for chaining.
        """
        self._ops.append(RecordCycleOp(label))
        return self

    def print_cycle_diff(
        self, start_label: str, end_label: str, message: str
    ) -> TestSequence:
        """Print the difference between two recorded cycle counts.

        Args:
            start_label: Label of the start cycle recording.
            end_label: Label of the end cycle recording.
            message: Description of what was measured.

        Returns:
            self for chaining.
        """
        self._ops.append(PrintCycleDiffOp(start_label, end_label, message))
        return self

    def expect_rule_fired(self, rule_name: str, fired: bool = True) -> TestSequence:
        """Assert that a rule fired (or did not fire) this cycle.

        Requires debug_ports=True when building the circuit.

        Args:
            rule_name: Name of the rule to check.
            fired: Expected firing status (default True = expect it fired).

        Returns:
            self for chaining.
        """
        self._ops.append(DebugPortCheckOp(rule_name, fired))
        return self

    def print_rule_status(self, rule_name: str) -> TestSequence:
        """Print whether a rule fired this cycle.

        Requires debug_ports=True when building the circuit.

        Args:
            rule_name: Name of the rule to check.

        Returns:
            self for chaining.
        """
        self._ops.append(DebugPortPrintOp(rule_name))
        return self

    def print_all_rule_status(self, rule_names: list[str]) -> TestSequence:
        """Print firing status of all specified rules.

        Requires debug_ports=True when building the circuit.

        Args:
            rule_names: List of rule names to display status for.

        Returns:
            self for chaining.
        """
        self._ops.append(DebugPortAllOp(tuple(rule_names)))
        return self

    def __enter__(self) -> TestSequence:
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


class Testbench:
    """Testbench description for CMT2 designs.

    Testbench provides a DSL for describing test sequences that can be
    compiled to C++ (Verilator) or Python (cocotb) testbenches.

    Example:
        tb = Testbench(circuit)

        with tb.sequence("basic") as seq:
            seq.reset(5)
            seq.wait(10)
            seq.expect("count", 10)

        # Generate with SimulationWorkspace
        ws = SimulationWorkspace(circuit, "./sim")
        ws.generate_with_testbench(tb)

    Example with debug ports:
        tb = Testbench(circuit, auto_debug_ports=True)

        with tb.sequence("test_rule_firing") as seq:
            seq.reset(5)
            seq.wait(1)
            # Check specific rule fired
            seq.expect_rule_fired("increment")
            # Print all rule firing status
            seq.print_all_rule_status(tb.get_rule_names())
    """

    def __init__(self, circuit: Circuit, auto_debug_ports: bool = False):
        """Create a testbench for a circuit.

        Args:
            circuit: The CMT2 circuit to test.
            auto_debug_ports: If True, enables debug port helpers for rule firing
                            observation. The circuit must be compiled with
                            debug_ports=True for these to work at runtime.
        """
        self.circuit = circuit
        self._sequences: list[TestSequence] = []
        self._auto_debug_ports = auto_debug_ports
        self._rule_names: list[str] | None = None

    @contextmanager
    def sequence(self, name: str) -> Iterator[TestSequence]:
        """Define a test sequence.

        Args:
            name: Name of the sequence.

        Yields:
            A TestSequence for adding test operations.

        Example:
            with tb.sequence("basic_count") as seq:
                seq.reset(5)
                seq.wait(10)
                seq.expect("count", 10)
        """
        seq = TestSequence(name)
        yield seq
        self._sequences.append(seq)

    def add_sequence(self, seq: TestSequence) -> Testbench:
        """Add an existing sequence to the testbench.

        Args:
            seq: The sequence to add.

        Returns:
            self for chaining.
        """
        self._sequences.append(seq)
        return self

    def get_rule_names(self, module_name: str | None = None) -> list[str]:
        """Get all rule names from the circuit.

        This collects rule names from both regular rules and procedural rules.
        Useful for auto_debug_ports functionality.

        Args:
            module_name: Optional specific module to get rules from.
                        If None, gets rules from the top-level module.

        Returns:
            List of rule names.
        """
        if self._rule_names is not None and module_name is None:
            return self._rule_names

        rule_names = []

        # Get target module
        if module_name:
            module = self.circuit._modules.get(module_name)
        elif self.circuit._modules:
            # Match SimulationWorkspace: last defined module is the user's top.
            module = list(self.circuit._modules.values())[-1]
        else:
            return []

        if module is None:
            return []

        # Collect regular rules
        rule_names.extend(module._rules.keys())

        # Collect procedural rules
        rule_names.extend(module._proc_rules.keys())

        if module_name is None:
            self._rule_names = rule_names

        return rule_names

    def add_debug_print_sequence(
        self, name: str = "debug_print_all", cycles: int = 10
    ) -> Testbench:
        """Add a sequence that prints debug port status each cycle.

        This creates a test sequence that runs for N cycles and prints
        the firing status of all rules on each cycle. Useful for debugging.

        Args:
            name: Name for the generated sequence.
            cycles: Number of cycles to run.

        Returns:
            self for chaining.
        """
        rule_names = self.get_rule_names()
        if not rule_names:
            return self

        seq = TestSequence(name)
        seq.reset(5)
        for i in range(cycles):
            seq.comment(f"Cycle {i}")
            seq.print_all_rule_status(rule_names)
            seq.wait(1)

        self._sequences.append(seq)
        return self

    def generate_cpp(self) -> str:
        """Generate C++ testbench code.

        Returns:
            C++ source code for the testbench.
        """
        top = self._get_top_module_name()
        sequences_code = []

        for seq in self._sequences:
            lines = [f"void run_{seq.name}() {{"]
            lines.append(f'    std::cout << "Running sequence: {seq.name}" << std::endl;')
            for op in seq._ops:
                for line in op.to_cpp().split("\n"):
                    lines.append(f"    {line}")
            lines.append("}")
            sequences_code.append("\n".join(lines))

        run_all_lines = ["void run_all_sequences() {"]
        for seq in self._sequences:
            run_all_lines.append(f"    run_{seq.name}();")
        run_all_lines.append("}")

        return "\n\n".join(sequences_code + ["\n".join(run_all_lines)])

    def generate_cocotb(self) -> str:
        """Generate cocotb (Python) testbench code.

        Returns:
            Python source code for the testbench.
        """
        top = self._get_top_module_name()
        lines = [
            "import cocotb",
            "from cocotb.clock import Clock",
            "from cocotb.triggers import RisingEdge, ClockCycles",
            "",
            "",
            "async def reset_dut(dut, cycles):",
            "    dut.rst.value = 1",
            "    await ClockCycles(dut.clk, cycles)",
            "    dut.rst.value = 0",
            "",
        ]

        for seq in self._sequences:
            lines.append(f"@cocotb.test()")
            lines.append(f"async def test_{seq.name}(dut):")
            lines.append(f'    """Test sequence: {seq.name}"""')
            lines.append(f"    clock = Clock(dut.clk, 10, units='ns')")
            lines.append(f"    cocotb.start_soon(clock.start())")
            lines.append("")
            for op in seq._ops:
                for line in op.to_python().split("\n"):
                    lines.append(f"    {line}")
            lines.append("")

        return "\n".join(lines)

    def _get_top_module_name(self) -> str:
        """Get the top-level module name from the circuit."""
        if self.circuit._modules:
            # Match SimulationWorkspace: last defined module is the user's top.
            return list(self.circuit._modules.keys())[-1]
        return self.circuit.name

    def interface_decl(self, name: str, module_name: str | None = None) -> Any:
        """Look up an InterfaceDecl by name (default: top module)."""
        if not self.circuit._modules:
            raise KeyError("Circuit has no modules")

        target_module_name = module_name or self._get_top_module_name()
        module = self.circuit._modules.get(target_module_name)
        if module is None:
            raise KeyError(f"No module named '{target_module_name}'")
        decls = getattr(module, "_interface_decls", {})
        if name not in decls:
            raise KeyError(f"Module '{target_module_name}' has no interface decl '{name}'")
        return decls[name]

    def __repr__(self) -> str:
        return f"Testbench({self.circuit.name!r}, sequences={len(self._sequences)})"
