# CMT2 Compilation Workflow

## Prerequisites

Ensure CIRCT is built:
```bash
ninja -C build
```

## Pipeline Stages

### Stage 1: CMT2 → FIRRTL

Basic conversion:
```bash
build/bin/circt-opt input.mlir --lower-cmt2-to-firrtl -o output.mlir
```

With static timing compilation:
```bash
build/bin/circt-opt input.mlir \
    --cmt2-static-inference \
    --cmt2-compile-static \
    --lower-cmt2-to-firrtl \
    -o output.mlir
```

### Stage 2: FIRRTL → SystemVerilog

Using firtool:
```bash
build/bin/firtool output.mlir \
    --format=mlir \
    -o output.sv
```

With optimization:
```bash
build/bin/firtool output.mlir \
    --format=mlir \
    --lowering-options=disallowLocalVariables \
    -o output.sv
```

### Stage 3: Complete Pipeline

All-in-one command:
```bash
build/bin/circt-opt input.mlir \
    --cmt2-static-inference \
    --cmt2-compile-static \
    --lower-cmt2-to-firrtl | \
build/bin/firtool --format=mlir -o output.sv
```

## Pass Reference

### Analysis Passes
- `-cmt2-print-call-info` - Print method/rule call information

### Transformation Passes
- `--cmt2-static-inference` - Infer static timing from step latencies
- `--cmt2-compile-static` - Generate FSM wrappers for static steps
- `--cmt2-inline-modules` - Inline module instances
- `--lower-cmt2-to-firrtl` - Lower CMT2 to FIRRTL

### Debug Options
```bash
# Print IR after each pass
--mlir-print-ir-after-all

# Include source locations
--mlir-print-debuginfo

# Print operation statistics
--mlir-pass-statistics
```

## Common Issues

### Missing FIRRTL modules
Ensure all external FIRRTL modules are defined in the input.

### Type mismatches
Check that FIRRTL types match between CMT2 bindings and FIRRTL modules.

### Undefined symbols
Verify all instances reference defined modules.
