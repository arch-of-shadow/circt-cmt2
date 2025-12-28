# Cmt2 Dialect

Cmt2 is a rule-based hardware description dialect implementing Guarded Atomic Actions (GAA) semantics. It provides a state-transition based RTL design paradigm with One-Rule-At-A-Time (ORAAT) semantics.

## Documentation

### Core Documentation

- [RationaleCmt2.md](RationaleCmt2.md) - Design rationale and GAA semantics
- [ecmt2-EDSL.md](ecmt2-EDSL.md) - Low-level embedded C++ DSL API
- [ecmt2-Class-API.md](ecmt2-Class-API.md) - High-level declarative class-based API

### Library & Infrastructure

- [ModuleLibrary.md](ModuleLibrary.md) - FIRRTL Module Library and STL components
- [VirtualInterfaceUsage.md](VirtualInterfaceUsage.md) - Interface patterns for module composition

## Quick Start

```cpp
#include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
using namespace circt::cmt2::ecmt2;

// Create circuit
mlir::MLIRContext context;
Circuit circuit("MyDesign", context);

// Add modules using STL components
auto* regMod = stl::STLLibrary::createRegModule(32, 0, circuit);
auto* myModule = circuit.addModule("MyModule");
// ... define rules, methods, values
```

## Key Features

- **Guarded Atomic Actions**: Rules with guard conditions and atomic bodies
- **Interface Abstraction**: Module communication through well-defined interfaces
- **Automatic Scheduling**: Conflict detection and rule scheduling
- **FIRRTL Integration**: Seamless conversion to FIRRTL for synthesis

[include "Dialects/Cmt2.md"]
