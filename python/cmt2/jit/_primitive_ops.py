#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Primitive operations mapping for CMT2 tracing.

This module provides mappings from Python trace-time primitives to CMT2/MLIR
dialect operations. It handles arithmetic, logical, and comparison operations.

Example:
    from cmt2.jit._primitive_ops import PrimitiveOpRegistry, OperationType
    
    # Get the MLIR operation for addition
    add_op = PrimitiveOpRegistry.get(OperationType.ADD)
    result = add_op(trace_context, lhs_value, rhs_value, result_type)
"""

from __future__ import annotations

from enum import Enum, auto
from typing import TYPE_CHECKING, Any, Callable, Dict, List, Optional, Tuple

if TYPE_CHECKING:
    from circt.ir import Value as MlirValue, Type as MlirType
    from cmt2.types import SignalType

# CIRCT bindings availability check
try:
    from circt import ir
    from circt.dialects import comb, hw
    _HAS_CIRCT_BINDINGS = True
except ImportError:
    _HAS_CIRCT_BINDINGS = False


class OperationType(Enum):
    """Enumeration of supported primitive operations."""
    
    # Arithmetic operations
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    FLOOR_DIV = auto()
    MOD = auto()
    POW = auto()
    
    # Bitwise operations
    AND = auto()
    OR = auto()
    XOR = auto()
    INVERT = auto()
    LSHIFT = auto()
    RSHIFT = auto()
    
    # Comparison operations
    EQ = auto()
    NE = auto()
    LT = auto()
    LE = auto()
    GT = auto()
    GE = auto()
    
    # Unary operations
    NEG = auto()
    ABS = auto()
    
    # Concatenation
    CONCAT = auto()


# Type alias for operation emitter functions
OpEmitter = Callable[..., "MlirValue"]


class PrimitiveOpRegistry:
    """Registry for primitive operation emitters.
    
    This registry maps operation types to functions that emit the corresponding
    MLIR operations. It supports both signed and unsigned variants for operations
    that need them.
    
    Example:
        registry = PrimitiveOpRegistry()
        
        # Register a custom emitter
        @registry.register(OperationType.ADD)
        def emit_add(ctx, lhs, rhs, result_type):
            return comb.AddOp([lhs, rhs]).result
        
        # Get and use an emitter
        emitter = registry.get(OperationType.ADD)
        result = emitter(ctx, lhs, rhs, result_type)
    """
    
    def __init__(self):
        """Initialize the registry with default emitters."""
        self._emitters: Dict[OperationType, OpEmitter] = {}
        self._signed_emitters: Dict[OperationType, OpEmitter] = {}
        self._unsigned_emitters: Dict[OperationType, OpEmitter] = {}
        self._register_default_emitters()
    
    def register(
        self,
        op_type: OperationType,
        signed: Optional[bool] = None
    ) -> Callable[[OpEmitter], OpEmitter]:
        """Decorator to register an operation emitter.
        
        Args:
            op_type: The operation type to register.
            signed: If None, register as default. If True, register as signed
                   variant. If False, register as unsigned variant.
        
        Returns:
            The decorator function.
        """
        def decorator(emitter: OpEmitter) -> OpEmitter:
            if signed is None:
                self._emitters[op_type] = emitter
            elif signed:
                self._signed_emitters[op_type] = emitter
            else:
                self._unsigned_emitters[op_type] = emitter
            return emitter
        return decorator
    
    def get(
        self,
        op_type: OperationType,
        signed: Optional[bool] = None
    ) -> OpEmitter:
        """Get an operation emitter.
        
        Args:
            op_type: The operation type to get.
            signed: If specified, get the signed/unsigned variant.
        
        Returns:
            The emitter function.
            
        Raises:
            KeyError: If the operation type is not registered.
        """
        if signed is not None:
            if signed and op_type in self._signed_emitters:
                return self._signed_emitters[op_type]
            elif not signed and op_type in self._unsigned_emitters:
                return self._unsigned_emitters[op_type]
        
        if op_type not in self._emitters:
            raise KeyError(f"No emitter registered for {op_type}")
        
        return self._emitters[op_type]
    
    def _register_default_emitters(self) -> None:
        """Register the default operation emitters."""
        if not _HAS_CIRCT_BINDINGS:
            # Skip emitter registration if CIRCT bindings are not available
            return
        self._register_arithmetic_emitters()
        self._register_bitwise_emitters()
        self._register_comparison_emitters()
        self._register_unary_emitters()
        self._register_misc_emitters()
    
    def _register_arithmetic_emitters(self) -> None:
        """Register arithmetic operation emitters."""
        if not _HAS_CIRCT_BINDINGS:
            return
        from circt.dialects import comb
        
        @self.register(OperationType.ADD)
        def emit_add(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit addition operation (comb.add)."""
            return comb.AddOp.create(lhs, rhs).result
        
        @self.register(OperationType.SUB)
        def emit_sub(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit subtraction operation (comb.sub)."""
            return comb.SubOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.MUL)
        def emit_mul(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit multiplication operation (comb.mul)."""
            return comb.MulOp.create(lhs, rhs).result
        
        @self.register(OperationType.DIV, signed=False)
        def emit_divu(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit unsigned division operation (comb.divu)."""
            return comb.DivUOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.DIV, signed=True)
        def emit_divs(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit signed division operation (comb.divs)."""
            return comb.DivSOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.FLOOR_DIV, signed=False)
        def emit_floor_divu(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit unsigned floor division (same as divu)."""
            return comb.DivUOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.FLOOR_DIV, signed=True)
        def emit_floor_divs(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit signed floor division (comb.divs)."""
            return comb.DivSOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.MOD, signed=False)
        def emit_modu(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit unsigned modulo operation (comb.modu)."""
            return comb.ModUOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.MOD, signed=True)
        def emit_mods(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit signed modulo operation (comb.mods)."""
            return comb.ModSOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
    
    def _register_bitwise_emitters(self) -> None:
        """Register bitwise operation emitters."""
        if not _HAS_CIRCT_BINDINGS:
            return
        from circt.dialects import comb
        
        @self.register(OperationType.AND)
        def emit_and(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit bitwise AND operation (comb.and)."""
            return comb.AndOp.create(lhs, rhs).result
        
        @self.register(OperationType.OR)
        def emit_or(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit bitwise OR operation (comb.or)."""
            return comb.OrOp.create(lhs, rhs).result
        
        @self.register(OperationType.XOR)
        def emit_xor(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit bitwise XOR operation (comb.xor)."""
            return comb.XorOp.create(lhs, rhs).result
        
        @self.register(OperationType.INVERT)
        def emit_invert(ctx, operand: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit bitwise NOT operation (comb.xor with all-ones constant)."""
            from circt.ir import IntegerAttr, IntegerType
            from circt.dialects import hw
            
            # Create all-ones constant of the same width
            width = result_type.width if hasattr(result_type, 'width') else 32
            ones_value = (1 << width) - 1
            ones_type = IntegerType.get_unsigned(width)
            ones_attr = IntegerAttr.get(ones_type, ones_value)
            ones_const = hw.ConstantOp(ones_attr)
            
            # XOR with all ones gives bitwise NOT
            return comb.XorOp.create(operand, ones_const.result).result
        
        @self.register(OperationType.LSHIFT)
        def emit_lshift(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit left shift operation (comb.shl)."""
            return comb.ShlOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.RSHIFT, signed=False)
        def emit_rshiftu(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit unsigned right shift operation (comb.shru)."""
            return comb.ShrUOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
        
        @self.register(OperationType.RSHIFT, signed=True)
        def emit_rshifts(ctx, lhs: "MlirValue", rhs: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit signed right shift operation (comb.shrs)."""
            return comb.ShrSOp.create(lhs=lhs, rhs=rhs, result_type=result_type).result
    
    def _register_comparison_emitters(self) -> None:
        """Register comparison operation emitters."""
        if not _HAS_CIRCT_BINDINGS:
            return
        from circt.dialects import comb
        from circt.ir import IntegerType
        
        # Comparison result type is always i1
        def get_bool_type(ctx):
            return IntegerType.get_signless(1)
        
        @self.register(OperationType.EQ)
        def emit_eq(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit equality comparison (comb.icmp eq)."""
            return comb.EqOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.NE)
        def emit_ne(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit inequality comparison (comb.icmp ne)."""
            return comb.NeOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.LT, signed=False)
        def emit_ltu(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit unsigned less-than comparison (comb.icmp ult)."""
            return comb.LtUOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.LT, signed=True)
        def emit_lts(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit signed less-than comparison (comb.icmp slt)."""
            return comb.LtSOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.LE, signed=False)
        def emit_leu(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit unsigned less-than-or-equal comparison (comb.icmp ule)."""
            return comb.LeUOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.LE, signed=True)
        def emit_les(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit signed less-than-or-equal comparison (comb.icmp sle)."""
            return comb.LeSOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.GT, signed=False)
        def emit_gtu(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit unsigned greater-than comparison (comb.icmp ugt)."""
            return comb.GtUOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.GT, signed=True)
        def emit_gts(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit signed greater-than comparison (comb.icmp sgt)."""
            return comb.GtSOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.GE, signed=False)
        def emit_geu(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit unsigned greater-than-or-equal comparison (comb.icmp uge)."""
            return comb.GeUOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
        
        @self.register(OperationType.GE, signed=True)
        def emit_ges(ctx, lhs: "MlirValue", rhs: "MlirValue") -> "MlirValue":
            """Emit signed greater-than-or-equal comparison (comb.icmp sge)."""
            return comb.GeSOp.create(lhs, rhs, result_type=get_bool_type(ctx)).result
    
    def _register_unary_emitters(self) -> None:
        """Register unary operation emitters."""
        if not _HAS_CIRCT_BINDINGS:
            return
        from circt.dialects import comb
        from circt.ir import IntegerType, IntegerAttr
        from circt.dialects import hw
        
        @self.register(OperationType.NEG)
        def emit_neg(ctx, operand: "MlirValue", result_type: "MlirType") -> "MlirValue":
            """Emit negation operation (0 - operand)."""
            # Create zero constant
            width = result_type.width if hasattr(result_type, 'width') else 32
            zero_type = IntegerType.get_signless(width)
            zero_attr = IntegerAttr.get(zero_type, 0)
            zero_const = hw.ConstantOp(zero_attr)
            
            # Subtract from zero
            return comb.SubOp.create(zero_const.result, operand, result_type=result_type).result
    
    def _register_misc_emitters(self) -> None:
        """Register miscellaneous operation emitters."""
        if not _HAS_CIRCT_BINDINGS:
            return
        from circt.dialects import comb
        
        @self.register(OperationType.CONCAT)
        def emit_concat(ctx, *operands: "MlirValue") -> "MlirValue":
            """Emit concatenation operation (comb.concat)."""
            return comb.ConcatOp.create(*operands).result


# Global registry instance
_primitive_registry: Optional[PrimitiveOpRegistry] = None


def get_primitive_registry() -> PrimitiveOpRegistry:
    """Get the global primitive operation registry.
    
    Returns:
        The global PrimitiveOpRegistry instance.
    """
    global _primitive_registry
    if _primitive_registry is None:
        _primitive_registry = PrimitiveOpRegistry()
    return _primitive_registry


def emit_primitive_op(
    op_type: OperationType,
    ctx: Any,
    *operands: "MlirValue",
    signed: Optional[bool] = None,
    result_type: Optional["MlirType"] = None
) -> "MlirValue":
    """Emit a primitive operation.
    
    This is a convenience function that uses the global registry.
    
    Args:
        op_type: The type of operation to emit.
        ctx: The trace context.
        *operands: The operation operands.
        signed: Whether to use signed variant (for applicable ops).
        result_type: The result type (required for most operations).
    
    Returns:
        The MLIR value representing the operation result.
    
    Example:
        result = emit_primitive_op(
            OperationType.ADD,
            trace_context,
            lhs_value,
            rhs_value,
            result_type=mlir_type
        )
    """
    registry = get_primitive_registry()
    emitter = registry.get(op_type, signed=signed)
    
    if result_type is not None:
        return emitter(ctx, *operands, result_type)
    else:
        return emitter(ctx, *operands)


# Mapping from Python operator names to OperationType
OPERATOR_TO_OP_TYPE: Dict[str, OperationType] = {
    "__add__": OperationType.ADD,
    "__sub__": OperationType.SUB,
    "__mul__": OperationType.MUL,
    "__truediv__": OperationType.DIV,
    "__floordiv__": OperationType.FLOOR_DIV,
    "__mod__": OperationType.MOD,
    "__pow__": OperationType.POW,
    "__and__": OperationType.AND,
    "__or__": OperationType.OR,
    "__xor__": OperationType.XOR,
    "__invert__": OperationType.INVERT,
    "__lshift__": OperationType.LSHIFT,
    "__rshift__": OperationType.RSHIFT,
    "__eq__": OperationType.EQ,
    "__ne__": OperationType.NE,
    "__lt__": OperationType.LT,
    "__le__": OperationType.LE,
    "__gt__": OperationType.GT,
    "__ge__": OperationType.GE,
    "__neg__": OperationType.NEG,
    "__abs__": OperationType.ABS,
}


def get_op_type_for_operator(op_name: str) -> Optional[OperationType]:
    """Get the OperationType for a Python operator name.
    
    Args:
        op_name: The Python special method name (e.g., '__add__').
    
    Returns:
        The corresponding OperationType, or None if not supported.
    
    Example:
        >>> get_op_type_for_operator('__add__')
        OperationType.ADD
        >>> get_op_type_for_operator('__unknown__')
        None
    """
    return OPERATOR_TO_OP_TYPE.get(op_name)
