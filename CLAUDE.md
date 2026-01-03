# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Before Starting Any Task

**IMPORTANT**: When working on implementation tasks:
1. **Read the plan/tracker first** - Check `docs/Dialects/Cmt2/CyclePreciseTimingImplementation.md` or relevant tracker to understand the task context and dependencies
2. **Follow the skill workflow** - See `.claude/skills/mlir-ops-passes.md` for the standard workflow when adding/modifying MLIR ops, attributes, or passes
3. **Update tracking after completion** - Mark tasks as completed and document changes in the tracker

## Project Overview

CIRCT (Circuit IR Compilers and Tools) is an experimental project applying MLIR and LLVM development methodology to hardware design tools. The project provides various dialects for representing and transforming hardware designs, from high-level abstractions down to Verilog generation.

## Cmt2 Dialect

Cmt2 implements Guarded Atomic Actions (GAA) with One-Rule-At-A-Time (ORAAT) semantics for hardware design. See `docs/Dialects/Cmt2/RationaleCmt2.md` for design philosophy.

### Documentation (`docs/Dialects/Cmt2`)

- `_index.md` - Overview and quick start
- `RationaleCmt2.md` - Design rationale and GAA semantics
- `ecmt2-EDSL.md` - Low-level C++ embedded DSL API
- `ecmt2-Class-API.md` - High-level declarative class-based API
- `ModuleLibrary.md` - STL and FIRRTL module library
- `VirtualInterfaceUsage.md` - Interface patterns for module composition

### Core Operations

- `cmt2.circuit` - Top-level circuit container
- `cmt2.module` - CMT2 module with rules, methods, values
- `cmt2.module.extern.firrtl` - External FIRRTL module binding
- `cmt2.rule` - Rule with guard and body regions
- `cmt2.method` - Action method (can modify state)
- `cmt2.value` - Value method (read-only)
- `cmt2.instance` - Module instantiation
- `cmt2.call` - Method/value invocation
- `cmt2.interface` / `cmt2.interface.decl` / `cmt2.interface.def` - Interface system

### ECMT2 API Structure

**Low-level API** (`circt::cmt2::ecmt2`):
- `Circuit` - Main entry point, manages modules and interfaces
- `Module` / `ExternalModule` - Module wrappers
- `Rule`, `Method`, `Value` - Function-like operations
- `Instance` - Instance management
- `Signal`, `Clock`, `Reset` - Signal types with operator overloading
- `InterfaceDecl`, `InterfaceDef` - Interface bindings
- `STLLibrary` - Standard components (Reg, Wire, FIFO, Memory)

**High-level API** (`circt::cmt2::ecmt2::highlevel`):
- Declarative class-based modules extending `Cmt2Module`
- Template-based `Method<RetType, Args...>`, `Value<RetType>`, `Rule`
- Registration macros: `INIT_RULE()`, `INIT_METHOD()`, `INIT_VALUE()`
- Helper functions: `Return()`, `Add()`, `UIntConst()`, `If()`, etc.

### Key Files

| Component | Header | Implementation |
|-----------|--------|----------------|
| Circuit | `ECMT2/Circuit.h` | `ECMT2/Circuit.cpp` |
| Module | `ECMT2/Module.h` | `ECMT2/Module.cpp` |
| Signal | `ECMT2/Signal.h` | `ECMT2/Signal.cpp` |
| STL Library | `ECMT2/STLLibrary.h` | `ECMT2/STLLibrary.cpp` |
| Interface | `ECMT2/Interface.h` | `ECMT2/Interface.cpp` |
| High-level | `ECMT2/HighLevel/*.h` | `ECMT2/HighLevel/*.cpp` |
| Ops/Dialect | `Cmt2Ops.h` | `Cmt2Ops.cpp` |
| Transforms | `Transforms/*.h` | `Transforms/*.cpp` |

### Test Files (`test/Dialect/Cmt2`)

- `gcd.mlir` - GCD algorithm example
- `hello.mlir` - Interface mechanism test
- `fifo1-push.mlir` - FIFO with scheduling test
- `virtual-interface.mlir` - Virtual interface patterns
- `if-test.mlir` - Conditional execution test
- `bundle-vector.mlir` - Bundle/vector type tests

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

## Cmt2 Development Workflow

### Adding New STL Components

1. Add factory method declaration in `include/circt/Dialect/Cmt2/ECMT2/STLLibrary.h`
2. Implement in `lib/Dialect/Cmt2/ECMT2/STLLibrary.cpp`
3. Update documentation in `docs/Dialects/Cmt2/ModuleLibrary.md`

### Adding New ECMT2 Features

1. For low-level API: Add to appropriate file in `lib/Dialect/Cmt2/ECMT2/`
2. For high-level API: Add to `lib/Dialect/Cmt2/ECMT2/HighLevel/`
3. Update headers in `include/circt/Dialect/Cmt2/ECMT2/`
4. Add tests in `test/Dialect/Cmt2/`

### Adding New Operations / Attributes / Passes

**IMPORTANT**: See `.claude/skills/mlir-ops-passes.md` for the complete workflow.

Quick checklist:
1. Edit TableGen definitions (`.td` files)
2. Edit C++ implementations (`.h` and `.cpp` files)
3. Update CMakeLists.txt if new files added
4. Build: `ninja -C build` and fix errors
5. Add backward-compatible builders if modifying existing ops
6. Add verifiers (`hasVerifier = 1` in .td, implement in .cpp)
7. Write tests in `test/Dialect/Cmt2/`
8. Run tests: `build/bin/llvm-lit -v test/Dialect/Cmt2/my-test.mlir`
9. Validate: Check IR dumps, run simulation/interpretation if hardware
10. Update tracking documentation

Key files for Cmt2:
- Ops: `include/circt/Dialect/Cmt2/Cmt2Ops.td` → `lib/Dialect/Cmt2/Cmt2Ops.cpp`
- Attrs: `include/circt/Dialect/Cmt2/Cmt2Attributes.td` → `lib/Dialect/Cmt2/Cmt2Attributes.cpp`
- Passes: `include/circt/Dialect/Cmt2/Cmt2Passes.td` → `lib/Dialect/Cmt2/Transforms/*.cpp`
- Analysis: `include/circt/Dialect/Cmt2/Analysis/*.h` → `lib/Dialect/Cmt2/Analysis/*.cpp`

### Running Cmt2 Tests

```bash
# All Cmt2 tests
build/bin/llvm-lit -v test/Dialect/Cmt2/

# Specific test
build/bin/llvm-lit -v test/Dialect/Cmt2/gcd.mlir

# With pass output
build/bin/circt-opt test/Dialect/Cmt2/hello.mlir -cmt2-print-call-info
build/bin/circt-opt test/Dialect/Cmt2/hello.mlir -cmt2-to-firrtl
```

### Common Cmt2 Passes

- `-cmt2-print-call-info` - Print call information analysis
- `-cmt2-to-firrtl` - Convert Cmt2 to FIRRTL
- `-cmt2-inline` - Inline module instances