#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""MLIR construction utilities for CMT2 JIT.

This module provides Python helpers for constructing MLIR operations
using CIRCT's Python bindings. It enables direct MLIR construction
without a custom intermediate IR.

Example:
    from cmt2.ir import MLIRBuilder, CMT2OpBuilder, TypeConverter
    from circt.ir import Context, InsertionPoint

    # Create MLIR module with CMT2 circuit
    builder = MLIRBuilder()
    
    with builder.context() as ctx:
        circuit = builder.create_circuit("MyDesign")
        
        with InsertionPoint(circuit.body):
            # Create a module
            mod_builder = CMT2OpBuilder(ctx)
            module = mod_builder.create_module("Counter")
            
            # Create a rule inside the module
            with InsertionPoint(module.body):
                rule = mod_builder.create_rule("increment")
                
                # Add operations to the rule body
                with InsertionPoint(rule.body_block):
                    # ... build rule body ...
                    pass
        
        mlir_text = builder.emit_mlir()
"""

from __future__ import annotations

import contextlib
from contextvars import ContextVar
from typing import TYPE_CHECKING, Iterator, Any, Sequence

if TYPE_CHECKING:
    from circt.ir import (
        Context as MlirContext,
        Module as MlirModule,
        Block,
        Value as MlirValue,
        Type as MlirType,
        Location,
        Operation,
    )

# Context variable for current MLIR context
_current_context: ContextVar[MlirContext | None] = ContextVar(
    "current_mlir_context", default=None
)

# Context variable for current builder
_current_builder: ContextVar[MLIRBuilder | None] = ContextVar(
    "current_mlir_builder", default=None
)


def get_current_context() -> MlirContext:
    """Get the current MLIR context or raise if none.
    
    Returns:
        The currently active MLIR context.
        
    Raises:
        RuntimeError: If not within an MLIR context.
    """
    ctx = _current_context.get()
    if ctx is None:
        raise RuntimeError(
            "No active MLIR context. Use within MLIRBuilder.context() context manager."
        )
    return ctx


def get_current_builder() -> MLIRBuilder:
    """Get the current MLIR builder or raise if none.
    
    Returns:
        The currently active MLIR builder.
        
    Raises:
        RuntimeError: If not within an MLIR builder context.
    """
    builder = _current_builder.get()
    if builder is None:
        raise RuntimeError(
            "No active MLIR builder. Use within MLIRBuilder context manager."
        )
    return builder


class MLIRBuilder:
    """Helper for constructing MLIR from Python.
    
    This class manages the MLIR context, module, and provides
    convenience methods for creating CMT2 operations.
    
    Example:
        builder = MLIRBuilder()
        
        with builder.context():
            circuit = builder.create_circuit("MyDesign")
            # ... build circuit ...
            
        mlir_text = builder.emit_mlir()
    """

    def __init__(self, context: MlirContext | None = None):
        """Initialize the MLIR builder.
        
        Args:
            context: Optional existing MLIR context. If not provided,
                    a new context will be created.
        """
        self._provided_context = context
        self._ctx: MlirContext | None = None
        self._module: MlirModule | None = None
        self._location: Location | None = None
        self._cmt2_dialect: Any = None
        self._firrtl_dialect: Any = None

    @property
    def mlir_context(self) -> MlirContext:
        """Get the MLIR context."""
        if self._ctx is None:
            raise RuntimeError("MLIR context not initialized. Use context() context manager.")
        return self._ctx

    @property
    def mlir_module(self) -> MlirModule:
        """Get the MLIR module."""
        if self._module is None:
            raise RuntimeError("MLIR module not initialized. Use context() context manager.")
        return self._module

    @property
    def location(self) -> Location:
        """Get the current MLIR location."""
        if self._location is None:
            raise RuntimeError("Location not initialized. Use context() context manager.")
        return self._location

    @contextlib.contextmanager
    def context(self) -> Iterator[MlirContext]:
        """Enter an MLIR context.
        
        This context manager:
        1. Creates or uses the provided MLIR context
        2. Registers CIRCT dialects
        3. Creates a module for building
        4. Sets up the location
        
        Yields:
            The MLIR context.
            
        Example:
            builder = MLIRBuilder()
            with builder.context() as ctx:
                circuit = builder.create_circuit("Test")
        """
        from circt.ir import Context as MlirContext, Location, Module as MlirModule
        import circt

        # Use provided context or create new one
        if self._provided_context is not None:
            self._ctx = self._provided_context
            entered = False
        else:
            self._ctx = MlirContext()
            entered = True
            self._ctx.__enter__()

        # Set context variable
        token = _current_context.set(self._ctx)
        builder_token = _current_builder.set(self)

        try:
            # Register dialects
            circt.register_dialects(self._ctx)

            # Create location
            self._location = Location.unknown(self._ctx)

            # Create module
            self._module = MlirModule.create(self._location)

            # Load dialect references
            from circt.dialects import cmt2, firrtl
            self._cmt2_dialect = cmt2
            self._firrtl_dialect = firrtl

            yield self._ctx

        finally:
            _current_builder.reset(builder_token)
            _current_context.reset(token)

            if entered:
                self._ctx.__exit__(None, None, None)

    def create_circuit(self, name: str) -> "cmt2.CircuitOp":
        """Create a cmt2.circuit operation.
        
        Args:
            name: The circuit name.
            
        Returns:
            The created CircuitOp.
            
        Example:
            with builder.context():
                circuit = builder.create_circuit("MyDesign")
                # circuit is inserted into the module body
        """
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2

        with InsertionPoint(self._module.body):
            circuit = cmt2.CircuitOp(sym_name=name, loc=self._location)
            # Add entry block
            circuit.body.append()
            return circuit

    def emit_mlir(self) -> str:
        """Emit the MLIR module as text.
        
        Returns:
            The MLIR text representation.
        """
        if self._module is None:
            raise RuntimeError("No module to emit. Use context() context manager first.")
        return str(self._module)

    def clone_module(self) -> MlirModule:
        """Clone the current MLIR module.
        
        This is useful for running passes without modifying the original.
        
        Returns:
            A cloned MLIR module.
        """
        from circt.ir import Module as MlirModule, InsertionPoint

        new_module = MlirModule.create(self._location)

        with InsertionPoint(new_module.body):
            for op in self._module.body:
                op.operation.clone()

        return new_module


class CMT2OpBuilder:
    """Builder for CMT2 dialect operations.
    
    This class provides methods for creating CMT2-specific operations
    within a circuit or module.
    
    Example:
        with builder.context():
            circuit = builder.create_circuit("Top")
            
            op_builder = CMT2OpBuilder(builder.mlir_context)
            with InsertionPoint(circuit.body):
                module = op_builder.create_module("Counter")
    """

    def __init__(self, ctx: MlirContext | None = None, loc: Location | None = None):
        """Initialize the CMT2 operation builder.
        
        Args:
            ctx: The MLIR context. If None, uses the current context.
            loc: The MLIR location. If None, uses unknown location.
        """
        if ctx is None:
            ctx = get_current_context()
        self._ctx = ctx
        
        if loc is None:
            from circt.ir import Location
            loc = Location.unknown(ctx)
        self._loc = loc

    @property
    def context(self) -> MlirContext:
        """Get the MLIR context."""
        return self._ctx

    @property
    def location(self) -> Location:
        """Get the MLIR location."""
        return self._loc

    def create_module(
        self,
        name: str,
        params: dict[str, Any] | None = None,
    ) -> "cmt2.ModuleOp":
        """Create a cmt2.module operation.
        
        Args:
            name: The module name.
            params: Optional module parameters.
            
        Returns:
            The created ModuleOp.
        """
        from circt.ir import InsertionPoint, ArrayAttr, Attribute
        from circt.dialects import cmt2

        attrs = {}
        if params:
            # Convert params to MLIR attributes
            param_attrs = []
            for key, value in params.items():
                # Create parameter attribute
                attr_str = f'#cmt2.param<"{key}", {value}>'
                attr = Attribute.parse(attr_str, self._ctx)
                param_attrs.append(attr)
            attrs["parameters"] = ArrayAttr.get(param_attrs)

        module = cmt2.ModuleOp(sym_name=name, loc=self._loc)
        
        # Add entry block to the module's body region
        if len(module.body.blocks) == 0:
            module.body.append()
            
        # Set any additional attributes
        for key, value in attrs.items():
            module.attributes[key] = value
            
        return module

    def create_rule(
        self,
        name: str,
        parent: "cmt2.ModuleOp | None" = None,
    ) -> "cmt2.RuleOp":
        """Create a cmt2.rule operation.
        
        Args:
            name: The rule name.
            parent: Optional parent module. If provided, the rule is
                   inserted into the module body.
            
        Returns:
            The created RuleOp.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2

        if parent is not None:
            with InsertionPoint(parent.body):
                rule = cmt2.RuleOp(sym_name=name, loc=self._loc)
        else:
            rule = cmt2.RuleOp(sym_name=name, loc=self._loc)

        # Ensure guard and body blocks exist
        if len(rule.guard_block.operations) == 0:
            # Guard block should have a terminator
            pass
        if len(rule.body_block.operations) == 0:
            # Body block is where the rule logic goes
            pass

        return rule

    def create_method(
        self,
        name: str,
        arg_types: Sequence[MlirType] | None = None,
        return_types: Sequence[MlirType] | None = None,
        parent: "cmt2.ModuleOp | None" = None,
    ) -> "cmt2.MethodOp":
        """Create a cmt2.method operation.
        
        Args:
            name: The method name.
            arg_types: Argument types for the method.
            return_types: Return types for the method.
            parent: Optional parent module.
            
        Returns:
            The created MethodOp.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2

        arg_types = arg_types or []
        return_types = return_types or []

        if parent is not None:
            with InsertionPoint(parent.body):
                method = cmt2.MethodOp(
                    sym_name=name,
                    arg_types=arg_types,
                    result_types=return_types,
                    loc=self._loc,
                )
        else:
            method = cmt2.MethodOp(
                sym_name=name,
                arg_types=arg_types,
                result_types=return_types,
                loc=self._loc,
            )

        return method

    def create_value(
        self,
        name: str,
        return_types: Sequence[MlirType] | None = None,
        parent: "cmt2.ModuleOp | None" = None,
    ) -> "cmt2.ValueOp":
        """Create a cmt2.value operation (read-only method).
        
        Args:
            name: The value name.
            return_types: Return types.
            parent: Optional parent module.
            
        Returns:
            The created ValueOp.
        """
        from circt.ir import InsertionPoint
        from circt.dialects import cmt2

        return_types = return_types or []

        if parent is not None:
            with InsertionPoint(parent.body):
                value = cmt2.ValueOp(
                    sym_name=name,
                    result_types=return_types,
                    loc=self._loc,
                )
        else:
            value = cmt2.ValueOp(
                sym_name=name,
                result_types=return_types,
                loc=self._loc,
            )

        return value

    def create_instance(
        self,
        module_name: str,
        instance_name: str,
        parent: "cmt2.ModuleOp | None" = None,
    ) -> "cmt2.InstanceOp":
        """Create a cmt2.instance operation.
        
        Args:
            module_name: The name of the module to instantiate.
            instance_name: The instance name.
            parent: Optional parent module.
            
        Returns:
            The created InstanceOp.
        """
        from circt.ir import InsertionPoint, FlatSymbolRefAttr
        from circt.dialects import cmt2

        module_ref = FlatSymbolRefAttr.get(module_name)

        if parent is not None:
            with InsertionPoint(parent.body):
                instance = cmt2.InstanceOp(
                    instance_name=instance_name,
                    module_ref=module_ref,
                    loc=self._loc,
                )
        else:
            instance = cmt2.InstanceOp(
                instance_name=instance_name,
                module_ref=module_ref,
                loc=self._loc,
            )

        return instance

    def create_call(
        self,
        instance: str | MlirValue | None,
        method: str,
        args: Sequence[MlirValue],
        result_types: Sequence[MlirType] | None = None,
    ) -> "cmt2.CallOp":
        """Create a cmt2.call operation.
        
        Args:
            instance: The instance name, value, or None for @this.
            method: The method name to call.
            args: Arguments to pass.
            result_types: Expected result types.
            
        Returns:
            The created CallOp.
        """
        from circt.ir import FlatSymbolRefAttr
        from circt.dialects import cmt2

        result_types = result_types or []

        # Determine instance reference
        if instance is None:
            callee = FlatSymbolRefAttr.get("this")
        elif isinstance(instance, str):
            callee = FlatSymbolRefAttr.get(instance)
        else:
            # If it's a value, we need to get the symbol from it
            # This is a simplified version - real implementation may differ
            callee = FlatSymbolRefAttr.get(str(instance))

        method_sym = FlatSymbolRefAttr.get(method)

        call = cmt2.CallOp(
            result_types=result_types,
            operands=args,
            callee=callee,
            method_sym=method_sym,
            loc=self._loc,
        )

        return call


class TypeConverter:
    """Convert Python/CMT2 types to MLIR types.
    
    This class provides utilities for converting between Python types,
    CMT2 frontend types, and MLIR/FIRRTL types.
    
    Example:
        converter = TypeConverter(ctx)
        
        # Convert width to FIRRTL UInt type
        uint_type = converter.to_firrtl_uint(32)
        
        # Convert from CMT2 dtype
        mlir_type = converter.from_cmt2_dtype(UInt(32))
    """

    def __init__(self, ctx: MlirContext | None = None):
        """Initialize the type converter.
        
        Args:
            ctx: The MLIR context. If None, uses the current context.
        """
        if ctx is None:
            ctx = get_current_context()
        self._ctx = ctx

    def to_firrtl_uint(self, width: int) -> MlirType:
        """Create a FIRRTL UInt type.
        
        Args:
            width: The bit width.
            
        Returns:
            The FIRRTL UInt type.
        """
        from circt.dialects import firrtl
        return firrtl.UIntType.get(self._ctx, width)

    def to_firrtl_sint(self, width: int) -> MlirType:
        """Create a FIRRTL SInt type.
        
        Args:
            width: The bit width.
            
        Returns:
            The FIRRTL SInt type.
        """
        from circt.dialects import firrtl
        return firrtl.SIntType.get(self._ctx, width)

    def to_firrtl_clock(self) -> MlirType:
        """Create a FIRRTL Clock type.
        
        Returns:
            The FIRRTL Clock type.
        """
        from circt.dialects import firrtl
        return firrtl.ClockType.get(self._ctx)

    def to_firrtl_reset(self) -> MlirType:
        """Create a FIRRTL Reset type.
        
        Returns:
            The FIRRTL Reset type.
        """
        from circt.dialects import firrtl
        return firrtl.ResetType.get(self._ctx)

    def to_firrtl_async_reset(self) -> MlirType:
        """Create a FIRRTL AsyncReset type.
        
        Returns:
            The FIRRTL AsyncReset type.
        """
        from circt.dialects import firrtl
        return firrtl.AsyncResetType.get(self._ctx)

    def from_cmt2_dtype(self, dtype) -> MlirType:
        """Convert a CMT2 dtype to MLIR type.
        
        Args:
            dtype: A CMT2 dtype (e.g., UInt(32), SInt(16)).
            
        Returns:
            The corresponding MLIR type.
        """
        # Handle pycmt2 types
        type_name = type(dtype).__name__
        
        if type_name == "UInt":
            return self.to_firrtl_uint(dtype.bit_width())
        elif type_name == "SInt":
            return self.to_firrtl_sint(dtype.bit_width())
        elif type_name == "ClockType":
            return self.to_firrtl_clock()
        elif type_name == "ResetType":
            return self.to_firrtl_reset()
        elif type_name == "AsyncResetType":
            return self.to_firrtl_async_reset()
        else:
            raise TypeError(f"Unsupported dtype: {dtype}")

    def from_python_type(self, py_type: type) -> MlirType:
        """Convert a Python type to MLIR type.
        
        Args:
            py_type: A Python type (int, bool, etc.).
            
        Returns:
            The corresponding MLIR type.
        """
        from circt.ir import IntegerType

        if py_type is int:
            # Default to 32-bit signed integer
            return IntegerType.get_signless(32, self._ctx)
        elif py_type is bool:
            return IntegerType.get_signless(1, self._ctx)
        else:
            raise TypeError(f"Unsupported Python type: {py_type}")


class InsertionPointManager:
    """Manage MLIR insertion points for nested construction.
    
    This class helps manage insertion points when building nested
    operations (e.g., circuit -> module -> rule -> body).
    
    Example:
        with InsertionPointManager() as mgr:
            # Insertion point is at module level
            with mgr.nested_point(module.body):
                # Insertion point is now inside module
                rule = builder.create_rule("test")
                with mgr.nested_point(rule.body_block):
                    # Insertion point is now inside rule body
                    pass
    """

    def __init__(self):
        """Initialize the insertion point manager."""
        self._stack: list[Any] = []

    def __enter__(self):
        """Enter the manager context."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Exit the manager context."""
        return False

    @contextlib.contextmanager
    def nested_point(self, block: Block) -> Iterator[None]:
        """Create a nested insertion point.
        
        Args:
            block: The block to set as insertion point.
            
        Yields:
            None.
        """
        from circt.ir import InsertionPoint

        self._stack.append(block)
        with InsertionPoint(block):
            yield
        self._stack.pop()


