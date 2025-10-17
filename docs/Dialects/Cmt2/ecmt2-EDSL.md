# ecmt2: Embedded C++ DSL for Cmt2

## Overview

The `ecmt2` embedded DSL provides a C++ API for programmatically constructing Cmt2 hardware designs. It offers an object-oriented interface that leverages modern C++ features (C++17/20) to provide type safety, RAII, and fluent APIs for hardware description.

**Key Principle**: ecmt2 directly constructs MLIR operations in-memory using the C++ OpBuilder API. There is **no serialization or text generation** - all operations are created using constructors like `builder.create<circt::cmt2::ModuleOp>()` and `builder.create<circt::firrtl::AddPrimOp>()`.

## Design Philosophy

1. **Zero Serialization Cost**: Directly construct MLIR operations using C++ OpBuilder patterns
2. **OOP-Based Design**: Use classes and inheritance to model hardware hierarchy
3. **Type Safety**: Leverage C++ type system with FIRRTL types
4. **RAII**: Automatic resource management for operations and regions
5. **Builder Pattern**: Fluent APIs for constructing complex structures
6. **Direct MLIR Integration**: Use `cmt2` and `firrtl` dialect builder functions exclusively

## MLIR Operation Construction

The ecmt2 DSL wraps MLIR's OpBuilder API to construct operations directly. Here's how it works:

### Example: Signal Addition

```cpp
// User code (ecmt2):
Signal c = a + b;

// What happens internally (direct MLIR construction):
mlir::Value result = builder_->create<circt::firrtl::AddPrimOp>(
    loc,
    a.getValue(),
    b.getValue()
);
return Signal(result, builder_);
```

### Example: Creating a Module

```cpp
// User code (ecmt2):
auto* gcd = circuit.addModule("gcd");

// What happens internally:
auto moduleOp = builder.create<circt::cmt2::ModuleOp>(
    loc,
    builder.getStringAttr("gcd"),
    /* function type will be set later */
);
return new Module(moduleOp, builder, loc);
```

### Example: Creating a Method Call

```cpp
// User code (ecmt2):
auto result = x->callMethod("write", {value}, builder);

// What happens internally:
auto callOp = builder.create<circt::cmt2::CallOp>(
    loc,
    resultTypes,                           // Result types
    builder.getSymbolRefAttr("x", "write"), // Callee reference
    args                                   // Arguments
);
return callOp.getResults();
```

### No String Templates or Parsing

```cpp
// ❌ WRONG - Don't do this:
std::string mlirText = "cmt2.module @gcd { ... }";
parseMLIRText(mlirText);  // Expensive!

// ✅ CORRECT - Direct construction:
auto op = builder.create<circt::cmt2::ModuleOp>(
    loc,
    builder.getStringAttr("gcd"),
    functionType
);
```

## Core Components

### 1. Signal Types

