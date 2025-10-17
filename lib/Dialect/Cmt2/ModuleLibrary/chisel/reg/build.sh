#!/bin/bash
#===- build.sh - Build script for parametric FIRRTL register ----*- Bash -*-===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===#
#
# This script builds the FIRRTLReg Chisel module with configurable width
# and converts it to MLIR format using firtool.
#
#===------------------------------------------------------------------------===#

set -e  # Exit on error

# Get width parameter (default: 32)
WIDTH=${1:-32}

# Output filename
OUTPUT_FILE="Reg_width${WIDTH}.mlir"

# Check if we can use Chisel (requires SBT)
if command -v sbt &> /dev/null; then
    echo "Building FIRRTLReg with Chisel (width=$WIDTH)..." >&2

    # Run Chisel to generate FIRRTL
    sbt "runMain cmt2.lib.FIRRTLRegMain $WIDTH" > /dev/null 2>&1

    # Check if FIRRTL was generated
    FIRRTL_FILE="Reg_width${WIDTH}.fir"
    if [ -f "$FIRRTL_FILE" ]; then
        # Convert FIRRTL to MLIR using firtool
        if command -v firtool &> /dev/null; then
            echo "Converting FIRRTL to MLIR using firtool..." >&2
            firtool --format=fir --ir-fir "$FIRRTL_FILE" > "$OUTPUT_FILE"

            # Clean up intermediate FIRRTL file
            rm -f "$FIRRTL_FILE"
        else
            echo "Warning: firtool not found, cannot convert to MLIR" >&2
            echo "Falling back to direct MLIR generation..." >&2
        fi
    else
        echo "Warning: Chisel did not generate $FIRRTL_FILE" >&2
        echo "Falling back to direct MLIR generation..." >&2
    fi
fi

# If Chisel build failed or firtool is not available, generate MLIR directly
# This is a fallback for development/testing
if [ ! -f "$OUTPUT_FILE" ]; then
    echo "Generating MLIR directly (fallback mode)..." >&2

    cat > "$OUTPUT_FILE" << EOF
module {
  firrtl.circuit "FIRRTLReg" {
    firrtl.module @FIRRTLReg(
      in %clock: !firrtl.clock,
      in %reset: !firrtl.uint<1>,
      in %write_enable: !firrtl.uint<1>,
      in %write_data: !firrtl.uint<${WIDTH}>,
      out %read_ready: !firrtl.uint<1>,
      out %read_data: !firrtl.uint<${WIDTH}>,
      out %write_ready: !firrtl.uint<1>
    ) {
      // Internal register
      %reg = firrtl.reg %clock : !firrtl.clock, !firrtl.uint<${WIDTH}>

      // Read is always ready, returns current register value
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_data, %reg : !firrtl.uint<${WIDTH}>

      // Write is always ready
      firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>

      // On write enable, update register
      firrtl.when %write_enable : !firrtl.uint<1> {
        firrtl.matchingconnect %reg, %write_data : !firrtl.uint<${WIDTH}>
      }
    }
  }
}
EOF
fi

# Make the output file available
echo "$OUTPUT_FILE"
