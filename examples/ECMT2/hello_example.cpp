//===- hello_example.cpp - Hello Example using ECMT2 DSL -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates the complete ECMT2 DSL capabilities including:
// - External FIRRTL modules from the module library
// - Interface mechanism (InterfaceDecl, InterfaceDef, interface bindings)
// - Hierarchical module composition with interface passing
// - Methods, rules, and values
//
// This generates the same design as test/Dialect/Cmt2/hello.mlir
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt::cmt2::ecmt2;

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<circt::cmt2::Cmt2Dialect>();
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  // Initialize module library
  auto &library = ModuleLibrary::getInstance();

  // Load manifest from build directory
  std::string manifestPath = "lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml";
  if (library.loadManifest(manifestPath).failed()) {
    llvm::errs() << "Warning: Failed to load module library manifest\n";
    llvm::errs() << "Looked for: " << manifestPath << "\n";
  } else {
    llvm::outs() << "Module library loaded successfully\n";
  }

  // Create circuit with "hello" as top module
  Circuit circuit("hello", context);

  // ============================================================================
  // Define Reader interface
  // ============================================================================
  auto *readerInterface = circuit.addInterface("Reader");
  readerInterface->addValue("getData", {},
    {mlir::TypeAttr::get(circt::firrtl::UIntType::get(&context, 32))});

  // ============================================================================
  // Define Writer interface
  // ============================================================================
  auto *writerInterface = circuit.addInterface("Writer");
  writerInterface->addMethod("store",
    {{"data", circt::firrtl::UIntType::get(&context, 32)}},
    {});

  // ============================================================================
  // Create external register module from library
  // ============================================================================
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {"read_data"})
        .bindMethod("write", "write_enable", "write_ready",
                   {"write_data"}, {})
        .addConflict("write", "write")      // write <> write
        .addConflictFree("read", "read")    // read / read
        .addSequenceBefore("read", "write"); // read < write

  // ============================================================================
  // Create child module
  // ============================================================================
  auto *childMod = circuit.addModule("child");

  // Add clock and reset as module arguments
  Clock childClk = childMod->addClockArgument("clk");
  Reset childRst = childMod->addResetArgument("rst");

  // Declare reader interface (inward interface - child needs this from parent)
  auto *childReaderDecl = childMod->defineInterface("reader", "Reader");

  // Create register instance in child
  auto *childReg = childMod->addInstance(
      "r", regMod,
      {childClk.getValue(), childRst.getValue()});

  // Add set method to child
  auto *setMethod = childMod->addMethod("set",
      {{"v", circt::firrtl::UIntType::get(&context, 32)}},
      {circt::firrtl::UIntType::get(&context, 32)});

  // Guard for set method (always ready)
  setMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    builder.create<circt::cmt2::ReturnOp>(
        childMod->getLoc(),
        mlir::ValueRange{});
  });

  // Body for set method
  setMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    // Call reader interface getData
    auto readerData = childReaderDecl->callValue("getData", builder);

    // Read current value from register
    auto currentVals = childReg->callValue("read", builder);
    Signal currentVal(currentVals[0], &builder, childMod->getLoc());

    // Get method argument %v
    auto methodArg = args[0];
    Signal vSignal(methodArg, &builder, childMod->getLoc());

    // Compute: reader_data + v
    Signal readerSig(readerData[0], &builder, childMod->getLoc());
    Signal sum1 = readerSig + vSignal;
    Signal sum1_trunc = sum1.bits(31, 0);

    // Compute: current + (reader_data + v)
    Signal sum2 = currentVal + sum1_trunc;
    Signal newVal = sum2.bits(31, 0);

    // Write back to register
    childReg->callMethod("write", {newVal.getValue()}, builder);

    // Return the new value
    builder.create<circt::cmt2::ReturnOp>(
        childMod->getLoc(),
        mlir::ValueRange{newVal.getValue()});
  });

  setMethod->finalize();

  // ============================================================================
  // Create hello module (top-level)
  // ============================================================================
  auto *helloMod = circuit.addModule("hello");

  // Add clock and reset as module arguments
  Clock helloClk = helloMod->addClockArgument("clk");
  Reset helloRst = helloMod->addResetArgument("rst");

  // Declare writer interface (outward interface - for external connections)
  auto *writerDecl = helloMod->defineInterface("writer", "Writer");

  // Create register instance @x
  auto *xReg = helloMod->addInstance(
      "x", regMod,
      {helloClk.getValue(), helloRst.getValue()});

  // Define ReadX interface that binds x.read to Reader.getData
  auto *readXDef = helloMod->defineInterfaceDef("ReadX", "Reader");
  readXDef->bind("x", "read", "getData");
  readXDef->finalize();

  // Create child instance with ReadX bound to reader
  auto *childInst = helloMod->addInstance(
      "c", childMod,
      {helloClk.getValue(), helloRst.getValue()},
      {{"ReadX", "reader"}});

  // Add write method to hello
  auto *writeMethod = helloMod->addMethod("write",
      {{"v", circt::firrtl::UIntType::get(&context, 32)}},
      {circt::firrtl::UIntType::get(&context, 32)});

  // Guard for write method
  writeMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    builder.create<circt::cmt2::ReturnOp>(
        helloMod->getLoc(),
        mlir::ValueRange{});
  });

  // Body for write method
  writeMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    // Read old value from x
    auto oldVals = xReg->callValue("read", builder);
    Signal oldVal(oldVals[0], &builder, helloMod->getLoc());

    // Get method argument %v
    auto methodArg = args[0];

    // Call child.set(v)
    llvm::SmallVector<mlir::Value> setArgs = {methodArg};
    childInst->callMethod("set", setArgs, builder);

    // Write v to x
    llvm::SmallVector<mlir::Value> writeArgs = {methodArg};
    xReg->callMethod("write", writeArgs, builder);

    // Return old value
    builder.create<circt::cmt2::ReturnOp>(
        helloMod->getLoc(),
        mlir::ValueRange{oldVal.getValue()});
  });

  writeMethod->finalize();

  // Add incr rule to hello
  auto *incrRule = helloMod->addRule("incr");

  // Guard for incr rule (always true)
  incrRule->guard([&](mlir::OpBuilder &builder) {
    builder.create<circt::cmt2::ReturnOp>(
        helloMod->getLoc(),
        mlir::ValueRange{});
  });

  // Body for incr rule
  incrRule->body([&](mlir::OpBuilder &builder) {
    // Read current value from x
    auto currentVals = xReg->callValue("read", builder);
    Signal current(currentVals[0], &builder, helloMod->getLoc());

    // Increment by 1
    auto one = UInt::constant(1, 32, builder, helloMod->getLoc());
    Signal sum = current + one;
    Signal newVal = sum.bits(31, 0);

    // Write back to x
    xReg->callMethod("write", {newVal.getValue()}, builder);

    // Call writer interface (outward call)
    writerDecl->callMethod("store", {newVal.getValue()}, builder);

    builder.create<circt::cmt2::ReturnOp>(
        helloMod->getLoc(),
        mlir::ValueRange{});
  });

  incrRule->finalize();

  // Set precedence: write before incr
  helloMod->setPrecedence({{"write", "incr"}});

  // ============================================================================
  // Generate outputs
  // ============================================================================
  llvm::outs() << "=== Generated MLIR (from ECMT2 DSL) ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Attempt FIRRTL conversion
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
