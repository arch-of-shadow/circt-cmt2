#!/bin/bash
# CMT2 to SystemVerilog compilation script
# Supports both MLIR (.mlir) and PyCMT2 (.py) inputs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

usage() {
    echo "Usage: $0 <input-file> [output-dir]"
    echo ""
    echo "Compiles CMT2 design to SystemVerilog."
    echo ""
    echo "Input can be:"
    echo "  - .mlir file: CMT2 MLIR directly"
    echo "  - .py file:   PyCMT2 Python script (generates MLIR first)"
    echo ""
    echo "Options:"
    echo "  output-dir   Directory for output files (default: ./output)"
    echo ""
    echo "Examples:"
    echo "  $0 test.mlir"
    echo "  $0 design.py ./sim_output"
    echo "  $0 examples/PyCMT2/gcd.py gcd_workspace"
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

INPUT_FILE="$1"
OUTPUT_DIR="${2:-./output}"

if [[ ! -f "$INPUT_FILE" ]]; then
    echo "Error: Input file not found: $INPUT_FILE"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Get file extension
EXT="${INPUT_FILE##*.}"
BASENAME="$(basename "$INPUT_FILE" ".$EXT")"

echo "=== CMT2 to SystemVerilog Compilation ==="
echo "Input:  $INPUT_FILE"
echo "Output: $OUTPUT_DIR/"

# Step 1: Handle PyCMT2 input
MLIR_FILE="$OUTPUT_DIR/$BASENAME.mlir"
if [[ "$EXT" == "py" ]]; then
    echo ""
    echo "Step 1: Running PyCMT2 to generate MLIR..."
    PYTHONPATH="$PROJECT_ROOT/build/tools/circt/python_packages/circt_core:$PYTHONPATH" \
        python3 "$INPUT_FILE" > "$MLIR_FILE"
    echo "  Generated: $MLIR_FILE"
else
    echo ""
    echo "Step 1: Using MLIR input directly..."
    cp "$INPUT_FILE" "$MLIR_FILE"
fi

# Step 2: Run CMT2 passes
FIRRTL_FILE="$OUTPUT_DIR/$BASENAME.firrtl.mlir"
echo ""
echo "Step 2: Running CMT2 compilation passes..."
"$BUILD_DIR/bin/circt-opt" "$MLIR_FILE" \
    --cmt2-inline-private-funcs \
    --cmt2-static-inference \
    --cmt2-compile-static \
    --lower-cmt2-to-firrtl \
    -o "$FIRRTL_FILE" 2>&1 || {
        echo "Warning: Full pipeline failed, trying minimal conversion..."
        "$BUILD_DIR/bin/circt-opt" "$MLIR_FILE" \
            --cmt2-inline-private-funcs \
            --lower-cmt2-to-firrtl \
            -o "$FIRRTL_FILE"
    }
echo "  Generated: $FIRRTL_FILE"

# Step 3: Generate SystemVerilog
SV_FILE="$OUTPUT_DIR/$BASENAME.sv"
echo ""
echo "Step 3: Generating SystemVerilog..."
"$BUILD_DIR/bin/firtool" "$FIRRTL_FILE" \
    --format=mlir \
    --lowering-options=disallowLocalVariables \
    -o "$SV_FILE" 2>&1 || {
        echo "Warning: firtool failed, trying without lowering options..."
        "$BUILD_DIR/bin/firtool" "$FIRRTL_FILE" \
            --format=mlir \
            -o "$SV_FILE"
    }
echo "  Generated: $SV_FILE"

echo ""
echo "=== Compilation Complete ==="
echo "Output files:"
echo "  MLIR:   $MLIR_FILE"
echo "  FIRRTL: $FIRRTL_FILE"
echo "  SV:     $SV_FILE"
