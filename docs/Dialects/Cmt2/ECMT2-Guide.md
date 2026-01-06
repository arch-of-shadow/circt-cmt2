# ECMT2 C++ API Guide

C++ embedded DSL for programmatically constructing CMT2 hardware designs.

---

## Overview

ECMT2 provides two API layers:

| Layer | Namespace | Use Case |
|-------|-----------|----------|
| **High-Level** | `highlevel` | Declarative class-based modules |
| **Low-Level** | `ecmt2` | Direct MLIR construction |

```cpp
#include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
using namespace circt::cmt2::ecmt2::highlevel;
```

---

## Quick Example

```cpp
class Counter : public Cmt2Module {
public:
    ClockInput clk;
    ResetInput rst;
    highlevel::Instance<ExternalModule> reg;
    highlevel::Rule incrementRule;

    Counter(ExternalModule *regMod) : Cmt2Module("Counter") {
        CMT2_ARG_CLOCK(clk);
        CMT2_ARG_RESET(rst);
        CMT2_REGISTER_INSTANCE(reg, regMod, clk.get().getValue(), rst.get().getValue());

        INIT_RULE(incrementRule)
            .guard([](mlir::OpBuilder &b) { Return(); })
            .body([this](mlir::OpBuilder &b) {
                auto val = reg.callValue("read", b)[0];
                auto newVal = Bits(Add(val, UIntConst(1, 32)), 31, 0);
                reg.callMethod("write", b, newVal);
                Return();
            });
    }

    void build() override {}  // Empty - fully declarative
};
```

---

## High-Level API

### Module Definition

```cpp
class MyModule : public Cmt2Module {
public:
    // Ports
    ClockInput clk;
    ResetInput rst;

    // Instances
    highlevel::Instance<ExternalModule> reg;

    // Functions
    highlevel::Rule myRule;
    highlevel::Method<UInt<32>, UInt<32>> myMethod;
    highlevel::Value<UInt<32>> myValue;

    MyModule(ExternalModule *regMod) : Cmt2Module("MyModule") {
        CMT2_ARG_CLOCK(clk);
        CMT2_ARG_RESET(rst);
        CMT2_REGISTER_INSTANCE(reg, regMod, clk.get().getValue(), rst.get().getValue());

        INIT_RULE(myRule).guard(...).body(...);
        INIT_METHOD(myMethod).guard(...).body(...);
        INIT_VALUE(myValue).guard(...).body(...);
    }

    void build() override {}
};
```

### Helper Functions

```cpp
// Constants
UIntConst(42, 32);  // 32-bit constant
SIntConst(-5, 16);  // 16-bit signed

// Arithmetic
Add(a, b); Sub(a, b); Mul(a, b);

// Comparison
Eq(a, b); Lt(a, b); Gt(a, b);

// Bitwise
And(a, b); Or(a, b); Xor(a, b); Not(a);

// Bit manipulation
Bits(val, 31, 0);  // Extract bits
Mux(sel, t, f);    // Select

// Control flow
Return(); Return(val);
```

### Conditional Execution

```cpp
auto result = If(condition,
    [](mlir::OpBuilder &b) { return thenValue; },
    [](mlir::OpBuilder &b) { return elseValue; },
    builder, loc);

// Without else (side effects only)
If(condition, [](mlir::OpBuilder &b) { /* actions */ }, builder, loc);
```

---

## Low-Level API

### Signal Types

```cpp
Signal a(value, &builder, loc);
Signal b(otherValue, &builder, loc);

// Arithmetic operators
Signal sum = a + b;
Signal diff = a - b;

// Comparison
Signal eq = a == b;
Signal lt = a < b;

// Bit manipulation
Signal bits = a.bits(31, 0);
Signal shifted = a.shl(2);
```

### Module Construction

```cpp
Circuit circuit("MyDesign", context);

// External module
auto* regMod = circuit.addExternalModule("reg", "FIRRTLReg", {{"width", 32}});
regMod->bindClock("clk", "clock")
      .bindReset("rst", "reset")
      .bindValue("read", "ready", {"data"})
      .bindMethod("write", "enable", "ready", {"data"}, {});

// CMT2 module
auto* mod = circuit.addModule("MyModule");
auto clk = mod->addClockArgument("clk");
auto rst = mod->addResetArgument("rst");
auto* inst = mod->addInstance("r", regMod, {clk.getValue(), rst.getValue()});

// Rule
auto* rule = mod->addRule("increment");
rule->guard([](mlir::OpBuilder& b) { /* return condition */ });
rule->body([&](mlir::OpBuilder& b) { /* actions */ });
rule->finalize();
```

---

## Interfaces

### Declaration (Consumer)

```cpp
// Module needs an interface
highlevel::InterfaceDecl<Reader> reader;
CMT2_INTERFACE_DECL(reader, "Reader");

// Use in method body
auto data = reader.callValue("getData", builder);
```

### Definition (Provider)

```cpp
// Bind interface to instance methods
INIT_INTERFACE_DEF(readX, "Reader")
    .bind("storage", "read", "getData");
```

### Instance Binding

```cpp
// Bind interface when instantiating
auto* child = parent->addInstance("c", childMod, {clk, rst},
    {{"readX", "reader"}});  // Provider -> Consumer
```

