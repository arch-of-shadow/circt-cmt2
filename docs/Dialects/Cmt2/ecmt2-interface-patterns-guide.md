# ECMT2 Interface Patterns Guide

## Overview

This guide demonstrates interface communication between modules at the same hierarchical level using the ECMT2 DSL. The patterns shown enable clean module-to-module communication without direct wiring dependencies.

## Key Architecture Pattern

```
TopModule
├── ModuleA (Interface Consumer)
└── ModuleB (Interface Provider)
```

- **ModuleB**: Implements the `Calculator` interface, provides arithmetic services
- **ModuleA**: Uses the `Calculator` interface to call ModuleB's methods
- **TopModule**: Instantiates both modules and binds the interface

## Core Interface Concepts

### 1. Interface Declaration vs Definition

#### Interface Declaration (Consumer Side)
```cpp
// In ModuleA: Declares that this module needs a Calculator interface
auto *calculatorDecl = moduleA->defineInterface("calc", "Calculator");
```

#### Interface Definition (Provider Side Binding)
```cpp
// In TopModule: Defines how ModuleB implements the Calculator interface
auto *moduleBCalcDef = topModule->defineInterfaceDef("ModuleBCalc", "Calculator");
moduleBCalcDef->bind("ModuleB", "add", "add");       // Calculator.add -> ModuleB.add
moduleBCalcDef->bind("ModuleB", "read", "getValue"); // Calculator.getValue -> ModuleB.read
```

### 2. Interface Method Definition

#### Interface Level (Declaration)
```cpp
// Define the interface with its methods
auto *calculatorInterface = circuit.addInterface("Calculator");

// Method with arguments and return value
calculatorInterface->addMethod("add",
    {{"a", circt::firrtl::UIntType::get(&context, 32)},
     {"b", circt::firrtl::UIntType::get(&context, 32)}},
    {circt::firrtl::UIntType::get(&context, 32)});

// Value method (like a getter)
calculatorInterface->addValue("getValue", {},
    {mlir::TypeAttr::get(circt::firrtl::UIntType::get(&context, 32))});
```

#### Implementation Level (Provider)
```cpp
// Method implementation in ModuleB
auto *addMethod = moduleB->addMethod("add", args, retType);
addMethod->guard(guardFunction);
addMethod->body(bodyFunction);
addMethod->finalize();

// Value implementation
auto *readMethod = moduleB->addValue("read", {returnType});
readMethod->guard(guardFunction);
readMethod->body(bodyFunction);
readMethod->finalize();
```

### 3. Interface Method Calling

#### From Consumer Module
```cpp
// Calling a method through interface
llvm::SmallVector<mlir::Value> addArgs = {argA, argB};
auto addResults = calculatorDecl->callMethod("add", addArgs, builder);

// Calling a value through interface
auto bValues = calculatorDecl->callValue("getValue", builder);
```

#### From Top Level (Method Forwarding)
```cpp
// Forwarding to instance method
auto *processDataMethodTop = topModule->addMethod("processData", args, retType);
processDataMethodTop->body([&](mlir::OpBuilder &builder, auto args) {
    llvm::SmallVector<mlir::Value> processArgs = {args[0], args[1]};
    auto processResults = moduleAInst->callMethod("processData", processArgs, builder);
    builder.create<circt::cmt2::ReturnOp>(topModule->getLoc(), processResults[0]);
});
```

## Complete Implementation Pattern

### Step 1: Define Interface
```cpp
auto *calculatorInterface = circuit.addInterface("Calculator");
calculatorInterface->addMethod("add",
    {{"a", u32Type}, {"b", u32Type}}, {u32Type});
calculatorInterface->addValue("getValue", {}, {u32Type});
```

