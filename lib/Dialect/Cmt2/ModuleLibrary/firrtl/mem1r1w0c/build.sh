#!/bin/bash
#===- build.sh - Build script for 1R1W Async Memory ------------*- Bash -*-===#
#
# Generates FIRRTL 1R1W asynchronous memory module (0-cycle read latency)
# Based on Rust CMT2: crates/cmt2/core/src/cmtrs/stl/mem.rs
#
# Parameters:
#   data_width - Width of data type (e.g., 32)
#   addr_width - Width of address type (e.g., 10)
#   depth      - Memory depth (number of entries, e.g., 1024)
#
#===------------------------------------------------------------------------===#

set -e  # Exit on error

# Parse named parameters (key=value format)
DATA_WIDTH=32    # Default
ADDR_WIDTH=10    # Default
DEPTH=1024       # Default

for arg in "$@"; do
  case $arg in
    data_width=*)
      DATA_WIDTH="${arg#*=}"
      echo "DEBUG: Parsed data_width=$DATA_WIDTH" >&2
      ;;
    addr_width=*)
      ADDR_WIDTH="${arg#*=}"
      echo "DEBUG: Parsed addr_width=$ADDR_WIDTH" >&2
      ;;
    depth=*)
      DEPTH="${arg#*=}"
      echo "DEBUG: Parsed depth=$DEPTH" >&2
      ;;
    *)
      echo "DEBUG: Unknown parameter: $arg" >&2
      ;;
  esac
done

echo "DEBUG: Final DATA_WIDTH=$DATA_WIDTH, ADDR_WIDTH=$ADDR_WIDTH, DEPTH=$DEPTH" >&2

# Output filename includes all parameters
OUTPUT_FILE="Mem1r1w0c_w${DATA_WIDTH}_a${ADDR_WIDTH}_d${DEPTH}.mlir"
MODULE_NAME="Mem1r1w0c_w${DATA_WIDTH}_a${ADDR_WIDTH}_d${DEPTH}"

echo "Generating FIRRTL 1R1W async memory (data_width=$DATA_WIDTH, addr_width=$ADDR_WIDTH, depth=$DEPTH)..." >&2

cat > "$OUTPUT_FILE" << EOF
module {
  firrtl.circuit "${MODULE_NAME}" {
    firrtl.module @${MODULE_NAME}(
      in %clk: !firrtl.clock,
      in %rst: !firrtl.uint<1>,
      in %en: !firrtl.uint<1>,
      in %raddr: !firrtl.uint<${ADDR_WIDTH}>,
      out %rdata: !firrtl.uint<${DATA_WIDTH}>,
      in %wen: !firrtl.uint<1>,
      in %waddr: !firrtl.uint<${ADDR_WIDTH}>,
      in %wdata: !firrtl.uint<${DATA_WIDTH}>
    ) {
      // Memory declaration using FIRRTL memory primitive
      // Async read: readLatency = 0, writeLatency = 1
      %mymemory_r, %mymemory_w = firrtl.mem Undefined {
        depth = ${DEPTH} : i64,
        name = "mymemory",
        portNames = ["r", "w"],
        readLatency = 0 : i32,
        writeLatency = 1 : i32
      } : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data flip: uint<${DATA_WIDTH}>>, !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data: uint<${DATA_WIDTH}>, mask: uint<1>>

      // Read port connections (asynchronous - no clock needed for read)
      %r_addr = firrtl.subfield %mymemory_r[addr] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data flip: uint<${DATA_WIDTH}>>
      %r_en = firrtl.subfield %mymemory_r[en] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data flip: uint<${DATA_WIDTH}>>
      %r_clk = firrtl.subfield %mymemory_r[clk] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data flip: uint<${DATA_WIDTH}>>
      %r_data = firrtl.subfield %mymemory_r[data] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data flip: uint<${DATA_WIDTH}>>

      firrtl.matchingconnect %r_addr, %raddr : !firrtl.uint<${ADDR_WIDTH}>
      firrtl.matchingconnect %r_en, %en : !firrtl.uint<1>
      firrtl.matchingconnect %r_clk, %clk : !firrtl.clock
      firrtl.matchingconnect %rdata, %r_data : !firrtl.uint<${DATA_WIDTH}>

      // Write port connections
      %w_addr = firrtl.subfield %mymemory_w[addr] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data: uint<${DATA_WIDTH}>, mask: uint<1>>
      %w_en = firrtl.subfield %mymemory_w[en] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data: uint<${DATA_WIDTH}>, mask: uint<1>>
      %w_clk = firrtl.subfield %mymemory_w[clk] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data: uint<${DATA_WIDTH}>, mask: uint<1>>
      %w_data = firrtl.subfield %mymemory_w[data] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data: uint<${DATA_WIDTH}>, mask: uint<1>>
      %w_mask = firrtl.subfield %mymemory_w[mask] : !firrtl.bundle<addr: uint<${ADDR_WIDTH}>, en: uint<1>, clk: clock, data: uint<${DATA_WIDTH}>, mask: uint<1>>

      firrtl.matchingconnect %w_addr, %waddr : !firrtl.uint<${ADDR_WIDTH}>
      firrtl.matchingconnect %w_en, %wen : !firrtl.uint<1>
      firrtl.matchingconnect %w_clk, %clk : !firrtl.clock
      firrtl.matchingconnect %w_data, %wdata : !firrtl.uint<${DATA_WIDTH}>

      // Write mask is always all 1s (full word write)
      %c1_mask = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.matchingconnect %w_mask, %c1_mask : !firrtl.uint<1>
    }
  }
}
EOF

echo "$OUTPUT_FILE"