#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from __future__ import annotations

import inspect
from dataclasses import dataclass
from typing import Any, Callable, get_args, get_origin


def _is_none_annotation(ann: Any) -> bool:
    return ann is None or ann is type(None)  # noqa: E721 (intentional)


def _unwrap_signal_annotation(ann: Any) -> Any:
    try:
        from circt.pycmt2.signals import Signal
    except Exception:
        Signal = None

    origin = get_origin(ann)
    if Signal is not None and origin is Signal:
        args = get_args(ann)
        if len(args) != 1:
            raise TypeError("Signal[T] annotations must have exactly 1 type parameter")
        return args[0]
    return ann


def cmt2_type_from_annotation(ann: Any):
    """Convert a Python annotation into a PyCMT2 `Cmt2Type` instance."""
    ann = _unwrap_signal_annotation(ann)

    try:
        from circt.pycmt2.types import Cmt2Type, UInt
    except Exception as e:
        raise RuntimeError("PyCMT2 types are required for typed JIT annotations") from e

    if isinstance(ann, Cmt2Type):
        return ann

    if ann is bool:
        return UInt(1)

    if isinstance(ann, type) and issubclass(ann, Cmt2Type):
        # For zero-arg types like ClockType, ResetType, etc.
        return ann()

    raise TypeError(
        "Expected a PyCMT2 type annotation like `UInt[32]`, `SInt[16]`, "
        "`SyncToken[UInt[32]]`, or `Bool`."
    )


def cmt2_return_types_from_annotation(ann: Any) -> list:
    """Convert a return annotation into a list of PyCMT2 `Cmt2Type`s."""
    if _is_none_annotation(ann):
        return []

    origin = get_origin(ann)
    if origin is tuple:
        args = list(get_args(ann))
        if len(args) == 2 and args[1] is Ellipsis:
            raise TypeError("Tuple return annotations must be fixed-length")
        return [cmt2_type_from_annotation(a) for a in args]

    return [cmt2_type_from_annotation(ann)]


