#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 type system for PyCMT2 EDSL."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from circt.ir import Type as MlirType, Context as MlirContext


@dataclass(frozen=True)
class Cmt2Type(ABC):
    """Base class for all CMT2 types."""

    @abstractmethod
    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        """Convert to FIRRTL MLIR type."""
        ...

    @abstractmethod
    def bit_width(self) -> int:
        """Return the bit width of this type."""
        ...


@dataclass(frozen=True)
class UInt(Cmt2Type):
    """Unsigned integer type with static width."""

    width: int

    def __post_init__(self):
        if self.width <= 0:
            raise ValueError(f"Width must be positive, got {self.width}")

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        from circt.dialects import firrtl
        return firrtl.UIntType.get(ctx, self.width)

    def bit_width(self) -> int:
        return self.width

    def __repr__(self) -> str:
        return f"UInt({self.width})"


@dataclass(frozen=True)
class SInt(Cmt2Type):
    """Signed integer type with static width."""

    width: int

    def __post_init__(self):
        if self.width <= 0:
            raise ValueError(f"Width must be positive, got {self.width}")

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        from circt.dialects import firrtl
        return firrtl.SIntType.get(ctx, self.width)

    def bit_width(self) -> int:
        return self.width

    def __repr__(self) -> str:
        return f"SInt({self.width})"


@dataclass(frozen=True)
class ClockType(Cmt2Type):
    """Clock signal type."""

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        from circt.dialects import firrtl
        return firrtl.ClockType.get(ctx)

    def bit_width(self) -> int:
        return 1

    def __repr__(self) -> str:
        return "Clock"


@dataclass(frozen=True)
class ResetType(Cmt2Type):
    """Synchronous reset type (1-bit unsigned)."""

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        from circt.dialects import firrtl
        return firrtl.UIntType.get(ctx, 1)

    def bit_width(self) -> int:
        return 1

    def __repr__(self) -> str:
        return "Reset"


@dataclass(frozen=True)
class AsyncResetType(Cmt2Type):
    """Asynchronous reset type."""

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        from circt.dialects import firrtl
        return firrtl.AsyncResetType.get(ctx)

    def bit_width(self) -> int:
        return 1

    def __repr__(self) -> str:
        return "AsyncReset"


@dataclass(frozen=True)
class Bundle(Cmt2Type):
    """Bundle (struct) type with named fields.

    Fields are specified as tuples of (name, type, is_flip).
    """

    fields: tuple[tuple[str, Cmt2Type, bool], ...]

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        from circt.dialects import firrtl
        field_infos = [
            (name, ty.to_firrtl_type(ctx), flip)
            for name, ty, flip in self.fields
        ]
        return firrtl.BundleType.get(ctx, field_infos)

    def bit_width(self) -> int:
        return sum(ty.bit_width() for _, ty, _ in self.fields)

    def __repr__(self) -> str:
        field_strs = [
            f"{'flip ' if flip else ''}{name}: {ty}"
            for name, ty, flip in self.fields
        ]
        return f"Bundle({{{', '.join(field_strs)}}})"


@dataclass(frozen=True)
class Vector(Cmt2Type):
    """Vector (array) type."""

    element: Cmt2Type
    size: int

    def __post_init__(self):
        if self.size <= 0:
            raise ValueError(f"Size must be positive, got {self.size}")

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        from circt.dialects import firrtl
        return firrtl.VectorType.get(self.element.to_firrtl_type(ctx), self.size)

    def bit_width(self) -> int:
        return self.element.bit_width() * self.size

    def __repr__(self) -> str:
        return f"Vector({self.element}, {self.size})"


@dataclass(frozen=True)
class SyncToken(Cmt2Type):
    """Synchronization token type for dataflow pipelines.

    Tokens carry optional data payloads and synchronize pipeline stages.
    They support two modes:
    - LS (Latency Sensitive): Implemented as shift registers
    - LI (Latency Insensitive): Implemented as FIFOs

    Args:
        data_type: Optional data type carried by the token (default: None for void token)
        mode: Token mode - "ls" or "li" (default: "ls")

    Example:
        # Token without data
        void_token = SyncToken()

        # Token carrying 32-bit data
        data_token = SyncToken(UInt(32))

        # Latency-insensitive token
        li_token = SyncToken(UInt(32), mode="li")
    """

    data_type: Cmt2Type | None = None
    mode: str = "ls"  # "ls" or "li"

    def __post_init__(self):
        if self.mode not in ("ls", "li"):
            raise ValueError(f"Mode must be 'ls' or 'li', got '{self.mode}'")

    def to_firrtl_type(self, ctx: MlirContext) -> MlirType:
        """Convert to CMT2 SyncTokenType MLIR type."""
        from circt.ir import Type

        # Build type string
        parts = []
        if self.data_type is not None:
            data_mlir = self.data_type.to_firrtl_type(ctx)
            parts.append(f"data = {data_mlir}")
        if self.mode == "li":
            parts.append("mode = li")

        if parts:
            type_str = f"!cmt2.sync_token<{', '.join(parts)}>"
        else:
            type_str = "!cmt2.sync_token"

        return Type.parse(type_str, context=ctx)

    def bit_width(self) -> int:
        """Bit width of the data payload (1 for void token)."""
        if self.data_type is None:
            return 1  # Just valid bit
        return self.data_type.bit_width()

    def has_data(self) -> bool:
        """Return True if this token carries data."""
        return self.data_type is not None

    def is_latency_insensitive(self) -> bool:
        """Return True if this is a latency-insensitive token."""
        return self.mode == "li"

    def __repr__(self) -> str:
        parts = []
        if self.data_type is not None:
            parts.append(f"data={self.data_type}")
        if self.mode != "ls":
            parts.append(f"mode={self.mode}")
        if parts:
            return f"SyncToken({', '.join(parts)})"
        return "SyncToken()"


# Singleton instances for common types
Clock = ClockType()
Reset = ResetType()
AsyncReset = AsyncResetType()
Bool = UInt(1)


def bundle(**fields: Cmt2Type | tuple[Cmt2Type, bool]) -> Bundle:
    """Create a bundle type with named fields.

    Args:
        **fields: Field names mapped to types or (type, is_flip) tuples.

    Example:
        valid_ready = bundle(valid=Bool, ready=(Bool, True), data=UInt(32))
    """
    field_list = []
    for name, spec in fields.items():
        if isinstance(spec, tuple):
            ty, flip = spec
        else:
            ty, flip = spec, False
        field_list.append((name, ty, flip))
    return Bundle(tuple(field_list))