### Step 2: Implement Provider Module
```cpp
auto *moduleB = circuit.addModule("ModuleB");
auto *addMethod = moduleB->addMethod("add", args, retType);
addMethod->guard([](auto builder, auto args) { /* always ready */ });
addMethod->body([](auto builder, auto args) { /* addition logic */ });
addMethod->finalize();
```

### Step 3: Declare Interface in Consumer Module
```cpp
auto *moduleA = circuit.addModule("ModuleA");
auto *calculatorDecl = moduleA->defineInterface("calc", "Calculator");
// Use calculatorDecl->callMethod() in module methods
```

### Step 4: Bind Interface at Top Level
```cpp
auto *topModule = circuit.addModule("InterfaceDemo");
auto *moduleBCalcDef = topModule->defineInterfaceDef("ModuleBCalc", "Calculator");
moduleBCalcDef->bind("ModuleB", "add", "add");
moduleBCalcDef->finalize();
```

### Step 5: Instantiate with Interface Binding
```cpp
auto *moduleBInst = topModule->addInstance("ModuleB", moduleB, {clk, rst});
auto *moduleAInst = topModule->addInstance("ModuleA", moduleA,
    {clk, rst}, {{"ModuleBCalc", "calc"}});  // Bind interface
```

## Method Patterns

### Always-Ready Guard Pattern
```cpp
method->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    Signal trueValue = UInt::constant(1, 1, builder, module->getLoc());
    builder.create<circt::cmt2::ReturnOp>(
        module->getLoc(),
        mlir::ValueRange{trueValue.getValue()});
});
```

### Simple Method Body Pattern
```cpp
method->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto operandA = args[0];
    auto operandB = args[1];

    Signal signalA(operandA, &builder, module->getLoc());
    Signal signalB(operandB, &builder, module->getLoc());
    Signal result = signalA + signalB;

    builder.create<circt::cmt2::ReturnOp>(
        module->getLoc(),
        mlir::ValueRange{result.getValue()});
});
```

## External Module Integration

### Using FIRRTL Modules
```cpp
// Load external FIRRTL register module
llvm::StringMap<int64_t> regParams;
regParams["width"] = 32;
auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);

// Configure module interface
regMod->bindClock("clk", "clock")
      .bindReset("rst", "reset")
      .bindValue("read", "read_ready", {"read_data"})
      .bindMethod("write", "write_enable", "write_ready", {"write_data"}, {});
```

## Error Handling and Validation

### Module Library Loading
```cpp
auto &library = ModuleLibrary::getInstance();
if (library.loadManifest(manifestPath).failed()) {
    llvm::errs() << "Warning: Failed to load module library manifest\n";
}
```

### Pipeline Validation
```cpp
if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::errs() << "✓ Interface conversion successful\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
} else {
    llvm::errs() << "Error: Failed to convert Cmt2 to FIRRTL\n";
}
```

## Best Practices

### 1. Interface Naming
- Use descriptive interface names (`Calculator`, `MemoryInterface`)
- Use consistent method naming (`add`, `getValue`, `processData`)

### 2. Method Structure
- Always call `finalize()` after defining method body and guard
- Use consistent guard patterns (always ready is common)
- Properly truncate/extend signals to match expected widths

### 3. Interface Binding
- Bind interfaces at the appropriate hierarchical level
- Use clear binding names that reflect the purpose (`ModuleBCalc`)

### 4. Error Handling
- Validate module library loading
- Check pipeline conversion success
- Provide clear error messages

## Key Takeaways

1. **Separation of Concerns**: Interface declaration (what I need) vs interface definition (how it's implemented)
2. **Hierarchical Binding**: Interfaces are bound at the module instantiation level, not directly in code
3. **Type Safety**: All interface methods have strongly typed arguments and return values
4. **Modular Design**: Modules can be developed independently and connected through interfaces
5. **Hardware Generation**: The interface system generates proper FIRRTL with proper wiring and arbitration

This pattern enables building complex, modular hardware designs where components communicate through well-defined interfaces rather than ad-hoc wiring.