#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Hardware Control Flow Constructs for CMT2.

This module provides hardware-aware control flow constructs that generate
multiplexers and parallel hardware structures. These constructs distinguish
between:

1. **Trace-time control flow**: Python's `if/for/while` statements execute
   during elaboration to build the circuit structure.
2. **Hardware-time control flow**: `when/switch/unroll` create hardware
   structures (muxes, parallel instances) that operate at runtime.

Example:
    import cmt2
    from cmt2 import Circuit, UInt, when, otherwise, switch, unroll
    from cmt2.stl import Reg

    @cmt2.elaborate
    def design():
        circuit = Circuit("Example")
        
        with circuit.module("Top") as m:
            clk, rst = m.clock(), m.reset()
            reg = m.instance(Reg.create(circuit, 32), "reg", clk=clk, rst=rst)
            
            with m.rule("update") as r:
                with r.body() as body:
                    val = body.call(reg, "read")
                    
                    # Hardware conditional (creates mux)
                    with cmt2.when(val == 0):
                        body.call(reg, "write", body.const(1, 32))
                    with cmt2.otherwise():
                        body.call(reg, "write", body.add(val, body.const(1, 32)))
        
        return circuit
"""

from __future__ import annotations

import warnings
from contextlib import contextmanager
from typing import TYPE_CHECKING, Iterator, Any

if TYPE_CHECKING:
    from circt.pycmt2 import Signal, RegionBuilder


# =============================================================================
# Trace-Time vs Hardware-Time Warnings
# =============================================================================

class ControlFlowWarning(UserWarning):
    """Warning for potentially confusing control flow patterns."""
    pass


def _warn_python_control_flow(context: str = "") -> None:
    """Emit a warning about Python control flow at trace time.
    
    This helps users understand when they might be confusing trace-time
    (Python) control flow with hardware-time control flow.
    
    Args:
        context: Additional context about where the warning occurred.
    """
    msg = (
        "Python control flow (if/for/while) executes at trace time (elaboration), "
        "not hardware time. Use cmt2.when() for hardware conditionals. "
    )
    if context:
        msg += f"Context: {context}"
    warnings.warn(msg, ControlFlowWarning, stacklevel=3)


# =============================================================================
# Hardware Conditional: when/otherwise
# =============================================================================

class WhenContext:
    """Context manager for hardware conditional execution.
    
    Creates a multiplexer structure where operations inside the block
    are conditionally executed based on the hardware condition.
    
    This is NOT a Python if-statement - it creates actual hardware
    (multiplexers) that select between values at runtime.
    
    Example:
        with cmt2.when(reg_val == 0):
            # This creates a mux, not a Python branch
            reg.next = 1
    """
    
    def __init__(self, condition: Signal):
        """Initialize the when context.
        
        Args:
            condition: A 1-bit signal (boolean condition) controlling the mux.
        """
        self._condition = condition
        self._builder: RegionBuilder | None = None
        self._entered = False
        self._exited = False
        self._has_otherwise = False
        
    def __enter__(self) -> WhenContext:
        """Enter the when context.
        
        Returns:
            Self for potential method chaining.
        """
        self._entered = True
        
        # Get the current builder from context
        from circt.pycmt2.builders import get_current_builder
        self._builder = get_current_builder()
        
        # Store the condition on the builder for nested operations to access
        self._builder._current_when_condition = self._condition
        
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        """Exit the when context."""
        self._exited = True
        
        # Clear the condition from builder
        if self._builder is not None:
            if hasattr(self._builder, '_current_when_condition'):
                delattr(self._builder, '_current_when_condition')
            if hasattr(self._builder, '_current_when_context'):
                delattr(self._builder, '_current_when_context')
        
        return False
    
    def _check_valid(self) -> None:
        """Validate that the context is being used correctly."""
        if not self._entered:
            raise RuntimeError("when() must be used as a context manager (with statement)")


class OtherwiseContext:
    """Context manager for the else branch of a when statement.
    
    Must follow a when() block and provides the "else" case for
    the hardware conditional.
    
    Example:
        with cmt2.when(reg_val == 0):
            reg.next = 1
        with cmt2.otherwise():
            reg.next = reg + 1
    """
    
    def __init__(self, when_ctx: WhenContext):
        """Initialize the otherwise context.
        
        Args:
            when_ctx: The WhenContext this otherwise belongs to.
        """
        self._when_ctx = when_ctx
        self._builder: RegionBuilder | None = None
        self._entered = False
        
    def __enter__(self) -> OtherwiseContext:
        """Enter the otherwise context.
        
        Returns:
            Self for potential method chaining.
        """
        if not self._when_ctx._exited:
            raise RuntimeError(
                "otherwise() must follow when() in the same scope. "
                "Use: 'with when(cond): ...' followed by 'with otherwise(): ...'"
            )
        
        self._entered = True
        self._when_ctx._has_otherwise = True
        
        # Get the current builder from context
        from circt.pycmt2.builders import get_current_builder
        self._builder = get_current_builder()
        
        # The condition is the negation of the when condition
        # In hardware this creates another path in the mux tree
        if self._when_ctx._builder is not None:
            # We need to negate the condition for the else branch
            # This is handled by the builder when generating assignments
            self._builder._current_otherwise_condition = self._when_ctx._condition
        
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        """Exit the otherwise context."""
        if self._builder is not None:
            if hasattr(self._builder, '_current_otherwise_condition'):
                delattr(self._builder, '_current_otherwise_condition')
        
        return False


def when(condition: Signal) -> WhenContext:
    """Create a hardware conditional block.
    
    The when() context manager creates hardware (multiplexers) that
    conditionally select between values at runtime. This is different
    from Python's if statement which executes at elaboration time.
    
    Args:
        condition: A boolean signal (1-bit unsigned) that controls
                  the conditional. When True, assignments inside the
                  block take effect.
    
    Returns:
        A WhenContext for use in a with statement.
    
    Example:
        @cmt2.elaborate
        def design():
            circuit = Circuit("Example")
            
            with circuit.module("Top") as m:
                clk, rst = m.clock(), m.reset()
                reg = m.instance(Reg.create(circuit, 32), "reg", clk=clk, rst=rst)
                
                with m.rule("update") as r:
                    with r.body() as body:
                        val = body.call(reg, "read")
                        
                        # Hardware conditional (creates mux)
                        with cmt2.when(val == 0):
                            body.call(reg, "write", body.const(1, 32))
            
            return circuit
    
    See Also:
        otherwise: For the else branch of a when statement.
    """
    return WhenContext(condition)


def otherwise() -> OtherwiseContext:
    """Create an else branch for a when statement.
    
    The otherwise() context manager must follow a when() block and
    provides the "else" case for the hardware conditional.
    
    Returns:
        An OtherwiseContext for use in a with statement.
    
    Raises:
        RuntimeError: If not used immediately after a when() block.
    
    Example:
        with cmt2.when(reg_val == 0):
            next_val = 1
        with cmt2.otherwise():
            next_val = reg_val + 1
        
        reg.next = next_val
    
    See Also:
        when: For the if branch of the conditional.
    """
    # We need to find the most recent when context
    # This is stored on the current builder
    from circt.pycmt2.builders import get_current_builder
    builder = get_current_builder()
    
    # Look for when context on the builder
    when_ctx = getattr(builder, '_current_when_context', None)
    if when_ctx is None:
        raise RuntimeError(
            "otherwise() must follow when(). "
            "Use: with cmt2.when(condition): ... with cmt2.otherwise(): ..."
        )
    
    return OtherwiseContext(when_ctx)


# =============================================================================
# Hardware Switch Statement
# =============================================================================

class SwitchCase:
    """Represents a single case within a switch statement.
    
    A case matches a specific value and executes its body when the
    switch expression equals that value.
    
    Example:
        with cmt2.switch(opcode) as sw:
            with sw.case(0):
                result = a + b
            with sw.case(1):
                result = a - b
            with sw.default():
                result = 0
    """
    
    def __init__(self, switch_ctx: SwitchContext, value: int | None):
        """Initialize a switch case.
        
        Args:
            switch_ctx: The parent SwitchContext.
            value: The value to match, or None for default case.
        """
        self._switch_ctx = switch_ctx
        self._value = value
        self._entered = False
        
    def __enter__(self) -> SwitchCase:
        """Enter the case context."""
        self._entered = True
        
        # Get the current builder
        from circt.pycmt2.builders import get_current_builder
        builder = get_current_builder()
        
        # Store case info on builder for nested operations
        builder._current_switch_case = self
        
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        """Exit the case context."""
        from circt.pycmt2.builders import get_current_builder
        builder = get_current_builder()
        
        if hasattr(builder, '_current_switch_case'):
            delattr(builder, '_current_switch_case')
        
        return False
    
    @property
    def value(self) -> int | None:
        """Get the case value (None for default)."""
        return self._value
    
    def matches(self, expr_value: int) -> bool:
        """Check if a value matches this case.
        
        Args:
            expr_value: The value to check.
        
        Returns:
            True if the value matches this case.
        """
        if self._value is None:  # Default case
            return True
        return self._value == expr_value


class SwitchContext:
    """Context manager for a hardware switch statement.
    
    Creates a multiplexer tree that selects between multiple cases
    based on a control expression. This is more efficient than
    nested when/otherwise for multiple conditions.
    
    Example:
        with cmt2.switch(opcode) as sw:
            with sw.case(0):
                result = a + b  # ADD
            with sw.case(1):
                result = a - b  # SUB
            with sw.case(2):
                result = a * b  # MUL
            with sw.default():
                result = 0      # Default
    """
    
    def __init__(self, expression: Signal):
        """Initialize the switch context.
        
        Args:
            expression: The signal to switch on. Must be an integer type.
        """
        self._expression = expression
        self._cases: list[SwitchCase] = []
        self._builder: RegionBuilder | None = None
        self._entered = False
        self._has_default = False
        
    def __enter__(self) -> SwitchContext:
        """Enter the switch context.
        
        Returns:
            Self for method chaining (to create cases).
        """
        self._entered = True
        
        # Get the current builder
        from circt.pycmt2.builders import get_current_builder
        self._builder = get_current_builder()
        
        # Store switch context on builder for otherwise() to find
        self._builder._current_switch_context = self
        
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        """Exit the switch context."""
        # Validate that we have cases
        if not self._cases:
            warnings.warn(
                "Switch statement has no cases. Did you forget to add 'with sw.case():' blocks?",
                ControlFlowWarning,
                stacklevel=2
            )
        
        # Clean up builder state
        if self._builder is not None:
            if hasattr(self._builder, '_current_switch_context'):
                delattr(self._builder, '_current_switch_context')
        
        return False
    
    def case(self, value: int) -> SwitchCase:
        """Create a case for the given value.
        
        Args:
            value: The integer value to match.
        
        Returns:
            A SwitchCase context manager.
        
        Raises:
            ValueError: If the value is already used in another case.
        """
        # Check for duplicate values
        for existing in self._cases:
            if existing.value == value:
                raise ValueError(f"Duplicate case value: {value}")
        
        case_ctx = SwitchCase(self, value)
        self._cases.append(case_ctx)
        return case_ctx
    
    def default(self) -> SwitchCase:
        """Create the default case.
        
        Returns:
            A SwitchCase context manager for the default case.
        
        Raises:
            RuntimeError: If a default case already exists.
        """
        if self._has_default:
            raise RuntimeError("Switch statement can only have one default case")
        
        self._has_default = True
        case_ctx = SwitchCase(self, None)  # None indicates default
        self._cases.append(case_ctx)
        return case_ctx


def switch(expression: Signal) -> SwitchContext:
    """Create a hardware switch statement.
    
    The switch() context manager creates a multiplexer tree that selects
    between multiple cases based on a control expression. This is more
    efficient than nested when/otherwise statements for multi-way branching.
    
    Args:
        expression: The signal to switch on. Should be an unsigned integer.
    
    Returns:
        A SwitchContext for use in a with statement.
    
    Example:
        @cmt2.elaborate
        def alu_design():
            circuit = Circuit("ALU")
            
            with circuit.module("ALU") as m:
                a = m.input("a", UInt(32))
                b = m.input("b", UInt(32))
                opcode = m.input("opcode", UInt(2))
                result = m.output("result", UInt(32))
                
                with m.rule("compute") as r:
                    with r.body() as body:
                        # Hardware switch (creates mux tree)
                        with cmt2.switch(opcode) as sw:
                            with sw.case(0):  # ADD
                                body.assign(result, body.add(a, b))
                            with sw.case(1):  # SUB
                                body.assign(result, body.sub(a, b))
                            with sw.case(2):  # AND
                                body.assign(result, body.and_(a, b))
                            with sw.default():  # Default
                                body.assign(result, body.const(0, 32))
            
            return circuit
    
    See Also:
        case: For creating individual cases within a switch.
    """
    return SwitchContext(expression)


# =============================================================================
# Unrolled Loops
# =============================================================================

class UnrollIterator:
    """Iterator that unrolls loops at trace time.
    
    This iterator yields each value in the range and creates a separate
    hardware instance for each iteration. This is different from a 
    hardware loop which would create sequential logic.
    
    The iterations are spatially parallel (unrolled), not temporally
    sequential.
    
    Example:
        # Creates 4 parallel multipliers
        for i in cmt2.unroll(range(4)):
            products[i] = inputs[i] * coeffs[i]
    """
    
    def __init__(self, iterable: range):
        """Initialize the unroll iterator.
        
        Args:
            iterable: A range object to unroll.
        
        Raises:
            TypeError: If the iterable is not a range.
            ValueError: If the range is too large for unrolling.
        """
        if not isinstance(iterable, range):
            raise TypeError(
                f"unroll() requires a range, got {type(iterable).__name__}. "
                "For trace-time iteration over other iterables, use standard Python for."
            )
        
        # Warn about large ranges
        size = len(iterable)
        if size > 128:
            raise ValueError(
                f"Range too large for unroll: {size} elements. "
                f"Maximum is 128 to prevent hardware explosion. "
                "Consider using sequential logic or reducing the range."
            )
        
        if size > 16:
            warnings.warn(
                f"Large unroll: {size} iterations will create significant hardware. "
                f"Consider using sequential logic for large ranges.",
                ControlFlowWarning,
                stacklevel=3
            )
        
        self._range = iterable
        self._index = 0
        
    def __iter__(self) -> UnrollIterator:
        """Return self as iterator."""
        return self
    
    def __next__(self) -> int:
        """Get the next iteration value."""
        if self._index >= len(self._range):
            raise StopIteration
        
        value = self._range[self._index]
        self._index += 1
        return value
    
    def __len__(self) -> int:
        """Get the number of iterations."""
        return len(self._range)


def unroll(iterable: range) -> UnrollIterator:
    """Unroll a loop at trace time (create parallel hardware).
    
    The unroll() function creates spatially parallel hardware by replicating
    the loop body for each iteration. This is different from a hardware loop
    which would create sequential logic.
    
    Use this when you want parallel instances of hardware, not sequential
    execution over time.
    
    Args:
        iterable: A range object specifying the iterations to unroll.
                 Must be a range (not list, etc.) to ensure bounds are
                 known at elaboration time.
    
    Returns:
        An iterator that yields each value in the range.
    
    Raises:
        TypeError: If the iterable is not a range.
        ValueError: If the range is too large (>128).
    
    Example:
        @cmt2.elaborate
        def fir_filter():
            circuit = Circuit("FIRFilter")
            
            with circuit.module("FIR") as m:
                coeffs = [0.1, 0.2, 0.3, 0.2]  # Filter coefficients
                
                # Create 4 parallel multiply-accumulate units
                taps = []
                for i in cmt2.unroll(range(4)):
                    # Each iteration creates independent hardware
                    tap = m.instance(Reg.create(circuit, 32), f"tap{i}")
                    taps.append(tap)
                
                # Parallel computation
                with m.rule("compute") as r:
                    with r.body() as body:
                        products = []
                        for i in cmt2.unroll(range(4)):
                            val = body.call(taps[i], "read")
                            coeff = body.const(int(coeffs[i] * 256), 32)
                            products.append(body.mul(val, coeff))
                        
                        # Sum all products
                        result = products[0]
                        for i in cmt2.unroll(range(1, 4)):
                            result = body.add(result, products[i])
            
            return circuit
    
    Note:
        - The range bounds must be known at elaboration time.
        - Each iteration creates independent hardware (area increases).
        - For sequential loops, use procedural CMT2 features instead.
    """
    return UnrollIterator(iterable)


# =============================================================================
# Utility Functions
# =============================================================================

def is_inside_when() -> bool:
    """Check if currently inside a when() block.
    
    Returns:
        True if the current builder has an active when condition.
    """
    try:
        from circt.pycmt2.builders import get_current_builder
        builder = get_current_builder()
        return hasattr(builder, '_current_when_condition')
    except RuntimeError:
        return False


def is_inside_switch() -> bool:
    """Check if currently inside a switch() block.
    
    Returns:
        True if the current builder has an active switch context.
    """
    try:
        from circt.pycmt2.builders import get_current_builder
        builder = get_current_builder()
        return hasattr(builder, '_current_switch_context')
    except RuntimeError:
        return False


def get_active_condition() -> Signal | None:
    """Get the currently active hardware condition.
    
    Returns:
        The active when condition, or None if not in a when block.
    """
    try:
        from circt.pycmt2.builders import get_current_builder
        builder = get_current_builder()
        return getattr(builder, '_current_when_condition', None)
    except RuntimeError:
        return None


# =============================================================================
# Case factory function for convenient imports
# =============================================================================

def case(value: int) -> SwitchCase:
    """Create a switch case (for use within switch context).
    
    This is a convenience function that looks up the current switch
    context and creates a case within it. It's equivalent to calling
    sw.case(value) on the switch context.
    
    Args:
        value: The case value to match.
    
    Returns:
        A SwitchCase context manager.
    
    Raises:
        RuntimeError: If not inside a switch() block.
    
    Example:
        with cmt2.switch(opcode):
            with cmt2.case(0):  # Equivalent to sw.case(0)
                result = a + b
            with cmt2.case(1):
                result = a - b
    """
    from circt.pycmt2.builders import get_current_builder
    builder = get_current_builder()
    
    switch_ctx = getattr(builder, '_current_switch_context', None)
    if switch_ctx is None:
        raise RuntimeError(
            "case() must be used inside a switch() block. "
            "Use: with cmt2.switch(expr): with cmt2.case(val): ..."
        )
    
    return switch_ctx.case(value)
