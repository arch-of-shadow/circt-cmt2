#!/bin/bash
# Generate Wire modules with different widths
# Circuit name has the postfix, but module name is just "Wire"

WIDTH=32  # Default

for arg in "$@"; do
  case $arg in
    width=*)
      WIDTH="${arg#*=}"
      # echo "DEBUG: Parsed width=$WIDTH" >&2
      ;;
    *)
      # echo "DEBUG: Unknown parameter: $arg" >&2
      ;;
  esac
done

OUTPUT_FILE="Wire_w${WIDTH}.mlir"

cat > "$OUTPUT_FILE" <<EOF
module {
  firrtl.circuit "Wire_w${WIDTH}" {
    firrtl.module @Wire_w${WIDTH}(
      in %write_enable: !firrtl.uint<1>,
      out %write_ready: !firrtl.uint<1>,
      in %write_data: !firrtl.uint<${WIDTH}>,
      out %read_data: !firrtl.uint<${WIDTH}>,
      out %read_ready: !firrtl.uint<1>
    ) {
      // Create a wire
      %wire_val = firrtl.wire : !firrtl.uint<${WIDTH}>

      // Default: wire is 0
      %c0 = firrtl.constant 0 : !firrtl.uint<${WIDTH}>
      %c1 = firrtl.constant 1 : !firrtl.uint<1>

      // Conditionally write to wire when write_enable is 1
      %wire_mux = firrtl.mux(%write_enable, %write_data, %c0) : (!firrtl.uint<1>, !firrtl.uint<${WIDTH}>, !firrtl.uint<${WIDTH}>) -> !firrtl.uint<${WIDTH}>
      firrtl.matchingconnect %wire_val, %wire_mux : !firrtl.uint<${WIDTH}>

      // Output is the wire value
      firrtl.matchingconnect %read_data, %wire_val : !firrtl.uint<${WIDTH}>
      firrtl.matchingconnect %write_ready, %c1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1 : !firrtl.uint<1>
    }
  }
}
EOF

echo $OUTPUT_FILE