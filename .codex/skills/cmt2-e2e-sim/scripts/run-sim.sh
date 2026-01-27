#!/bin/bash
# Run simulation in a workspace
# Handles compilation and execution, reports results

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    echo "Usage: $0 <workspace-dir> [options]"
    echo ""
    echo "Runs simulation in an existing workspace."
    echo ""
    echo "Options:"
    echo "  --waves     Generate and open waveform viewer"
    echo "  --verbose   Show detailed output"
    echo "  --clean     Clean before building"
    echo ""
    echo "Examples:"
    echo "  $0 my_sim/"
    echo "  $0 gcd_workspace/ --waves"
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

WORKSPACE="$1"
WAVES=0
VERBOSE=0
CLEAN=0

shift
while [[ $# -gt 0 ]]; do
    case "$1" in
        --waves)
            WAVES=1
            shift
            ;;
        --verbose)
            VERBOSE=1
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

if [[ ! -d "$WORKSPACE" ]]; then
    echo "Error: Workspace not found: $WORKSPACE"
    exit 1
fi

if [[ ! -f "$WORKSPACE/sim/Makefile" ]]; then
    echo "Error: Not a valid simulation workspace (no Makefile found)"
    echo "Create one with: create-sim-workspace.sh <input> $WORKSPACE"
    exit 1
fi

echo "=== Running Simulation ==="
echo "Workspace: $WORKSPACE"
echo ""

cd "$WORKSPACE/sim"

# Clean if requested
if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning..."
    make clean
    echo ""
fi

# Compile
echo "Compiling..."
if [[ $VERBOSE -eq 1 ]]; then
    make compile
else
    make compile > /dev/null 2>&1 || {
        echo "Compilation failed. Details:"
        cat ../logs/compile.log
        exit 1
    }
fi

if [[ ! -f "sim.out" ]]; then
    echo "Error: Compilation produced no output"
    cat ../logs/compile.log
    exit 1
fi
echo "  Compilation successful"
echo ""

# Run simulation
echo "Running simulation..."
if [[ $VERBOSE -eq 1 ]]; then
    make run
else
    make run > /dev/null 2>&1
fi

# Check results
echo ""
echo "=== Simulation Results ==="
if [[ -f "../logs/sim.log" ]]; then
    # Check for common result patterns
    if grep -q "PASS" ../logs/sim.log; then
        echo "Status: PASS"
        grep "PASS" ../logs/sim.log | head -5
    elif grep -q "FAIL" ../logs/sim.log; then
        echo "Status: FAIL"
        grep "FAIL" ../logs/sim.log | head -5
    elif grep -q "ERROR" ../logs/sim.log; then
        echo "Status: ERROR"
        grep "ERROR" ../logs/sim.log | head -5
    else
        echo "Status: Complete (check log for results)"
    fi

    echo ""
    echo "Log file: $WORKSPACE/logs/sim.log"

    if [[ $VERBOSE -eq 1 ]]; then
        echo ""
        echo "--- Full Log ---"
        cat ../logs/sim.log
    fi
else
    echo "Warning: No simulation log found"
fi

# Open waveforms if requested
if [[ $WAVES -eq 1 ]]; then
    if [[ -f "sim.vcd" ]]; then
        echo ""
        echo "Opening waveform viewer..."
        gtkwave sim.vcd &
    else
        echo "Warning: No VCD file found"
    fi
fi

echo ""
echo "=== Done ==="
