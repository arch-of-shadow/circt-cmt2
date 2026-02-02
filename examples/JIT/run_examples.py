#!/usr/bin/env python3
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Run JIT (stacked on PyCMT2) E2E examples.

Usage:
  cd circt-cmt2/build
  PYTHONPATH=tools/circt/python_packages/circt_core:../python \
    python3 ../examples/JIT/run_examples.py
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys


def _repo_root(script_dir: str) -> str:
    # script_dir = <repo>/examples/JIT
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
        os.environ['PYTHONPATH'] = os.pathsep.join(parts)


EXAMPLES = [
    'alu.py',
    'banked_gemm_dataflow.py',
    'comprehensive_dataflow_example.py',
    'comprehensive_example.py',
    'counter.py',
    'dataflow_forkjoin.py',
    'dataflow_proc_control.py',
    'debug_testbench_example.py',
    'diagnostics.py',
    'division_pipeline.py',
    'dynamic_pipeline_fifo.py',
    'gcd.py',
    'interpret.py',
    'interface_hello.py',
    'jit_counter.py',
    'jit_fifo.py',
    'li_token_pipeline.py',
    'memory_proc.py',
    'nested_dataflow_example.py',
    'pipeline_e2e.py',
    'pipeline_fifo_testbench.py',
    'proc.py',
    'proc_par_test.py',
    'proc_pipeline.py',
    'proc_testbench.py',
    'simulation_workspace.py',
    'static_proc.py',
    'stl_test.py',
    'systolic.py',
    'test_cond_if.py',
    'test_submodule_proc_step.py',
    'timing.py',
    'value_args.py',
    'while_loop_example.py',
]


def _default_log_dir(script_dir: str) -> str:
    return os.path.join(script_dir, "_logs")


def run_example(name: str, path: str) -> bool:
    print("\n" + "=" * 60)
    print(f"Running: {name}")
    print("=" * 60)
    result = subprocess.run([sys.executable, path])
    if result.returncode != 0:
        print(f"\nFAIL: {name} exited with {result.returncode}")
        return False
    print(f"\nPASS: {name}")
    return True


def run_example_captured(name: str, path: str, log_dir: str, *, strict_pass_markers: bool) -> bool:
    print("\n" + "=" * 60)
    print(f"Running: {name}")
    print("=" * 60)

    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, f"{name}.log")

    # Capture output so we can review simulation results per-example.
    env = os.environ.copy()
    # Defaults optimized for running the whole suite:
    # - Disable VCD tracing unless explicitly requested (waves are still available
    #   when running an example directly with `PYCMT2_TRACE=1`).
    # - Enable Verilator output splitting to avoid huge single-TU compiles on
    #   large designs (can be overridden by the caller).
    env.setdefault("PYCMT2_TRACE", "0")
    env.setdefault("PYCMT2_VERILATOR_OUTPUT_SPLIT", "1")
    env.setdefault("PYCMT2_FAST_BUILD", "1")

    result = subprocess.run(
        [sys.executable, path],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    out = (result.stdout or "") + (result.stderr or "")

    with open(log_path, "w", encoding="utf-8", errors="replace") as f:
        f.write(out)

    if result.returncode != 0:
        print(f"\nFAIL: {name} exited with {result.returncode} (log: {log_path})")
        return False

    # Basic sanity: most examples print explicit PASS markers after checking results.
    # We keep this lightweight; correctness is still primarily enforced by the
    # example's own assertions/testbench checks.
    has_pass_marker = (
        "PASSED: All tests passed" in out
        or "E2E Simulation PASSED!" in out
        or "Status: ALL TESTS PASSED" in out
        or "Total: 19 passed, 0 failed" in out  # interpret.py
        or "Diagnostics Example Complete" in out  # diagnostics.py
        or "All tests passed!" in out  # stl_test.py
    )
    if not has_pass_marker:
        msg = f"{name} produced no PASS marker (log: {log_path})"
        if strict_pass_markers:
            print(f"\nFAIL: {msg}")
            return False
        print(f"\nWARN: {msg}")

    print(f"\nPASS: {name} (log: {log_path})")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description='Run JIT E2E examples')
    parser.add_argument('--list', action='store_true', help='List examples and exit')
    parser.add_argument('--keep-going', action='store_true', help='Continue after failures')
    parser.add_argument('--log-dir', default=None, help='Write per-example logs to this dir')
    parser.add_argument(
        '--strict-pass-markers',
        action='store_true',
        help='Fail if an example exits 0 but prints no known PASS marker',
    )
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

    log_dir = args.log_dir or _default_log_dir(script_dir)

    results: dict[str, bool] = {}
    for ex in EXAMPLES:
        name = ex[:-3] if ex.endswith('.py') else ex
        path = os.path.join(script_dir, ex)
        if not os.path.exists(path):
            print(f"\nMISSING: {ex}")
            results[name] = False
            if not args.keep_going:
                break
            continue

        results[name] = run_example_captured(
            name, path, log_dir, strict_pass_markers=args.strict_pass_markers
        )
        if not results[name] and not args.keep_going:
            break

    passed = sum(1 for v in results.values() if v)
    failed = len(results) - passed
    print(f"\nTotal: {passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
