# MLIR Ops and Passes Development Workflow

This skill documents the standard workflow for adding or updating MLIR operations, attributes, and passes in the circt-cmt2 project.

## Overview

When modifying MLIR dialect components, follow these steps in order:

1. Edit TableGen definitions (`.td` files)
2. Edit C++ implementations (`.h` and `.cpp` files)
3. Update CMakeLists.txt if needed
4. Build and fix compilation errors
5. Update Python bindings if applicable
6. Write tests and examples
7. Run tests and validate output
8. Update tracking documentation

---

## Step 1: Edit TableGen Definitions

### Adding/Modifying Operations (`Cmt2Ops.td`)

Location: `include/circt/Dialect/Cmt2/Cmt2Ops.td`

```tablegen
def MyNewOp : Cmt2Op<"my.new", [/* traits */]> {
  let summary = "Brief description";
  let description = [{ Detailed description }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    // Add arguments...
  );

  let results = (outs /* result types */);

  let assemblyFormat = [{ /* format */ }];

  // For custom parsing/printing:
  // let hasCustomAssemblyFormat = 1;

  // For verification:
  // let hasVerifier = 1;

  // For backward-compatible builders:
  let builders = [
    OpBuilder<(ins "arg1":$arg1), [{
      build($_builder, $_state, arg1, /*defaults*/);
    }]>
  ];

  let extraClassDeclaration = [{
    // Helper methods
  }];
}
```

### Adding Attributes (`Cmt2Attributes.td`)

Location: `include/circt/Dialect/Cmt2/Cmt2Attributes.td`

```tablegen
def MyAttr : AttrDef<Cmt2Dialect, "MyAttr"> {
  let mnemonic = "myattr";
  let parameters = (ins "int64_t":$value);
  let assemblyFormat = [{ `<` $value `>` }];
  let genVerifyDecl = 1;  // If verification needed
}
```

### Adding Passes (`Cmt2Passes.td`)

Location: `include/circt/Dialect/Cmt2/Cmt2Passes.td`

```tablegen
def MyPass : Pass<"cmt2-my-pass", "CircuitOp"> {
  let summary = "Brief description";
  let description = [{ Detailed description }];
  let constructor = "circt::cmt2::createMyPass()";
  let options = [
    Option<"optionName", "option-name", "type", "default", "description">
  ];
}
```

---

## Step 2: Edit C++ Implementations

### Operation Implementation (`Cmt2Ops.cpp`)

Location: `lib/Dialect/Cmt2/Cmt2Ops.cpp`

```cpp
// For custom parsing:
ParseResult MyNewOp::parse(OpAsmParser &parser, OperationState &result) {
  // Parse logic...
  return success();
}

// For custom printing:
void MyNewOp::print(OpAsmPrinter &p) {
  // Print logic...
}

// For verification:
LogicalResult MyNewOp::verify() {
  // Validation logic...
  return success();
}
```

### Attribute Implementation (`Cmt2Attributes.cpp`)

Location: `lib/Dialect/Cmt2/Cmt2Attributes.cpp`

```cpp
LogicalResult MyAttr::verify(
    ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError,
    int64_t value) {
  if (value < 0)
    return emitError() << "value must be non-negative";
  return success();
}
```

### Pass Implementation

Location: `lib/Dialect/Cmt2/Transforms/MyPass.cpp`

```cpp
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/Pass/Pass.h"

namespace circt {
namespace cmt2 {

#define GEN_PASS_DEF_MYPASS
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"

struct MyPass : public impl::MyPassBase<MyPass> {
  void runOnOperation() override {
    auto circuitOp = getOperation();
    // Pass logic...
  }
};

std::unique_ptr<mlir::Pass> createMyPass() {
  return std::make_unique<MyPass>();
}

} // namespace cmt2
} // namespace circt
```

---

## Step 3: Update CMakeLists.txt

### For new source files in `lib/Dialect/Cmt2/`

Edit: `lib/Dialect/Cmt2/CMakeLists.txt`

```cmake
add_circt_dialect_library(CIRCTCmt2
  Cmt2Dialect.cpp
  Cmt2Ops.cpp
  Cmt2Attributes.cpp  # Add new files here
  NewFile.cpp
  ...
)
```

### For new subdirectories (e.g., Analysis)

1. Create subdirectory CMakeLists.txt:
```cmake
add_circt_library(CIRCTCmt2Analysis
  TimingAnalysis.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MAIN_INCLUDE_DIR}/circt/Dialect/Cmt2/Analysis

  DEPENDS
  MLIRCmt2IncGen

  LINK_LIBS PUBLIC
  CIRCTCmt2
  MLIRIR
)
```

2. Add to parent CMakeLists.txt:
```cmake
add_subdirectory(Analysis)
```

### For TableGen attribute generation

Edit: `include/circt/Dialect/Cmt2/CMakeLists.txt`

```cmake
set(LLVM_TARGET_DEFINITIONS Cmt2.td)
mlir_tablegen(Cmt2Attributes.h.inc -gen-attrdef-decls -attrdefs-dialect=cmt2)
mlir_tablegen(Cmt2Attributes.cpp.inc -gen-attrdef-defs -attrdefs-dialect=cmt2)
mlir_tablegen(Cmt2Enums.h.inc -gen-enum-decls)
mlir_tablegen(Cmt2Enums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(CIRCTCmt2AttrsIncGen)
add_dependencies(circt-headers CIRCTCmt2AttrsIncGen)
```

