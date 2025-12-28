# Interface Patterns in ECMT2

## Overview

The interface mechanism in ECMT2 enables clean module composition and communication through well-defined interfaces. This document covers both peer-to-peer communication (modules at the same level) and hierarchical communication (parent-child module communication).

## Interface Fundamentals

### Interface Declaration vs Definition

- **Interface Declaration (`InterfaceDecl`)**: Declares that a module *needs* an interface (consumer side)
- **Interface Definition (`InterfaceDef`)**: Defines how a module *implements* an interface (provider side binding)

### Core Concepts

```cpp
// 1. Define interface at circuit level
auto *readerInterface = circuit.addInterface("Reader");
readerInterface->addValue("getData", {}, {u32Type});

// 2. Consumer module declares interface usage
auto *consumer = circuit.addModule("Consumer");
auto *readerDecl = consumer->defineInterfaceDecl("reader", "Reader");

// 3. Provider module or parent binds the interface
auto *provider = circuit.addModule("Provider");
auto *readerDef = provider->defineInterfaceDef("ReaderImpl", "Reader");
readerDef->bind("storage", "read", "getData");  // instance.method -> interface.method
readerDef->finalize();
```

---

## Pattern 1: Peer-to-Peer Communication

Modules at the same hierarchical level communicate through a parent that binds their interfaces.

```
TopModule
├── ModuleA (Consumer) - declares @calc interface
└── ModuleB (Provider) - implements Calculator methods
    └── TopModule binds ModuleB's methods to ModuleA's interface
```

### Example

```cpp
// Define interface
auto *calcInterface = circuit.addInterface("Calculator");
calcInterface->addMethod("add", {{"a", u32Type}, {"b", u32Type}}, {u32Type});

// ModuleA consumes the interface
auto *moduleA = circuit.addModule("ModuleA");
auto *calcDecl = moduleA->defineInterfaceDecl("calc", "Calculator");

// In ModuleA's method:
auto results = calcDecl->callMethod("add", {argA, argB}, builder);

// ModuleB provides the implementation
auto *moduleB = circuit.addModule("ModuleB");
auto *addMethod = moduleB->addMethod("add", ...);
// ... implement the method

// TopModule binds them together
auto *top = circuit.addModule("Top");
auto *calcDef = top->defineInterfaceDef("ModuleBCalc", "Calculator");
calcDef->bind("moduleB", "add", "add");
calcDef->finalize();

// Instantiate with binding
auto *moduleAInst = top->addInstance("a", moduleA, {clk, rst},
    {{"ModuleBCalc", "calc"}});  // bind interface
auto *moduleBInst = top->addInstance("b", moduleB, {clk, rst});
```

---

## Pattern 2: Virtual Interface (Upward Communication)

## Architecture Pattern

```
Circuit (Top Level)
├── Interface Definition (OuterInterface)
├── Module A (Top Module)
│   ├── Interface Declaration (needs OuterInterface)
│   └── Module B Instance (submodule with interface binding)
└── Module B (Submodule)
    ├── Interface Declaration (needs OuterInterface)
    ├── Internal Components (registers, logic)
    └── Methods that use outer interface
```

## Interface Usage Procedure

### 1. Define Interface at Circuit Level

```cpp
// Create circuit with interface
auto *outerInterface = circuit.addInterface("OuterInterface");

// Add methods to interface
outerInterface->addMethod("get_outer_data", {},  // no arguments
    {circt::firrtl::UIntType::get(&context, 32)}); // returns 32-bit uint
```

### 2. Declare Interface Usage in Modules

**In Module B (Submodule):**
```cpp
// Module B declares it needs access to OuterInterface
auto *outerInterfaceDecl = moduleB->defineInterfaceDecl("outer_iface", "OuterInterface");
```

**In Module A (Parent Module):**
```cpp
// Module A also declares interface usage
auto *aOuterInterfaceDecl = moduleA->defineInterfaceDecl("outer_iface", "OuterInterface");
```

### 3. Instantiate Submodule with Interface Binding