class ConstantBuilder:
    """Builder for MLIR constants.
    
    Provides utilities for creating constant values in MLIR.
    """

    def __init__(self, ctx: MlirContext | None = None, loc: Location | None = None):
        """Initialize the constant builder.
        
        Args:
            ctx: The MLIR context. If None, uses current context.
            loc: The MLIR location. If None, uses unknown location.
        """
        if ctx is None:
            ctx = get_current_context()
        self._ctx = ctx
        
        if loc is None:
            from circt.ir import Location
            loc = Location.unknown(ctx)
        self._loc = loc

    def create_int_constant(self, value: int, width: int, signed: bool = False) -> Operation:
        """Create an integer constant.
        
        Args:
            value: The constant value.
            width: The bit width.
            signed: Whether the constant is signed.
            
        Returns:
            The constant operation.
        """
        from circt.ir import IntegerAttr, IntegerType, InsertionPoint
        from circt.dialects import firrtl

        if signed:
            int_ty = IntegerType.get_signed(width)
            firrtl_ty = firrtl.SIntType.get(self._ctx, width)
        else:
            int_ty = IntegerType.get_unsigned(width)
            firrtl_ty = firrtl.UIntType.get(self._ctx, width)

        attr = IntegerAttr.get(int_ty, value)

        with InsertionPoint.current():
            return firrtl.ConstantOp(firrtl_ty, attr, loc=self._loc)

    def create_bool_constant(self, value: bool) -> Operation:
        """Create a boolean constant.
        
        Args:
            value: The boolean value.
            
        Returns:
            The constant operation.
        """
        return self.create_int_constant(1 if value else 0, 1, signed=False)


# Utility functions
def get_or_create_builder(context: MlirContext | None = None) -> MLIRBuilder:
    """Get the current builder or create a new one.
    
    Args:
        context: Optional MLIR context.
        
    Returns:
        An MLIRBuilder instance.
    """
    try:
        return get_current_builder()
    except RuntimeError:
        return MLIRBuilder(context)


def with_context(fn):
    """Decorator to ensure function runs within an MLIR context.
    
    Example:
        @with_context
        def build_circuit(builder: MLIRBuilder):
            circuit = builder.create_circuit("Test")
            # ...
    """
    def wrapper(*args, **kwargs):
        try:
            get_current_context()
            return fn(*args, **kwargs)
        except RuntimeError:
            builder = MLIRBuilder()
            with builder.context():
                return fn(*args, **kwargs)
    return wrapper
