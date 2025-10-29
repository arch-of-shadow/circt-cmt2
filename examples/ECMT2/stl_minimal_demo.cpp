//===- stl_minimal_demo.cpp - Minimal ECMT2 STL Usage Example -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates the minimal ECMT2 Standard Template Library (STL).
// The STL provides factory methods that create Module instances for common
// hardware building blocks: registers, FIFOs, and memories.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/ValueRange.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt::cmt2::ecmt2;
using namespace circt::cmt2::ecmt2::stl;

int main() {
  // Initialize MLIR context
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

  // Create circuit
  Circuit circuit("STLMinimalDemo", context);

  // Create main module
  // auto *mainMod = circuit.addModule("STLMinimalDemoModule");
  // auto loc = mainMod->getLoc();

  // // Add clock and reset
  // auto clk = mainMod->addClockArgument("clk");
  // auto rst = mainMod->addResetArgument("reset");

  llvm::outs() << "=== Minimal ECMT2 STL Demo ===\n";

  // === Create STL Modules using Factory Methods ===
  // auto *wireModule = STLLibrary::createWireDefaultModule(32, 0, circuit);

  // 1. Create a 32-bit register module
  // auto *reg32Module = STLLibrary::createRegModule(32, 0, circuit);
  // llvm::outs() << "✓ Created 32-bit register module\n";
  // auto *reg32Inst = mainMod->addInstance("my_reg", reg32Module, {clk.getValue(), rst.getValue()});
  // llvm::outs() << "✓ Instantiated register: my_reg\n";

  // 2. Create a depth-1 FIFO for 32-bit data (push-based)
  // auto *fifo32Module = STLLibrary::createFIFO1PushModule(32, circuit);
  // llvm::outs() << "✓ Created depth-1 FIFO module (32-bit data)\n";

  auto *fifo32Module = STLLibrary::createFIFO2IModule(32, circuit);
  llvm::outs() << "✓ Created depth-1 FIFO module (32-bit data)\n";
  // auto *fifo32Inst = mainMod->addInstance("my_fifo", fifo32Module, {} /* {clk.getValue(), rst.getValue()} */);
  // llvm::outs() << "✓ Instantiated FIFO: my_fifo\n";

  // 2. Create a depth-1 FIFO for 32-bit data (push-based)
  // auto *fifo32Module = STLLibrary::createFIFO1PullModule(32, circuit);
  // llvm::outs() << "✓ Created depth-1 FIFO module (32-bit data)\n";
  // auto *fifo32Inst = mainMod->addInstance("my_fifo", fifo32Module, {} /* {clk.getValue(), rst.getValue()} */);
  // llvm::outs() << "✓ Instantiated FIFO: my_fifo\n";
  
  // // 3. Create a memory module (1KB, 32-bit data, 10-bit address)
  // auto *mainMod = circuit.addModule("STLMinimalDemoModule");
  // auto loc = mainMod->getLoc();
  // auto &b = mainMod->getBuilder();

  // auto clk = mainMod->addClockArgument("clk");
  // auto rst = mainMod->addResetArgument("reset");
  // auto *memModule = STLLibrary::createMem1r1w1cModule( 32, 10, 1024, 
  //   1, circuit);
  // llvm::outs() << "✓ Created 1KB memory module (32-bit data)\n";
  // auto *memInst = mainMod->addInstance("my_memory", memModule, {clk.getValue(), rst.getValue()});

  // auto u10Type = circt::firrtl::UIntType::get(b.getContext(), 10);
  // auto u32Type = circt::firrtl::UIntType::get(b.getContext(), 32);
  // auto *rd0_method = mainMod->addMethod("rd0", {{"inr_", u10Type}}, {});
  // rd0_method->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   Signal trueValue = UInt::constant(1, 1, b, loc);
  //   b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  // });
  // rd0_method->body([&, memInst](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   auto inVal = args[0];
  //   memInst->callMethod("rd0", {inVal}, b);
  //   b.create<circt::cmt2::ReturnOp>(loc);
  // });
  // rd0_method->finalize();

  // auto *rd1_value = mainMod->addValue("rd1", {u32Type});
  // rd1_value->guard([&](mlir::OpBuilder &b) {
  //   Signal trueValue = UInt::constant(1, 1, b, loc);
  //   b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  // });
  // rd1_value->body([&, memInst](mlir::OpBuilder &b) {
  //   auto vals = memInst->callValue("rd1", b);
  //   b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{vals[0]});
  // });
  // rd1_value->finalize();

  // auto *write_method = mainMod->addMethod("write", {{"inwd_", u32Type},{"inwa_", u10Type}}, {});
  // write_method->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   Signal trueValue = UInt::constant(1, 1, b, loc);
  //   b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  // });
  // write_method->body([&, memInst](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   auto wdata = args[0];
  //   auto waddr = args[1];
  //   memInst->callMethod("write", {wdata, waddr}, b);
  //   b.create<circt::cmt2::ReturnOp>(loc);
  // });
  // write_method->finalize();
  // llvm::outs() << "✓ Instantiated memory: my_memory\n";


  // // 4. Create a memory module (1KB, 32-bit data, 10-bit address) LATENCY = 0
  // auto *mainMod = circuit.addModule("STLMinimalDemoModule");
  // auto loc = mainMod->getLoc();
  // auto &b = mainMod->getBuilder();

  // auto clk = mainMod->addClockArgument("clk");
  // auto rst = mainMod->addResetArgument("reset");
  // auto *memModule = STLLibrary::createMem1r1w0cModule( 32, 10, 1024, 
  //   1, circuit);
  // llvm::outs() << "✓ Created 1KB memory module (32-bit data)\n";
  // auto *memInst = mainMod->addInstance("my_memory", memModule, {clk.getValue(), rst.getValue()});

  // auto u10Type = circt::firrtl::UIntType::get(b.getContext(), 10);
  // auto u32Type = circt::firrtl::UIntType::get(b.getContext(), 32);
  // auto *read_method = mainMod->addMethod("read", {{"raddr", u10Type}}, {u32Type});
  // read_method->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   Signal trueValue = UInt::constant(1, 1, b, loc);
  //   b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  // });
  // read_method->body([&, memInst](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   auto addr = args[0];
  //   auto ret = memInst->callMethod("read", {addr}, b);
  //   b.create<circt::cmt2::ReturnOp>(loc,mlir::ValueRange{ret[0]});
  // });
  // read_method->finalize();

  // auto *write_method = mainMod->addMethod("write", {{"wdata", u32Type},{"waddr", u10Type}}, {});
  // write_method->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   Signal trueValue = UInt::constant(1, 1, b, loc);
  //   b.create<circt::cmt2::ReturnOp>(loc, trueValue.getValue());
  // });
  // write_method->body([&, memInst](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
  //   auto wdata = args[0];
  //   auto waddr = args[1];
  //   memInst->callMethod("write", {wdata, waddr}, b);
  //   b.create<circt::cmt2::ReturnOp>(loc);
  // });
  // write_method->finalize();
  // // llvm::outs() << "✓ Instantiated memory: my_memory\n";

  // Generate MLIR output
  llvm::outs() << "\n=== Generated MLIR ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Attempt FIRRTL conversion
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
    llvm::outs() << "\n✓ Minimal STL Demo: Conversion successful!\n";
  } else {
    llvm::outs() << "\n✗ Minimal STL Demo: Conversion failed\n";
  }

  return 0;
}