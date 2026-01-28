#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Tracing infrastructure for CMT2 JIT compilation.

This module provides the core tracing functionality that intercepts Python
operations and records them as MLIR operations. It implements a proxy-based
approach where SignalTracer objects wrap MLIR values and intercept operations.

Example:
    from cmt2.jit._tracer import Cmt2Trace, SignalTracer
    from cmt2.ir import MLIRBuilder
    from cmt2.types import UInt
    
    builder = MLIRBuilder()
    with builder.context():
        trace = Cmt2Trace(builder)
        
        # Create input tracers
        a = trace.input(UInt(32), "a")
        b = trace.input(UInt(32), "b")
        
        # Operations are intercepted and recorded as MLIR
        sum_val = a + b  # Emits comb.add
        
        # Create output
        trace.output(sum_val, "result")
"""

from __future__ import annotations

import contextlib
import inspect
import sys
from contextvars import ContextVar
from typing import (
    Any,
    Callable,
    Dict,
    Generic,
    List,
    Optional,
    Set,
    Tuple,
    TypeVar,
    Union,
)

from cmt2.types import SignalType, UInt, SInt, Bits
from cmt2._type_promotion import promote_types, promote_unary, Operation as TypeOp
from cmt2.jit._primitive_ops import (
    OperationType,
    get_primitive_registry,
    get_op_type_for_operator,
)

if sys.version_info >= (3, 10):
    from typing import ParamSpec
else:
    from typing_extensions import ParamSpec

# Type variables
T = TypeVar("T")
P = ParamSpec("P")

# MLIR imports (only available when CIRCT bindings are installed)
try:
    from circt import ir
    from cmt2.ir import MLIRBuilder
    _HAS_MLIR_BINDINGS = True
except ImportError:
    _HAS_MLIR_BINDINGS = False
    # Type placeholders for type checking
    class _PlaceholderIR:  # type: ignore
        class Value: pass
        class Type: pass
    ir = _PlaceholderIR()  # type: ignore
    class MLIRBuilder: pass  # type: ignore

# Context variable for current trace
_current_trace: ContextVar[Optional["Cmt2Trace"]] = ContextVar(
    "current_cmt2_trace", default=None
)


class TracingError(Exception):
    """Exception raised for errors during CMT2 tracing."""
    pass


def get_current_trace() -> "Cmt2Trace":
    """Get the current CMT2 trace context.
    
    Returns:
        The currently active Cmt2Trace instance.
        
    Raises:
        TracingError: If not within a trace context.
    """
    trace = _current_trace.get()
    if trace is None:
        raise TracingError(
            "No active CMT2 trace. Use within Cmt2Trace context manager."
        )
    return trace


def _type_op_to_operation_type(type_op: TypeOp) -> OperationType:
    """Convert TypeOp to OperationType.
    
    Args:
        type_op: The type promotion operation.
    
    Returns:
        The corresponding primitive operation type.
    """
    mapping = {
        TypeOp.ADD: OperationType.ADD,
        TypeOp.SUB: OperationType.SUB,
        TypeOp.MUL: OperationType.MUL,
        TypeOp.DIV: OperationType.DIV,
        TypeOp.MOD: OperationType.MOD,
        TypeOp.BITWISE: OperationType.AND,  # Will be overridden by specific op
        TypeOp.LSHIFT: OperationType.LSHIFT,
        TypeOp.RSHIFT: OperationType.RSHIFT,
        TypeOp.COMPARE: OperationType.EQ,  # Will be overridden by specific op
        TypeOp.CONCAT: OperationType.CONCAT,
        TypeOp.NEG: OperationType.NEG,
        TypeOp.INVERT: OperationType.INVERT,
        TypeOp.ABS: OperationType.ABS,
    }
    return mapping.get(type_op, OperationType.ADD)


def _python_op_to_type_op(op_name: str) -> Optional[TypeOp]:
    """Convert Python operator name to TypeOp.
    
    Args:
        op_name: The Python special method name.
    
    Returns:
        The corresponding type promotion operation, or None.
    """
    mapping = {
        "__add__": TypeOp.ADD,
        "__sub__": TypeOp.SUB,
        "__mul__": TypeOp.MUL,
        "__truediv__": TypeOp.DIV,
        "__floordiv__": TypeOp.DIV,
        "__mod__": TypeOp.MOD,
        "__and__": TypeOp.BITWISE,
        "__or__": TypeOp.BITWISE,
        "__xor__": TypeOp.BITWISE,
        "__lshift__": TypeOp.LSHIFT,
        "__rshift__": TypeOp.RSHIFT,
        "__lt__": TypeOp.COMPARE,
        "__le__": TypeOp.COMPARE,
        "__gt__": TypeOp.COMPARE,
        "__ge__": TypeOp.COMPARE,
        "__eq__": TypeOp.COMPARE,
        "__ne__": TypeOp.COMPARE,
        "__neg__": TypeOp.NEG,
        "__invert__": TypeOp.INVERT,
        "__abs__": TypeOp.ABS,
    }
    return mapping.get(op_name)


class SignalTracer:
    """Proxy object that intercepts operations during tracing.
    
    SignalTracer wraps an MLIR value and intercepts Python operations,
    converting them to MLIR operations via the trace context.
    
    Attributes:
        _value: The underlying MLIR value.
        _dtype: The CMT2 signal type.
        _trace: The parent trace context.
    
    Example:
        >>> trace = Cmt2Trace(builder)
        >>> a = trace.input(UInt(32), "a")
        >>> b = trace.input(UInt(32), "b")
        >>> c = a + b  # Returns new SignalTracer with comb.add result
    """
    
    def __init__(
        self,
        value: "ir.Value",
        dtype: SignalType,
        trace: "Cmt2Trace",
    ):
        """Initialize a SignalTracer.
        
        Args:
            value: The MLIR value to wrap.
            dtype: The CMT2 signal type.
            trace: The parent trace context.
        """
        self._value = value
        self._dtype = dtype
        self._trace = trace
    
    @property
    def value(self) -> "ir.Value":
        """Get the underlying MLIR value."""
        return self._value
    
    @property
    def dtype(self) -> SignalType:
        """Get the CMT2 signal type."""
        return self._dtype
    
    @property
    def trace(self) -> "Cmt2Trace":
        """Get the parent trace context."""
        return self._trace
    
    def __repr__(self) -> str:
        return f"SignalTracer({self._dtype}, {self._value})"
    
    # Arithmetic operations
    
    def __add__(self, other: Any) -> "SignalTracer":
        """Intercept addition operation."""
        return self._trace._emit_binary_op(self, other, "__add__", OperationType.ADD)
    
    def __radd__(self, other: Any) -> "SignalTracer":
        """Intercept reverse addition operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__add__(self)
    
    def __sub__(self, other: Any) -> "SignalTracer":
        """Intercept subtraction operation."""
        return self._trace._emit_binary_op(self, other, "__sub__", OperationType.SUB)
    
    def __rsub__(self, other: Any) -> "SignalTracer":
        """Intercept reverse subtraction operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__sub__(self)
    
    def __mul__(self, other: Any) -> "SignalTracer":
        """Intercept multiplication operation."""
        return self._trace._emit_binary_op(self, other, "__mul__", OperationType.MUL)
    
    def __rmul__(self, other: Any) -> "SignalTracer":
        """Intercept reverse multiplication operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__mul__(self)
    
    def __truediv__(self, other: Any) -> "SignalTracer":
        """Intercept division operation."""
        signed = self._dtype.is_signed
        return self._trace._emit_binary_op(
            self, other, "__truediv__", OperationType.DIV, signed=signed
        )
    
    def __rtruediv__(self, other: Any) -> "SignalTracer":
        """Intercept reverse division operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__truediv__(self)
    
    def __floordiv__(self, other: Any) -> "SignalTracer":
        """Intercept floor division operation."""
        signed = self._dtype.is_signed
        return self._trace._emit_binary_op(
            self, other, "__floordiv__", OperationType.FLOOR_DIV, signed=signed
        )
    
    def __rfloordiv__(self, other: Any) -> "SignalTracer":
        """Intercept reverse floor division operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__floordiv__(self)
    
    def __mod__(self, other: Any) -> "SignalTracer":
        """Intercept modulo operation."""
        signed = self._dtype.is_signed
        return self._trace._emit_binary_op(
            self, other, "__mod__", OperationType.MOD, signed=signed
        )
    
    def __rmod__(self, other: Any) -> "SignalTracer":
        """Intercept reverse modulo operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__mod__(self)
    
    def __pow__(self, other: Any, modulo: Optional[int] = None) -> "SignalTracer":
        """Intercept power operation."""
        if modulo is not None:
            raise TracingError("Three-argument pow() not supported in CMT2 tracing")
        return self._trace._emit_binary_op(self, other, "__pow__", OperationType.POW)
    
    # Bitwise operations
    
    def __and__(self, other: Any) -> "SignalTracer":
        """Intercept bitwise AND operation."""
        return self._trace._emit_binary_op(self, other, "__and__", OperationType.AND)
    
    def __rand__(self, other: Any) -> "SignalTracer":
        """Intercept reverse bitwise AND operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__and__(self)
    
    def __or__(self, other: Any) -> "SignalTracer":
        """Intercept bitwise OR operation."""
        return self._trace._emit_binary_op(self, other, "__or__", OperationType.OR)
    
    def __ror__(self, other: Any) -> "SignalTracer":
        """Intercept reverse bitwise OR operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__or__(self)
    
    def __xor__(self, other: Any) -> "SignalTracer":
        """Intercept bitwise XOR operation."""
        return self._trace._emit_binary_op(self, other, "__xor__", OperationType.XOR)
    
    def __rxor__(self, other: Any) -> "SignalTracer":
        """Intercept reverse bitwise XOR operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__xor__(self)
    
    def __invert__(self) -> "SignalTracer":
        """Intercept bitwise NOT operation."""
        return self._trace._emit_unary_op(self, "__invert__", OperationType.INVERT)
    
    def __lshift__(self, other: Any) -> "SignalTracer":
        """Intercept left shift operation."""
        # Convert shift amount to tracer (handles Python int -> MLIR constant)
        other_tracer = self._trace.to_tracer(other)
        return self._trace._emit_binary_op(
            self, other_tracer, "__lshift__", OperationType.LSHIFT
        )
    
    def __rlshift__(self, other: Any) -> "SignalTracer":
        """Intercept reverse left shift operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__lshift__(self)
    
    def __rshift__(self, other: Any) -> "SignalTracer":
        """Intercept right shift operation."""
        signed = self._dtype.is_signed
        # Convert shift amount to tracer (handles Python int -> MLIR constant)
        other_tracer = self._trace.to_tracer(other)
        return self._trace._emit_binary_op(
            self, other_tracer, "__rshift__", OperationType.RSHIFT, signed=signed
        )
    
    def __rrshift__(self, other: Any) -> "SignalTracer":
        """Intercept reverse right shift operation."""
        other_tracer = self._trace.to_tracer(other)
        return other_tracer.__rshift__(self)
    
    # Comparison operations (return Bits(1))
    
    def __eq__(self, other: Any) -> "SignalTracer":  # type: ignore[override]
        """Intercept equality comparison."""
        return self._trace._emit_comparison_op(self, other, "__eq__", OperationType.EQ)
    
    def __ne__(self, other: Any) -> "SignalTracer":  # type: ignore[override]
        """Intercept inequality comparison."""
        return self._trace._emit_comparison_op(self, other, "__ne__", OperationType.NE)
    
    def __lt__(self, other: Any) -> "SignalTracer":
        """Intercept less-than comparison."""
        signed = self._dtype.is_signed
        return self._trace._emit_comparison_op(
            self, other, "__lt__", OperationType.LT, signed=signed
        )
    
    def __le__(self, other: Any) -> "SignalTracer":
        """Intercept less-than-or-equal comparison."""
        signed = self._dtype.is_signed
        return self._trace._emit_comparison_op(
            self, other, "__le__", OperationType.LE, signed=signed
        )
    
    def __gt__(self, other: Any) -> "SignalTracer":
        """Intercept greater-than comparison."""
        signed = self._dtype.is_signed
        return self._trace._emit_comparison_op(
            self, other, "__gt__", OperationType.GT, signed=signed
        )
    
    def __ge__(self, other: Any) -> "SignalTracer":
        """Intercept greater-than-or-equal comparison."""
        signed = self._dtype.is_signed
        return self._trace._emit_comparison_op(
            self, other, "__ge__", OperationType.GE, signed=signed
        )
    
    # Unary operations
    
    def __neg__(self) -> "SignalTracer":
        """Intercept unary negation."""
        return self._trace._emit_unary_op(self, "__neg__", OperationType.NEG)
    
    def __abs__(self) -> "SignalTracer":
        """Intercept absolute value."""
        return self._trace._emit_unary_op(self, "__abs__", OperationType.ABS)
    
    # Concatenation
    
    def concat(self, other: "SignalTracer") -> "SignalTracer":
        """Concatenate with another signal.
        
        Args:
            other: The signal to concatenate.
        
        Returns:
            A new SignalTracer representing the concatenation.
        """
        other_tracer = self._trace.to_tracer(other)
        result_dtype = self._dtype.concat(other_tracer._dtype)
        result_mlir_type = self._trace._convert_dtype_to_mlir(result_dtype)
        
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.CONCAT)
        result_value = emitter(self._trace, self._value, other_tracer._value)
        
        return SignalTracer(result_value, result_dtype, self._trace)
    
    # Bit slicing
    
    def __getitem__(self, key: Union[int, slice]) -> "SignalTracer":
        """Extract bits or bit range.
        
        Args:
            key: An integer index or slice for the bit range.
        
        Returns:
            A new SignalTracer representing the extracted bits.
        
        Example:
            >>> val = trace.input(UInt(32), "val")
            >>> bit0 = val[0]        # Extract bit 0
            >>> upper = val[16:32]   # Extract bits 16-31
        """
        from circt.dialects import comb
        from circt.ir import InsertionPoint, IntegerType
        
        if isinstance(key, int):
            # Single bit extraction
            if key < 0 or key >= self._dtype.width:
                raise IndexError(f"Bit index {key} out of range for width {self._dtype.width}")
            
            with InsertionPoint.current():
                # Use comb.extract to get the single bit
                result_value = comb.ExtractOp.create(
                    low_bit=key,
                    result_type=IntegerType.get_signless(1),
                    input=self._value
                ).result
                return SignalTracer(result_value, Bits(1), self._trace)
        
        elif isinstance(key, slice):
            # Range extraction
            start, stop, step = key.indices(self._dtype.width)
            if step != 1:
                raise TracingError("Only step=1 supported for bit slicing")
            
            width = stop - start
            if width <= 0:
                raise TracingError(f"Invalid bit slice: [{start}:{stop}]")
            
            with InsertionPoint.current():
                result_value = comb.ExtractOp.create(
                    low_bit=start,
                    result_type=IntegerType.get_signless(width),
                    input=self._value
                ).result
                # Preserve signedness for the result
                if isinstance(self._dtype, SInt):
                    result_dtype = SInt(width)
                elif isinstance(self._dtype, UInt):
                    result_dtype = UInt(width)
                else:
                    result_dtype = Bits(width)
                return SignalTracer(result_value, result_dtype, self._trace)
        
        else:
            raise TypeError(f"Invalid key type for bit slicing: {type(key)}")


class Cmt2Trace:
    """Context for building CMT2 IR through tracing.
    
    Cmt2Trace provides a context manager for tracing hardware operations.
    Within the trace context, operations on SignalTracer objects are
    intercepted and recorded as MLIR operations.
    
    Attributes:
        builder: The MLIRBuilder instance.
        values: List of traced values.
        constants: Cache of constant values.
    
    Example:
        >>> builder = MLIRBuilder()
        >>> with builder.context():
        ...     with Cmt2Trace(builder) as trace:
        ...         a = trace.input(UInt(32), "a")
        ...         b = trace.input(UInt(32), "b")
        ...         c = a + b
        ...         trace.output(c, "sum")
    """
    
    def __init__(self, builder: "MLIRBuilder"):
        """Initialize the CMT2 trace context.
        
        Args:
            builder: The MLIRBuilder instance for MLIR construction.
        """
        self.builder = builder
        self.values: List["ir.Value"] = []
        self.constants: Dict[Tuple[type, Any, int, bool], "ir.Value"] = {}
        self.inputs: Dict[str, SignalTracer] = {}
        self.outputs: Dict[str, SignalTracer] = {}
        self._token: Any = None
    
    def __enter__(self) -> "Cmt2Trace":
        """Enter the trace context.
        
        Returns:
            The trace instance for method chaining.
        """
        self._token = _current_trace.set(self)
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Exit the trace context."""
        if self._token:
            _current_trace.reset(self._token)
        return False
    
    def to_tracer(self, value: Any) -> SignalTracer:
        """Convert a Python value to a SignalTracer.
        
        Args:
            value: The value to convert. Can be:
                - SignalTracer: returned as-is
                - int: converted to constant
                - bool: converted to constant
        
        Returns:
            A SignalTracer wrapping the value.
        
        Raises:
            TracingError: If the value type is not supported.
        """
        if isinstance(value, SignalTracer):
            return value
        
        if isinstance(value, bool):
            return self._create_bool_tracer(value)
        
        if isinstance(value, int):
            return self._create_int_tracer(value)
        
        raise TracingError(
            f"Cannot convert {type(value).__name__} to SignalTracer. "
            f"Supported types: SignalTracer, int, bool"
        )
    
    def _create_int_tracer(self, value: int, width: Optional[int] = None) -> SignalTracer:
        """Create a SignalTracer for an integer constant.
        
        Args:
            value: The integer value.
            width: Optional bit width. If not provided, inferred from value.
        
        Returns:
            A SignalTracer representing the constant.
        """
        from circt.ir import IntegerAttr, IntegerType, InsertionPoint
        from circt.dialects import hw
        
        # Infer width if not provided
        if width is None:
            if value == 0:
                width = 1
            else:
                import math
                width = max(1, int(math.log2(abs(value))) + 1)
                # Add sign bit for negative values
                if value < 0:
                    width += 1
        
        # Check cache
        cache_key = (int, value, width, value < 0)
        if cache_key in self.constants:
            mlir_value = self.constants[cache_key]
            dtype = SInt(width) if value < 0 else UInt(width)
            return SignalTracer(mlir_value, dtype, self)
        
        # Create constant
        with InsertionPoint.current():
            if value < 0:
                # Signed constant
                int_ty = IntegerType.get_signed(width)
                dtype = SInt(width)
            else:
                # Unsigned constant
                int_ty = IntegerType.get_unsigned(width)
                dtype = UInt(width)
            
            attr = IntegerAttr.get(int_ty, value)
            const_op = hw.ConstantOp(attr)
            mlir_value = const_op.result
        
        self.constants[cache_key] = mlir_value
        return SignalTracer(mlir_value, dtype, self)
    
    def _create_bool_tracer(self, value: bool) -> SignalTracer:
        """Create a SignalTracer for a boolean constant.
        
        Args:
            value: The boolean value.
        
        Returns:
            A SignalTracer representing the constant (Bits(1)).
        """
        return self._create_int_tracer(1 if value else 0, width=1)
    
    def _convert_dtype_to_mlir(self, dtype: SignalType) -> "ir.Type":
        """Convert a CMT2 dtype to MLIR type.
        
        Args:
            dtype: The CMT2 signal type.
        
        Returns:
            The corresponding MLIR type.
        """
        from circt.ir import IntegerType
        
        if isinstance(dtype, SInt):
            return IntegerType.get_signed(dtype.width)
        elif isinstance(dtype, UInt):
            return IntegerType.get_unsigned(dtype.width)
        elif isinstance(dtype, Bits):
            return IntegerType.get_signless(dtype.width)
        else:
            raise TracingError(f"Unsupported dtype: {dtype}")
    
    def input(self, dtype: SignalType, name: Optional[str] = None) -> SignalTracer:
        """Create an input tracer.
        
        Args:
            dtype: The signal type for the input.
            name: Optional name for the input.
        
        Returns:
            A SignalTracer representing the input.
        """
        from circt.ir import BlockArgument, InsertionPoint
        from circt.dialects import hw
        
        mlir_type = self._convert_dtype_to_mlir(dtype)
        
        # Try to get current insertion point, fall back to module body if none
        try:
            insertion_point = InsertionPoint.current()
        except ValueError:
            # No current insertion point, use module body
            if hasattr(self.builder, '_module') and self.builder._module:
                insertion_point = InsertionPoint(self.builder._module.body)
            else:
                raise TracingError(
                    "No insertion point available. "
                    "Use within a builder.context() and with InsertionPoint(block)."
                )
        
        with insertion_point:
            # Create a wire as placeholder for input
            # In full implementation, this would be proper module input
            wire_op = hw.WireOp(mlir_type, name or "input")
            mlir_value = wire_op.result
        
        tracer = SignalTracer(mlir_value, dtype, self)
        
        if name:
            self.inputs[name] = tracer
        
        return tracer
    
    def output(self, value: SignalTracer, name: Optional[str] = None) -> None:
        """Create an output.
        
        Args:
            value: The SignalTracer to output.
            name: Optional name for the output.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import hw
        
        # Try to get current insertion point
        try:
            insertion_point = InsertionPoint.current()
        except ValueError:
            # No current insertion point, use module body
            if hasattr(self.builder, '_module') and self.builder._module:
                insertion_point = InsertionPoint(self.builder._module.body)
            else:
                raise TracingError(
                    "No insertion point available. "
                    "Use within a builder.context() and with InsertionPoint(block)."
                )
        
        with insertion_point:
            # Drive the output
            # In full implementation, this would create proper output
            if name:
                output_wire = hw.WireOp(self._convert_dtype_to_mlir(value.dtype), name)
                hw.DriveOp(output_wire.result, value.value)
        
        if name:
            self.outputs[name] = value
    
    def _emit_binary_op(
        self,
        lhs: SignalTracer,
        rhs: Any,
        op_name: str,
        op_type: OperationType,
        signed: Optional[bool] = None,
    ) -> SignalTracer:
        """Emit a binary operation.
        
        Args:
            lhs: Left-hand side operand.
            rhs: Right-hand side operand.
            op_name: Python operator name.
            op_type: Operation type.
            signed: Whether to use signed variant.
        
        Returns:
            SignalTracer representing the result.
        """
        # Convert rhs to tracer
        rhs_tracer = self.to_tracer(rhs)
        
        # Determine result type via type promotion
        type_op = _python_op_to_type_op(op_name)
        if type_op:
            result_dtype = promote_types(lhs.dtype, rhs_tracer.dtype, type_op)
        else:
            result_dtype = lhs.dtype
        
        result_mlir_type = self._convert_dtype_to_mlir(result_dtype)
        
        # Get the operation emitter
        registry = get_primitive_registry()
        emitter = registry.get(op_type, signed=signed)
        
        # Emit the operation
        result_value = emitter(self, lhs.value, rhs_tracer.value, result_mlir_type)
        
        return SignalTracer(result_value, result_dtype, self)
    
    def _emit_comparison_op(
        self,
        lhs: SignalTracer,
        rhs: Any,
        op_name: str,
        op_type: OperationType,
        signed: Optional[bool] = None,
    ) -> SignalTracer:
        """Emit a comparison operation.
        
        Args:
            lhs: Left-hand side operand.
            rhs: Right-hand side operand.
            op_name: Python operator name.
            op_type: Operation type.
            signed: Whether to use signed variant.
        
        Returns:
            SignalTracer representing the boolean result.
        """
        # Convert rhs to tracer
        rhs_tracer = self.to_tracer(rhs)
        
        # Comparison result is always Bits(1)
        result_dtype = Bits(1)
        
        # Get the operation emitter
        registry = get_primitive_registry()
        emitter = registry.get(op_type, signed=signed)
        
        # Emit the operation
        result_value = emitter(self, lhs.value, rhs_tracer.value)
        
        return SignalTracer(result_value, result_dtype, self)
    
    def _emit_unary_op(
        self,
        operand: SignalTracer,
        op_name: str,
        op_type: OperationType,
    ) -> SignalTracer:
        """Emit a unary operation.
        
        Args:
            operand: The operand.
            op_name: Python operator name.
            op_type: Operation type.
        
        Returns:
            SignalTracer representing the result.
        """
        # Determine result type via type promotion
        type_op = _python_op_to_type_op(op_name)
        if type_op:
            result_dtype = promote_unary(operand.dtype, type_op)
        else:
            result_dtype = operand.dtype
        
        result_mlir_type = self._convert_dtype_to_mlir(result_dtype)
        
        # Get the operation emitter
        registry = get_primitive_registry()
        emitter = registry.get(op_type)
        
        # Emit the operation
        result_value = emitter(self, operand.value, result_mlir_type)
        
        return SignalTracer(result_value, result_dtype, self)
    
    # Convenience methods for direct operation emission
    
    def emit_add(
        self,
        lhs: "ir.Value",
        rhs: "ir.Value",
        result_type: "ir.Type",
    ) -> "ir.Value":
        """Emit addition operation.
        
        Args:
            lhs: Left-hand side MLIR value.
            rhs: Right-hand side MLIR value.
            result_type: Result MLIR type.
        
        Returns:
            The result MLIR value.
        """
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.ADD)
        return emitter(self, lhs, rhs, result_type)
    
    def emit_sub(
        self,
        lhs: "ir.Value",
        rhs: "ir.Value",
        result_type: "ir.Type",
    ) -> "ir.Value":
        """Emit subtraction operation."""
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.SUB)
        return emitter(self, lhs, rhs, result_type)
    
    def emit_mul(
        self,
        lhs: "ir.Value",
        rhs: "ir.Value",
        result_type: "ir.Type",
    ) -> "ir.Value":
        """Emit multiplication operation."""
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.MUL)
        return emitter(self, lhs, rhs, result_type)
    
    def emit_eq(
        self,
        lhs: "ir.Value",
        rhs: "ir.Value",
    ) -> "ir.Value":
        """Emit equality comparison."""
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.EQ)
        return emitter(self, lhs, rhs)
    
    def emit_and(
        self,
        lhs: "ir.Value",
        rhs: "ir.Value",
        result_type: "ir.Type",
    ) -> "ir.Value":
        """Emit bitwise AND operation."""
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.AND)
        return emitter(self, lhs, rhs, result_type)
    
    def emit_or(
        self,
        lhs: "ir.Value",
        rhs: "ir.Value",
        result_type: "ir.Type",
    ) -> "ir.Value":
        """Emit bitwise OR operation."""
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.OR)
        return emitter(self, lhs, rhs, result_type)
    
    def emit_xor(
        self,
        lhs: "ir.Value",
        rhs: "ir.Value",
        result_type: "ir.Type",
    ) -> "ir.Value":
        """Emit bitwise XOR operation."""
        registry = get_primitive_registry()
        emitter = registry.get(OperationType.XOR)
        return emitter(self, lhs, rhs, result_type)
    
    def emit_mux(
        self,
        condition: "ir.Value",
        true_value: "ir.Value",
        false_value: "ir.Value",
        result_type: "ir.Type",
    ) -> "ir.Value":
        """Emit multiplexer operation.
        
        Args:
            condition: The condition value (1-bit).
            true_value: Value when condition is true.
            false_value: Value when condition is false.
            result_type: Result MLIR type.
        
        Returns:
            The result MLIR value.
        """
        from circt.dialects import comb
        from circt.ir import InsertionPoint
        
        with InsertionPoint.current():
            return comb.MuxOp(condition, true_value, false_value).result


def trace_function(
    fn: Callable[P, T],
    builder: "MLIRBuilder",
    *args: P.args,
    **kwargs: P.kwargs
) -> Tuple[T, "Cmt2Trace"]:
    """Trace a function and capture its operations.
    
    This is a convenience function that sets up a trace context and
    executes the function within it.
    
    Args:
        fn: The function to trace.
        builder: The MLIRBuilder instance.
        *args: Positional arguments to the function.
        **kwargs: Keyword arguments to the function.
    
    Returns:
        A tuple of (function result, trace context).
    
    Example:
        >>> def my_design(a, b):
        ...     return a + b
        >>> result, trace = trace_function(my_design, builder, 1, 2)
    """
    with Cmt2Trace(builder) as trace:
        result = fn(*args, **kwargs)
    return result, trace
