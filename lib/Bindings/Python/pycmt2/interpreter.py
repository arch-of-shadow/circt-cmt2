#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
GAA Interpreter for PyCMT2.

This module provides a Python-native interpreter for CMT2 circuits that
implements Guarded Atomic Actions (GAA) semantics with One-Rule-At-A-Time
(ORAAT) execution.

The interpreter can be used directly with PyCMT2 circuits without needing
to export MLIR or use the CLI tool.

Example:
    from circt.pycmt2 import Circuit, UInt, Reg
    from circt.pycmt2.interpreter import Interpreter

    circuit = Circuit("Counter")
    # ... define circuit ...

    # Create interpreter and run simulation
    interp = Interpreter(circuit)
    interp.reset()

    for _ in range(10):
        results = interp.step()
        print(f"Cycle {interp.cycle}: {interp.get_register('counter')}")
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import TYPE_CHECKING, Callable, Any
import re

if TYPE_CHECKING:
    from .circuit import Circuit


class BreakpointType(Enum):
    """Types of breakpoints supported by the interpreter."""
    RULE_FIRE = auto()
    REGISTER_WRITE = auto()
    CYCLE = auto()
    CONDITION = auto()


@dataclass
class Breakpoint:
    """A breakpoint definition."""
    id: int
    type: BreakpointType
    target: str = ""  # Rule name or register name
    cycle: int = 0  # For cycle breakpoints
    condition: Callable[['Interpreter'], bool] | None = None  # For condition breakpoints
    enabled: bool = True


@dataclass
class RuleResult:
    """Result of evaluating/executing a rule."""
    name: str
    guard_enabled: bool
    fired: bool
    blocked_by: str | None = None  # Name of higher-priority rule that blocked this one


@dataclass
class CycleTrace:
    """Trace entry for a single cycle."""
    cycle: int
    rules_fired: list[str] = field(default_factory=list)
    rules_enabled: list[str] = field(default_factory=list)
    state_before: dict[str, int] = field(default_factory=dict)
    state_after: dict[str, int] = field(default_factory=dict)


