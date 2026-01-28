#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Tests for CMT2 tracer implementation.

This module contains unit tests for the SignalTracer and Cmt2Trace classes.
"""

import unittest
from typing import Optional

from cmt2.types import UInt, SInt, Bits
from cmt2.jit._primitive_ops import (
    OperationType,
    get_primitive_registry,
    get_op_type_for_operator,
    OPERATOR_TO_OP_TYPE,
)
from cmt2.jit._tracer import (
    SignalTracer,
    Cmt2Trace,
    TracingError,
    get_current_trace,
    _python_op_to_type_op,
)


class TestOperationType(unittest.TestCase):
    """Test OperationType enum."""
    
    def test_operation_type_values(self):
        """Test that all operation types exist."""
        self.assertIsNotNone(OperationType.ADD)
        self.assertIsNotNone(OperationType.SUB)
        self.assertIsNotNone(OperationType.MUL)
        self.assertIsNotNone(OperationType.DIV)
        self.assertIsNotNone(OperationType.AND)
        self.assertIsNotNone(OperationType.OR)
        self.assertIsNotNone(OperationType.XOR)
        self.assertIsNotNone(OperationType.EQ)
        self.assertIsNotNone(OperationType.LT)
        self.assertIsNotNone(OperationType.NEG)


class TestPrimitiveOpRegistry(unittest.TestCase):
    """Test PrimitiveOpRegistry."""
    
    def test_get_primitive_registry(self):
        """Test getting the global registry."""
        from cmt2.jit._primitive_ops import _HAS_CIRCT_BINDINGS
        
        registry = get_primitive_registry()
        self.assertIsNotNone(registry)
        
        # Getting again returns same instance
        registry2 = get_primitive_registry()
        self.assertIs(registry, registry2)
    
    def test_get_emitter(self):
        """Test getting operation emitters."""
        from cmt2.jit._primitive_ops import _HAS_CIRCT_BINDINGS
        
        registry = get_primitive_registry()
        
        if not _HAS_CIRCT_BINDINGS:
            self.skipTest("CIRCT bindings not available")
        
        # Test getting various emitters
        add_emitter = registry.get(OperationType.ADD)
        self.assertIsNotNone(add_emitter)
        
        sub_emitter = registry.get(OperationType.SUB)
        self.assertIsNotNone(sub_emitter)
        
        # Test signed/unsigned variants
        divu_emitter = registry.get(OperationType.DIV, signed=False)
        self.assertIsNotNone(divu_emitter)
        
        divs_emitter = registry.get(OperationType.DIV, signed=True)
        self.assertIsNotNone(divs_emitter)


class TestOperatorMapping(unittest.TestCase):
    """Test operator to operation type mapping."""
    
    def test_arithmetic_operators(self):
        """Test arithmetic operator mappings."""
        self.assertEqual(get_op_type_for_operator("__add__"), OperationType.ADD)
        self.assertEqual(get_op_type_for_operator("__sub__"), OperationType.SUB)
        self.assertEqual(get_op_type_for_operator("__mul__"), OperationType.MUL)
        self.assertEqual(get_op_type_for_operator("__truediv__"), OperationType.DIV)
        self.assertEqual(get_op_type_for_operator("__floordiv__"), OperationType.FLOOR_DIV)
        self.assertEqual(get_op_type_for_operator("__mod__"), OperationType.MOD)
    
    def test_bitwise_operators(self):
        """Test bitwise operator mappings."""
        self.assertEqual(get_op_type_for_operator("__and__"), OperationType.AND)
        self.assertEqual(get_op_type_for_operator("__or__"), OperationType.OR)
        self.assertEqual(get_op_type_for_operator("__xor__"), OperationType.XOR)
        self.assertEqual(get_op_type_for_operator("__invert__"), OperationType.INVERT)
        self.assertEqual(get_op_type_for_operator("__lshift__"), OperationType.LSHIFT)
        self.assertEqual(get_op_type_for_operator("__rshift__"), OperationType.RSHIFT)
    
    def test_comparison_operators(self):
        """Test comparison operator mappings."""
        self.assertEqual(get_op_type_for_operator("__eq__"), OperationType.EQ)
        self.assertEqual(get_op_type_for_operator("__ne__"), OperationType.NE)
        self.assertEqual(get_op_type_for_operator("__lt__"), OperationType.LT)
        self.assertEqual(get_op_type_for_operator("__le__"), OperationType.LE)
        self.assertEqual(get_op_type_for_operator("__gt__"), OperationType.GT)
        self.assertEqual(get_op_type_for_operator("__ge__"), OperationType.GE)
    
    def test_unary_operators(self):
        """Test unary operator mappings."""
        self.assertEqual(get_op_type_for_operator("__neg__"), OperationType.NEG)
        self.assertEqual(get_op_type_for_operator("__abs__"), OperationType.ABS)
    
    def test_unknown_operator(self):
        """Test unknown operator returns None."""
        self.assertIsNone(get_op_type_for_operator("__unknown__"))


class TestPythonOpToTypeOp(unittest.TestCase):
    """Test _python_op_to_type_op helper."""
    
    def test_arithmetic_mapping(self):
        """Test arithmetic to type op mapping."""
        from cmt2._type_promotion import Operation as TypeOp
        
        self.assertEqual(_python_op_to_type_op("__add__"), TypeOp.ADD)
        self.assertEqual(_python_op_to_type_op("__sub__"), TypeOp.SUB)
        self.assertEqual(_python_op_to_type_op("__mul__"), TypeOp.MUL)
    
    def test_bitwise_mapping(self):
        """Test bitwise to type op mapping."""
        from cmt2._type_promotion import Operation as TypeOp
        
        self.assertEqual(_python_op_to_type_op("__and__"), TypeOp.BITWISE)
        self.assertEqual(_python_op_to_type_op("__or__"), TypeOp.BITWISE)
    
    def test_comparison_mapping(self):
        """Test comparison to type op mapping."""
        from cmt2._type_promotion import Operation as TypeOp
        
        self.assertEqual(_python_op_to_type_op("__eq__"), TypeOp.COMPARE)
        self.assertEqual(_python_op_to_type_op("__lt__"), TypeOp.COMPARE)


class TestSignalTracerBasics(unittest.TestCase):
    """Test SignalTracer basic functionality."""
    
    def test_tracer_creation(self):
        """Test creating a SignalTracer."""
        # This test uses mock values since we don't have CIRCT bindings in test
        class MockValue:
            pass
        
        class MockTrace:
            pass
        
        value = MockValue()
        trace = MockTrace()
        dtype = UInt(32)
        
        tracer = SignalTracer(value, dtype, trace)
        
        self.assertIs(tracer.value, value)
        self.assertIs(tracer.dtype, dtype)
        self.assertIs(tracer.trace, trace)
    
    def test_tracer_repr(self):
        """Test SignalTracer repr."""
        class MockValue:
            pass
        
        class MockTrace:
            pass
        
        tracer = SignalTracer(MockValue(), UInt(8), MockTrace())
        repr_str = repr(tracer)
        
        self.assertIn("SignalTracer", repr_str)
        self.assertIn("UInt(8)", repr_str)


class TestTypePromotionIntegration(unittest.TestCase):
    """Test type promotion integration in tracer."""
    
    def test_uint_promotion_rules(self):
        """Test UInt type promotion rules."""
        # UInt + UInt = UInt with +1 width
        t1 = UInt(8)
        t2 = UInt(16)
        result = t1 + t2
        
        self.assertIsInstance(result, UInt)
        self.assertEqual(result.width, 17)  # max(8, 16) + 1
    
    def test_mixed_signedness_promotion(self):
        """Test mixed signedness promotion."""
        # SInt + UInt = SInt with +1 width
        s1 = SInt(8)
        u1 = UInt(16)
        result = s1 + u1
        
        self.assertIsInstance(result, SInt)
        self.assertEqual(result.width, 17)
    
    def test_multiplication_promotion(self):
        """Test multiplication width rules."""
        # UInt * UInt = UInt with w1 + w2 width
        t1 = UInt(8)
        t2 = UInt(8)
        result = t1 * t2
        
        self.assertIsInstance(result, UInt)
        self.assertEqual(result.width, 16)  # 8 + 8
    
    def test_comparison_result_type(self):
        """Test comparison returns Bits(1) via type promotion."""
        from cmt2._type_promotion import promote_types, Operation as TypeOp
        
        t1 = UInt(32)
        t2 = UInt(32)
        result = promote_types(t1, t2, TypeOp.COMPARE)
        
        self.assertIsInstance(result, Bits)
        self.assertEqual(result.width, 1)


class TestCmt2TraceContext(unittest.TestCase):
    """Test Cmt2Trace context management."""
    
    def test_no_active_trace(self):
        """Test that get_current_trace raises outside context."""
        with self.assertRaises(TracingError):
            get_current_trace()
    
    def test_to_tracer_error(self):
        """Test to_tracer raises on unsupported type."""
        class MockBuilder:
            pass
        
        trace = Cmt2Trace(MockBuilder())
        
        with self.assertRaises(TracingError):
            trace.to_tracer("string not supported")
        
        with self.assertRaises(TracingError):
            trace.to_tracer([1, 2, 3])


class TestTracingError(unittest.TestCase):
    """Test TracingError exception."""
    
    def test_error_message(self):
        """Test error message formatting."""
        error = TracingError("Test error message")
        self.assertEqual(str(error), "Test error message")
    
    def test_error_is_exception(self):
        """Test TracingError is an Exception."""
        with self.assertRaises(TracingError):
            raise TracingError("test")


class TestOperatorCoverage(unittest.TestCase):
    """Test that all expected operators are covered."""
    
    def test_all_arithmetic_ops(self):
        """Test all arithmetic operators are in mapping."""
        arithmetic_ops = [
            "__add__", "__sub__", "__mul__", "__truediv__",
            "__floordiv__", "__mod__", "__pow__",
        ]
        for op in arithmetic_ops:
            self.assertIn(op, OPERATOR_TO_OP_TYPE, f"Missing operator: {op}")
    
    def test_all_bitwise_ops(self):
        """Test all bitwise operators are in mapping."""
        bitwise_ops = [
            "__and__", "__or__", "__xor__", "__invert__",
            "__lshift__", "__rshift__",
        ]
        for op in bitwise_ops:
            self.assertIn(op, OPERATOR_TO_OP_TYPE, f"Missing operator: {op}")
    
    def test_all_comparison_ops(self):
        """Test all comparison operators are in mapping."""
        comparison_ops = [
            "__eq__", "__ne__", "__lt__", "__le__", "__gt__", "__ge__",
        ]
        for op in comparison_ops:
            self.assertIn(op, OPERATOR_TO_OP_TYPE, f"Missing operator: {op}")
    
    def test_all_unary_ops(self):
        """Test all unary operators are in mapping."""
        unary_ops = ["__neg__", "__abs__"]
        for op in unary_ops:
            self.assertIn(op, OPERATOR_TO_OP_TYPE, f"Missing operator: {op}")


if __name__ == "__main__":
    unittest.main()
