# Module Library System

## Overview

The Module Library System provides reusable hardware components for ECMT2 designs through two complementary approaches:

1. **STL Library** (`stl::STLLibrary`) - High-level factory methods for common hardware building blocks
2. **FIRRTL Module Library** (`ModuleLibrary`) - External FIRRTL module catalog with caching

## STL Library (Standard Template Library)

The STL Library provides factory methods that create Module instances for common hardware components.

### Usage

```cpp
#include "circt/Dialect/Cmt2/ECMT2/STLLibrary.h"
using namespace circt::cmt2::ecmt2::stl;

// Create modules using factory methods
auto* wireModule = STLLibrary::createWireModule(32, circuit);
auto* regModule = STLLibrary::createRegModule(32, 0, circuit);
auto* fifoModule = STLLibrary::createFIFO1PushModule(32, circuit);
```

### Available Components

#### Wire Modules
- `createWireModule(width, circuit)` - Wire with specified width
- `createWireDefaultModule(width, init, circuit)` - Wire with default init value

#### Register Modules
- `createRegModule(width, init, circuit)` - Register with specified width and init value

#### FIFO Modules
- `createFIFO1PushModule(dataWidth, circuit)` - Depth-1 FIFO (actively push)
- `createFIFO1PullModule(dataWidth, circuit)` - Depth-1 FIFO (actively pull)
- `createFIFO2IModule(dataWidth, circuit)` - Depth-2 FIFO (independent enq/deq, double buffered)

#### Memory Modules
- `createMem1r1w1cModule(dataWidth, addrWidth, depth, circuit)` - 1R1W memory with read/write latency of 1
- `createMem1r1w0cModule(dataWidth, addrWidth, depth, circuit)` - 1R1W memory with write latency 1, read latency 0

### Example

```cpp
Circuit circuit("Counter", context);

// Create 32-bit register initialized to 0
auto* regMod = STLLibrary::createRegModule(32, 0, circuit);

// Create counter module
auto* counter = circuit.addModule("Counter");
auto clk = counter->addClockArgument("clk");
auto rst = counter->addResetArgument("rst");

// Instantiate register
auto* r = counter->addInstance("r", regMod, {clk.getValue(), rst.getValue()});

// Add increment rule
auto* incr = counter->addRule("incr");
// ... define guard and body
```

---

## FIRRTL Module Library

The FIRRTL Module Library provides a centralized catalog of external FIRRTL modules that can be automatically loaded and instantiated.

## Library Directory Structure

```
lib/Dialect/Cmt2/ModuleLibrary/
├── manifest.yaml                  # Module index/catalog
├── static/                        # Pre-built MLIR modules
│   ├── reg.mlir                  # Simple register
│   ├── fifo.mlir                 # FIFO queue
│   ├── bram.mlir                 # Block RAM
│   └── ...
├── chisel/                        # Chisel generators
│   ├── reg/
│   │   ├── Reg.scala            # Chisel source
│   │   ├── build.sh             # Build script
│   │   └── params.yaml          # Parameter schema
│   ├── fifo/
│   │   ├── FIFO.scala
│   │   ├── build.sh
│   │   └── params.yaml
│   └── ...
└── cache/                         # Cached generated modules
    ├── fifo_depth8_width32.mlir
    └── ...
```

## Manifest Format

The `manifest.yaml` file serves as the module catalog, describing available modules, their parameters, ports, methods, and conflict relationships.

```yaml
version: "1.0"

modules:
  - name: "reg"
    type: "static"
    path: "static/reg.mlir"
    description: "Simple 1-cycle register with ready/enable interface"
    ports:
      clock: "!firrtl.clock"
      reset: "!firrtl.uint<1>"
    methods:
      - name: "read"
        ready: true
        enable: false
        outputs: ["data: !firrtl.uint<?>"]
      - name: "write"
        ready: true
        enable: true
        inputs: ["data: !firrtl.uint<?>"]
    conflict_matrix:
      - [read, write, "SequentialBefore"]  # read < write

  - name: "fifo"
    type: "chisel"
    path: "chisel/fifo"
    description: "Parameterized FIFO with configurable depth and width"
    parameters:
      - name: "depth"
        type: "integer"
        default: 8
        range: [2, 1024]
      - name: "width"
        type: "integer"
        required: true
        range: [1, 128]
    ports:
      clock: "!firrtl.clock"
      reset: "!firrtl.uint<1>"
    methods:
      - name: "enq"
        ready: true
        enable: true
        inputs: ["data: !firrtl.uint<${width}>"]
      - name: "deq"
        ready: true
        enable: true
        outputs: ["data: !firrtl.uint<${width}>"]
    conflict_matrix:
      - [enq, deq, "ConflictFree"]
    build:
      command: "./build.sh"
      args: ["--depth=${depth}", "--width=${width}"]
      output_pattern: "fifo_depth${depth}_width${width}.mlir"
      cache: true

  - name: "bram"
    type: "chisel"
    path: "chisel/bram"
    description: "Dual-port block RAM"
    parameters:
      - name: "addr_width"
        type: "integer"
        required: true
      - name: "data_width"
        type: "integer"
        required: true
    # ... similar structure ...
```

