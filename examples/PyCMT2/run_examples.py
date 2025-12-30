#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Run All PyCMT2 Examples

This script runs all PyCMT2 examples and reports their status.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core python3 ../examples/PyCMT2/run_examples.py
"""

import sys
import os
import importlib.util
import traceback

# Add the pycmt2 package to path
script_dir = os.path.dirname(os.path.abspath(__file__))
build_dir = os.path.dirname(os.path.dirname(script_dir))
python_packages = os.path.join(build_dir, "build/tools/circt/python_packages/circt_core")
if python_packages not in sys.path:
    sys.path.insert(0, python_packages)


def run_example(name: str, path: str) -> bool:
    """Run a single example and return True if successful."""
    print(f"\n{'=' * 60}")
    print(f"Running: {name}")
    print('=' * 60)

    try:
        # Load module dynamically
        spec = importlib.util.spec_from_file_location(name, path)
        if spec is None or spec.loader is None:
            print(f"ERROR: Could not load {path}")
            return False

        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)

        # Run main function if it exists
        if hasattr(module, 'main'):
            result = module.main()
            if result != 0:
                print(f"ERROR: {name} returned non-zero exit code: {result}")
                return False

        print(f"\nSUCCESS: {name} completed successfully")
        return True

    except Exception as e:
        print(f"\nERROR: {name} failed with exception:")
        traceback.print_exc()
        return False


def main():
    print("=" * 60)
    print("PyCMT2 Examples Test Runner")
    print("=" * 60)

    # Find all example files
    examples = [
        ("counter_example", os.path.join(script_dir, "counter_example.py")),
        ("proc_example", os.path.join(script_dir, "proc_example.py")),
    ]

    results = {}
    for name, path in examples:
        if os.path.exists(path):
            results[name] = run_example(name, path)
        else:
            print(f"\nWARNING: Example not found: {path}")
            results[name] = False

    # Summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)

    passed = sum(1 for v in results.values() if v)
    failed = len(results) - passed

    for name, success in results.items():
        status = "PASS" if success else "FAIL"
        print(f"  {name}: {status}")

    print(f"\nTotal: {passed} passed, {failed} failed")

    if failed > 0:
        print("\nSome examples failed!")
        return 1

    print("\nAll examples passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
