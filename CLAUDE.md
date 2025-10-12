# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CIRCT (Circuit IR Compilers and Tools) is an experimental project applying MLIR and LLVM development methodology to hardware design tools. The project provides various dialects for representing and transforming hardware designs, from high-level abstractions down to Verilog generation.

## Key Architecture

### Dialect Structure
CIRCT implements multiple hardware design dialects in `lib/Dialect/` and `include/circt/Dialect/`:
- **FIRRTL**: Flexible Intermediate Representation for RTL
- **HW**: Core hardware dialect for structural circuit descriptions
- **Comb**: Combinational logic operations
- **Seq**: Sequential logic operations
- **SV**: SystemVerilog-specific constructs
- **ESI**: Elastic Silicon Interconnect for latency-insensitive designs
- **Handshake**: Dataflow/handshake-based hardware descriptions
- **Calyx**: High-level synthesis IR
- **Arc**: State machine and reactive system modeling

### Tool Structure
Primary tools in `tools/`:
- `circt-opt`: Main optimization and transformation driver
- `circt-translate`: Translation between formats (FIRRTL to Verilog, etc.)
- `firtool`: Integrated FIRRTL compiler
- `circt-reduce`: Test case reduction tool

## Build Commands

```bash
# Quick build after changes
ninja -C build

# Run all tests
ninja -C build check-circt

# Run integration tests
ninja -C build check-circt-integration

# Build specific tool only
ninja -C build circt-opt

# Run specific dialect tests
build/bin/llvm-lit -v test/Dialect/FIRRTL/
build/bin/llvm-lit -v test/Dialect/HW/

# Run a single test file
build/bin/llvm-lit -v test/Dialect/FIRRTL/canonicalization.mlir
```

## Testing Infrastructure

Tests use LLVM's lit framework with FileCheck patterns. Test files are in `test/` organized by dialect and feature.

Common test patterns:
```mlir
// RUN: circt-opt %s | FileCheck %s
// RUN: circt-opt --canonicalize %s | FileCheck %s --check-prefix=CANON
```

To debug test failures:
```bash
# Run test with verbose output
build/bin/llvm-lit -v -a test/path/to/test.mlir

# Run FileCheck manually to see what's not matching
build/bin/circt-opt test.mlir | build/bin/FileCheck test.mlir
```

## Development Workflow

### Adding Operations to Existing Dialects
1. Define op in `include/circt/Dialect/*/Ops.td`
2. Implement methods in `lib/Dialect/*/Ops.cpp`
3. Add tests in `test/Dialect/*/`
4. Update dialect documentation if needed

### Common Transformations Location
- Canonicalizations: In dialect's `Ops.cpp` via `getCanonicalizationPatterns()`
- Conversion passes: `lib/Conversion/`
- Dialect-specific passes: `lib/Dialect/*/Transforms/`

### Python Bindings
Located in `lib/Bindings/Python/`. Build with `-DCIRCT_BINDINGS_PYTHON_ENABLED=ON`.

## Debugging Tips

- Use `mlir::OpPrintingFlags` for detailed IR printing
- `--mlir-print-ir-after-all` shows IR after each pass
- `--mlir-print-debuginfo` includes location information
- `circt-reduce` automatically minimizes failing test cases

## Code Style

- Follow LLVM coding standards
- Use TableGen for operation definitions when possible
- Prefer declarative patterns in TableGen over C++ when feasible
- Test canonicalizations and folders separately from passes