```cpp
// Create Module B instance inside Module A with interface binding
auto *moduleBInst = moduleA->addInstance(
    "submodule_b", moduleB,
    {moduleAClk.getValue(), moduleARst.getValue()},  // clock/reset arguments
    {{"outer_iface", "outer_iface"}});  // bind interface: {decl_name, def_name}
```

### 4. Use Interface in Submodule Methods

```cpp
// Module B method that calls outer interface
auto *writeDataMethod = moduleB->addMethod("write_data", {}, {});

writeDataMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    // Call outer interface method to get data
    auto outerData = outerInterfaceDecl->callMethod("get_outer_data", {}, builder);

    // Use the data (e.g., write to internal register)
    moduleBReg->callMethod("write", {outerData[0]}, builder);

    builder.create<circt::cmt2::ReturnOp>(moduleB->getLoc(), mlir::ValueRange{});
});
```

## Interface API Reference

### Circuit-Level Interface Operations

#### `Circuit::addInterface(name)`
Creates a new interface definition at the circuit level.
- **Parameters**: `name` - interface name
- **Returns**: `Interface*` pointer

#### `Interface::addMethod(name, args, results)`
Adds a method to the interface.
- **Parameters**:
  - `name` - method name
  - `args` - vector of (arg_name, arg_type) pairs
  - `results` - vector of result types
- **Returns**: `Interface&` for chaining

#### `Interface::addValue(name, args, results)`
Adds a value to the interface.
- **Parameters**: Same as `addMethod`
- **Returns**: `Interface&` for chaining

### Module-Level Interface Operations

#### `Module::defineInterfaceDecl(name, type)`
Declares that the module uses an interface.
- **Parameters**:
  - `name` - declaration name (local reference)
  - `type` - interface type name
- **Returns**: `InterfaceDecl*` pointer

#### `Module::addInstance(name, module, args, interfaceBindings)`
Creates a submodule instance with optional interface bindings.
- **Parameters**:
  - `name` - instance name
  - `module` - module to instantiate
  - `args` - constructor arguments
  - `interfaceBindings` - map of {decl_name, def_name} pairs
- **Returns**: `Instance*` pointer

### Interface Declaration Operations

#### `InterfaceDecl::callMethod(method, args, builder)`
Calls a method through the interface.
- **Parameters**:
  - `method` - method name to call
  - `args` - method arguments
  - `builder` - MLIR OpBuilder
- **Returns**: `SmallVector<Value, 4>` - method return values

#### `InterfaceDecl::callValue(value, builder)`
Accesses a value through the interface.
- **Parameters**:
  - `value` - value name to access
  - `builder` - MLIR OpBuilder
- **Returns**: `SmallVector<Value, 4>` - value results

## Key Design Patterns

### 1. Interface-Inside Pattern
The submodule (Module B) declares interface usage and calls outer methods directly. The parent module (Module A) provides the interface binding when instantiating the submodule.

### 2. Hierarchical Composition
Multiple levels of hierarchy can be created, with each level potentially providing different interface implementations.

## Benefits

1. **Abstraction**: Modules depend on interfaces rather than concrete implementations
2. **Reusability**: Same submodule can work with different parent modules providing the same interface
3. **Testability**: Interfaces can be mocked for testing
4. **Clean Separation**: Clear boundaries between module responsibilities
5. **Upward Communication**: Enables submodules to access parent module functionality

## Common Use Cases

1. **Configuration Access**: Submodules accessing configuration registers from parent
2. **Status Reporting**: Submodules reporting status to parent modules
3. **Shared Resources**: Accessing shared resources (memory, buses) through parent
4. **Hierarchical Control**: Parent modules controlling submodule behavior through interfaces

## Example: Virtual Interface Demo

The complete example demonstrates:
- Module A containing Module B as submodule
- Module B with internal register and `read_by` method
- Outer interface with `get_outer_data` method
- Interface binding between modules
- Pass-through methods in Module A

See `circt/examples/ECMT2/virtual_interface_demo.cpp` for the full implementation.

## Related Documentation

- [Interface Helpers Summary](INTERFACE_HELPERS_SUMMARY.md) - Low-level interface helper functions
- [ECMT2 EDSL Documentation](ecmt2-EDSL.md) - General ECMT2 embedded DSL usage
- [Module Library](ModuleLibrary.md) - Pre-built module usage