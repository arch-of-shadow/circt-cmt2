#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Run PyCMT2 Examples (E2E)

Runs a curated set of end-to-end PyCMT2 examples that generate Verilator
workspaces and validate outputs via the Testbench DSL.

Usage:
    cd circt-cmt2/build
    PYTHONPATH=tools/circt/python_packages/circt_core \\
      python3 ../examples/PyCMT2/run_examples.py
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys


def _repo_root(script_dir: str) -> str:
    # script_dir = <repo>/examples/PyCMT2
    return os.path.dirname(os.path.dirname(script_dir))


def _default_circt_core(repo_root: str) -> str | None:
    candidate = os.path.join(repo_root, "build/tools/circt/python_packages/circt_core")
    return candidate if os.path.isdir(candidate) else None


def _default_repo_python(repo_root: str) -> str:
    return os.path.join(repo_root, "python")


def _ensure_pythonpath_contains(path: str) -> None:
    cur = os.environ.get("PYTHONPATH", "")
    parts = [p for p in cur.split(os.pathsep) if p]
    if path not in parts:
        parts.append(path)
        os.environ["PYTHONPATH"] = os.pathsep.join(parts)


EXAMPLES = [
    "alu.py",
    "banked_gemm_dataflow.py",
    "comprehensive_dataflow_example.py",
    "comprehensive_example.py",
    "counter.py",
    "dataflow_forkjoin.py",
    "dataflow_proc_control.py",
    "debug_testbench_example.py",
    "division_pipeline.py",
    "dynamic_pipeline_fifo.py",
    "external_module_custom_rtl.py",
    "gcd.py",
    "li_token_pipeline.py",
    "memory_proc.py",
    "nested_dataflow_example.py",
    "pipeline_e2e.py",
    "pipeline_fifo_testbench.py",
    "proc.py",
    "proc_par_test.py",
    "proc_pipeline.py",
    "proc_testbench.py",
    "simulation_workspace.py",
    "static_proc.py",
    "systolic.py",
    "test_cond_if.py",
    "test_submodule_proc_step.py",
    "timing.py",
    "value_args.py",
    "while_loop_example.py",
]


def run_example(name: str, path: str) -> bool:
    print(f"\n{'=' * 60}")
    print(f"Running: {name}")
    print("=" * 60)
    result = subprocess.run([sys.executable, path])
    if result.returncode != 0:
        print(f"\nFAIL: {name} exited with {result.returncode}")
        return False
    print(f"\nPASS: {name}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Run PyCMT2 E2E examples")
    parser.add_argument("--list", action="store_true", help="List examples and exit")
    parser.add_argument("--keep-going", action="store_true", help="Continue after failures")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))

    repo_root = _repo_root(script_dir)
    circt_core = _default_circt_core(repo_root)
    repo_python = _default_repo_python(repo_root)

    if circt_core:
        _ensure_pythonpath_contains(circt_core)
    _ensure_pythonpath_contains(repo_python)

    if args.list:
        for ex in EXAMPLES:
            print(ex)
        return 0

    print("=" * 60)
    print("PyCMT2 Examples Test Runner (E2E)")
    print("=" * 60)

    results: dict[str, bool] = {}
    for ex in EXAMPLES:
        name = ex[:-3] if ex.endswith(".py") else ex
        path = os.path.join(script_dir, ex)
        if not os.path.exists(path):
            print(f"\nMISSING: {ex}")
            results[name] = False
            if not args.keep_going:
                break
            continue

        results[name] = run_example(name, path)
        if not results[name] and not args.keep_going:
            break

    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)

    passed = sum(1 for v in results.values() if v)
    failed = len(results) - passed
    for name, ok in results.items():
        print(f"  {name}: {'PASS' if ok else 'FAIL'}")
    print(f"\nTotal: {passed} passed, {failed} failed")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