class Interpreter:
    """GAA Interpreter for CMT2 circuits.

    Implements cycle-accurate simulation with GAA semantics:
    - One-Rule-At-A-Time (ORAAT) execution
    - Conflict resolution via precedence
    - Breakpoint support
    - State inspection and modification

    The interpreter parses the MLIR representation of the circuit and
    executes it cycle by cycle.
    """

    def __init__(self, circuit: Circuit, output: Callable[[str], None] | None = None):
        """Create an interpreter for a circuit.

        Args:
            circuit: The PyCMT2 Circuit to interpret.
            output: Optional callback for output messages. Defaults to print.
        """
        self._circuit = circuit
        self._output = output or print

        # State
        self._cycle = 0
        self._registers: dict[str, int] = {}
        self._register_widths: dict[str, int] = {}
        self._register_resets: dict[str, int] = {}

        # Rules and precedence
        self._rules: list[str] = []
        self._proc_rules: list[str] = []
        self._precedence: dict[str, int] = {}  # rule name -> priority (lower = higher priority)

        # Proc FSM state
        self._proc_fsm_state: dict[str, int] = {}  # proc rule name -> current state

        # Breakpoints
        self._breakpoints: list[Breakpoint] = []
        self._next_breakpoint_id = 1

        # Tracing
        self._tracing_enabled = False
        self._traces: list[CycleTrace] = []
        self._max_traces = 1000

        # Custom guard and body callbacks
        self._guard_callbacks: dict[str, Callable[['Interpreter'], bool]] = {}
        self._body_callbacks: dict[str, Callable[['Interpreter'], None]] = {}

        # Parse the circuit
        self._parse_circuit()

    def _parse_circuit(self):
        """Parse the circuit MLIR to extract rules, registers, and precedence."""
        mlir_text = self._circuit.emit_mlir()

        # Extract precedence from anywhere in the MLIR
        # Format: {precedence = [[@rule1, @rule2], ...]}
        # Look for precedence attribute - it may contain nested brackets
        prec_match = re.search(r'precedence\s*=\s*\[(\[.*?\](?:\s*,\s*\[.*?\])*)\]', mlir_text)
        if prec_match:
            prec_content = prec_match.group(1)
            # Parse chains like [@a, @b], [@c, @d]
            chains = re.findall(r'\[([^\]]+)\]', prec_content)
            priority = 0
            for chain in chains:
                rule_names = re.findall(r'@(\w+)', chain)
                for rule_name in rule_names:
                    if rule_name not in self._precedence:
                        self._precedence[rule_name] = priority
                        priority += 1

        # Extract rules
        # cmt2.rule @name ...
        for match in re.finditer(r'cmt2\.rule\s+@(\w+)', mlir_text):
            rule_name = match.group(1)
            self._rules.append(rule_name)
            if rule_name not in self._precedence:
                self._precedence[rule_name] = len(self._precedence)

        # Extract proc.rules
        # cmt2.proc.rule @name ...
        for match in re.finditer(r'cmt2\.proc\.rule\s+@(\w+)', mlir_text):
            rule_name = match.group(1)
            self._proc_rules.append(rule_name)
            self._proc_fsm_state[rule_name] = 0  # Start idle
            if rule_name not in self._precedence:
                self._precedence[rule_name] = len(self._precedence)

        # Extract instances of register modules
        # cmt2.instance @name = @Reg...
        for match in re.finditer(r'cmt2\.instance\s+@(\w+)\s*=\s*@(Reg\d*|Wire\d*)', mlir_text):
            reg_name = match.group(1)
            reg_type = match.group(2)
            # Extract width from type name (Reg32 -> 32)
            width_match = re.search(r'(\d+)', reg_type)
            width = int(width_match.group(1)) if width_match else 32
            self._registers[reg_name] = 0
            self._register_widths[reg_name] = width
            self._register_resets[reg_name] = 0

        # Also look for external module register definitions
        for match in re.finditer(r'cmt2\.module\.extern\.firrtl\s+@(\w+)', mlir_text):
            ext_name = match.group(1)
            if 'Reg' in ext_name:
                width_match = re.search(r'(\d+)', ext_name)
                width = int(width_match.group(1)) if width_match else 32
                # Find instances of this external module
                for inst_match in re.finditer(rf'cmt2\.instance\s+@(\w+)\s*=\s*@{ext_name}', mlir_text):
                    inst_name = inst_match.group(1)
                    if inst_name not in self._registers:
                        self._registers[inst_name] = 0
                        self._register_widths[inst_name] = width
                        self._register_resets[inst_name] = 0

    @property
    def cycle(self) -> int:
        """Get the current cycle number."""
        return self._cycle

    @property
    def registers(self) -> dict[str, int]:
        """Get a copy of all register values."""
        return dict(self._registers)

    @property
    def rules(self) -> list[str]:
        """Get list of all rule names."""
        return self._rules + self._proc_rules

    def reset(self):
        """Reset the circuit to initial state."""
        self._cycle = 0
        for reg_name in self._registers:
            self._registers[reg_name] = self._register_resets.get(reg_name, 0)
        for proc_rule in self._proc_rules:
            self._proc_fsm_state[proc_rule] = 0
        self._traces.clear()

    def get_register(self, name: str) -> int | None:
        """Read a register value.

        Args:
            name: Register name.

        Returns:
            The register value, or None if not found.
        """
        return self._registers.get(name)

    def set_register(self, name: str, value: int) -> bool:
        """Set a register value (for debugging).

        Args:
            name: Register name.
            value: Value to set.

        Returns:
            True if successful, False if register not found.
        """
        if name not in self._registers:
            return False
        width = self._register_widths.get(name, 32)
        mask = (1 << width) - 1
        self._registers[name] = value & mask
        return True

    def step(self, count: int = 1) -> list[list[RuleResult]]:
        """Execute one or more cycles.

        Args:
            count: Number of cycles to execute.

        Returns:
            List of RuleResult lists, one per cycle.
        """
        all_results = []
        for _ in range(count):
            results = self._execute_cycle()
            all_results.append(results)

            # Check breakpoints
            bp = self._check_breakpoints(results)
            if bp:
                self._output(f"Breakpoint {bp.id} hit at cycle {self._cycle}")
                break

        return all_results

    def run(self, max_cycles: int = 1000000) -> Breakpoint | None:
        """Run until a breakpoint is hit or max cycles reached.

        Args:
            max_cycles: Maximum number of cycles to run.

        Returns:
            The breakpoint that was hit, or None.
        """
        for _ in range(max_cycles):
            results = self._execute_cycle()
            bp = self._check_breakpoints(results)
            if bp:
                return bp
        return None

    def _execute_cycle(self) -> list[RuleResult]:
        """Execute a single cycle with ORAAT semantics."""
        state_before = dict(self._registers) if self._tracing_enabled else {}

        # Evaluate all guards
        enabled_rules = self._evaluate_guards()

        # Resolve conflicts
        rules_to_fire = self._resolve_conflicts(enabled_rules)

        # Execute selected rules
        results = []
        for rule_name in self._rules + self._proc_rules:
            guard_enabled = rule_name in enabled_rules
            fired = rule_name in rules_to_fire
            blocked_by = None

            if guard_enabled and not fired:
                # Find which higher-priority rule blocked this one
                for other in rules_to_fire:
                    if self._precedence.get(other, 999) < self._precedence.get(rule_name, 999):
                        blocked_by = other
                        break

            results.append(RuleResult(
                name=rule_name,
                guard_enabled=guard_enabled,
                fired=fired,
                blocked_by=blocked_by
            ))

            if fired:
                self._execute_rule(rule_name)

        # Increment cycle
        self._cycle += 1

        # Add trace
        if self._tracing_enabled:
            trace = CycleTrace(
                cycle=self._cycle - 1,
                rules_fired=rules_to_fire,
                rules_enabled=enabled_rules,
                state_before=state_before,
                state_after=dict(self._registers)
            )
            self._traces.append(trace)
            if len(self._traces) > self._max_traces:
                self._traces.pop(0)

        return results

    def register_guard(self, rule_name: str, callback: Callable[['Interpreter'], bool]):
        """Register a custom guard callback for a rule.

        When the guard for this rule is evaluated, the callback will be
        called instead of using the default (always enabled).

        Args:
            rule_name: Name of the rule.
            callback: Function that takes interpreter and returns True if enabled.

        Example:
            def reset_guard(interp):
                return interp.get_register("counter") == 10

            interp.register_guard("reset_at_10", reset_guard)
        """
        self._guard_callbacks[rule_name] = callback

    def register_body(self, rule_name: str, callback: Callable[['Interpreter'], None]):
        """Register a custom body callback for a rule.

        When the rule fires, the callback will be called to execute
        the rule's effects.

        Args:
            rule_name: Name of the rule.
            callback: Function that takes interpreter and performs state updates.

        Example:
            def increment_body(interp):
                val = interp.get_register("counter")
                interp.set_register("counter", val + 1)

            interp.register_body("increment", increment_body)
        """
        self._body_callbacks[rule_name] = callback

    def _evaluate_guards(self) -> list[str]:
        """Evaluate all rule guards and return enabled rule names.

        If a custom guard callback is registered, it will be used.
        Otherwise:
        - Regular rules are assumed always enabled
        - Proc rules are enabled when FSM is idle (state 0)
        """
        enabled = []

        # Regular rules
        for rule_name in self._rules:
            if rule_name in self._guard_callbacks:
                # Use custom guard callback
                if self._guard_callbacks[rule_name](self):
                    enabled.append(rule_name)
            else:
                # Default: always enabled
                enabled.append(rule_name)

        # Proc rules - enabled when FSM is idle
        for rule_name in self._proc_rules:
            fsm_idle = self._proc_fsm_state.get(rule_name, 0) == 0
            if fsm_idle:
                if rule_name in self._guard_callbacks:
                    if self._guard_callbacks[rule_name](self):
                        enabled.append(rule_name)
                else:
                    enabled.append(rule_name)

        return enabled

    def _resolve_conflicts(self, enabled_rules: list[str]) -> list[str]:
        """Resolve conflicts using precedence.

        In ORAAT semantics, only one rule fires per cycle.
        We select the highest-priority enabled rule.
        """
        if not enabled_rules:
            return []

        # Sort by priority (lower number = higher priority)
        sorted_rules = sorted(
            enabled_rules,
            key=lambda r: self._precedence.get(r, 999)
        )

        # Return only the highest-priority rule
        return [sorted_rules[0]]

    def _execute_rule(self, rule_name: str):
        """Execute a rule's body.

        If a custom body callback is registered, it will be called.
        For proc rules, FSM state is also managed.
        """
        # For proc rules, advance FSM state
        if rule_name in self._proc_rules:
            # Simple FSM: 0 (idle) -> 1 (running) -> 0 (done)
            current_state = self._proc_fsm_state.get(rule_name, 0)
            if current_state == 0:
                # Starting - execute step
                self._proc_fsm_state[rule_name] = 0  # Single-cycle step, go back to idle

        # Execute custom body callback if registered
        if rule_name in self._body_callbacks:
            self._body_callbacks[rule_name](self)

    # =========================================================================
    # Breakpoints
    # =========================================================================

    def add_breakpoint_on_rule(self, rule_name: str) -> int:
        """Add a breakpoint that triggers when a rule fires.

        Args:
            rule_name: Name of the rule.

        Returns:
            Breakpoint ID.
        """
        bp = Breakpoint(
            id=self._next_breakpoint_id,
            type=BreakpointType.RULE_FIRE,
            target=rule_name
        )
        self._breakpoints.append(bp)
        self._next_breakpoint_id += 1
        return bp.id

    def add_breakpoint_on_register(self, register_name: str) -> int:
        """Add a breakpoint that triggers when a register is written.

        Args:
            register_name: Name of the register.

        Returns:
            Breakpoint ID.
        """
        bp = Breakpoint(
            id=self._next_breakpoint_id,
            type=BreakpointType.REGISTER_WRITE,
            target=register_name
        )
        self._breakpoints.append(bp)
        self._next_breakpoint_id += 1
        return bp.id

    def add_breakpoint_at_cycle(self, cycle: int) -> int:
        """Add a breakpoint at a specific cycle.

        Args:
            cycle: Cycle number.

        Returns:
            Breakpoint ID.
        """
        bp = Breakpoint(
            id=self._next_breakpoint_id,
            type=BreakpointType.CYCLE,
            cycle=cycle
        )
        self._breakpoints.append(bp)
        self._next_breakpoint_id += 1
        return bp.id

    def add_breakpoint_on_condition(self, condition: Callable[['Interpreter'], bool]) -> int:
        """Add a breakpoint with a custom condition.

        Args:
            condition: A function that takes the interpreter and returns True to break.

        Returns:
            Breakpoint ID.
        """
        bp = Breakpoint(
            id=self._next_breakpoint_id,
            type=BreakpointType.CONDITION,
            condition=condition
        )
        self._breakpoints.append(bp)
        self._next_breakpoint_id += 1
        return bp.id

    def remove_breakpoint(self, bp_id: int) -> bool:
        """Remove a breakpoint by ID.

        Args:
            bp_id: Breakpoint ID.

        Returns:
            True if removed, False if not found.
        """
        for i, bp in enumerate(self._breakpoints):
            if bp.id == bp_id:
                self._breakpoints.pop(i)
                return True
        return False

    def clear_breakpoints(self):
        """Remove all breakpoints."""
        self._breakpoints.clear()

    def _check_breakpoints(self, results: list[RuleResult]) -> Breakpoint | None:
        """Check if any breakpoint is hit."""
        for bp in self._breakpoints:
            if not bp.enabled:
                continue

            if bp.type == BreakpointType.RULE_FIRE:
                for result in results:
                    if result.fired and result.name == bp.target:
                        return bp

            elif bp.type == BreakpointType.CYCLE:
                if self._cycle == bp.cycle:
                    return bp

            elif bp.type == BreakpointType.CONDITION:
                if bp.condition and bp.condition(self):
                    return bp

            # TODO: REGISTER_WRITE requires tracking writes during execution

        return None

    # =========================================================================
    # Tracing
    # =========================================================================

    def enable_tracing(self, enabled: bool = True):
        """Enable or disable execution tracing.

        Args:
            enabled: Whether to enable tracing.
        """
        self._tracing_enabled = enabled

    @property
    def tracing_enabled(self) -> bool:
        """Check if tracing is enabled."""
        return self._tracing_enabled

    @property
    def traces(self) -> list[CycleTrace]:
        """Get the trace history."""
        return list(self._traces)

    def clear_traces(self):
        """Clear the trace history."""
        self._traces.clear()

    # =========================================================================
    # Output
    # =========================================================================

    def print_state(self):
        """Print current state."""
        self._output(f"=== State at cycle {self._cycle} ===")
        for name, value in sorted(self._registers.items()):
            self._output(f"  {name} = {value}")

    def print_rules(self):
        """Print rule status."""
        enabled = self._evaluate_guards()
        self._output(f"=== Rules at cycle {self._cycle} ===")
        for rule_name in self._rules + self._proc_rules:
            status = "enabled" if rule_name in enabled else "disabled"
            priority = self._precedence.get(rule_name, "?")
            self._output(f"  {rule_name}: {status} (priority {priority})")

    def print_trace(self, trace: CycleTrace):
        """Print a trace entry."""
        self._output(f"Cycle {trace.cycle}:")
        self._output(f"  Enabled: {', '.join(trace.rules_enabled) or 'none'}")
        self._output(f"  Fired: {', '.join(trace.rules_fired) or 'none'}")
        if trace.state_before != trace.state_after:
            self._output("  State changes:")
            for name in trace.state_after:
                if trace.state_before.get(name) != trace.state_after.get(name):
                    self._output(f"    {name}: {trace.state_before.get(name, '?')} -> {trace.state_after.get(name)}")