```cpp
namespace ecmt2 {

// Base signal class
class Signal {
public:
    Signal(mlir::Value value, mlir::OpBuilder* builder)
        : value_(value), builder_(builder) {}
    virtual ~Signal() = default;

    // Arithmetic operations (creates firrtl.add, firrtl.sub, etc.)
    Signal operator+(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::AddPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator-(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::SubPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator*(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::MulPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator/(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::DivPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator%(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::RemPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    // Bitwise operations (creates firrtl.and, firrtl.or, etc.)
    Signal operator&(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::AndPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator|(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::OrPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator^(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::XorPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator~() const {
        auto result = builder_->create<circt::firrtl::NotPrimOp>(
            value_.getLoc(), value_);
        return Signal(result.getResult(), builder_);
    }

    // Comparison operations (creates firrtl.eq, firrtl.neq, etc.)
    Signal operator==(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::EQPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator!=(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::NEQPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator<(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::LTPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator<=(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::LEQPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator>(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::GTPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal operator>=(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::GEQPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    // Bit manipulation (creates firrtl.bits, firrtl.cat)
    Signal bits(unsigned high, unsigned low) const {
        auto result = builder_->create<circt::firrtl::BitsPrimOp>(
            value_.getLoc(), value_, high, low);
        return Signal(result.getResult(), builder_);
    }

    Signal bit(unsigned index) const {
        return bits(index, index);
    }

    Signal cat(const Signal& other) const {
        auto result = builder_->create<circt::firrtl::CatPrimOp>(
            value_.getLoc(), value_, other.value_);
        return Signal(result.getResult(), builder_);
    }

    // FIRRTL operations (creates firrtl.mux, firrtl.pad, etc.)
    Signal mux(const Signal& trueVal, const Signal& falseVal) const {
        auto result = builder_->create<circt::firrtl::MuxPrimOp>(
            value_.getLoc(), value_, trueVal.value_, falseVal.value_);
        return Signal(result.getResult(), builder_);
    }

    Signal pad(unsigned width) const {
        auto result = builder_->create<circt::firrtl::PadPrimOp>(
            value_.getLoc(), value_, width);
        return Signal(result.getResult(), builder_);
    }

    Signal shl(unsigned amount) const {
        auto result = builder_->create<circt::firrtl::ShlPrimOp>(
            value_.getLoc(), value_, amount);
        return Signal(result.getResult(), builder_);
    }

    Signal shr(unsigned amount) const {
        auto result = builder_->create<circt::firrtl::ShrPrimOp>(
            value_.getLoc(), value_, amount);
        return Signal(result.getResult(), builder_);
    }

    // Getters
    mlir::Value getValue() const { return value_; }
    mlir::Type getType() const { return value_.getType(); }
    unsigned getWidth() const;

protected:
    mlir::Value value_;
    mlir::OpBuilder* builder_;
};

// Unsigned integer signal
class UInt : public Signal {
public:
    // Creates a wire with firrtl.uint<width> type
    explicit UInt(unsigned width, mlir::OpBuilder& builder, mlir::Location loc)
        : Signal(createWire(width, builder, loc), &builder) {}

    // Creates firrtl.constant operation
    static UInt constant(unsigned value, unsigned width,
                        mlir::OpBuilder& builder, mlir::Location loc) {
        auto type = circt::firrtl::UIntType::get(builder.getContext(), width);
        auto constOp = builder.create<circt::firrtl::ConstantOp>(
            loc, type, llvm::APInt(width, value));
        return UInt(constOp.getResult(), &builder);
    }

private:
    explicit UInt(mlir::Value value, mlir::OpBuilder* builder)
        : Signal(value, builder) {}

    static mlir::Value createWire(unsigned width, mlir::OpBuilder& builder, mlir::Location loc) {
        auto type = circt::firrtl::UIntType::get(builder.getContext(), width);
        auto wireOp = builder.create<circt::firrtl::WireOp>(loc, type);
        return wireOp.getResult();
    }
};

// Signed integer signal
class SInt : public Signal {
public:
    // Creates a wire with firrtl.sint<width> type
    explicit SInt(unsigned width, mlir::OpBuilder& builder, mlir::Location loc)
        : Signal(createWire(width, builder, loc), &builder) {}

    // Creates firrtl.constant operation
    static SInt constant(int value, unsigned width,
                        mlir::OpBuilder& builder, mlir::Location loc) {
        auto type = circt::firrtl::SIntType::get(builder.getContext(), width);
        auto constOp = builder.create<circt::firrtl::ConstantOp>(
            loc, type, llvm::APInt(width, value, /*isSigned=*/true));
        return SInt(constOp.getResult(), &builder);
    }

private:
    explicit SInt(mlir::Value value, mlir::OpBuilder* builder)
        : Signal(value, builder) {}

    static mlir::Value createWire(unsigned width, mlir::OpBuilder& builder, mlir::Location loc) {
        auto type = circt::firrtl::SIntType::get(builder.getContext(), width);
        auto wireOp = builder.create<circt::firrtl::WireOp>(loc, type);
        return wireOp.getResult();
    }
};

// Clock signal - wraps a module argument
class Clock {
public:
    Clock() = default;
    explicit Clock(mlir::Value value) : value_(value) {}
    mlir::Value getValue() const { return value_; }

private:
    mlir::Value value_;
};

// Reset signal - wraps a module argument
class Reset : public UInt {
public:
    Reset() = default;
    explicit Reset(mlir::Value value, mlir::OpBuilder* builder, mlir::Location loc)
        : UInt(value, builder) {}
};

} // namespace ecmt2
```

### 2. Module Classes