## Module Types

### Static Modules

**Type**: `static`

Static modules are pre-built MLIR files that are loaded directly without any build step.

**Characteristics:**
- Pre-built MLIR files
- Fast loading (no build step)
- Use for simple, non-parametric modules
- Example: basic registers, simple counters

**Advantages:**
- Instant loading
- No build dependencies
- Simple to maintain

**Disadvantages:**
- Cannot be parameterized
- Requires manual updates for changes

### Chisel-Generated Modules

**Type**: `chisel`

Chisel-generated modules are built on-demand from Chisel Scala source code with specified parameters.

**Characteristics:**
- Chisel Scala source code
- Built on-demand with parameters
- Results cached for reuse
- Use for complex/parametric modules
- Example: FIFOs with configurable depth, parametric memories

**Advantages:**
- Parametric design support
- Reuse existing Chisel infrastructure
- Flexible and expressive

**Disadvantages:**
- Requires Chisel toolchain
- Slower first build (cached afterward)
- More complex setup

## Build Scripts

Build scripts are shell scripts that generate MLIR from Chisel source code with specified parameters.

**Example `build.sh` for FIFO:**

```bash
#!/bin/bash
# Build FIFO with parameters
DEPTH=$1
WIDTH=$2

# Generate Chisel code with parameters
sbt "runMain fifo.FIFOGenerator --depth=$DEPTH --width=$WIDTH --output=output.fir"

# Convert FIRRTL to MLIR
firtool output.fir --format=fir --emit-mlir > fifo_depth${DEPTH}_width${WIDTH}.mlir

# Output the generated file path
echo "fifo_depth${DEPTH}_width${WIDTH}.mlir"
```

**Requirements:**
- Must be executable
- Take parameters as command-line arguments
- Generate MLIR output file
- Echo the output file path to stdout

## Parameter Schema

The `params.yaml` file defines the parameter schema for Chisel modules, including types, constraints, and descriptions.

```yaml
parameters:
  - name: depth
    type: integer
    description: "FIFO depth (number of entries)"
    constraints:
      min: 2
      max: 1024
      power_of_two: true

  - name: width
    type: integer
    description: "Data width in bits"
    constraints:
      min: 1
      max: 128
```

## API Design

### ModuleLibrary Class

The `ModuleLibrary` class is a singleton that manages the module catalog and handles module loading.

```cpp
// In include/circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h
namespace circt::cmt2::ecmt2 {

/// Module library manager - singleton pattern
class ModuleLibrary {
public:
  static ModuleLibrary& getInstance();

  /// Initialize library from manifest
  LogicalResult loadManifest(StringRef path);

  /// Query if a module exists
  bool hasModule(StringRef name) const;

  /// Get module metadata
  struct ModuleInfo {
    std::string name;
    std::string description;
    enum Type { Static, Chisel } type;
    std::string path;

    // Port information
    llvm::StringMap<mlir::Type> ports;

    // Method/Value information
    struct MethodInfo {
      std::string name;
      bool hasReady;
      bool hasEnable;
      std::vector<std::pair<std::string, mlir::Type>> inputs;
      std::vector<std::pair<std::string, mlir::Type>> outputs;
    };
    std::vector<MethodInfo> methods;

    // Conflict matrix
    struct ConflictEntry {
      std::string func1, func2;
      enum Relation { Conflict, ConflictFree, SequentialBefore } relation;
    };
    std::vector<ConflictEntry> conflictMatrix;

    // Parameters (for Chisel modules)
    struct ParamInfo {
      std::string name;
      std::string type;
      std::optional<int64_t> defaultValue;
      bool required;
    };
    std::vector<ParamInfo> parameters;
  };

  std::optional<ModuleInfo> getModuleInfo(StringRef name) const;

  /// Load/generate FIRRTL module MLIR
  /// For static modules: loads from file
  /// For Chisel modules: builds with parameters (cached)
  FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
  loadModule(StringRef name,
             const llvm::StringMap<int64_t>& params,
             mlir::MLIRContext& context);

  /// Insert module into circuit
  /// This is called automatically by Circuit::addExternalModule
  LogicalResult insertModuleIntoCircuit(
      StringRef name,
      const llvm::StringMap<int64_t>& params,
      mlir::OpBuilder& builder,
      mlir::Location loc);

private:
  ModuleLibrary() = default;

  // Module catalog
  llvm::StringMap<ModuleInfo> modules_;

  // Cache for generated modules
  struct CacheKey {
    std::string moduleName;
    std::map<std::string, int64_t> params;
    bool operator<(const CacheKey& other) const;
  };
  std::map<CacheKey, std::string> cache_; // CacheKey -> cached file path

  // Helper methods
  LogicalResult loadStaticModule(const ModuleInfo& info,
                                 mlir::MLIRContext& context,
                                 mlir::OwningOpRef<mlir::ModuleOp>& result);

  LogicalResult buildChiselModule(const ModuleInfo& info,
                                  const llvm::StringMap<int64_t>& params,
                                  mlir::MLIRContext& context,
                                  mlir::OwningOpRef<mlir::ModuleOp>& result);

  std::string getCachePath(const std::string& moduleName,
                          const llvm::StringMap<int64_t>& params) const;
};

} // namespace circt::cmt2::ecmt2
```