---

## Step 4: Build and Fix Errors

### Build Command

```bash
ninja -C build
```

### Common Errors and Fixes

| Error | Cause | Fix |
|-------|-------|-----|
| `no matching function for call to 'build'` | Missing builder for new attributes | Add custom builder in `.td` |
| `def already exists` | Double include in TableGen | Remove duplicate include |
| `unknown type name` | Missing include | Add `using namespace` or include |
| `'this' argument has type 'const X'` | Const method calling non-const | Remove `const` from method |
| `defs belonging to more than one dialect` | Missing dialect flag | Add `-attrdefs-dialect=cmt2` to CMake |

### Iterative Fix Process

```bash
# Build and check errors
ninja -C build 2>&1 | grep -E "error:|Error:" | head -20

# Get context for an error
ninja -C build 2>&1 | grep -B10 "error:" | head -30
```

---

## Step 5: Update Python Bindings (if applicable)

Location: `lib/Bindings/Python/Cmt2Module.cpp`

For new ops that need Python exposure, add bindings:

```cpp
// In the module initialization
m.def("create_my_op", [](MlirContext ctx, ...) {
  // Create op via C API
});
```

### Test Python bindings

```bash
cd build && ninja check-circt-python
```

---

## Step 6: Write Tests and Examples

### MLIR FileCheck Tests

Location: `test/Dialect/Cmt2/`

```mlir
// RUN: circt-opt %s | circt-opt | FileCheck %s

// CHECK-LABEL: cmt2.module @TestModule
cmt2.module @TestModule(%clk: !firrtl.clock) {
  // Test operations...
}
```

### Pass Tests

```mlir
// RUN: circt-opt %s -cmt2-my-pass | FileCheck %s

// CHECK: expected output after pass
```

### Negative Tests (Error Detection)

```mlir
// RUN: circt-opt %s -verify-diagnostics

// expected-error @+1 {{error message}}
cmt2.my.op invalid_args
```

---

## Step 7: Run Tests and Validate

### Run Specific Test

```bash
build/bin/llvm-lit -v test/Dialect/Cmt2/my-test.mlir
```

### Run All Cmt2 Tests

```bash
build/bin/llvm-lit -v test/Dialect/Cmt2/
```

### Validation Methods

1. **IR Dump Analysis**: Check that parsed IR matches expected structure
   ```bash
   build/bin/circt-opt test.mlir --mlir-print-ir-after-all
   ```

2. **Pass Output Verification**: Verify transformation results
   ```bash
   build/bin/circt-opt test.mlir -cmt2-my-pass | build/bin/FileCheck test.mlir
   ```

3. **Simulation/Interpretation** (for hardware):
   ```bash
   # Run CMT2 interpreter
   build/bin/cmt2-dbg test.mlir

   # Or convert to FIRRTL and simulate
   build/bin/circt-opt test.mlir -cmt2-to-firrtl | firtool - -o test.sv
   ```

4. **Python Integration Test**:
   ```python
   import circt
   # Test Python bindings
   ```

---

## Step 8: Update Tracking Documentation

Location: `docs/Dialects/Cmt2/CyclePreciseTimingImplementation.md` (or relevant tracker)

Update:
1. Task status `[ ]` → `[x]`
2. Summary table with new counts
3. Recent Changes section with implementation details

---

## Quick Reference: File Locations

| Component | Header | Implementation | TableGen |
|-----------|--------|----------------|----------|
| Ops | `include/circt/Dialect/Cmt2/Cmt2Ops.h` | `lib/Dialect/Cmt2/Cmt2Ops.cpp` | `include/.../Cmt2Ops.td` |
| Attributes | `include/.../Cmt2Attributes.h` | `lib/.../Cmt2Attributes.cpp` | `include/.../Cmt2Attributes.td` |
| Dialect | `include/.../Cmt2Dialect.h` | `lib/.../Cmt2Dialect.cpp` | `include/.../Cmt2Dialect.td` |
| Passes | `include/.../Cmt2Passes.h` | `lib/.../Transforms/*.cpp` | `include/.../Cmt2Passes.td` |
| Analysis | `include/.../Analysis/*.h` | `lib/.../Analysis/*.cpp` | - |
| Tests | - | - | `test/Dialect/Cmt2/*.mlir` |

---

## Checklist Template

When adding a new op/attribute/pass, copy and use this checklist:

```markdown
- [ ] 1. Edit TableGen definition in `.td` file
- [ ] 2. Add C++ implementation in `.cpp` file
- [ ] 3. Update CMakeLists.txt if new files added
- [ ] 4. Build: `ninja -C build`
- [ ] 5. Fix any compilation errors
- [ ] 6. Add backward-compatible builders if needed
- [ ] 7. Add verifiers if needed
- [ ] 8. Update Python bindings (if applicable)
- [ ] 9. Create test file in `test/Dialect/Cmt2/`
- [ ] 10. Run tests: `build/bin/llvm-lit -v test/Dialect/Cmt2/my-test.mlir`
- [ ] 11. Validate output (IR dump, simulation, etc.)
- [ ] 12. Update tracking documentation
```