```cpp
namespace ecmt2 {

// Forward declarations
class Rule;
class Method;
class Value;
class Instance;
class InterfaceDecl;
class InterfaceDef;

// Base module interface
class ModuleBase {
public:
    virtual ~ModuleBase() = default;

    virtual llvm::StringRef getName() const = 0;
    virtual mlir::Operation* getOperation() const = 0;
};

// External FIRRTL module wrapper
class ExternalModule : public ModuleBase {
public:
    ExternalModule(llvm::StringRef name, llvm::StringRef firrtlModule,
                   mlir::OpBuilder& builder, mlir::Location loc);

    // Fluent API for binding
    ExternalModule& bindClock(llvm::StringRef port);
    ExternalModule& bindReset(llvm::StringRef port);

    ExternalModule& bindMethod(llvm::StringRef name,
                               llvm::StringRef enablePort,
                               llvm::StringRef readyPort,
                               llvm::ArrayRef<std::string> inputPorts,
                               llvm::ArrayRef<std::string> outputPorts);

    ExternalModule& bindValue(llvm::StringRef name,
                             llvm::StringRef readyPort,
                             llvm::ArrayRef<std::string> dataPorts);

    // Conflict matrix setup
    ExternalModule& addConflict(llvm::StringRef a, llvm::StringRef b);
    ExternalModule& addConflictFree(llvm::StringRef a, llvm::StringRef b);
    ExternalModule& addSequenceBefore(llvm::StringRef before, llvm::StringRef after);

    // Overrides
    llvm::StringRef getName() const override { return name_; }
    mlir::Operation* getOperation() const override { return op_; }

private:
    std::string name_;
    mlir::Operation* op_;
    mlir::OpBuilder& builder_;
};

// Cmt2 Module
class Module : public ModuleBase {
public:
    Module(llvm::StringRef name, mlir::OpBuilder& builder, mlir::Location loc);
    ~Module() override;

    // Clock and reset setup
    void setClockReset(const Clock& clk, const Reset& rst);

    // Instance management
    Instance* addInstance(llvm::StringRef name, ModuleBase* moduleType,
                         llvm::ArrayRef<mlir::Value> args,
                         llvm::ArrayRef<std::pair<std::string, std::string>> interfaceBindings = {});

    // Function-like operations
    Rule* addRule(llvm::StringRef name);
    Method* addMethod(llvm::StringRef name,
                     llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
                     llvm::ArrayRef<mlir::Type> results);
    Value* addValue(llvm::StringRef name,
                   llvm::ArrayRef<mlir::Type> results);

    // Interface support
    InterfaceDecl* defineInterface(llvm::StringRef name, llvm::StringRef type);
    InterfaceDef* defineInterfaceDef(llvm::StringRef name, llvm::StringRef type);

    // Access internal functions
    template<typename Func>
    Func* getFunction(llvm::StringRef name);

    // Overrides
    llvm::StringRef getName() const override { return name_; }
    mlir::Operation* getOperation() const override { return op_; }

    // MLIR integration
    mlir::OpBuilder& getBuilder() { return builder_; }
    mlir::Location getLoc() const { return loc_; }

private:
    std::string name_;
    circt::cmt2::ModuleOp op_;
    mlir::OpBuilder& builder_;
    mlir::Location loc_;

    std::vector<std::unique_ptr<Instance>> instances_;
    std::vector<std::unique_ptr<Rule>> rules_;
    std::vector<std::unique_ptr<Method>> methods_;
    std::vector<std::unique_ptr<Value>> values_;
    std::vector<std::unique_ptr<InterfaceDecl>> interfaces_;
};

} // namespace ecmt2
```

### 3. Function-Like Operations

