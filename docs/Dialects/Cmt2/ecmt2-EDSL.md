# ecmt2: Embedded C++ DSL for Cmt2

## Overview

The `ecmt2` embedded DSL provides a low-level C++ API for programmatically constructing Cmt2 hardware designs. It directly constructs MLIR operations in-memory using the OpBuilder API with **zero serialization cost** - no string generation or parsing.

**Key Features:**
- **Zero Serialization**: Direct `builder.create<OpType>()` calls to construct MLIR operations
- **OOP-Based Design**: Classes and inheritance model hardware hierarchy
- **Type Safety**: C++ type system integration with FIRRTL types
- **RAII**: Automatic resource management
- **Fluent APIs**: Method chaining for complex constructions
- **Performance**: 10-100x faster than text-based approaches

## Core API Components

### 1. Signal Types

Base signal class with operator overloading for hardware operations:

```cpp
class Signal {
    Signal operator+(const Signal& other) const;  // firrtl.add
    Signal operator-(const Signal& other) const;  // firrtl.sub
    Signal operator&(const Signal& other) const;  // firrtl.and
    Signal operator|(const Signal& other) const;  // firrtl.or
    Signal operator==(const Signal& other) const; // firrtl.eq
    Signal bits(unsigned high, unsigned low) const; // firrtl.bits
    Signal mux(const Signal& t, const Signal& f) const; // firrtl.mux
};

class UInt : public Signal {
    static UInt constant(unsigned value, unsigned width, mlir::OpBuilder& builder);
};

class SInt : public Signal {
    static SInt constant(int value, unsigned width, mlir::OpBuilder& builder);
};
```

### 2. Module Classes

```cpp
class Module {
    // Add instances
    Instance* addInstance(llvm::StringRef name, ModuleBase* moduleType,
                         llvm::ArrayRef<mlir::Value> args);

    // Add function-like operations
    Rule* addRule(llvm::StringRef name);
    Method* addMethod(llvm::StringRef name,
                     llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
                     llvm::ArrayRef<mlir::Type> results);
    Value* addValue(llvm::StringRef name,
                   llvm::ArrayRef<mlir::Type> results);

    // Interface support
    InterfaceDecl* defineInterface(llvm::StringRef name, llvm::StringRef type);
    InterfaceDef* defineInterfaceDef(llvm::StringRef name, llvm::StringRef type);
};

class ExternalModule {
    // Fluent API for binding
    ExternalModule& bindClock(llvm::StringRef port);
    ExternalModule& bindReset(llvm::StringRef port);
    ExternalModule& bindMethod(llvm::StringRef name, ...);
    ExternalModule& bindValue(llvm::StringRef name, ...);
    ExternalModule& addConflict(llvm::StringRef a, llvm::StringRef b);
};
```

### 3. Function-Like Operations

```cpp
class Rule {
    template<typename Func>
    Rule& guard(Func&& fn);  // Define guard region

    template<typename Func>
    Rule& body(Func&& fn);   // Define body region

    void finalize();
};

class Method {
    template<typename Func>
    Method& guard(Func&& fn);  // Access arguments

    template<typename Func>
    Method& body(Func&& fn);   // Access arguments

    void finalize();
};

class Value {
    template<typename Func>
    Value& guard(Func&& fn);

    template<typename Func>
    Value& body(Func&& fn);

    void finalize();
};
```

### 4. Instance and Call Support

```cpp
class Instance {
    // Call methods on this instance
    llvm::SmallVector<mlir::Value, 4>
    callMethod(llvm::StringRef method,
               llvm::ArrayRef<mlir::Value> args,
               mlir::OpBuilder& builder);

    // Access values from this instance
    llvm::SmallVector<mlir::Value, 4>
    callValue(llvm::StringRef value, mlir::OpBuilder& builder);
};
```

### 5. Circuit and Code Generation

