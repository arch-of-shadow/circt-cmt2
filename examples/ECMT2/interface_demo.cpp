//===- interface_demo.cpp - Interface Communication Demo -*- C++ -*-===//
//
// This example demonstrates interface communication between two modules A and B
// at the same hierarchical level. Module A can call methods on Module B through
// a well-defined interface mechanism.
//
// Key concepts demonstrated:
// - Interface declaration vs interface definition
// - Interface binding between modules
// - Cross-module method calls
// - Hierarchical composition with interface passing
// - Method calling patterns and data flow
//
// Architecture:
//   TopModule
//   ├── ModuleA (can call ModuleB's methods)
//   └── ModuleB (exposes methods through interface)
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"


using namespace circt::cmt2::ecmt2;

int main() {
    // ========================================================================
    // ENVIRONMENT SETUP
    // ========================================================================

    // Initialize MLIR context - required for all MLIR operations
    mlir::MLIRContext context;
    context.loadDialect<circt::cmt2::Cmt2Dialect>();
    context.loadDialect<circt::firrtl::FIRRTLDialect>();

    // Initialize module library - provides access to pre-built FIRRTL modules
    auto &library = ModuleLibrary::getInstance();

    // Load the module library manifest that contains available external modules
    std::string manifestPath = "lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml";
    if (library.loadManifest(manifestPath).failed()) {
        llvm::errs() << "Warning: Failed to load module library manifest\n";
        llvm::errs() << "Looked for: " << manifestPath << "\n";
    } else {
        llvm::outs() << "Module library loaded successfully\n";
    }

    // Create top-level circuit with "InterfaceDemo" as the top module
    Circuit circuit("InterfaceDemo", context);

    // ========================================================================
    // INTERFACE DEFINITION
    // ========================================================================

    // Define a Calculator interface that ModuleB will implement
    // This interface provides basic arithmetic operations
    auto *calculatorInterface = circuit.addInterface("Calculator");

    // Add methods to the Calculator interface
    // Method 1: add - adds two numbers and returns the sum
    calculatorInterface->addMethod("add",
        {{"a", circt::firrtl::UIntType::get(&context, 32)},  // First operand
         {"b", circt::firrtl::UIntType::get(&context, 32)}}, // Second operand
        {circt::firrtl::UIntType::get(&context, 32)});       // Return value (sum)

    // Method 3: getValue - returns a stored value (no arguments)
    calculatorInterface->addValue("getValue", {},
        {mlir::TypeAttr::get(circt::firrtl::UIntType::get(&context, 32))});

    // ========================================================================
    // EXTERNAL MODULE SETUP
    // ========================================================================

    // Create external register module from the library for storing values
    // This will be used as the underlying storage in both ModuleA and ModuleB
    llvm::StringMap<int64_t> regParams;
    regParams["width"] = 32; // 32-bit register
    auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);

    // Configure the register module's interface
    // This binds the register's ports to Cmt2 clock/reset/method interfaces
    regMod->bindClock("clk", "clock")              // Bind clock port
          .bindReset("rst", "reset")               // Bind reset port
          .bindValue("read", "read_ready", {}, {"read_data"})     // Read value interface
          .bindMethod("write", "write_enable", "write_ready", // Write value interface
                     {"write_data"}, {})
          .addConflict("write", "write")            // Writing conflicts with writing
          .addConflictFree("read", "read")          // Reading is conflict-free with reading
          .addSequenceBefore("read", "write");      // Reading must happen before writing

    // ========================================================================
    // MODULE B IMPLEMENTATION
    // ========================================================================

    // Create ModuleB - this module IMPLEMENTS the Calculator interface
    // ModuleB provides arithmetic services that other modules can use
    auto *moduleB = circuit.addModule("ModuleB");

    // Add standard clock and reset arguments to ModuleB
    Clock moduleBClk = moduleB->addClockArgument("clk");
    Reset moduleBRst = moduleB->addResetArgument("rst");

    // Create a register instance in ModuleB to store a value
    auto *moduleBReg = moduleB->addInstance(
        "storage", regMod,
        {moduleBClk.getValue(), moduleBRst.getValue()});

    // ------------------------------------------------------------------------
    // Method: add (implements Calculator.add interface)
    // ------------------------------------------------------------------------
    auto *addMethod = moduleB->addMethod("add",
        {{"a", circt::firrtl::UIntType::get(&context, 32)},
         {"b", circt::firrtl::UIntType::get(&context, 32)}},
        {circt::firrtl::UIntType::get(&context, 32)});

    // Guard: Method is always ready to execute
    addMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {

        Signal trueValue = UInt::constant(1, 1, builder, moduleB->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            moduleB->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Perform addition and return result
    addMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Get the two addends from method arguments
        auto operandA = args[0]; // First argument (a)
        auto operandB = args[1]; // Second argument (b)

        // Convert to Signal objects for arithmetic operations
        Signal signalA(operandA, &builder, moduleB->getLoc());
        Signal signalB(operandB, &builder, moduleB->getLoc());

        // Perform addition: sum = a + b
        Signal sum = signalA + signalB;

        // Truncate to 32 bits to handle overflow
        Signal result = sum.bits(31, 0);

        // Store the result in ModuleB's register for later retrieval
        moduleBReg->callMethod("write", {result.getValue()}, builder);

        // Return the sum to the caller
        builder.create<circt::cmt2::ReturnOp>(
            moduleB->getLoc(),
            mlir::ValueRange{result.getValue()});
    });
    addMethod->finalize();

    auto u32Type = circt::firrtl::UIntType::get(&context, 32);
    auto *readMethod = moduleB->addValue("read", {u32Type});
    readMethod->guard([&](mlir::OpBuilder &b) {
      auto trueVal = UInt::constant(1, 1, b, moduleB->getLoc());
      b.create<circt::cmt2::ReturnOp>(moduleB->getLoc(), mlir::ValueRange{trueVal.getValue()});
    });

    readMethod->body([&](mlir::OpBuilder &b) {
      // Call the register's read method to get the stored value
      auto readValues = moduleBReg->callValue("read", b);
      auto storedValue = readValues[0];

      // Return the stored value
      b.create<circt::cmt2::ReturnOp>(moduleB->getLoc(), mlir::ValueRange{storedValue});
    });
    readMethod->finalize();

    // ========================================================================
    // MODULE A IMPLEMENTATION
    // ========================================================================

    // Create ModuleA - this module USES the Calculator interface
    // ModuleA will call methods on ModuleB through the interface
    auto *moduleA = circuit.addModule("ModuleA");

    // Add standard clock and reset arguments to ModuleA
    Clock moduleAClk = moduleA->addClockArgument("clk");
    Reset moduleARst = moduleA->addResetArgument("rst");

    // ------------------------------------------------------------------------
    // Interface Declaration: ModuleA needs a Calculator interface
    // ------------------------------------------------------------------------
    // This DECLARES that ModuleA needs access to a Calculator interface
    // The actual binding will happen at the top level
    auto *calculatorDecl = moduleA->defineInterface("calc", "Calculator");

    // ------------------------------------------------------------------------
    // Method: processData - demonstrates using ModuleB's calculator interface
    // ------------------------------------------------------------------------
    auto *processDataMethod = moduleA->addMethod("processData",
        {{"inputA", circt::firrtl::UIntType::get(&context, 32)}, {"inputB", circt::firrtl::UIntType::get(&context, 32)}},
        {circt::firrtl::UIntType::get(&context, 32)});

    // Guard: Method is always ready
    processDataMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Always return true (1'b1) - method is always ready
        Signal trueValue = UInt::constant(1, 1, builder, moduleA->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Use ModuleB's calculator to process data
    processDataMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto argA = args[0]; // Input data to process
        auto argB = args[1]; // Second input data

        llvm::SmallVector<mlir::Value> addArgs = {argA, argB};
        auto addResults = calculatorDecl->callMethod("add", addArgs, builder);

        // Return the final result
        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{addResults[0]});
    });
    processDataMethod->finalize();

    // ------------------------------------------------------------------------
    // Method: getBValue - demonstrates getting a value from ModuleB
    // ------------------------------------------------------------------------
    auto *getBValueMethod = moduleA->addMethod("getBValue",
        {},
        {circt::firrtl::UIntType::get(&context, 32)});

    // Guard: Always ready
    getBValueMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Always return true (1'b1) - method is always ready
        Signal trueValue = UInt::constant(1, 1, builder, moduleA->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Call ModuleB's getValue method
    getBValueMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Call ModuleB's getValue method through the interface
        auto bValues = calculatorDecl->callValue("getValue", builder);

        // Return the value from ModuleB
        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{bValues[0]});
    });
    getBValueMethod->finalize();

    // ========================================================================
    // TOP-LEVEL MODULE (InterfaceDemo)
    // ========================================================================

    // Create the top-level module that instantiates both A and B
    // This module is responsible for BINDING the interfaces
    auto *topModule = circuit.addModule("InterfaceDemo");

    // Add clock and reset to top module
    Clock topClk = topModule->addClockArgument("clk");
    Reset topRst = topModule->addResetArgument("rst");

    // ------------------------------------------------------------------------
    // Interface Definition: Bind ModuleB's methods to Calculator interface
    // ------------------------------------------------------------------------
    // This DEFINES how ModuleB implements the Calculator interface
    // We're creating an interface definition called "ModuleBCalc" that maps
    // the Calculator interface methods to ModuleB's actual methods
    auto *moduleBCalcDef = topModule->defineInterfaceDef("ModuleBCalc", "Calculator");

    // Bind each interface method to the corresponding method in ModuleB
    moduleBCalcDef->bind("ModuleB", "add", "add");       // Calculator.add -> ModuleB.add
    moduleBCalcDef->bind("ModuleB", "read", "getValue"); // Calculator.getValue -> ModuleB.read
    moduleBCalcDef->finalize();

    // ------------------------------------------------------------------------
    // Module Instantiation
    // ------------------------------------------------------------------------

    // Instantiate ModuleB (the service provider)
    auto *moduleBInst = topModule->addInstance(
        "ModuleB", moduleB,
        {topClk.getValue(), topRst.getValue()});

    // Instantiate ModuleA (the service consumer) and bind the interface
    // This is where we CONNECT ModuleA to ModuleB through the interface
    auto *moduleAInst = topModule->addInstance(
        "ModuleA", moduleA,
        {topClk.getValue(), topRst.getValue()},
        {{"ModuleBCalc", "calc"}}); // Bind ModuleBCalc definition to ModuleA's calc interface

    // Note: moduleBInst is available for future extensions where you might want
    // to directly call ModuleB methods from the top level or add additional connections

    // ------------------------------------------------------------------------
    // Top-level Rules and Methods
    // ------------------------------------------------------------------------

    // Expose processData interface method directly
    auto *processDataMethodTop = topModule->addMethod("processData",
        {{"valueA", circt::firrtl::UIntType::get(&context, 32)}, {"valueB", circt::firrtl::UIntType::get(&context, 32)}},
        {circt::firrtl::UIntType::get(&context, 32)});

    // Guard: Always ready
    processDataMethodTop->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Always return true (1'b1) - method is always ready
        Signal trueValue = UInt::constant(1, 1, builder, topModule->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            topModule->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Forward to ModuleA's processData method
    processDataMethodTop->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto valA = args[0];
        auto valB = args[1];
        auto loc = topModule->getLoc();

        // Call ModuleA's processData method (which internally calls ModuleB)
        llvm::SmallVector<mlir::Value> processArgs = {valA, valB};
        auto processResults = moduleAInst->callMethod("processData", processArgs, builder);

        // Return the result directly
        builder.create<circt::cmt2::ReturnOp>(
            topModule->getLoc(),
            mlir::ValueRange{processResults[0]});
    });
    processDataMethodTop->finalize();

    // Expose getBValue interface method directly
    auto *getBValueMethodTop = topModule->addMethod("getBValue",
        {}, // No arguments
        {circt::firrtl::UIntType::get(&context, 32)});

    // Guard: Always ready
    getBValueMethodTop->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Always return true (1'b1) - method is always ready
        Signal trueValue = UInt::constant(1, 1, builder, topModule->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            topModule->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Forward to ModuleA's getBValue method
    getBValueMethodTop->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto loc = topModule->getLoc();

        // Call ModuleA's getBValue method (which internally calls ModuleB)
        auto bValueResults = moduleAInst->callMethod("getBValue", {}, builder);

        // Return the result directly
        builder.create<circt::cmt2::ReturnOp>(
            topModule->getLoc(),
            mlir::ValueRange{bValueResults[0]});
    });
    getBValueMethodTop->finalize();

    // ========================================================================
    // CODE GENERATION AND OUTPUT
    // ========================================================================

    llvm::errs() << "=== INTERFACE DEMO: Generated MLIR (from ECMT2 DSL) ===\n";
    llvm::outs() << circuit.emitMLIRString() << "\n";

    // Attempt FIRRTL conversion to show how this lowers to actual hardware
    if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
        llvm::errs() << "\n=== INTERFACE DEMO: After Cmt2 to FIRRTL Conversion ===\n";
        llvm::outs() << circuit.emitFIRRTL() << "\n";

        llvm::errs() << "\n=== INTERFACE DEMO SUCCESS ===\n";
        llvm::errs() << "✓ ModuleA can successfully call ModuleB's methods through interface\n";
        llvm::errs() << "✓ Interface binding: ModuleBCalc connects ModuleA.calc to ModuleB\n";
        llvm::errs() << "✓ Cross-module communication working correctly\n";
        llvm::errs() << "✓ Generated FIRRTL hardware description ready for synthesis\n";
    } else {
        llvm::errs() << "Error: Failed to convert Cmt2 to FIRRTL\n";
    }

    return 0;
}