```cpp
namespace ecmt2 {

// Base class for function-like operations with guard/body regions
class FunctionLike {
public:
    virtual ~FunctionLike() = default;

    // Access to guard and body builders
    mlir::OpBuilder& getGuardBuilder() { return *guardBuilder_; }
    mlir::OpBuilder& getBodyBuilder() { return *bodyBuilder_; }

    // Block arguments (shared between guard and body)
    llvm::ArrayRef<mlir::BlockArgument> getArguments() const { return arguments_; }

    // Finalize construction
    virtual void finalize() = 0;

protected:
    FunctionLike(mlir::Operation* op, mlir::OpBuilder& builder);

    mlir::Operation* op_;
    mlir::OpBuilder& parentBuilder_;
    std::unique_ptr<mlir::OpBuilder> guardBuilder_;
    std::unique_ptr<mlir::OpBuilder> bodyBuilder_;
    llvm::SmallVector<mlir::BlockArgument, 4> arguments_;
};

// Rule: no inputs, guard returns i1
class Rule : public FunctionLike {
public:
    Rule(llvm::StringRef name, Module* parent);

    // Guard region builder
    template<typename Func>
    Rule& guard(Func&& fn) {
        fn(getGuardBuilder());
        return *this;
    }

    // Body region builder
    template<typename Func>
    Rule& body(Func&& fn) {
        fn(getBodyBuilder());
        return *this;
    }

    void finalize() override;

private:
    circt::cmt2::RuleOp op_;
};

// Method: inputs, guard returns i1, body may have side effects
class Method : public FunctionLike {
public:
    Method(llvm::StringRef name,
           llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
           llvm::ArrayRef<mlir::Type> results,
           Module* parent);

    // Guard region builder (has access to arguments)
    template<typename Func>
    Method& guard(Func&& fn) {
        fn(getGuardBuilder(), getArguments());
        return *this;
    }

    // Body region builder (has access to arguments)
    template<typename Func>
    Method& body(Func&& fn) {
        fn(getBodyBuilder(), getArguments());
        return *this;
    }

    // Set custom signal names
    Method& setEnableName(llvm::StringRef name);
    Method& setReadyName(llvm::StringRef name);

    void finalize() override;

private:
    circt::cmt2::MethodOp op_;
};

// Value: no inputs or with inputs, guard returns i1, body returns data
class Value : public FunctionLike {
public:
    Value(llvm::StringRef name,
          llvm::ArrayRef<mlir::Type> results,
          Module* parent);

    // Guard region builder
    template<typename Func>
    Value& guard(Func&& fn) {
        fn(getGuardBuilder());
        return *this;
    }

    // Body region builder
    template<typename Func>
    Value& body(Func&& fn) {
        fn(getBodyBuilder());
        return *this;
    }

    // Set custom ready name
    Value& setReadyName(llvm::StringRef name);

    void finalize() override;

private:
    circt::cmt2::ValueOp op_;
};

} // namespace ecmt2
```

### 4. Instance and Call Support

```cpp
namespace ecmt2 {

class Instance {
public:
    Instance(llvm::StringRef name, ModuleBase* moduleType,
             llvm::ArrayRef<mlir::Value> args,
             Module* parent,
             llvm::ArrayRef<std::pair<std::string, std::string>> interfaceBindings = {});

    // Call methods on this instance
    llvm::SmallVector<mlir::Value, 4>
    callMethod(llvm::StringRef method,
               llvm::ArrayRef<mlir::Value> args,
               mlir::OpBuilder& builder);

    // Access values from this instance
    llvm::SmallVector<mlir::Value, 4>
    callValue(llvm::StringRef value, mlir::OpBuilder& builder);

    llvm::StringRef getName() const { return name_; }
    mlir::Operation* getOperation() const { return op_; }

private:
    std::string name_;
    circt::cmt2::InstanceOp op_;
    ModuleBase* moduleType_;
};

// Helper class for building cmt2.call operations
class CallBuilder {
public:
    static llvm::SmallVector<mlir::Value, 4>
    buildCall(Instance* instance,
              llvm::StringRef entity,
              llvm::ArrayRef<mlir::Value> args,
              mlir::OpBuilder& builder,
              mlir::Location loc);
};

} // namespace ecmt2
```

### 5. Interface Support

```cpp
namespace ecmt2 {

class InterfaceDecl {
public:
    InterfaceDecl(llvm::StringRef name, llvm::StringRef type, Module* parent);

    // Call methods through the interface
    llvm::SmallVector<mlir::Value, 4>
    callMethod(llvm::StringRef method,
               llvm::ArrayRef<mlir::Value> args,
               mlir::OpBuilder& builder);

    llvm::SmallVector<mlir::Value, 4>
    callValue(llvm::StringRef value, mlir::OpBuilder& builder);

    llvm::StringRef getName() const { return name_; }
    llvm::StringRef getType() const { return type_; }

private:
    std::string name_;
    std::string type_;
    circt::cmt2::InterfaceDeclOp op_;
};

class InterfaceDef {
public:
    InterfaceDef(llvm::StringRef name, llvm::StringRef type, Module* parent);

    // Bind instance methods to interface methods
    InterfaceDef& bind(llvm::StringRef instance,
                      llvm::StringRef instanceMethod,
                      llvm::StringRef interfaceMethod);

    void finalize();

private:
    std::string name_;
    std::string type_;
    circt::cmt2::InterfaceDefOp op_;
    llvm::SmallVector<std::tuple<std::string, std::string, std::string>, 4> bindings_;
};

} // namespace ecmt2
```