---

## STL Components

```cpp
#include "circt/Dialect/Cmt2/ECMT2/STLLibrary.h"
using namespace circt::cmt2::ecmt2::stl;

// Register
auto* regMod = STLLibrary::createRegModule(32, 0, circuit);

// FIFO
auto* fifoMod = STLLibrary::createFIFO1PushModule(32, circuit);

// Memory
auto* memMod = STLLibrary::createMem1r1w1cModule(32, 8, 256, circuit);
```

### Methods

| Component | Methods |
|-----------|---------|
| **Reg** | `read()`, `write(data)` |
| **FIFO** | `enq(data)`, `deq()`, `first()`, `notEmpty()`, `notFull()` |
| **Memory** | `read(addr)`, `write(addr, data)` |

---

## Bundle and Vector

```cpp
// Create bundle
auto packet = BundleBuilder(&context)
    .addUInt("addr", 32)
    .addUInt("data", 64)
    .addVector("tags", UIntType::get(&context, 8), 4)
    .build(builder, loc);

// Access fields
Bundle bundle = AsBundle(packet, &builder, loc);
Signal addr = bundle["addr"];

// Vector access
FVector vec = AsVector(bundle["tags"], &builder, loc);
Signal tag0 = vec[0];
```

---

## Code Generation

```cpp
// Generate MLIR
llvm::outs() << circuit.emitMLIRString();

// Convert to FIRRTL
if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << circuit.emitFIRRTL();
}
```

---

## Build Integration

```cmake
add_executable(my_design main.cpp)
target_link_libraries(my_design PRIVATE
    CIRCTECMT2
    CIRCTCmt2
    CIRCTFIRRTL
    MLIRIR
    MLIRSupport
)
```

---

## Complete GCD Example

```cpp
class GCD : public Cmt2Module {
public:
    ClockInput clk;
    ResetInput rst;
    highlevel::Instance<ExternalModule> x, y;
    highlevel::Value<UInt<1>> doing;
    highlevel::Rule swap, sub;
    highlevel::Method<void, UInt<32>, UInt<32>> start;
    highlevel::Value<UInt<32>> result;

    GCD(ExternalModule *regMod) : Cmt2Module("gcd") {
        CMT2_ARG_CLOCK(clk);
        CMT2_ARG_RESET(rst);
        CMT2_REGISTER_INSTANCE(x, regMod, clk.get().getValue(), rst.get().getValue());
        CMT2_REGISTER_INSTANCE(y, regMod, clk.get().getValue(), rst.get().getValue());

        // doing = y != 0
        INIT_VALUE(doing)
            .guard([](mlir::OpBuilder &b) { Return(); })
            .body([this](mlir::OpBuilder &b) {
                auto yVal = y.callValue("read", b)[0];
                Return(Neq(yVal, UIntConst(0, 32)));
            });

        // swap: if y > x
        INIT_RULE(swap)
            .guard([this](mlir::OpBuilder &b) {
                auto xVal = x.callValue("read", b)[0];
                auto yVal = y.callValue("read", b)[0];
                Return(And(Gt(yVal, xVal), callDoing(b)));
            })
            .body([this](mlir::OpBuilder &b) {
                auto xVal = x.callValue("read", b)[0];
                auto yVal = y.callValue("read", b)[0];
                x.callMethod("write", b, yVal);
                y.callMethod("write", b, xVal);
                Return();
            });

        // sub: x = x - y
        INIT_RULE(sub)
            .guard([this](mlir::OpBuilder &b) {
                auto xVal = x.callValue("read", b)[0];
                auto yVal = y.callValue("read", b)[0];
                Return(And(Geq(xVal, yVal), callDoing(b)));
            })
            .body([this](mlir::OpBuilder &b) {
                auto xVal = x.callValue("read", b)[0];
                auto yVal = y.callValue("read", b)[0];
                x.callMethod("write", b, Bits(Sub(xVal, yVal), 31, 0));
                Return();
            });

        // start(a, b)
        INIT_METHOD(start)
            .guard([this](mlir::OpBuilder &b, auto args) {
                Return(Not(callDoing(b)));
            })
            .body([this](mlir::OpBuilder &b, auto args) {
                x.callMethod("write", b, args[0]);
                y.callMethod("write", b, args[1]);
                Return();
            });

        // result
        INIT_VALUE(result)
            .guard([this](mlir::OpBuilder &b) {
                Return(Not(callDoing(b)));
            })
            .body([this](mlir::OpBuilder &b) {
                Return(x.callValue("read", b)[0]);
            });
    }

    void build() override {
        lowLevelModule()->setPrecedence({{"swap", "sub"}});
    }

private:
    mlir::Value callDoing(mlir::OpBuilder &b) {
        return b.create<cmt2::CallOp>(loc(),
            firrtl::UIntType::get(b.getContext(), 1),
            b.getSymbolRefAttr("this", "doing"),
            mlir::ValueRange{}).getResult(0);
    }
};
```

---

## Next Steps

- [MultiCycle.md](MultiCycle.md) - Multi-cycle operations
- [Operations.md](Operations.md) - CMT2 operations reference
- [Examples.md](Examples.md) - More examples
