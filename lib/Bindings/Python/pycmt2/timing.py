#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Timing helpers for PyCMT2 EDSL.

This module provides utilities for working with timing in dataflow
pipelines and procedural constructs.

Example:
    from pycmt2.timing import timing_interval, pipeline_timing, validate_timing

    # Create timing interval
    t = timing_interval(0, 4)  # [0, 4) = cycles 0, 1, 2, 3

    # Generate timing for N-stage pipeline
    stages = pipeline_timing(stages=4, stage_latency=2)
    # Returns: [(0, 2), (2, 4), (4, 6), (6, 8)]

    # Validate timing constraints
    validate_timing(timing_list, interval=2)
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence


@dataclass(frozen=True)
class TimingInterval:
    """Represents a half-open timing interval [start, end).

    The interval includes cycles from `start` to `end - 1`.
    """

    start: int
    end: int

    def __post_init__(self):
        if self.start < 0:
            raise ValueError(f"Timing start must be non-negative, got {self.start}")
        if self.end <= self.start:
            raise ValueError(
                f"Timing end must be greater than start, got [{self.start}, {self.end})"
            )

    @property
    def latency(self) -> int:
        """Duration of the interval in cycles."""
        return self.end - self.start

    def overlaps(self, other: TimingInterval) -> bool:
        """Check if this interval overlaps with another."""
        return self.start < other.end and other.start < self.end

    def contains(self, cycle: int) -> bool:
        """Check if a cycle is within this interval."""
        return self.start <= cycle < self.end

    def shift(self, offset: int) -> TimingInterval:
        """Return a new interval shifted by offset cycles."""
        return TimingInterval(self.start + offset, self.end + offset)

    def to_tuple(self) -> tuple[int, int]:
        """Convert to (start, end) tuple."""
        return (self.start, self.end)

    def __repr__(self) -> str:
        return f"[{self.start}, {self.end})"


def timing_interval(start: int, end: int) -> TimingInterval:
    """Create a timing interval.

    Args:
        start: Start cycle (inclusive).
        end: End cycle (exclusive).

    Returns:
        A TimingInterval representing [start, end).

    Example:
        t = timing_interval(0, 4)  # Cycles 0, 1, 2, 3
    """
    return TimingInterval(start, end)


def single_cycle(cycle: int) -> TimingInterval:
    """Create a single-cycle timing interval.

    Args:
        cycle: The cycle number.

    Returns:
        A TimingInterval for one cycle.

    Example:
        t = single_cycle(5)  # [5, 6)
    """
    return TimingInterval(cycle, cycle + 1)


def pipeline_timing(
    stages: int,
    stage_latency: int = 1,
    start_cycle: int = 0,
) -> list[TimingInterval]:
    """Generate timing intervals for a linear pipeline.

    Each stage occupies `stage_latency` cycles, starting after
    the previous stage completes.

    Args:
        stages: Number of pipeline stages.
        stage_latency: Latency of each stage in cycles (default 1).
        start_cycle: Starting cycle for the pipeline (default 0).

    Returns:
        List of TimingInterval for each stage.

    Example:
        # 4-stage pipeline, 1 cycle per stage
        timing = pipeline_timing(4)
        # Returns: [[0,1), [1,2), [2,3), [3,4)]

        # 3-stage pipeline, 2 cycles per stage
        timing = pipeline_timing(3, stage_latency=2)
        # Returns: [[0,2), [2,4), [4,6)]
    """
    intervals = []
    current = start_cycle
    for _ in range(stages):
        intervals.append(TimingInterval(current, current + stage_latency))
        current += stage_latency
    return intervals


def interleaved_timing(
    tasks: int,
    latency: int,
    interval: int,
) -> list[TimingInterval]:
    """Generate timing for interleaved (pipelined) execution.

    When multiple operations are pipelined with initiation interval II,
    each starts II cycles after the previous.

    Args:
        tasks: Number of tasks.
        latency: Latency of each task.
        interval: Initiation interval (II).

    Returns:
        List of TimingInterval for each task.

    Example:
        # 4 pipelined tasks, latency=4, II=1
        timing = interleaved_timing(4, latency=4, interval=1)
        # Returns: [[0,4), [1,5), [2,6), [3,7)]
    """
    return [
        TimingInterval(i * interval, i * interval + latency) for i in range(tasks)
    ]