### 6. Circuit and Code Generation

```cpp
namespace ecmt2 {

class Circuit {
public:
    Circuit(llvm::StringRef topModule, mlir::MLIRContext& context);
    ~Circuit();

    // Module management
    Module* addModule(llvm::StringRef name);
    ExternalModule* addExternalModule(llvm::StringRef name,
                                      llvm::StringRef firrtlModule);

    // Code generation
    mlir::OwningOpRef<mlir::ModuleOp> generateMLIR();
    std::string emitMLIRString();

    // Conversion pipeline
    mlir::LogicalResult runCmt2ToFIRRTLPipeline();
    std::string emitFIRRTL();

    // Using firtool
    std::string emitVerilog();

    // File I/O
    mlir::LogicalResult saveToFile(llvm::StringRef filename,
                                   FileFormat format = FileFormat::MLIR);

    enum class FileFormat {
        MLIR,
        FIRRTL,
        Verilog
    };

private:
    mlir::MLIRContext& context_;
    mlir::OpBuilder builder_;
    mlir::Location loc_;
    std::string topModule_;

    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<std::unique_ptr<ExternalModule>> externalModules_;
};

} // namespace ecmt2
```

## Usage Examples

### Example 1: Simple Register-Based GCD

```cpp
#include "ecmt2/ecmt2.h"

using namespace ecmt2;

int main() {
    mlir::MLIRContext context;
    context.loadDialect<circt::cmt2::Cmt2Dialect>();
    context.loadDialect<circt::firrtl::FIRRTLDialect>();

    Circuit circuit("gcd", context);

    // Create external register module
    auto* regModule = circuit.addExternalModule("reg", "Reg32");
    regModule->bindClock("clock")
             ->bindReset("reset")
             ->bindMethod("write", "writeEnable", "writeReady",
                         {"write"}, {})
             ->bindValue("read", "readReady", {"read"})
             ->addConflict("write", "write")
             ->addConflictFree("read", "read")
             ->addSequenceBefore("read", "write");

    // Create GCD module
    auto* gcd = circuit.addModule("gcd");
    auto loc = gcd->getLoc();
    auto& builder = gcd->getBuilder();

    // Setup clock and reset
    Clock clk(builder, loc);
    Reset rst(builder, loc);
    gcd->setClockReset(clk, rst);

    // Add register instances
    auto* x = gcd->addInstance("x", regModule, {clk.getValue(), rst.getValue()});
    auto* y = gcd->addInstance("y", regModule, {clk.getValue(), rst.getValue()});

    // Add "doing" value (checks if y != 0)
    auto* doing = gcd->addValue("doing", {UInt(1, builder, loc).getType()});
    doing->guard([](mlir::OpBuilder& b) {
        // Guard always returns true
        auto loc = b.getUnknownLoc();
        auto one = UInt::constant(1, 1, b, loc);
        b.create<circt::cmt2::ReturnOp>(loc, one.getValue());
    });
    doing->body([&](mlir::OpBuilder& b) {
        auto loc = b.getUnknownLoc();
        // Get y's value
        auto yVals = y->callValue("read", b);
        auto zero = UInt::constant(0, 32, b, loc);
        // Check if y != 0
        UInt yVal(yVals[0]);
        Signal result = yVal != zero;
        b.create<circt::cmt2::ReturnOp>(loc, result.getValue());
    });
    doing->finalize();

    // Add swap rule
    auto* swap = gcd->addRule("swap");
    swap->guard([&](mlir::OpBuilder& b) {
        auto loc = b.getUnknownLoc();
        // Get x and y values
        auto xVals = x->callValue("read", b);
        auto yVals = y->callValue("read", b);

        UInt xVal(xVals[0]);
        UInt yVal(yVals[0]);

        // y > x
        Signal cond1 = yVal > xVal;

        // Call @this @doing (private function)
        auto doingVals = CallBuilder::buildCall(nullptr, "this.doing", {}, b, loc);
        Signal cond2(doingVals[0]);

        // Combine conditions
        Signal guard = cond1 & cond2;
        b.create<circt::cmt2::ReturnOp>(loc, guard.getValue());
    });
    swap->body([&](mlir::OpBuilder& b) {
        auto loc = b.getUnknownLoc();
        // Read values
        auto xVals = x->callValue("read", b);
        auto yVals = y->callValue("read", b);

        // Swap
        x->callMethod("write", {yVals[0]}, b);
        y->callMethod("write", {xVals[0]}, b);
    });
    swap->finalize();

    // Add "start" method
    auto uint32Type = UInt(32, builder, loc).getType();
    auto* start = gcd->addMethod("start",
                                 {{"a", uint32Type}, {"b", uint32Type}},
                                 {});
    start->guard([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto loc = b.getUnknownLoc();
        // Guard: !doing
        auto doingVals = CallBuilder::buildCall(nullptr, "this.doing", {}, b, loc);
        Signal doingSignal(doingVals[0]);
        Signal guard = ~doingSignal;
        b.create<circt::cmt2::ReturnOp>(loc, guard.getValue());
    });
    start->body([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto loc = b.getUnknownLoc();
        // Write a to x, b to y
        x->callMethod("write", {args[0]}, b);
        y->callMethod("write", {args[1]}, b);
    });
    start->finalize();

    // Generate MLIR
    circuit.saveToFile("gcd.mlir", Circuit::FileFormat::MLIR);

    // Run conversion pipeline and generate Verilog
    if (succeeded(circuit.runCmt2ToFIRRTLPipeline())) {
        circuit.saveToFile("gcd.v", Circuit::FileFormat::Verilog);
    }

    return 0;
}
```

