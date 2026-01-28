#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""CMT2 IR Construction Utilities

This package provides Python utilities for constructing MLIR operations
using CIRCT's Python bindings. It implements the Direct MLIR Construction
design decision (see docs/Cmt2/future/IRDesign.md).

Key Classes:
    MLIRBuilder: Main entry point for creating MLIR modules and circuits.
    CMT2OpBuilder: Builder for CMT2-specific operations (rules, methods, etc.).
    TypeConverter: Convert between Python/CMT2 types and MLIR types.
    ConstantBuilder: Create constant values in MLIR.
    InsertionPointManager: Manage nested insertion points.

Context Utilities:
    get_current_context: Get the current MLIR context.
    get_current_builder: Get the current MLIR builder.
    get_or_create_builder: Get or create an MLIR builder.
    with_context: Decorator to ensure MLIR context is available.

Example:
    from cmt2.ir import MLIRBuilder, CMT2OpBuilder
    from circt.ir import InsertionPoint

    # Create a circuit
    builder = MLIRBuilder()
    
    with builder.context():
        circuit = builder.create_circuit("Counter")
        
        # Create a module
        op_builder = CMT2OpBuilder()
        with InsertionPoint(circuit.body):
            module = op_builder.create_module("Counter")
            
            # Create a rule
            with InsertionPoint(module.body):
                rule = op_builder.create_rule("increment")
                
        # Emit MLIR
        mlir_text = builder.emit_mlir()
        print(mlir_text)
"""

from __future__ import annotations

# Core builders
from ._core import (
    # Main builders
    MLIRBuilder,
    CMT2OpBuilder,
    TypeConverter,
    ConstantBuilder,
    InsertionPointManager,
    # Context utilities
    get_current_context,
    get_current_builder,
    get_or_create_builder,
    with_context,
)

__all__ = [
    # Core builders
    "MLIRBuilder",
    "CMT2OpBuilder",
    "TypeConverter",
    "ConstantBuilder",
    "InsertionPointManager",
    # Context utilities
    "get_current_context",
    "get_current_builder",
    "get_or_create_builder",
    "with_context",
]

# Version of the IR construction API
__version__ = "0.1.0"