def total_latency(intervals: Sequence[TimingInterval]) -> int:
    """Calculate total latency from a sequence of timing intervals.

    Returns the end cycle of the last interval.

    Args:
        intervals: Sequence of timing intervals.

    Returns:
        Total latency in cycles.

    Example:
        timing = pipeline_timing(4, stage_latency=2)
        total = total_latency(timing)  # Returns 8
    """
    if not intervals:
        return 0
    return max(t.end for t in intervals)


def validate_timing(
    intervals: Sequence[TimingInterval],
    interval: int | None = None,
    allow_overlap: bool = False,
) -> list[str]:
    """Validate timing constraints and return any violations.

    Checks:
    - Intervals are valid (end > start)
    - Intervals don't overlap (unless allow_overlap=True)
    - Pipeline interval constraints are respected

    Args:
        intervals: Sequence of timing intervals to validate.
        interval: Optional initiation interval to check against.
        allow_overlap: Whether overlapping intervals are allowed.

    Returns:
        List of error messages (empty if valid).

    Example:
        errors = validate_timing(timing_list, interval=2)
        if errors:
            for e in errors:
                print(f"Timing error: {e}")
    """
    errors = []

    # Check for valid intervals
    for i, t in enumerate(intervals):
        if t.end <= t.start:
            errors.append(f"Interval {i} invalid: {t}")

    # Check for overlaps
    if not allow_overlap:
        for i, t1 in enumerate(intervals):
            for j, t2 in enumerate(intervals):
                if i < j and t1.overlaps(t2):
                    errors.append(f"Intervals {i} and {j} overlap: {t1} and {t2}")

    # Check initiation interval
    if interval is not None and len(intervals) >= 2:
        total = total_latency(intervals)
        if total > interval:
            # Check if operations could overlap when pipelined
            for i, t in enumerate(intervals):
                shifted = t.shift(interval)
                for j, t2 in enumerate(intervals):
                    if shifted.overlaps(t2):
                        errors.append(
                            f"With II={interval}, operation {i} shifted by II "
                            f"overlaps with operation {j}: {shifted} overlaps {t2}"
                        )
                        break

    return errors


def timing_to_attr_tuple(interval: TimingInterval) -> tuple[int, int]:
    """Convert TimingInterval to tuple format for MLIR attributes.

    Args:
        interval: The timing interval.

    Returns:
        (start, end) tuple for use with call() timing arguments.
    """
    return interval.to_tuple()


def timing_list_to_tuples(
    intervals: Sequence[TimingInterval],
) -> list[tuple[int, int]]:
    """Convert a list of TimingIntervals to tuples.

    Args:
        intervals: Sequence of timing intervals.

    Returns:
        List of (start, end) tuples.
    """
    return [t.to_tuple() for t in intervals]


# Convenience constants for common timing patterns
IMMEDIATE = TimingInterval(0, 1)  # Single-cycle at cycle 0


def arg_timing(
    num_args: int, start: int = 0, duration: int = 1
) -> list[tuple[int, int]]:
    """Generate timing for call arguments.

    All arguments are assumed to be provided at the same time.

    Args:
        num_args: Number of arguments.
        start: Start cycle (default 0).
        duration: Duration in cycles (default 1).

    Returns:
        List of (start, end) tuples, one per argument.

    Example:
        # Two arguments provided at cycle 0
        arg_t = arg_timing(2)  # [(0, 1), (0, 1)]

        # Three arguments provided at cycle 2 for 2 cycles
        arg_t = arg_timing(3, start=2, duration=2)  # [(2, 4), (2, 4), (2, 4)]
    """
    return [(start, start + duration)] * num_args


def result_timing(
    num_results: int, start: int, duration: int = 1
) -> list[tuple[int, int]]:
    """Generate timing for call results.

    All results are assumed to be available at the same time.

    Args:
        num_results: Number of results.
        start: Start cycle when results are available.
        duration: Duration results are valid (default 1).

    Returns:
        List of (start, end) tuples, one per result.

    Example:
        # Single result available at cycle 4
        res_t = result_timing(1, start=4)  # [(4, 5)]
    """
    return [(start, start + duration)] * num_results