### Example 2: Parametric Counter Generator

```cpp
#include "ecmt2/ecmt2.h"

using namespace ecmt2;

// Parametric counter module generator
class CounterGenerator {
public:
    static Module* generate(Circuit& circuit, llvm::StringRef name, unsigned width) {
        auto* counter = circuit.addModule(name);
        auto loc = counter->getLoc();
        auto& builder = counter->getBuilder();

        // Setup clock and reset
        Clock clk(builder, loc);
        Reset rst(builder, loc);
        counter->setClockReset(clk, rst);

        // Add register instance (assuming reg module is already defined)
        auto* regInst = counter->addInstance("count_reg", /* reg module */,
                                             {clk.getValue(), rst.getValue()});

        // Increment method
        auto uintType = UInt(width, builder, loc).getType();
        auto* increment = counter->addMethod("increment", {}, {uintType});

        increment->guard([](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument>) {
            auto loc = b.getUnknownLoc();
            auto one = UInt::constant(1, 1, b, loc);
            b.create<circt::cmt2::ReturnOp>(loc, one.getValue());
        });

        increment->body([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument>) {
            auto loc = b.getUnknownLoc();
            auto vals = regInst->callValue("read", b);
            UInt current(vals[0]);
            UInt one = UInt::constant(1, width, b, loc);
            Signal next = current + one;

            regInst->callMethod("write", {next.getValue()}, b);
            b.create<circt::cmt2::ReturnOp>(loc, next.getValue());
        });
        increment->finalize();

        // Get value
        auto* getValue = counter->addValue("get_value", {uintType});
        getValue->guard([](mlir::OpBuilder& b) {
            auto loc = b.getUnknownLoc();
            auto one = UInt::constant(1, 1, b, loc);
            b.create<circt::cmt2::ReturnOp>(loc, one.getValue());
        });
        getValue->body([&](mlir::OpBuilder& b) {
            auto loc = b.getUnknownLoc();
            auto vals = regInst->callValue("read", b);
            b.create<circt::cmt2::ReturnOp>(loc, vals[0]);
        });
        getValue->finalize();

        return counter;
    }
};

// Usage
int main() {
    mlir::MLIRContext context;
    Circuit circuit("top", context);

    auto* counter8 = CounterGenerator::generate(circuit, "counter8", 8);
    auto* counter16 = CounterGenerator::generate(circuit, "counter16", 16);
    auto* counter32 = CounterGenerator::generate(circuit, "counter32", 32);

    circuit.saveToFile("counters.mlir");
    return 0;
}
```

### Example 3: Interface-Based Design

