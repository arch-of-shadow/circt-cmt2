#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Interface builders for PyCMT2.

PyCMT2 interfaces are defined at circuit scope using `cmt2.interface` and can be
declared/defined inside modules using `cmt2.interface.decl` / `cmt2.interface.def`.

This module implements:
- `InterfaceBuilder`: creates a `cmt2.interface` and defines its methods/values.
- `InterfaceDefBuilder`: builds a `cmt2.interface.def` mapping interface functions
  to underlying instance functions.
"""

from __future__ import annotations

from contextlib import contextmanager
import inspect
from typing import TYPE_CHECKING, Any, Callable, Iterator, TypeVar, get_args, get_origin

from .function_builders import MethodBuilder, ValueBuilder

if TYPE_CHECKING:
    from .circuit import Circuit
    from .types import Cmt2Type

F = TypeVar("F", bound=Callable[..., Any])


def _cmt2_type_from_annotation(ann: Any) -> "Cmt2Type":
    from .types import Cmt2Type, UInt

    if isinstance(ann, Cmt2Type):
        return ann
    if ann is bool:
        return UInt(1)
    if isinstance(ann, type) and issubclass(ann, Cmt2Type):
        return ann()
    raise TypeError("Expected a PyCMT2 type annotation like `UInt[32]` or `Bool`.")


def _cmt2_return_types_from_annotation(ann: Any) -> list["Cmt2Type"]:
    if ann is None or ann is type(None):  # noqa: E721 (intentional)
        return []
    origin = get_origin(ann)
    if origin is tuple:
        args = list(get_args(ann))
        if len(args) == 2 and args[1] is Ellipsis:
            raise TypeError("Tuple return annotations must be fixed-length")
        return [_cmt2_type_from_annotation(a) for a in args]
    return [_cmt2_type_from_annotation(ann)]


def _parse_interface_signature(
    func: Callable[..., Any],
    *,
    definition_locals: dict[str, Any] | None = None,
) -> tuple[list[tuple[str, "Cmt2Type"]], list["Cmt2Type"]]:
    sig = inspect.signature(func)
    params = list(sig.parameters.values())
    for p in params:
        if p.kind not in (
            inspect.Parameter.POSITIONAL_ONLY,
            inspect.Parameter.POSITIONAL_OR_KEYWORD,
        ):
            raise TypeError("Varargs/kwargs are not supported in interface signatures")

    globalns = getattr(func, "__globals__", {}) or {}
    localns: dict[str, Any] = {}
    closure = inspect.getclosurevars(func)
    localns.update(getattr(closure, "nonlocals", {}))
    localns.update(getattr(closure, "locals", {}))
    if definition_locals:
        localns.update(definition_locals)
    raw = getattr(func, "__annotations__", {}) or {}

    def _eval_ann(ann: Any, *, where: str) -> Any:
        if ann is inspect._empty:
            return ann
        if isinstance(ann, str):
            try:
                return eval(ann, globalns, localns)  # noqa: S307 (user-authored annotations)
            except Exception as e:
                raise TypeError(f"Failed to evaluate {where} annotation {ann!r}: {e}") from e
        return ann

    arg_types: list[tuple[str, "Cmt2Type"]] = []
    for p in params:
        ann = raw.get(p.name, p.annotation)
        ann = _eval_ann(ann, where=f"argument '{p.name}'")
        if ann is None or ann is inspect._empty:
            raise TypeError(f"Missing type annotation for argument '{p.name}'")
        arg_types.append((p.name, _cmt2_type_from_annotation(ann)))

    ret_ann = raw.get("return", sig.return_annotation)
    ret_ann = _eval_ann(ret_ann, where="return")
    if ret_ann is inspect._empty:
        raise TypeError("Missing return type annotation")
    return arg_types, _cmt2_return_types_from_annotation(ret_ann)


def _default_value_for_type(builder: Any, ty: "Cmt2Type") -> Any:
    from .types import UInt, SInt

    if isinstance(ty, UInt):
        return builder.const(0, ty.width)
    if isinstance(ty, SInt):
        return builder.const(0, ty.width).as_sint()
    raise TypeError(f"No default value strategy for interface return type {ty}")


class InterfaceBuilder:
    """Builder for a circuit-level interface.

    Example:
        with circuit.interface("Reader") as i:
            with i.value("getData", returns=[UInt(32)]) as v:
                with v.guard() as g: g.always()
                with v.body() as b: ...
    """

    def __init__(self, circuit: "Circuit", name: str | None = None):
        self._circuit = circuit
        self._name = name
        self._op = None
        self._methods: dict[str, MethodBuilder] = {}
        self._values: dict[str, ValueBuilder] = {}

        self._create_interface_op()

    @property
    def name(self) -> str:
        if self._name is None:
            from .circuit import _get_assignment_target

            # depth=5 to skip: _get_assignment_target -> name -> _create_interface_op
            # -> __init__ -> circuit.interface() -> user code
            jit_name = _get_assignment_target(depth=5)
            if jit_name:
                self._name = jit_name
            else:
                self._name = f"Interface_{id(self):x}"
        return self._name

    def _create_interface_op(self) -> None:
        from circt.ir import InsertionPoint, StringAttr
        from circt.dialects import cmt2

        with InsertionPoint(self._circuit._op.body):
            self._op = cmt2.InterfaceOp(
                sym_name=StringAttr.get(self.name),
                loc=self._circuit._ctx.location,
            )

        # Add an entry block to the interface body region.
        self._op.regions[0].blocks.append()

    @contextmanager
    def method(
        self,
        name: str | None = None,
        args: list[tuple[str, "Cmt2Type"]] | None = None,
        returns: list["Cmt2Type"] | None = None,
    ) -> Iterator[MethodBuilder]:
        builder = MethodBuilder(self, name, args or [], returns or [])
        yield builder
        builder._finalize()
        self._methods[builder.name] = builder

    @contextmanager
    def value(
        self, name: str | None = None, returns: list["Cmt2Type"] | None = None
    ) -> Iterator[ValueBuilder]:
        builder = ValueBuilder(self, name, returns or [])
        yield builder
        builder._finalize()
        self._values[builder.name] = builder

    def method_sig(self, func: F | None = None, *, name: str | None = None) -> F | Callable[[F], F]:
        """Define an interface method signature from a typed Python function.

        Interface methods are signatures (types) used for lowering and port
        generation. This helper creates a trivial always-ready stub body so the
        IR is structurally valid.
        """

        frame = inspect.currentframe()
        definition_locals = None
        if frame is not None and frame.f_back is not None:
            definition_locals = dict(frame.f_back.f_locals)
        del frame

        def deco(f: F) -> F:
            meth_name = name or f.__name__
            arg_types, ret_types = _parse_interface_signature(f, definition_locals=definition_locals)
            with self.method(meth_name, args=arg_types, returns=ret_types) as m:
                with m.guard() as g:
                    g.always()
                with m.body() as b:
                    if ret_types:
                        b.returns(*[_default_value_for_type(b, t) for t in ret_types])
                    else:
                        b.returns()
            return f

        if func is not None:
            return deco(func)
        return deco

    def value_sig(self, func: F | None = None, *, name: str | None = None) -> F | Callable[[F], F]:
        """Define an interface value signature from a typed Python function."""

        frame = inspect.currentframe()
        definition_locals = None
        if frame is not None and frame.f_back is not None:
            definition_locals = dict(frame.f_back.f_locals)
        del frame

        def deco(f: F) -> F:
            val_name = name or f.__name__
            arg_types, ret_types = _parse_interface_signature(f, definition_locals=definition_locals)
            if arg_types:
                raise TypeError("Interface values cannot take arguments")
            with self.value(val_name, returns=ret_types) as v:
                with v.guard() as g:
                    g.always()
                with v.body() as b:
                    if ret_types:
                        b.returns(*[_default_value_for_type(b, t) for t in ret_types])
                    else:
                        b.returns()
            return f

        if func is not None:
            return deco(func)
        return deco

    def get_function(self, name: str) -> MethodBuilder | ValueBuilder | None:
        return self._methods.get(name) or self._values.get(name)

    def _finalize(self) -> None:
        # Nothing to do yet; methods/values are finalized as they are created.
        return None


class InterfaceDefBuilder:
    """Helper for building a `cmt2.interface.def` mapping list."""

    def __init__(self, module_builder: Any, name: str, op: Any, interface: InterfaceBuilder):
        self._module_builder = module_builder
        self._name = name
        self._op = op
        self._interface = interface
        self._mappings: list[tuple[str, str, str]] = []

    @property
    def name(self) -> str:
        return self._name

    @property
    def interface(self) -> InterfaceBuilder:
        return self._interface

    def bind(self, target: Any, target_func: Any, interface_func: Any) -> "InterfaceDefBuilder":
        """Bind an interface function to a target instance/decl function.

        Args:
            target: `Instance`, `ExternalInstance`, `InterfaceDecl`, or string symbol.
            target_func: `MethodRef`/`ValueRef` or string name on the target.
            interface_func: Name of the interface method/value.
        """
        # Unwrap common wrappers (e.g. JIT SignalRef/InterfaceRef) without
        # taking a dependency on those packages.
        if not hasattr(target, "name") and hasattr(target, "_instance"):
            target = getattr(target, "_instance")

        target_name = getattr(target, "name", None) or getattr(target, "_name", None) or target
        if not isinstance(target_name, str):
            raise TypeError(f"Expected target name to be str-like, got {type(target).__name__}")

        func_name = getattr(target_func, "name", None) or getattr(target_func, "__name__", None) or target_func
        if not isinstance(func_name, str):
            raise TypeError(
                f"Expected target_func to be MethodRef/ValueRef or str, got {type(target_func).__name__}"
            )

        iface_name = (
            getattr(interface_func, "name", None)
            or getattr(interface_func, "__name__", None)
            or interface_func
        )
        if not isinstance(iface_name, str):
            raise TypeError(
                f"Expected interface_func to be a function-like or str, got {type(interface_func).__name__}"
            )

        if iface_name not in self._interface._methods and iface_name not in self._interface._values:
            raise KeyError(
                f"Interface '{self._interface.name}' has no function '{iface_name}'"
            )

        self._mappings.append((target_name, func_name, iface_name))
        self._flush()
        return self

    def _flush(self) -> None:
        from circt.ir import ArrayAttr, FlatSymbolRefAttr

        ctx = self._module_builder._circuit._ctx.mlir_context
        entries = []
        for target_name, func_name, iface_name in self._mappings:
            entries.append(
                ArrayAttr.get(
                    [
                        FlatSymbolRefAttr.get(target_name, context=ctx),
                        FlatSymbolRefAttr.get(func_name, context=ctx),
                        FlatSymbolRefAttr.get(iface_name, context=ctx),
                    ],
                    context=ctx,
                )
            )
        self._op.attributes["methods"] = ArrayAttr.get(entries, context=ctx)
