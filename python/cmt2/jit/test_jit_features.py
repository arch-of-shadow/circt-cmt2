#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Comprehensive test suite for CMT2 JIT features.

This module tests all JIT functionality including:
- Decorators (@elaborate, @simulate)
- Static argument handling
- Caching
- Staged compilation
- Debug info
"""

from __future__ import annotations

import tempfile
import unittest
from typing import Annotated, List

import sys
sys.path.insert(0, '../../../python')

import cmt2
from cmt2 import elaborate, simulate, static
from cmt2.jit import (
    ElaboratedCircuit,
    LoweredCircuit,
    CompiledCircuit,
    SourceLocation,
    DebugInfo,
    get_current_location,
    Cmt2TracingError,
)
from cmt2.jit._cache import (
    JITCache,
    get_function_ast_hash,
)
from cmt2.jit._static_args import compute_cache_key


# Module-level test functions for proper source inspection

@elaborate
def _test_simple_design():
    return {"name": "Simple", "ops": 10}


@elaborate
def _test_param_design(
    width: Annotated[int, static] = 32,
    depth: Annotated[int, static] = 8,
):
    return {"width": width, "depth": depth}


class TestElaborateDecorator(unittest.TestCase):
    """Test the @elaborate decorator."""
    
    def setUp(self):
        """Clear cache before each test."""
        from cmt2.jit._cache import cache_clear
        cache_clear()
    
    def test_basic_elaboration(self):
        """Test basic elaboration without static args."""
        result = _test_simple_design()
        self.assertEqual(result["name"], "Simple")
        self.assertEqual(result["ops"], 10)
    
    def test_elaboration_with_static_args(self):
        """Test elaboration with static arguments."""
        # Create fresh function to avoid cache issues
        @elaborate
        def fresh_design(width: Annotated[int, static] = 32, depth: Annotated[int, static] = 8):
            return {"width": width, "depth": depth}
        
        # Different static args should give different results
        result1 = fresh_design(width=16, depth=4)
        self.assertEqual(result1["width"], 16)
        self.assertEqual(result1["depth"], 4)
        
        # Clear cache to test different args
        from cmt2.jit._cache import cache_clear
        cache_clear()
        
        result2 = fresh_design(width=32, depth=8)
        self.assertEqual(result2["width"], 32)
        self.assertEqual(result2["depth"], 8)
    
    def test_elaboration_returns_circuit(self):
        """Test that elaboration returns a circuit object."""
        result = _test_simple_design()
        self.assertIsInstance(result, dict)
        self.assertIn("name", result)


class TestSimulateDecorator(unittest.TestCase):
    """Test the @simulate decorator."""
    
    def test_basic_simulation(self):
        """Test basic simulation execution."""
        @simulate
        def simple_sim():
            return {"status": "success", "cycles": 100}
        
        result = simple_sim()
        self.assertEqual(result["status"], "success")
        self.assertEqual(result["cycles"], 100)
    
    def test_simulation_with_static_args(self):
        """Test simulation with static arguments."""
        @simulate
        def param_sim(
            width: Annotated[int, static] = 32,
            cycles: int = 100,
        ):
            return {"width": width, "cycles": cycles}
        
        result = param_sim(width=16, cycles=50)
        self.assertEqual(result["width"], 16)
        self.assertEqual(result["cycles"], 50)


class TestStaticArguments(unittest.TestCase):
    """Test static argument handling via the decorator."""
    
    def setUp(self):
        """Clear cache before each test."""
        from cmt2.jit._cache import cache_clear
        cache_clear()
    
    def test_static_args_basic(self):
        """Test static arguments produce correct results."""
        @elaborate
        def design(width: Annotated[int, static] = 32, depth: Annotated[int, static] = 8):
            return {"width": width, "depth": depth}
        
        result = design(width=16, depth=4)
        self.assertEqual(result, {"width": 16, "depth": 4})
    
    def test_static_str_via_elaborate(self):
        """Test static string arguments."""
        @elaborate
        def design_str(mode: Annotated[str, static] = "basic"):
            return mode
        
        self.assertEqual(design_str(mode="advanced"), "advanced")


class TestCaching(unittest.TestCase):
    """Test caching functionality."""
    
    def setUp(self):
        """Clear cache before each test."""
        from cmt2.jit._cache import cache_clear
        cache_clear()
    
    def test_cache_key_composition(self):
        """Test that cache keys are properly composed."""
        # Use the module-level _test_param_design function
        _test_param_design(width=32, depth=8)
        key = _test_param_design.cache_key
        
        self.assertIsNotNone(key)
        self.assertIsInstance(key, str)
    
    def test_cache_hit_miss(self):
        """Test cache hit and miss behavior."""
        cache = JITCache[str]()
        
        # Put value
        cache.put("key1", "value1", static_args={})
        
        # Get hit
        entry = cache.get("key1")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.value, "value1")
        
        # Get miss
        entry = cache.get("nonexistent")
        self.assertIsNone(entry)
    
    def test_cache_clear(self):
        """Test cache clearing."""
        cache = JITCache[str]()
        cache.put("key1", "value1", static_args={})
        
        # Clear
        cache.clear()
        
        # Should be empty
        entry = cache.get("key1")
        self.assertIsNone(entry)


class TestStagedCompilation(unittest.TestCase):
    """Test staged compilation classes."""
    
    def test_elaborated_circuit_creation(self):
        """Test ElaboratedCircuit creation."""
        circuit = {"name": "Test", "ops": 10}
        
        elaborated = ElaboratedCircuit(
            mlir_module=circuit,
            name="Test",
            static_args={"width": 32},
        )
        
        self.assertEqual(elaborated.name, "Test")
        self.assertEqual(elaborated.static_args, {"width": 32})
    
    def test_elaborated_circuit_repr(self):
        """Test ElaboratedCircuit string representation."""
        elaborated = ElaboratedCircuit(
            mlir_module=None,
            name="Test",
            static_args={"width": 32},
        )
        
        repr_str = repr(elaborated)
        self.assertIn("Test", repr_str)
        self.assertIn("width", repr_str)
    
    def test_lowered_circuit_creation(self):
        """Test LoweredCircuit creation."""
        lowered = LoweredCircuit(
            mlir_module={},
            target="verilog",
            optimization_level=2,
        )
        
        self.assertEqual(lowered.target, "verilog")
        self.assertEqual(lowered.optimization_level, 2)
    
    def test_compiled_circuit_creation(self):
        """Test CompiledCircuit creation."""
        compiled = CompiledCircuit(
            artifact="verilog code",
            target="verilog",
        )
        
        self.assertEqual(compiled.target, "verilog")
        self.assertEqual(compiled.artifact, "verilog code")
    
    def test_compiled_circuit_codegen(self):
        """Test CompiledCircuit code generation."""
        compiled = CompiledCircuit(
            artifact="// Verilog code",
            target="verilog",
        )
        
        verilog = compiled.codegen(format="verilog")
        self.assertIn("Verilog", verilog)


class TestDebugInfo(unittest.TestCase):
    """Test debug information support."""
    
    def test_source_location_creation(self):
        """Test SourceLocation creation."""
        loc = SourceLocation(
            file="test.py",
            line=42,
            function="test_fn",
        )
        
        self.assertEqual(loc.file, "test.py")
        self.assertEqual(loc.line, 42)
        self.assertEqual(loc.function, "test_fn")
    
    def test_source_location_str(self):
        """Test SourceLocation string formatting."""
        loc = SourceLocation(
            file="test.py",
            line=42,
            function="test_fn",
        )
        
        str_repr = str(loc)
        self.assertIn("test.py", str_repr)
        self.assertIn("42", str_repr)
        self.assertIn("test_fn", str_repr)
    
    def test_get_current_location(self):
        """Test getting current location."""
        loc = get_current_location()
        
        self.assertIsInstance(loc, SourceLocation)
        self.assertIsNotNone(loc.file)
        self.assertIsInstance(loc.line, int)
        self.assertGreater(loc.line, 0)
    
    def test_debug_info_context(self):
        """Test DebugInfo context manager."""
        loc = SourceLocation("test.py", 10, function="test")
        
        # Initially no location
        self.assertIsNone(DebugInfo.get_current())
        
        # Set location via context
        with DebugInfo.attach(loc):
            current = DebugInfo.get_current()
            self.assertIsNotNone(current)
            self.assertEqual(current.file, "test.py")
            self.assertEqual(current.line, 10)
        
        # Location cleared after context
        self.assertIsNone(DebugInfo.get_current())
    
    def test_tracing_error(self):
        """Test Cmt2TracingError with location."""
        error = Cmt2TracingError("Test error message")
        
        self.assertEqual(error.message, "Test error message")
        self.assertIsNotNone(error.source_location)
        
        # String representation should include location
        str_repr = str(error)
        self.assertIn("Test error message", str_repr)


class TestIntegration(unittest.TestCase):
    """Integration tests for complete JIT workflow."""
    
    def test_full_workflow(self):
        """Test complete elaboration and simulation workflow."""
        
        @elaborate
        def design(width: Annotated[int, static] = 32):
            return {"name": "Test", "width": width}
        
        @simulate
        def test_design():
            circuit = design(width=16)
            return {
                "circuit": circuit,
                "success": True,
            }
        
        # Run the workflow
        result = test_design()
        self.assertTrue(result["success"])
        self.assertEqual(result["circuit"]["width"], 16)


class TestTypePromotion(unittest.TestCase):
    """Test type promotion rules."""
    
    def test_uint_promotion(self):
        """Test UInt type promotion."""
        from cmt2.types import UInt
        
        u8 = UInt(8)
        u16 = UInt(16)
        
        # Addition: max(width1, width2) + 1
        result = u8 + u16
        self.assertIsInstance(result, UInt)
        self.assertEqual(result.width, 17)
        
        # Multiplication: width1 + width2
        result = u8 * u8
        self.assertIsInstance(result, UInt)
        self.assertEqual(result.width, 16)
    
    def test_mixed_signedness(self):
        """Test mixed signed/unsigned promotion."""
        from cmt2.types import UInt, SInt
        
        u8 = UInt(8)
        s8 = SInt(8)
        
        # UInt + SInt -> SInt
        result = u8 + s8
        self.assertIsInstance(result, SInt)


def run_tests():
    """Run all tests."""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    
    # Add all test classes
    test_classes = [
        TestElaborateDecorator,
        TestSimulateDecorator,
        TestStaticArguments,
        TestCaching,
        TestStagedCompilation,
        TestDebugInfo,
        TestIntegration,
        TestTypePromotion,
    ]
    
    for test_class in test_classes:
        tests = loader.loadTestsFromTestCase(test_class)
        suite.addTests(tests)
    
    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    return result.wasSuccessful()


if __name__ == "__main__":
    success = run_tests()
    sys.exit(0 if success else 1)