### Integration with Circuit

The `Circuit::addExternalModule` method is enhanced to automatically query the library and insert modules.

```cpp
// In Circuit.cpp - enhanced addExternalModule
ExternalModule* Circuit::addExternalModule(
    llvm::StringRef name,
    llvm::StringRef firrtlModuleName,
    const llvm::StringMap<int64_t>& params) {

  // Check library for the FIRRTL module
  auto& library = ModuleLibrary::getInstance();
  if (library.hasModule(firrtlModuleName)) {
    // Get module info for conflict matrix, etc.
    auto moduleInfo = library.getModuleInfo(firrtlModuleName);

    // Load/generate the FIRRTL module
    if (failed(library.insertModuleIntoCircuit(
            firrtlModuleName, params, builder_, loc_))) {
      // Fallback: create placeholder external module
    }

    // Create ExternalModule wrapper with metadata
    auto extModule = std::make_unique<ExternalModule>(
        name, firrtlModuleName, builder_, loc_);

    // Apply conflict matrix from library
    if (moduleInfo) {
      for (auto& entry : moduleInfo->conflictMatrix) {
        switch (entry.relation) {
        case ModuleInfo::ConflictEntry::Conflict:
          extModule->addConflict(entry.func1, entry.func2);
          break;
        case ModuleInfo::ConflictEntry::ConflictFree:
          extModule->addConflictFree(entry.func1, entry.func2);
          break;
        case ModuleInfo::ConflictEntry::SequentialBefore:
          extModule->addSequenceBefore(entry.func1, entry.func2);
          break;
        }
      }
    }

    auto* ptr = extModule.get();
    externalModules_.push_back(std::move(extModule));
    return ptr;
  }

  // Module not in library - create basic external reference
  auto extModule = std::make_unique<ExternalModule>(
      name, firrtlModuleName, builder_, loc_);
  auto* ptr = extModule.get();
  externalModules_.push_back(std::move(extModule));
  return ptr;
}
```

## Usage Example

```cpp
// Initialize library (once at program start)
auto& library = ModuleLibrary::getInstance();
library.loadManifest("path/to/circt/lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml");

// Create circuit
Circuit circuit("Top", context);

// Add external module - library automatically loaded
auto* fifoMod = circuit.addExternalModule("MyFIFO", "fifo", {
  {"depth", 16},
  {"width", 32}
});
// Library builds: chisel/fifo/build.sh --depth=16 --width=32
// Output cached to: cache/fifo_depth16_width32.mlir
// Module definition inserted into circuit

// Use the module
auto* topMod = circuit.addModule("Top");
auto* fifo = topMod->addInstance("buffer", fifoMod, {clk, rst});

// Conflict matrix automatically applied from manifest
```

## Caching Strategy

The library implements an intelligent caching system to avoid redundant builds.

### Cache Key

The cache key is a hash of `(module_name, sorted_params)`. For example:
- `fifo` with `{depth=16, width=32}` → `cache/fifo_depth16_width32.mlir`

### Cache Location

All cached files are stored in `ModuleLibrary/cache/`.

### Cache Validation

1. Check if cached file exists
2. Verify file timestamp vs source timestamp
3. Validate MLIR on load

### Cache Invalidation

**Manual**: Delete cache directory

**Automatic**: Source file modified (timestamp check)

## Build Integration

```cmake
# In lib/Dialect/Cmt2/CMakeLists.txt
add_custom_target(cmt2-module-library
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/ModuleLibrary
          ${CMAKE_BINARY_DIR}/lib/Dialect/Cmt2/ModuleLibrary
  COMMENT "Copying Cmt2 module library"
)

add_dependencies(CIRCTECMT2 cmt2-module-library)
```

## Error Handling

The library provides comprehensive error handling for various failure scenarios:

1. **Missing Module**: Warning + fallback to basic external reference
2. **Build Failure**: Detailed error message with build command output
3. **Invalid Parameters**: Validate against schema, provide clear error
4. **Parse Failure**: Report MLIR parse error with file location

## Benefits

1. **Reusability**: Common modules (reg, FIFO, RAM) shared across designs
2. **Parametric Design**: Generate modules with specific parameters
3. **Performance**: Caching avoids redundant builds
4. **Correctness**: Conflict matrices defined once, applied automatically
5. **Extensibility**: Easy to add new modules to library
6. **Integration**: Seamless with existing ECMT2 DSL
