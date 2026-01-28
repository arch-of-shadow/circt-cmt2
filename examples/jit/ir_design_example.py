#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Example demonstrating the Direct MLIR Construction design.

This example shows how to use the cmt2.ir module to construct
CMT2 MLIR operations directly via Python bindings.

Usage:
    python examples/jit/ir_design_example.py

Requirements:
    - CIRCT Python bindings must be installed
    - See install.sh in project root for setup instructions
"""

from __future__ import annotations


def example_basic_circuit():
    """Example: Create a basic CMT2 circuit with MLIRBuilder."""
    print("=" * 60)
    print("Example 1: Basic Circuit Creation")
    print("=" * 60)

    try:
        from cmt2.ir import MLIRBuilder, CMT2OpBuilder
        from circt.ir import InsertionPoint

        # Create the MLIR builder
        builder = MLIRBuilder()

        # Enter the MLIR context
        with builder.context():
            # Create a circuit
            circuit = builder.create_circuit("Counter")
            print(f"✓ Created circuit: {circuit}")

            # Create a module inside the circuit
            op_builder = CMT2OpBuilder()
            with InsertionPoint(circuit.body):
                module = op_builder.create_module("Counter")
                print(f"✓ Created module: {module}")

                # Create a rule inside the module
                with InsertionPoint(module.body):
                    rule = op_builder.create_rule("increment")
                    print(f"✓ Created rule: {rule}")

            # Emit the MLIR
            mlir_text = builder.emit_mlir()
            print("\nGenerated MLIR:")
            print("-" * 40)
            print(mlir_text)

    except ImportError as e:
        print(f"⚠ CIRCT bindings not available: {e}")
        print("  Run ./install.sh to build and install CIRCT Python bindings")
        return False

    return True


def example_with_types():
    """Example: Type conversion and constants."""
    print("\n" + "=" * 60)
    print("Example 2: Type Conversion")
    print("=" * 60)

    try:
        from cmt2.ir import MLIRBuilder, TypeConverter, ConstantBuilder
        from circt.ir import InsertionPoint

        builder = MLIRBuilder()

        with builder.context():
            # Create type converter
            converter = TypeConverter()

            # Create FIRRTL types
            uint32 = converter.to_firrtl_uint(32)
            sint16 = converter.to_firrtl_sint(16)
            clock = converter.to_firrtl_clock()

            print(f"✓ Created FIRRTL UInt<32>: {uint32}")
            print(f"✓ Created FIRRTL SInt<16>: {sint16}")
            print(f"✓ Created FIRRTL Clock: {clock}")

            # Create constants
            const_builder = ConstantBuilder()
            with InsertionPoint(builder.mlir_module.body):
                const_op = const_builder.create_int_constant(42, 32)
                print(f"✓ Created constant: {const_op}")

            mlir_text = builder.emit_mlir()
            print("\nGenerated MLIR:")
            print("-" * 40)
            print(mlir_text)

    except ImportError as e:
        print(f"⚠ CIRCT bindings not available: {e}")
        return False

    return True


def example_module_with_interface():
    """Example: Module with methods and values."""
    print("\n" + "=" * 60)
    print("Example 3: Module with Interface")
    print("=" * 60)

    try:
        from cmt2.ir import MLIRBuilder, CMT2OpBuilder, TypeConverter
        from circt.ir import InsertionPoint

        builder = MLIRBuilder()

        with builder.context():
            circuit = builder.create_circuit("InterfaceExample")
            op_builder = CMT2OpBuilder()
            converter = TypeConverter()

            with InsertionPoint(circuit.body):
                # Create module
                module = op_builder.create_module("DataProvider")

                with InsertionPoint(module.body):
                    # Create a value method (read-only)
                    value_ret_type = converter.to_firrtl_uint(32)
                    value = op_builder.create_value(
                        name="read",
                        return_types=[value_ret_type],
                    )
                    print(f"✓ Created value method: {value}")

                    # Create an action method
                    arg_type = converter.to_firrtl_uint(32)
                    method = op_builder.create_method(
                        name="write",
                        arg_types=[arg_type],
                        return_types=[],
                    )
                    print(f"✓ Created action method: {method}")

            mlir_text = builder.emit_mlir()
            print("\nGenerated MLIR:")
            print("-" * 40)
            print(mlir_text)

    except ImportError as e:
        print(f"⚠ CIRCT bindings not available: {e}")
        return False

    return True


def example_instance_and_call():
    """Example: Module instantiation and method calls."""
    print("\n" + "=" * 60)
    print("Example 4: Instances and Calls")
    print("=" * 60)

    try:
        from cmt2.ir import MLIRBuilder, CMT2OpBuilder
        from circt.ir import InsertionPoint

        builder = MLIRBuilder()

        with builder.context():
            circuit = builder.create_circuit("InstanceExample")
            op_builder = CMT2OpBuilder()

            with InsertionPoint(circuit.body):
                # Create a register module
                reg_module = op_builder.create_module("Reg32")

                with InsertionPoint(reg_module.body):
                    # Add read/write methods
                    op_builder.create_value("read")
                    op_builder.create_method("write")

                # Create an instance of the register
                instance = op_builder.create_instance(
                    module_name="Reg32",
                    instance_name="count_reg",
                )
                print(f"✓ Created instance: {instance}")

                # Create a rule that calls the register
                rule = op_builder.create_rule("update")

                with InsertionPoint(rule.body_block):
                    # Call read method
                    call_op = op_builder.create_call(
                        instance="count_reg",
                        method="read",
                        args=[],
                    )
                    print(f"✓ Created call: {call_op}")

            mlir_text = builder.emit_mlir()
            print("\nGenerated MLIR:")
            print("-" * 40)
            print(mlir_text)

    except ImportError as e:
        print(f"⚠ CIRCT bindings not available: {e}")
        return False

    return True


def main():
    """Run all examples."""
    print("\n" + "=" * 60)
    print("CMT2 Direct MLIR Construction Examples")
    print("=" * 60)
    print()
    print("These examples demonstrate the IR design decision documented in:")
    print("  docs/Cmt2/future/IRDesign.md")
    print()
    print("Design: Direct MLIR Construction (not custom Cmt2Jaxpr)")
    print()

    results = []

    # Run examples
    results.append(("Basic Circuit", example_basic_circuit()))
    results.append(("Type Conversion", example_with_types()))
    results.append(("Module Interface", example_module_with_interface()))
    results.append(("Instances and Calls", example_instance_and_call()))

    # Summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)

    all_passed = True
    for name, passed in results:
        status = "✓ PASS" if passed else "✗ SKIP"
        print(f"  {status}: {name}")
        if not passed:
            all_passed = False

    if all_passed:
        print("\n✓ All examples completed successfully!")
    else:
        print("\n⚠ Some examples skipped (CIRCT bindings not available)")
        print("  Install CIRCT Python bindings to run all examples:")
        print("    ./install.sh")

    return 0 if all_passed else 1


if __name__ == "__main__":
    exit(main())
