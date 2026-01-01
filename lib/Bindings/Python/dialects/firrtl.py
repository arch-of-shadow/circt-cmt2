#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""FIRRTL dialect Python bindings for type creation and operations."""

from __future__ import annotations

from .._mlir_libs._circt._firrtl import *
from ..ir import *
from ..dialects._ods_common import _cext as _ods_cext


def register_dialect(context):
    """Register the FIRRTL dialect with the given context."""
    from .._mlir_libs._circt import register_dialects
    register_dialects(context)


class ConstantOp:
    """Wrapper for firrtl.constant operation."""

    def __init__(self, result_type, value, *, loc=None, ip=None):
        self.result_type = result_type
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        # Create the constant operation using MLIR's generic Operation builder
        from ..ir import Operation, IntegerAttr, IntegerType

        # Get the width from the FIRRTL type
        # For now, assume it's a UInt type and get width
        self.op = Operation.create(
            "firrtl.constant",
            results=[result_type],
            attributes={"value": value},
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class _BinaryPrimOp:
    """Base class for binary primitive operations."""

    OP_NAME = None

    def __init__(self, lhs, rhs, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        # Result type is typically derived from operand types
        # For simplicity, use lhs type (this may need adjustment)
        self.op = Operation.create(
            self.OP_NAME,
            results=[lhs.type],
            operands=[lhs, rhs],
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


def _get_uint_width(firrtl_type) -> int:
    """Extract width from a FIRRTL UInt type.

    Parses the type string representation (e.g., "!firrtl.uint<32>") to get width.
    """
    import re
    type_str = str(firrtl_type)
    # Match patterns like !firrtl.uint<32> or !firrtl.sint<16>
    match = re.search(r'!firrtl\.[us]int<(\d+)>', type_str)
    if match:
        return int(match.group(1))
    # Handle analog types similarly if needed
    match = re.search(r'!firrtl\.analog<(\d+)>', type_str)
    if match:
        return int(match.group(1))
    # Default case
    return 32


class _WidenResultBinaryPrimOp:
    """Base class for binary primitive operations that widen the result by 1 bit.

    Used for add and sub operations where the result width is max(w1, w2) + 1.
    """

    OP_NAME = None

    def __init__(self, lhs, rhs, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        # Get widths from operand types
        ctx = lhs.type.context
        lhs_width = _get_uint_width(lhs.type)
        rhs_width = _get_uint_width(rhs.type)
        result_width = max(lhs_width, rhs_width) + 1
        result_type = UIntType.get(ctx, result_width)

        self.op = Operation.create(
            self.OP_NAME,
            results=[result_type],
            operands=[lhs, rhs],
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class AddPrimOp(_WidenResultBinaryPrimOp):
    OP_NAME = "firrtl.add"


class SubPrimOp(_WidenResultBinaryPrimOp):
    OP_NAME = "firrtl.sub"


class MulPrimOp:
    """Multiplication: result width is w1 + w2."""
    OP_NAME = "firrtl.mul"

    def __init__(self, lhs, rhs, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        # Get widths from operand types
        ctx = lhs.type.context
        lhs_width = _get_uint_width(lhs.type)
        rhs_width = _get_uint_width(rhs.type)
        result_width = lhs_width + rhs_width
        result_type = UIntType.get(ctx, result_width)

        self.op = Operation.create(
            self.OP_NAME,
            results=[result_type],
            operands=[lhs, rhs],
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class AndPrimOp(_BinaryPrimOp):
    OP_NAME = "firrtl.and"


class OrPrimOp(_BinaryPrimOp):
    OP_NAME = "firrtl.or"


class XorPrimOp(_BinaryPrimOp):
    OP_NAME = "firrtl.xor"


class EQPrimOp(_BinaryPrimOp):
    OP_NAME = "firrtl.eq"

    def __init__(self, lhs, rhs, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        # EQ returns UInt<1>
        ctx = lhs.type.context
        result_type = UIntType.get(ctx, 1)
        self.op = Operation.create(
            self.OP_NAME,
            results=[result_type],
            operands=[lhs, rhs],
            loc=loc,
            ip=ip,
        )


class NEQPrimOp(EQPrimOp):
    OP_NAME = "firrtl.neq"


class LTPrimOp(EQPrimOp):
    OP_NAME = "firrtl.lt"


class LEQPrimOp(EQPrimOp):
    OP_NAME = "firrtl.leq"


class GTPrimOp(EQPrimOp):
    OP_NAME = "firrtl.gt"


class GEQPrimOp(EQPrimOp):
    OP_NAME = "firrtl.geq"


class _UnaryPrimOp:
    """Base class for unary primitive operations."""

    OP_NAME = None

    def __init__(self, input, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        self.op = Operation.create(
            self.OP_NAME,
            results=[input.type],
            operands=[input],
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class NotPrimOp(_UnaryPrimOp):
    OP_NAME = "firrtl.not"


class AndRPrimOp:
    """Reduction AND operation."""
    OP_NAME = "firrtl.andr"

    def __init__(self, input, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        ctx = input.type.context
        result_type = UIntType.get(ctx, 1)
        self.op = Operation.create(
            self.OP_NAME,
            results=[result_type],
            operands=[input],
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class OrRPrimOp(AndRPrimOp):
    OP_NAME = "firrtl.orr"


class XorRPrimOp(AndRPrimOp):
    OP_NAME = "firrtl.xorr"


class ShlPrimOp:
    """Shift left by constant amount."""

    def __init__(self, input, amount, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation, IntegerAttr, IntegerType

        self.op = Operation.create(
            "firrtl.shl",
            results=[input.type],
            operands=[input],
            attributes={"amount": IntegerAttr.get(IntegerType.get_signless(32), amount)},
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class ShrPrimOp:
    """Shift right by constant amount."""

    def __init__(self, input, amount, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation, IntegerAttr, IntegerType

        self.op = Operation.create(
            "firrtl.shr",
            results=[input.type],
            operands=[input],
            attributes={"amount": IntegerAttr.get(IntegerType.get_signless(32), amount)},
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class DShlPrimOp(_BinaryPrimOp):
    """Dynamic shift left."""
    OP_NAME = "firrtl.dshl"


class DShrPrimOp(_BinaryPrimOp):
    """Dynamic shift right."""
    OP_NAME = "firrtl.dshr"


class BitsPrimOp:
    """Extract bits [hi:lo] from input."""

    def __init__(self, input, hi, lo, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation, IntegerAttr, IntegerType

        ctx = input.type.context
        result_width = hi - lo + 1
        result_type = UIntType.get(ctx, result_width)

        self.op = Operation.create(
            "firrtl.bits",
            results=[result_type],
            operands=[input],
            attributes={
                "hi": IntegerAttr.get(IntegerType.get_signless(32), hi),
                "lo": IntegerAttr.get(IntegerType.get_signless(32), lo),
            },
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class CatPrimOp(_BinaryPrimOp):
    """Concatenate two values."""
    OP_NAME = "firrtl.cat"

    def __init__(self, lhs, rhs, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        # Result width is sum of input widths
        # For simplicity, just use lhs type for now
        self.op = Operation.create(
            self.OP_NAME,
            results=[lhs.type],
            operands=[lhs, rhs],
            loc=loc,
            ip=ip,
        )


class AsUIntPrimOp(_UnaryPrimOp):
    """Cast to unsigned integer."""
    OP_NAME = "firrtl.asUInt"


class AsSIntPrimOp(_UnaryPrimOp):
    """Cast to signed integer."""
    OP_NAME = "firrtl.asSInt"


class PadPrimOp:
    """Pad to specified width."""

    def __init__(self, input, width, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation, IntegerAttr, IntegerType

        ctx = input.type.context
        result_type = UIntType.get(ctx, width)

        self.op = Operation.create(
            "firrtl.pad",
            results=[result_type],
            operands=[input],
            attributes={"amount": IntegerAttr.get(IntegerType.get_signless(32), width)},
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result


class MuxPrimOp:
    """Multiplexer: select between two values based on condition."""

    def __init__(self, sel, high, low, *, loc=None, ip=None):
        if loc is None:
            loc = Location.unknown()
        if ip is None:
            ip = InsertionPoint.current

        from ..ir import Operation

        self.op = Operation.create(
            "firrtl.mux",
            results=[high.type],
            operands=[sel, high, low],
            loc=loc,
            ip=ip,
        )

    @property
    def result(self):
        return self.op.result