def _collect_eval_namespaces(
    func: Callable[..., Any],
    *,
    definition_locals: dict[str, Any] | None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Collect eval namespaces for resolving string annotations.

    JIT annotations deliberately evaluate to *instances* (e.g. `UInt[32]` yields
    `UInt(32)`), so we cannot use `typing.get_type_hints()` (it insists forward
    refs resolve to types).

    `definition_locals` is required for concise `UInt[width]`-style annotations
    where `width` exists only in the definer's frame (it won't be captured as a
    closure variable when annotations are stored as strings).
    """
    globalns = getattr(func, "__globals__", {}) or {}

    localns: dict[str, Any] = {}
    closure = inspect.getclosurevars(func)
    localns.update(getattr(closure, "nonlocals", {}))
    localns.update(getattr(closure, "locals", {}))
    if definition_locals:
        # Definition locals should win.
        localns.update(definition_locals)

    return globalns, localns


def _eval_annotation(
    ann: Any,
    *,
    globalns: dict[str, Any],
    localns: dict[str, Any],
    where: str,
) -> Any:
    if ann is inspect._empty:
        return ann
    if isinstance(ann, str):
        try:
            return eval(ann, globalns, localns)  # noqa: S307 (user-authored annotations)
        except Exception as e:
            raise TypeError(f"Failed to evaluate {where} annotation {ann!r}: {e}") from e
    return ann


@dataclass(frozen=True)
class ArgProxy:
    """Signal-like proxy that resolves to the current builder's argument signal."""

    name: str

    def _resolve(self):
        from ._method_ref import _get_current_builder

        builder = _get_current_builder()
        if builder is None:
            raise RuntimeError(
                f"Cannot access argument '{self.name}' outside of guard/body context."
            )

        # Guard builders expose arguments as attributes. Body builders use `.arg(name)`.
        if hasattr(builder, self.name):
            return getattr(builder, self.name)
        if hasattr(builder, "arg"):
            return builder.arg(self.name)
        raise RuntimeError(
            f"Active builder does not expose argument '{self.name}' (missing attribute and `.arg()`)."
        )

    @property
    def value(self):
        return self._resolve().value

    @property
    def type(self):
        return self._resolve().type

    def __getitem__(self, key):
        return self._resolve().__getitem__(key)

    def __add__(self, other):
        return self._resolve().__add__(other)

    def __radd__(self, other):
        return self._resolve().__radd__(other)

    def __sub__(self, other):
        return self._resolve().__sub__(other)

    def __rsub__(self, other):
        return self._resolve().__rsub__(other)

    def __mul__(self, other):
        return self._resolve().__mul__(other)

    def __rmul__(self, other):
        return self._resolve().__rmul__(other)

    def __and__(self, other):
        return self._resolve().__and__(other)

    def __rand__(self, other):
        return self._resolve().__rand__(other)

    def __or__(self, other):
        return self._resolve().__or__(other)

    def __ror__(self, other):
        return self._resolve().__ror__(other)

    def __xor__(self, other):
        return self._resolve().__xor__(other)

    def __rxor__(self, other):
        return self._resolve().__rxor__(other)

    def __invert__(self):
        return self._resolve().__invert__()

    def __lshift__(self, other):
        return self._resolve().__lshift__(other)

    def __rshift__(self, other):
        return self._resolve().__rshift__(other)

    def __eq__(self, other):  # type: ignore[override]
        return self._resolve().__eq__(other)

    def __ne__(self, other):  # type: ignore[override]
        return self._resolve().__ne__(other)

    def __lt__(self, other):
        return self._resolve().__lt__(other)

    def __le__(self, other):
        return self._resolve().__le__(other)

    def __gt__(self, other):
        return self._resolve().__gt__(other)

    def __ge__(self, other):
        return self._resolve().__ge__(other)

    def concat(self, *others):
        return self._resolve().concat(*others)

    def reduce_and(self):
        return self._resolve().reduce_and()

    def reduce_or(self):
        return self._resolve().reduce_or()

    def reduce_xor(self):
        return self._resolve().reduce_xor()

    def as_uint(self):
        return self._resolve().as_uint()

    def as_sint(self):
        return self._resolve().as_sint()

    def pad(self, width: int):
        return self._resolve().pad(width)


def parse_typed_signature(
    func: Callable[..., Any],
    *,
    require_return: bool = True,
    definition_locals: dict[str, Any] | None = None,
):
    """Return (arg_types, return_types) from function annotations.

    `arg_types` is a list of (name, Cmt2Type) for parameters excluding the first
    context parameter.
    """
    sig = inspect.signature(func)
    params = list(sig.parameters.values())
    if not params:
        raise TypeError("Expected at least one parameter (the context object)")

    for p in params:
        if p.kind not in (
            inspect.Parameter.POSITIONAL_ONLY,
            inspect.Parameter.POSITIONAL_OR_KEYWORD,
        ):
            raise TypeError("Varargs/kwargs are not supported in JIT signatures")

    globalns, localns = _collect_eval_namespaces(func, definition_locals=definition_locals)
    raw = getattr(func, "__annotations__", {}) or {}

    arg_types = []
    for p in params[1:]:
        ann = raw.get(p.name, p.annotation)
        ann = _eval_annotation(ann, globalns=globalns, localns=localns, where=f"argument '{p.name}'")
        if ann is None or ann is inspect._empty:
            raise TypeError(f"Missing type annotation for argument '{p.name}'")
        arg_types.append((p.name, cmt2_type_from_annotation(ann)))

    ret_ann = raw.get("return", sig.return_annotation)
    ret_ann = _eval_annotation(ret_ann, globalns=globalns, localns=localns, where="return")
    if ret_ann is inspect._empty:
        if require_return:
            raise TypeError("Missing return type annotation")
        ret_ann = None
    return_types = cmt2_return_types_from_annotation(ret_ann)

    return arg_types, return_types
