# Interface Communication Demo (interface_demo.cpp)

## Overview

This example demonstrates **interface-based communication** between two hardware modules at the same hierarchical level using the Cmt2 ECMT2 embedded DSL. Module A can call methods on Module B through a well-defined interface mechanism.

## Architecture

```
TopModule (InterfaceDemo)
├── ModuleA (consumer) - uses Calculator interface
└── ModuleB (provider) - implements Calculator interface
```

### Key Concepts Demonstrated

1. **Interface Declaration vs Definition**
   - `ModuleA` *declares* it needs a `Calculator` interface
   - `TopModule` *defines* how `ModuleB` implements the `Calculator` interface
   - Interface binding connects the declaration to the definition

2. **Cross-Module Method Calls**
   - `ModuleA` can call methods on `ModuleB` through the interface
   - Methods include: `add(a, b)`, `multiply(a, b)`, and `getValue()`
   - All calls are type-safe and properly synchronized

3. **Hierarchical Composition**
   - Top-level module instantiates both modules
   - Interface binding happens at instantiation time
   - Clear separation of concerns between modules

## Interface Definition

```cpp
// Define the Calculator interface at the circuit level
auto *calculatorInterface = circuit.addInterface("Calculator");

// Method: add(a, b) -> sum
calculatorInterface->addMethod("add",
    {{"a", UInt32}, {"b", UInt32}}, {UInt32});

// Method: multiply(a, b) -> product
calculatorInterface->addMethod("multiply",
    {{"a", UInt32}, {"b", UInt32}}, {UInt32});

// Value: getValue() -> stored_value
calculatorInterface->addValue("getValue", {},
    {TypeAttr::get(UInt32)});
```

## Module B Implementation (Interface Provider)

Module B **implements** the Calculator interface:

```cpp
// Method implementations
auto *addMethod = moduleB->addMethod("add", args, results);
addMethod->body([&](auto &builder, auto &args) {
    // Perform addition: sum = a + b
    Signal signalA(args[0], &builder, loc);
    Signal signalB(args[1], &builder, loc);
    Signal sum = signalA + signalB;
    Signal result = sum.bits(31, 0);

    // Store and return result
    moduleBReg->callMethod("write", {result.getValue()}, builder);
    builder.create<ReturnOp>(loc, mlir::ValueRange{result.getValue()});
});
```

## Module A Implementation (Interface Consumer)

Module A **uses** the Calculator interface:

```cpp
// Declare that ModuleA needs a Calculator interface
auto *calculatorDecl = moduleA->defineInterface("calc", "Calculator");

// Use the interface in a method
auto processArgs = {input, constant.getValue()};
auto addResults = calculatorDecl->callMethod("add", addArgs, builder);

auto multiplyArgs = {addResults[0], multiplyConstant.getValue()};
auto finalResults = calculatorDecl->callMethod("multiply", multiplyArgs, builder);
```

## Interface Binding (Top Level)

The top-level module **connects** the interface:

```cpp
// Define how ModuleB implements Calculator
auto *moduleBCalcDef = topModule->defineInterfaceDef("ModuleBCalc", "Calculator");

// Bind interface methods to ModuleB's actual methods
moduleBCalcDef->bind("ModuleB", "add", "add");           // Calculator.add -> ModuleB.add
moduleBCalcDef->bind("ModuleB", "multiply", "multiply"); // Calculator.multiply -> ModuleB.multiply
moduleBCalcDef->bind("ModuleB", "read", "getValue");    // Calculator.getValue -> ModuleB.read
moduleBCalcDef->finalize();

// Instantiate ModuleA with the interface binding
auto *moduleAInst = topModule->addInstance("ModuleA", moduleA,
    {topClk.getValue(), topRst.getValue()},
    {{"ModuleBCalc", "calc"}}); // Bind ModuleBCalc to ModuleA's calc interface
```

## Key Features

### 1. Type Safety
- All method calls are type-checked at compile time
- Interface definitions enforce consistent signatures
- No runtime type casting required

### 2. Modularity
- Modules can be developed independently
- Interfaces serve as contracts between modules
- Easy to swap implementations (different ModuleB implementations)

### 3. Scalability
- Multiple modules can use the same interface
- Interfaces can have multiple methods and values
- Hierarchical composition through multiple interface levels

### 4. Hardware Generation
- Generates proper FIRRTL hardware descriptions
- Method calls become hardware module instantiations
- Interface bindings become wiring connections

## Building and Running

```bash
# Build the demo
pixi run build

# Run the interface demo
./build/bin/ecmt2-interface-demo

# Expected output:
# ✓ ModuleA can successfully call ModuleB's methods through interface
# ✓ Interface binding: ModuleBCalc connects ModuleA.calc to ModuleB
# ✓ Cross-module communication working correctly
# ✓ Generated FIRRTL hardware description ready for synthesis
```

## Generated Hardware

The demo generates both:
1. **MLIR IR** - Shows the high-level Cmt2 representation
2. **FIRRTL** - Lower-level hardware description ready for synthesis

The generated hardware includes:
- Module instantiations with proper clock/reset connections
- Interface method call wiring
- Register implementations for state storage
- Combinational logic for arithmetic operations

## Use Cases

This interface pattern is useful for:

- **Service-oriented architecture** - Hardware modules providing services to others
- **IP core integration** - Connecting pre-designed modules through standard interfaces
- **Modular design** - Large designs split into manageable, reusable modules
- **Design reuse** - Same interface, different implementations (e.g., different ALU designs)

## Advanced Extensions

The interface mechanism can be extended for:

- **Parameterized interfaces** - Generic interfaces with type parameters
- **Interface composition** - Interfaces that extend other interfaces
- **Bidirectional communication** - Methods with both inputs and outputs
- **Streaming interfaces** - For dataflow and pipeline communication
- **Memory interfaces** - Standardized access to different memory types

## References

- [Cmt2 ECMT2 Documentation](../../../docs/Dialects/Cmt2/ecmt2-EDSL.md)
- [Hello Example](hello_example.cpp) - Basic interface usage
- [Cmt2 Rationale](../../../docs/Dialects/Cmt2/RationaleCmt2.md) - Design philosophy