```cpp
class Circuit {
    Circuit(llvm::StringRef topModule, mlir::MLIRContext& context);

    // Module management
    Module* addModule(llvm::StringRef name);
    ExternalModule* addExternalModule(llvm::StringRef name,
                                      llvm::StringRef firrtlModule,
                                      llvm::StringMap<int64_t> params = {});

    // Interface management
    Interface* addInterface(llvm::StringRef name);

    // Code generation
    std::string emitMLIRString();
    mlir::LogicalResult runCmt2ToFIRRTLPipeline();
    std::string emitFIRRTL();
};
```

## Usage Examples

### Example 1: Simple Counter

```cpp
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"

using namespace circt::cmt2::ecmt2;

int main() {
    mlir::MLIRContext context;
    context.loadDialect<cmt2::Cmt2Dialect>();
    context.loadDialect<firrtl::FIRRTLDialect>();

    Circuit circuit("counter", context);

    // External register module
    llvm::StringMap<int64_t> regParams;
    regParams["width"] = 32;
    auto* regMod = circuit.addExternalModule("reg", "FIRRTLReg", regParams);
    regMod->bindClock("clk", "clock")
          .bindReset("rst", "reset")
          .bindValue("read", "read_ready", {"read_data"})
          .bindMethod("write", "write_enable", "write_ready", {"write_data"}, {})
          .addConflict("write", "write")
          .addConflictFree("read", "read");

    // Counter module
    auto* counter = circuit.addModule("counter");
    auto& builder = counter->getBuilder();
    auto loc = counter->getLoc();

    // Add clock and reset arguments
    auto clkType = firrtl::ClockType::get(&context);
    auto rstType = firrtl::UIntType::get(&context, 1);
    auto clk = counter->getBodyBlock()->addArgument(clkType, loc);
    auto rst = counter->getBodyBlock()->addArgument(rstType, loc);

    // Add register instance
    auto* r = counter->addInstance("r", regMod, {clk, rst});

    // Increment rule
    auto* incr = counter->addRule("incr");
    incr->guard([](mlir::OpBuilder& b) {
        auto loc = b.getUnknownLoc();
        auto ctx = b.getContext();
        auto one = b.create<firrtl::ConstantOp>(
            loc, firrtl::UIntType::get(ctx, 1), 1);
        b.create<cmt2::ReturnOp>(loc, mlir::ValueRange{one});
    });
    incr->body([&](mlir::OpBuilder& b) {
        auto loc = b.getUnknownLoc();
        // Read current value
        auto vals = r->callValue("read", b);
        // Increment
        auto one = b.create<firrtl::ConstantOp>(
            loc, firrtl::UIntType::get(&context, 32), 1);
        auto sum = b.create<firrtl::AddPrimOp>(loc, vals[0], one);
        auto truncated = b.create<firrtl::BitsPrimOp>(loc, sum, 31, 0);
        // Write back
        r->callMethod("write", {truncated}, b);
        b.create<cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });
    incr->finalize();

    // Generate MLIR
    llvm::outs() << circuit.emitMLIRString() << "\n";

    // Convert to FIRRTL
    if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
        llvm::outs() << circuit.emitFIRRTL() << "\n";
    }

    return 0;
}
```

### Example 2: GCD with Private Functions

