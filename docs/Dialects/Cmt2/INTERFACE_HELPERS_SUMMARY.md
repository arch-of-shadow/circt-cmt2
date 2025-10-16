# Interface Mechanism Helper Functions - Summary

## Overview
Added comprehensive interface mechanism support to `Cmt2Ops.cpp` and `Cmt2Ops.h` to facilitate working with Cmt2's interface system. The interface mechanism allows modules to abstract their dependencies and enables flexible module composition through interface binding.

## Files Modified

### 1. `/home/uvxiao/circt-cmt2/lib/Dialect/Cmt2/Cmt2Ops.cpp`
Added interface-related helper functions (lines 475-614):

#### Interface Lookup Functions
- `getInterfaceDefs(ModuleOp)` - Get all InterfaceDefOp operations in a module
- `getInterfaceDecls(ModuleOp)` - Get all InterfaceDeclOp operations in a module
- `lookupInterfaceDef(ModuleOp, StringRef)` - Look up an InterfaceDefOp by name
- `lookupInterfaceDecl(ModuleOp, StringRef)` - Look up an InterfaceDeclOp by name
- `lookupInterface(CircuitOp, StringRef)` - Look up an InterfaceOp by name in circuit

#### Interface Resolution Functions
- `getInterfaceForDecl(InterfaceDeclOp)` - Get the InterfaceOp that a decl refers to
- `getInterfaceForDef(InterfaceDefOp)` - Get the InterfaceOp that a def refers to
- `resolveInterfaceCall(ModuleOp, SymbolRefAttr, SymbolRefAttr)` - Resolve interface call to actual instance and method/value
- `isInterfaceCall(CallOp)` - Check if a CallOp is calling through an interface
- `getInterfaceBindings(InstanceOp)` - Get interface bindings for an instance

#### General Helper Functions
- `getFunctions(ModuleOp)` - Get all function-like operations (RuleOp, MethodOp, ValueOp)
- `getInstances(ModuleOp)` - Get all instances in a module

#### Formatting Fixes
Fixed spacing in print methods for function-like operations:
- `RuleOp::print()` (line 340) - Added space before symbol name
- `MethodOp::print()` (line 367) - Added space before symbol name
- `ValueOp::print()` (line 394) - Added space before symbol name

### 2. `/home/uvxiao/circt-cmt2/include/circt/Dialect/Cmt2/Cmt2Ops.h`
Added function declarations (lines 48-91) for all helper functions listed above.

## Interface Mechanism Components

### Operations
1. **InterfaceOp** - Defines an interface type with methods/values
2. **InterfaceDefOp** - Defines concrete implementation of an interface in a module
3. **InterfaceDeclOp** - Declares that a module uses an interface (parameter)

### Usage Pattern
```mlir
// 1. Define interface
cmt2.interface @Reader {
  cmt2.value @getData() -> !firrtl.uint<32> {}{}
}

// 2. Consumer module declares interface usage
cmt2.module @consumer() {
  cmt2.interface.decl @reader : @Reader
  // Use @reader in calls
  %data = cmt2.call @reader @getData() : () -> !firrtl.uint<32>
}

// 3. Provider module defines interface binding
cmt2.module @provider() {
  cmt2.instance @reg = @storage(...)

  // Bind @reg's @read method to @Reader's @getData
  cmt2.interface.def @MyReader : @Reader [
    [@reg, @read, @getData]
  ]

  // Pass interface binding when instantiating consumer
  cmt2.instance @c = @consumer(...) with [[@MyReader, @reader]]
}
```

## Example Usage

### Example 1: Resolving Interface Calls
```cpp
// Given a CallOp that calls through an interface
CallOp call = ...;
ModuleOp module = call->getParentOfType<ModuleOp>();

if (isInterfaceCall(call)) {
  // Resolve to actual instance and method
  auto [instanceRef, methodRef] = resolveInterfaceCall(
    module,
    call.getCalleeAttr(),
    call.getMethodOrValueAttr()
  );

  if (instanceRef) {
    llvm::outs() << "Resolved to: " << instanceRef
                 << " -> " << methodRef << "\n";
  }
}
```

### Example 2: Getting Interface Bindings
```cpp
InstanceOp instance = ...;
auto bindings = getInterfaceBindings(instance);

for (auto [declName, defName] : bindings) {
  llvm::outs() << "Interface binding: " << declName
               << " <- " << defName << "\n";
}
```

### Example 3: Walking Interface Definitions
```cpp
ModuleOp module = ...;

// Get all interface definitions
auto defs = getInterfaceDefs(module);
for (auto def : defs) {
  // Get the interface type
  InterfaceOp iface = getInterfaceForDef(def);
  // Process interface bindings
  auto methods = def.getMethods();
  // ...
}
```

## Testing

### Test Files
1. **test/Dialect/Cmt2/hello.mlir** - Original test with interface mechanism
2. **test/Dialect/Cmt2/interface-test.mlir** - New comprehensive interface test

### Running Tests
```bash
# Test parsing with proper formatting
build/bin/circt-opt test/Dialect/Cmt2/hello.mlir

# Test CallInfo analysis with interfaces
build/bin/circt-opt test/Dialect/Cmt2/hello.mlir -cmt2-print-call-info

# Test with new interface test
build/bin/circt-opt test/Dialect/Cmt2/interface-test.mlir
build/bin/circt-opt test/Dialect/Cmt2/interface-test.mlir -cmt2-print-call-info
```

## Benefits

1. **Abstraction** - Modules can depend on interfaces rather than concrete implementations
2. **Reusability** - Modules can be reused with different interface bindings
3. **Flexibility** - Interface bindings are resolved at instantiation time
4. **Maintainability** - Helper functions provide clean API for working with interfaces

## Integration with Existing Analyses

The interface helper functions are used by existing analyses:

1. **CallInfo Analysis** - Already uses `lookupInterfaceDef` and manual interface resolution
2. **ConflictMatrix Analysis** - Can use interface resolution for conflict inference
3. **ModuleInliner** - Can use interface bindings for proper remapping during inlining
4. **Scheduler Analysis** - Can use interface resolution for scheduling decisions

## Build Status

✅ **Build Successful** - All 35 targets compiled without errors
✅ **Tests Pass** - hello.mlir and interface-test.mlir parse correctly
✅ **CallInfo Works** - Interface calls are tracked in CallInfo analysis
✅ **Formatting Fixed** - Function-like operations now print with proper spacing
