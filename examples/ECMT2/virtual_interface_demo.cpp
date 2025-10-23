//===- virtual_interface_demo.cpp - Virtual Interface Demo -*- C++ -*-===//
//
// This example demonstrates a virtual interface pattern where Module A contains
// Module B as a submodule. Module B has a "read_by" method that reads its
// internal register, and Module A exposes pass-through methods to access it.
//
// Key concepts demonstrated:
// - Hierarchical module composition (Module A contains Module B)
// - Pass-through interface methods
// - Internal register access through submodule methods
// - Virtual interface pattern abstraction
// - STL (Standard Template Library) usage for register creation
//
// Architecture:
//   ModuleA (Top Module)
//   └── ModuleB (submodule with internal register and read_by method)
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/ValueRange.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt::cmt2::ecmt2;
using namespace circt::cmt2::ecmt2::stl;

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

    // Create top-level circuit with "ModuleA" as the top module
    Circuit circuit("ModuleA", context);

    // ========================================================================
    // INTERFACE DEFINITION
    // ========================================================================

    // Define a simple interface for upward communication
    // This interface allows Module A to call methods on outer modules
    auto *outerInterface = circuit.addInterface("OuterInterface");

    // Add one simple method to get data from outer module
    outerInterface->addMethod("get_outer_data", {},
        {circt::firrtl::UIntType::get(&context, 32)});

    // ========================================================================
    // EXTERNAL MODULE SETUP (Using STL)
    // ========================================================================

    // Use STL to create a 32-bit register module for internal storage
    // This will be used as the internal register in Module B
    auto *regMod = STLLibrary::createRegModule(32, 0, circuit);
    llvm::outs() << "✓ Created 32-bit register module using STL\n";

    // ========================================================================
    // MODULE B IMPLEMENTATION (Submodule with read_by method)
    // ========================================================================

    // Create ModuleB - this module has an internal register and read_by method
    // Module B will be instantiated as a submodule inside Module A
    auto *moduleB = circuit.addModule("ModuleB");

    // Add standard clock and reset arguments to ModuleB
    Clock moduleBClk = moduleB->addClockArgument("clk");
    Reset moduleBRst = moduleB->addResetArgument("rst");

    // ------------------------------------------------------------------------
    // Interface Declaration: Module B needs access to outer interface
    // ------------------------------------------------------------------------
    // Module B declares that it needs access to OuterInterface
    auto *outerInterfaceDecl = moduleB->defineInterfaceDecl("outer_iface", "OuterInterface");

    // Create an internal register instance in ModuleB using STL
    auto *moduleBReg = moduleB->addInstance(
        "internal_data", regMod,
        {moduleBClk.getValue(), moduleBRst.getValue()});

    // ------------------------------------------------------------------------
    // Method: write_data (gets data from outer interface and writes to internal register)
    // ------------------------------------------------------------------------
    auto *writeDataMethod = moduleB->addMethod("write_data", {}, {});

    // Guard: Method is always ready to execute
    writeDataMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        Signal trueValue = UInt::constant(1, 1, builder, moduleB->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            moduleB->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Get data from outer interface and write to internal register
    writeDataMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Call the outer interface's get_outer_data method to get data
        auto outerData = outerInterfaceDecl->callMethod("get_outer_data", {}, builder);

        // Write the data from outer interface to internal register
        moduleBReg->callMethod("write", {outerData[0]}, builder);

        // Return (no return value for method)
        builder.create<circt::cmt2::ReturnOp>(
            moduleB->getLoc(),
            mlir::ValueRange{});
    });
    writeDataMethod->finalize();

    // ------------------------------------------------------------------------
    // Method: read_by (reads internal register by ID)
    // ------------------------------------------------------------------------
    auto *readByMethod = moduleB->addMethod("read_by",
        {{"id", circt::firrtl::UIntType::get(&context, 8)}}, // ID parameter (8-bit)
        {circt::firrtl::UIntType::get(&context, 32)});      // Returns 32-bit data

    // Guard: Method is always ready to execute
    readByMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        Signal trueValue = UInt::constant(1, 1, builder, moduleB->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            moduleB->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Read from internal register (ignoring ID for this demo)
    readByMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Note: ID parameter is unused in this simple demo
        // In a real design, this could select different registers based on ID

        // Read from internal register
        auto readValues = moduleBReg->callValue("read", builder);
        auto storedValue = readValues[0];

        // Return the stored value
        builder.create<circt::cmt2::ReturnOp>(
            moduleB->getLoc(),
            mlir::ValueRange{storedValue});
    });
    readByMethod->finalize();

    // ========================================================================
    // MODULE A AS TOP MODULE (Contains Module B as submodule)
    // ========================================================================

    // Module A is now the top module - it contains Module B as a submodule
    // and exposes pass-through methods to access Module B's functionality
    auto *moduleA = circuit.addModule("ModuleA");

    // Add standard clock and reset arguments to ModuleA
    Clock moduleAClk = moduleA->addClockArgument("clk");
    Reset moduleARst = moduleA->addResetArgument("rst");

    
    // ------------------------------------------------------------------------
    // Interface Declaration: Module A needs access to outer interface
    // ------------------------------------------------------------------------
    // Module A declares that it needs access to OuterInterface
    auto *aOuterInterfaceDecl = moduleA->defineInterfaceDecl("outer_iface", "OuterInterface");


    // ------------------------------------------------------------------------
    // Submodule Instantiation: Module B inside Module A with interface binding
    // ------------------------------------------------------------------------
    auto *moduleBInst = moduleA->addInstance(
        "submodule_b", moduleB,
        {moduleAClk.getValue(), moduleARst.getValue()},
        {{"outer_iface", "outer_iface"}});  // Bind the interface

    // ------------------------------------------------------------------------
    // Top-level Methods (Module A's interface)
    // ------------------------------------------------------------------------

    // Convenience method to test write operation (Module B gets data itself)
    auto *testWriteMethod = moduleA->addMethod("test_write", {},
        {});

    // Guard: Always ready
    testWriteMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        Signal trueValue = UInt::constant(1, 1, builder, moduleA->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Trigger Module B's write_data method (it will get data from outer interface itself)
    testWriteMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Call Module B's write_data method - it will get data from outer interface itself
        moduleBInst->callMethod("write_data", {}, builder);

        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{});
    });
    testWriteMethod->finalize();

    // Convenience method to test read operation only
    auto *testReadMethod = moduleA->addMethod("test_read",
        {{"id", circt::firrtl::UIntType::get(&context, 8)}},
        {circt::firrtl::UIntType::get(&context, 32)});

    // Guard: Always ready
    testReadMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        Signal trueValue = UInt::constant(1, 1, builder, moduleA->getLoc());
        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{trueValue.getValue()});
    });

    // Body: Forward read operation to ModuleB
    testReadMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        auto id = args[0];
        auto readResults = moduleBInst->callMethod("read_by", {id}, builder);
        builder.create<circt::cmt2::ReturnOp>(
            moduleA->getLoc(),
            mlir::ValueRange{readResults[0]});
    });
    testReadMethod->finalize();

    // ========================================================================
    // CODE GENERATION AND OUTPUT
    // ========================================================================

    llvm::errs() << "=== VIRTUAL INTERFACE DEMO: Generated MLIR (from ECMT2 DSL) ===\n";
    llvm::outs() << circuit.emitMLIRString() << "\n";

    // Attempt FIRRTL conversion to show how this lowers to actual hardware
    if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
        llvm::errs() << "\n=== VIRTUAL INTERFACE DEMO: After Cmt2 to FIRRTL Conversion ===\n";
        llvm::outs() << circuit.emitFIRRTL() << "\n";

        llvm::errs() << "\n=== VIRTUAL INTERFACE DEMO SUCCESS ===\n";
        llvm::errs() << "✓ ModuleA is the top module containing ModuleB as submodule\n";
        llvm::errs() << "✓ ModuleB's read_by method reads internal register\n";
        llvm::errs() << "✓ ModuleB declares OuterInterface for upward communication\n";
        llvm::errs() << "✓ ModuleA binds OuterInterface for ModuleB's use\n";
        llvm::errs() << "✓ ModuleB gets data directly from outer interface\n";
        llvm::errs() << "✓ Interface-inside pattern implemented correctly\n";
        llvm::errs() << "✓ STL register creation working properly\n";
        llvm::errs() << "✓ Generated FIRRTL hardware description ready for synthesis\n";
    } else {
        llvm::errs() << "Error: Failed to convert Cmt2 to FIRRTL\n";
    }

    return 0;
}