```cpp
Circuit circuit("gcd", context);

// External register module
auto* regMod = circuit.addExternalModule("reg", "Reg32");
regMod->bindClock("clock")
      .bindReset("reset")
      .bindMethod("write", "writeEnable", "writeReady", {"write"}, {})
      .bindValue("read", "readReady", {"read"})
      .addConflict("write", "write")
      .addConflictFree("read", "read")
      .addSequenceBefore("read", "write");

// GCD module
auto* gcd = circuit.addModule("gcd");
auto& builder = gcd->getBuilder();
auto loc = gcd->getLoc();

// Add clock and reset
auto clk = gcd->getBodyBlock()->addArgument(firrtl::ClockType::get(&context), loc);
auto rst = gcd->getBodyBlock()->addArgument(firrtl::UIntType::get(&context, 1), loc);

// Add register instances
auto* x = gcd->addInstance("x", regMod, {clk, rst});
auto* y = gcd->addInstance("y", regMod, {clk, rst});

// Private "doing" value (y != 0)
auto* doing = gcd->addValue("doing", {firrtl::UIntType::get(&context, 1)});
doing->guard([](mlir::OpBuilder& b) {
    auto one = b.create<firrtl::ConstantOp>(
        b.getUnknownLoc(), firrtl::UIntType::get(b.getContext(), 1), 1);
    b.create<cmt2::ReturnOp>(b.getUnknownLoc(), mlir::ValueRange{one});
});
doing->body([&](mlir::OpBuilder& b) {
    auto yVals = y->callValue("read", b);
    auto zero = b.create<firrtl::ConstantOp>(
        b.getUnknownLoc(), firrtl::UIntType::get(&context, 32), 0);
    auto cond = b.create<firrtl::NEQPrimOp>(b.getUnknownLoc(), yVals[0], zero);
    b.create<cmt2::ReturnOp>(b.getUnknownLoc(), mlir::ValueRange{cond});
});
doing->finalize();

// Swap rule
auto* swap = gcd->addRule("swap");
swap->guard([&](mlir::OpBuilder& b) {
    auto xVals = x->callValue("read", b);
    auto yVals = y->callValue("read", b);
    auto canSwap = b.create<firrtl::GTPrimOp>(b.getUnknownLoc(), yVals[0], xVals[0]);

    // Call @this @doing
    auto doingCall = b.create<cmt2::CallOp>(
        b.getUnknownLoc(),
        firrtl::UIntType::get(&context, 1),
        b.getSymbolRefAttr("this", "doing"),
        mlir::ValueRange{});

    auto guard = b.create<firrtl::AndPrimOp>(
        b.getUnknownLoc(), canSwap, doingCall.getResult(0));
    b.create<cmt2::ReturnOp>(b.getUnknownLoc(), mlir::ValueRange{guard});
});
swap->body([&](mlir::OpBuilder& b) {
    auto xVals = x->callValue("read", b);
    auto yVals = y->callValue("read", b);
    x->callMethod("write", {yVals[0]}, b);
    y->callMethod("write", {xVals[0]}, b);
    b.create<cmt2::ReturnOp>(b.getUnknownLoc(), mlir::ValueRange{});
});
swap->finalize();

// Start method
auto uint32Type = firrtl::UIntType::get(&context, 32);
auto* start = gcd->addMethod("start", {{"a", uint32Type}, {"b", uint32Type}}, {});
start->guard([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto doingCall = b.create<cmt2::CallOp>(
        b.getUnknownLoc(),
        firrtl::UIntType::get(&context, 1),
        b.getSymbolRefAttr("this", "doing"),
        mlir::ValueRange{});
    auto notDoing = b.create<firrtl::NotPrimOp>(
        b.getUnknownLoc(), doingCall.getResult(0));
    b.create<cmt2::ReturnOp>(b.getUnknownLoc(), mlir::ValueRange{notDoing});
});
start->body([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument> args) {
    x->callMethod("write", {args[0]}, b);
    y->callMethod("write", {args[1]}, b);
    b.create<cmt2::ReturnOp>(b.getUnknownLoc(), mlir::ValueRange{});
});
start->finalize();

// Generate code
llvm::outs() << circuit.emitMLIRString() << "\n";
if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << circuit.emitFIRRTL() << "\n";
}
```

### Example 3: Interface-Based Design