```cpp
#include "ecmt2/ecmt2.h"

using namespace ecmt2;

int main() {
    mlir::MLIRContext context;
    Circuit circuit("hello", context);

    // Child module with interface declaration
    auto* child = circuit.addModule("child");
    auto loc = child->getLoc();
    auto& builder = child->getBuilder();

    Clock clk(builder, loc);
    Reset rst(builder, loc);
    child->setClockReset(clk, rst);

    // Declare interface for reading data
    auto* readerIface = child->defineInterface("reader", "Reader");

    // Add a register instance
    auto* r = child->addInstance("r", /* reg module */,
                                  {clk.getValue(), rst.getValue()});

    // Process method using interface
    auto uint32Type = UInt(32, builder, loc).getType();
    auto* process = child->addMethod("set",
                                     {{"v", uint32Type}},
                                     {uint32Type});

    process->guard([](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument>) {
        auto loc = b.getUnknownLoc();
        auto one = UInt::constant(1, 1, b, loc);
        b.create<circt::cmt2::ReturnOp>(loc, one.getValue());
    });

    process->body([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto loc = b.getUnknownLoc();

        // Call interface method
        auto interfaceData = readerIface->callValue("getData", b);

        UInt data(interfaceData[0]);
        UInt arg(args[0]);
        Signal result = data + arg;

        // Write to register
        auto rData = r->callValue("read", b);
        UInt rVal(rData[0]);
        Signal sum = result + rVal;

        r->callMethod("write", {sum.getValue()}, b);

        b.create<circt::cmt2::ReturnOp>(loc, sum.getValue());
    });
    process->finalize();

    // Parent module
    auto* parent = circuit.addModule("hello");
    auto& parentBuilder = parent->getBuilder();
    auto parentLoc = parent->getLoc();

    Clock parentClk(parentBuilder, parentLoc);
    Reset parentRst(parentBuilder, parentLoc);
    parent->setClockReset(parentClk, parentRst);

    // Add storage register
    auto* x = parent->addInstance("x", /* reg module */,
                                   {parentClk.getValue(), parentRst.getValue()});

    // Define interface binding
    auto* readIface = parent->defineInterfaceDef("ReadX", "Reader");
    readIface->bind("x", "read", "getData");
    readIface->finalize();

    // Instantiate child with interface binding
    auto* childInst = parent->addInstance("c", child,
                                          {parentClk.getValue(), parentRst.getValue()},
                                          {{"reader", "ReadX"}});

    // Add method that calls child
    auto* write = parent->addMethod("write",
                                    {{"v", uint32Type}},
                                    {uint32Type});
    write->guard([](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument>) {
        auto loc = b.getUnknownLoc();
        auto one = UInt::constant(1, 1, b, loc);
        b.create<circt::cmt2::ReturnOp>(loc, one.getValue());
    });
    write->body([&](mlir::OpBuilder& b, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto loc = b.getUnknownLoc();

        // Call child's set method
        auto result = childInst->callMethod("set", {args[0]}, b);

        // Also write to x
        x->callMethod("write", {args[0]}, b);

        b.create<circt::cmt2::ReturnOp>(loc, result[0]);
    });
    write->finalize();

    circuit.saveToFile("hello.mlir");
    return 0;
}
```

## Advanced Features

### Signal Manipulation Utilities

```cpp
namespace ecmt2 {

// Helper functions for common patterns
class SignalUtils {
public:
    // Create a reduction operation
    static Signal reduceAnd(llvm::ArrayRef<Signal> signals, mlir::OpBuilder& builder);
    static Signal reduceOr(llvm::ArrayRef<Signal> signals, mlir::OpBuilder& builder);
    static Signal reduceXor(llvm::ArrayRef<Signal> signals, mlir::OpBuilder& builder);

    // Multiplexer
    static Signal mux(const Signal& sel, const Signal& trueVal, const Signal& falseVal);

    // Priority encoder
    static Signal priorityMux(llvm::ArrayRef<std::pair<Signal, Signal>> cases,
                             const Signal& defaultVal,
                             mlir::OpBuilder& builder);
};

} // namespace ecmt2
```

### Type Conversion Utilities

```cpp
namespace ecmt2 {

class TypeConverter {
public:
    // Convert between UInt and SInt
    static SInt toSInt(const UInt& u);
    static UInt toUInt(const SInt& s);

    // Sign extension
    static Signal signExtend(const Signal& s, unsigned width);
    static Signal zeroExtend(const Signal& s, unsigned width);
};

} // namespace ecmt2
```

