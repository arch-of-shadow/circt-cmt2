#!/bin/bash
#  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
#  See https://llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# run-pycmt2.sh - Helper script to run PyCMT2 Python scripts
#
# This script sets up the correct PYTHONPATH and working directory
# for running PyCMT2 examples and scripts.
#
# Usage:
#   ./scripts/run-pycmt2.sh <python_script> [args...]
#   ./scripts/run-pycmt2.sh --list              # List available examples
#   ./scripts/run-pycmt2.sh --help              # Show help
#
# Examples:
#   ./scripts/run-pycmt2.sh examples/PyCMT2/counter_example.py
#   ./scripts/run-pycmt2.sh examples/PyCMT2/gcd_example.py
#   ./scripts/run-pycmt2.sh examples/PyCMT2/test_gcd_simulation.py
#   ./scripts/run-pycmt2.sh -i  # Interactive Python with pycmt2

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Find the repository root (where this script's parent directory is)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default build directory
BUILD_DIR="${CIRCT_BUILD_DIR:-$REPO_ROOT/build}"

# Python packages path
PYTHON_PACKAGES="$BUILD_DIR/tools/circt/python_packages/circt_core"

usage() {
    echo "Usage: $(basename "$0") [OPTIONS] <python_script> [args...]"
    echo ""
    echo "Run PyCMT2 Python scripts with the correct environment."
    echo ""
    echo "Options:"
    echo "  -h, --help      Show this help message"
    echo "  -l, --list      List available PyCMT2 examples"
    echo "  -i, --interactive  Start interactive Python with pycmt2 loaded"
    echo "  -b, --build-dir DIR  Specify build directory (default: \$REPO_ROOT/build)"
    echo "  -v, --verbose   Show environment setup details"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") examples/PyCMT2/counter_example.py"
    echo "  $(basename "$0") examples/PyCMT2/gcd_example.py"
    echo "  $(basename "$0") examples/PyCMT2/test_gcd_simulation.py"
    echo "  $(basename "$0") -i"
    echo ""
    echo "Environment Variables:"
    echo "  CIRCT_BUILD_DIR    Override the build directory location"
    echo ""
}

list_examples() {
    echo -e "${BLUE}Available PyCMT2 Examples:${NC}"
    echo ""

    EXAMPLES_DIR="$REPO_ROOT/examples/PyCMT2"

    if [[ -d "$EXAMPLES_DIR" ]]; then
        echo -e "${GREEN}Basic Examples:${NC}"
        for f in "$EXAMPLES_DIR"/*_example.py; do
            if [[ -f "$f" ]]; then
                name=$(basename "$f")
                # Extract first line of docstring if available
                desc=$(head -20 "$f" | grep -A1 '"""' | tail -1 | sed 's/^[ ]*//' | head -c 60)
                echo "  $name"
                if [[ -n "$desc" && "$desc" != '"""' ]]; then
                    echo "    $desc"
                fi
            fi
        done

        echo ""
        echo -e "${GREEN}Simulation Tests:${NC}"
        for f in "$EXAMPLES_DIR"/test_*_simulation.py; do
            if [[ -f "$f" ]]; then
                name=$(basename "$f")
                echo "  $name"
            fi
        done

        echo ""
        echo -e "${GREEN}Other Scripts:${NC}"
        for f in "$EXAMPLES_DIR"/*.py; do
            if [[ -f "$f" ]]; then
                name=$(basename "$f")
                if [[ ! "$name" =~ _example\.py$ && ! "$name" =~ ^test_ ]]; then
                    echo "  $name"
                fi
            fi
        done
    else
        echo -e "${RED}Examples directory not found: $EXAMPLES_DIR${NC}"
    fi

    echo ""
    echo "Run with: $(basename "$0") examples/PyCMT2/<example>.py"
}

check_environment() {
    local verbose=$1
    local errors=0

    # Check build directory exists
    if [[ ! -d "$BUILD_DIR" ]]; then
        echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
        echo "Please build CIRCT first or set CIRCT_BUILD_DIR"
        errors=1
    fi

    # Check Python packages exist
    if [[ ! -d "$PYTHON_PACKAGES" ]]; then
        echo -e "${RED}Error: Python packages not found: $PYTHON_PACKAGES${NC}"
        echo "Please build CIRCT with Python bindings enabled:"
        echo "  cmake -DCIRCT_BINDINGS_PYTHON_ENABLED=ON ..."
        errors=1
    fi

    # Check for pycmt2 module
    if [[ ! -d "$PYTHON_PACKAGES/circt/pycmt2" ]]; then
        echo -e "${RED}Error: pycmt2 module not found in Python packages${NC}"
        echo "The pycmt2 module may not be installed. Check your build."
        errors=1
    fi

    if [[ $errors -ne 0 ]]; then
        exit 1
    fi

    if [[ "$verbose" == "true" ]]; then
        echo -e "${GREEN}Environment:${NC}"
        echo "  Repository root: $REPO_ROOT"
        echo "  Build directory: $BUILD_DIR"
        echo "  Python packages: $PYTHON_PACKAGES"
        echo "  Python: $(which python3)"
        echo ""
    fi
}

start_interactive() {
    check_environment "true"

    echo -e "${BLUE}Starting interactive Python with pycmt2...${NC}"
    echo ""
    echo "Try:"
    echo "  from circt.pycmt2 import Circuit, UInt"
    echo "  from circt.pycmt2.stl import Reg"
    echo ""

    cd "$BUILD_DIR"
    PYTHONPATH="$PYTHON_PACKAGES:$PYTHONPATH" python3 -i -c "
from circt.pycmt2 import Circuit, UInt, ModuleBuilder
from circt.pycmt2.stl import Reg, Wire, clear_stl_registry
from circt.pycmt2.simulation import SimulationWorkspace
print('PyCMT2 loaded successfully!')
print('')
print('Available imports:')
print('  Circuit, UInt, ModuleBuilder')
print('  Reg, Wire, clear_stl_registry')
print('  SimulationWorkspace')
print('')
"
}

run_script() {
    local script=$1
    shift
    local args=("$@")
    local verbose=$1

    # Handle relative paths
    if [[ ! "$script" = /* ]]; then
        # If path doesn't start with /, make it relative to repo root
        if [[ -f "$REPO_ROOT/$script" ]]; then
            script="$REPO_ROOT/$script"
        elif [[ -f "$script" ]]; then
            script="$(pwd)/$script"
        fi
    fi

    if [[ ! -f "$script" ]]; then
        echo -e "${RED}Error: Script not found: $script${NC}"
        exit 1
    fi

    check_environment "$verbose"

    echo -e "${BLUE}Running: $(basename "$script")${NC}"
    echo ""

    cd "$BUILD_DIR"
    PYTHONPATH="$PYTHON_PACKAGES:$PYTHONPATH" python3 "$script" "${args[@]}"
}

# Parse arguments
VERBOSE="false"
INTERACTIVE="false"

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -l|--list)
            list_examples
            exit 0
            ;;
        -i|--interactive)
            INTERACTIVE="true"
            shift
            ;;
        -b|--build-dir)
            BUILD_DIR="$2"
            PYTHON_PACKAGES="$BUILD_DIR/tools/circt/python_packages/circt_core"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE="true"
            shift
            ;;
        -*)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
        *)
            # First non-option argument is the script
            break
            ;;
    esac
done

if [[ "$INTERACTIVE" == "true" ]]; then
    start_interactive
    exit 0
fi

if [[ $# -eq 0 ]]; then
    usage
    exit 1
fi

run_script "$@"