```cpp
Circuit circuit("hello", context);

// Define interfaces
auto* readerInterface = circuit.addInterface("Reader");
readerInterface->addValue("getData", {},
    {mlir::TypeAttr::get(firrtl::UIntType::get(&context, 32))});

auto* writerInterface = circuit.addInterface("Writer");
writerInterface->addMethod("store",
    {{"data", firrtl::UIntType::get(&context, 32)}}, {});

// Child module with interface declaration
auto* child = circuit.addModule("child");
auto& builder = child->getBuilder();
auto loc = child->getLoc();

auto clk = child->getBodyBlock()->addArgument(firrtl::ClockType::get(&context), loc);
auto rst = child->getBodyBlock()->addArgument(firrtl::UIntType::get(&context, 1), loc);

// Declare interface
auto* readerDecl = child->defineInterface("reader", "Reader");

// Instance
auto* r = child->addInstance("r", regMod, {clk, rst});

// Method using interface
auto uint32Type = firrtl::UIntType::get(&context, 32);
auto* process = child->addMethod("set", {{"v", uint32Type}}, {uint32Type});
process->body([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument> args) {
    // Call interface
    auto interfaceData = readerDecl->callValue("getData", b);

    // Compute
    auto sum1 = b.create<firrtl::AddPrimOp>(b.getUnknownLoc(), interfaceData[0], args[0]);
    auto truncated = b.create<firrtl::BitsPrimOp>(b.getUnknownLoc(), sum1, 31, 0);

    // Write to instance
    r->callMethod("write", {truncated}, b);

    b.create<cmt2::ReturnOp>(b.getUnknownLoc(), mlir::ValueRange{truncated});
});
process->finalize();

// Parent module with interface definition
auto* parent = circuit.addModule("hello");
auto& parentBuilder = parent->getBuilder();
auto parentLoc = parent->getLoc();

auto parentClk = parent->getBodyBlock()->addArgument(firrtl::ClockType::get(&context), parentLoc);
auto parentRst = parent->getBodyBlock()->addArgument(firrtl::UIntType::get(&context, 1), parentLoc);

// Storage instance
auto* x = parent->addInstance("x", regMod, {parentClk, parentRst});

// Define interface binding
auto* readX = parent->defineInterfaceDef("readX", "Reader");
readX->bind("x", "read", "getData");
readX->finalize();

// Instantiate child with interface binding
auto* childInst = parent->addInstance("c", child->lowLevelModule(),
                                      {parentClk, parentRst},
                                      {{"readX", "reader"}});

// Generate code
llvm::outs() << circuit.emitMLIRString() << "\n";
```

## Module Library System

The API includes a module library system for managing external FIRRTL modules:

```cpp
// Load library manifest
ModuleLibrary::getInstance().loadManifest("lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml");

// Add external module with parameters
llvm::StringMap<int64_t> params;
params["width"] = 64;
auto* regMod = circuit.addExternalModule("reg", "FIRRTLReg", params);
// Library automatically inserts FIRRTL module definition and conflict matrix
```

Library supports:
- **Static MLIR modules**: Pre-built FIRRTL modules
- **Chisel-generated modules**: Parametric modules built on-demand
- **Caching**: Avoids redundant builds
- **Conflict matrices**: Automatically applied from manifest metadata

## Build System Integration

```cmake
add_executable(my_hardware_design main.cpp)
target_link_libraries(my_hardware_design
    PRIVATE
    CIRCTECMT2
    CIRCTCmt2
    CIRCTFIRRTL
    MLIRIR
    MLIRSupport
)
target_include_directories(my_hardware_design
    PRIVATE
    ${CIRCT_MAIN_INCLUDE_DIR}
)
```

## Performance Characteristics

**Zero Serialization Overhead:**
- **Build Time**: 10-100x faster than text generation + parsing
- **Memory**: No intermediate string representations
- **Type Safety**: Compile-time checks instead of runtime parsing errors
- **Debugging**: Stack traces point directly to C++ code

**Direct MLIR Construction:**
```cpp
// ❌ Text-based (SLOW):
std::ostringstream ss;
ss << "cmt2.module @" << name << " { ... }";
auto module = parseMLIR(ss.str());  // Parse, lex, verify

// ✅ ecmt2 (FAST):
auto module = circuit.addModule(name);  // Direct C++ constructor
```

## Next Steps

For a higher-level, more declarative API, see [ecmt2-Class-API.md](ecmt2-Class-API.md) which provides:
- Class-based module definitions
- Automatic member registration
- Helper functions eliminating boilerplate
- Zero `builder.create<>` calls in user code