### Debugging and Introspection

```cpp
namespace ecmt2 {

class DebugUtils {
public:
    // Print module structure
    static void printModuleHierarchy(const Module* module, llvm::raw_ostream& os);

    // Verify correctness
    static mlir::LogicalResult verifyModule(const Module* module);

    // Dump operations
    static void dumpMLIR(const Circuit& circuit, llvm::raw_ostream& os);
};

} // namespace ecmt2
```

## Build System Integration

### CMakeLists.txt

```cmake
# ecmt2 library
add_library(ecmt2
    lib/Signal.cpp
    lib/Module.cpp
    lib/FunctionLike.cpp
    lib/Instance.cpp
    lib/Interface.cpp
    lib/Circuit.cpp
    lib/SignalUtils.cpp
    lib/TypeConverter.cpp
    lib/DebugUtils.cpp
)

target_include_directories(ecmt2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ecmt2 PUBLIC
    CIRCTCmt2
    CIRCTCmt2ToFIRRTL
    CIRCTCmt2Transforms
    CIRCTHW
    CIRCTFIRRTL
    MLIRIR
)

# Example executables
add_executable(gcd_example examples/gcd.cpp)
target_link_libraries(gcd_example ecmt2)

add_executable(counter_example examples/counter.cpp)
target_link_libraries(counter_example ecmt2)
```

## Error Handling

```cpp
namespace ecmt2 {

class Cmt2Exception : public std::runtime_error {
public:
    explicit Cmt2Exception(const std::string& msg) : std::runtime_error(msg) {}
};

class TypeMismatchException : public Cmt2Exception {
public:
    TypeMismatchException(mlir::Type expected, mlir::Type actual)
        : Cmt2Exception("Type mismatch: expected " +
                       expected.dump() + ", got " + actual.dump()) {}
};

class ModuleNotFoundException : public Cmt2Exception {
public:
    explicit ModuleNotFoundException(llvm::StringRef name)
        : Cmt2Exception("Module not found: " + name.str()) {}
};

} // namespace ecmt2
```

## Performance Characteristics

### Zero Serialization Overhead

The ecmt2 DSL has **zero serialization cost** because:

1. **Direct Construction**: All operations are created using `builder.create<OpType>(...)` calls
2. **In-Memory IR**: MLIR operations exist only in memory, never as text
3. **No Parsing**: No string parsing, lexing, or grammar processing
4. **Immediate Verification**: MLIR's built-in verifiers catch errors immediately

### Comparison with Other Approaches

```cpp
// ❌ Text-based approach (SLOW):
std::ostringstream ss;
ss << "cmt2.module @" << name << " { ... }";
auto module = parseMLIR(ss.str());  // Parse, lex, verify - expensive!

// ✅ ecmt2 approach (FAST):
auto module = circuit.addModule(name);  // Direct C++ constructor - instant!
```

### Performance Benefits

- **Build Time**: 10-100x faster than text generation + parsing
- **Memory**: No intermediate string representations
- **Type Safety**: Compile-time checks instead of runtime parsing errors
- **Debugging**: Stack traces point directly to C++ code, not parser internals

## Best Practices

1. **Zero Serialization**: Never generate MLIR text - always use OpBuilder
2. **RAII**: Always use smart pointers and RAII for resource management
3. **Type Safety**: Leverage C++ type system to catch errors at compile time
4. **Const Correctness**: Mark methods const when they don't modify state
5. **Builder Pattern**: Use method chaining for fluent APIs
6. **Error Handling**: Use exceptions for exceptional conditions, LogicalResult for expected failures
7. **Documentation**: Document all public APIs with Doxygen comments
8. **Testing**: Write unit tests for all components
9. **Direct MLIR API**: When in doubt, use MLIR OpBuilder directly

## Future Extensions

1. **Template Metaprogramming**: Use C++ templates for parametric hardware generation
2. **Concepts**: Use C++20 concepts for better type constraints
3. **Coroutines**: Potentially use C++20 coroutines for behavioral descriptions
4. **Static Analysis**: Provide compile-time checks for common errors
5. **DSL Extensions**: Add domain-specific extensions for common patterns (FSMs, pipelines, etc.)
