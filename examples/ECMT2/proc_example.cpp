//===- proc_example.cpp - Procedural Control Example --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates procedural control operations using the ECMT2 DSL.
// It creates a simple module with groups and a procedural rule.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
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

  // Create circuit
  Circuit circuit("ProcExample", context);

  // Create a register external module from library with width=32
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {}, {"read_data"})
        .bindMethod("write", "write_enable", "write_ready",
                   {"write_data"}, {})
        .addSequenceBefore("read", "write");

  // Create module with procedural rule
  auto *procMod = circuit.addModule("SeqModule");

  // Add clock and reset as module arguments
  Clock clk = procMod->addClockArgument("clk");
  Reset rst = procMod->addResetArgument("rst");

  // Add register instance
  auto *regInst = procMod->addInstance("reg", regMod,
                                        {clk.getValue(), rst.getValue()});

  // Create steps
  auto *loadStep = procMod->addProcStep("load");
  loadStep->body([&](mlir::OpBuilder &builder) {
    // Read from register
    regInst->callValue("read", builder);
    // Signal done
    auto done = UInt::constant(1, 1, builder, procMod->getLoc());
    loadStep->stepDone(done.getValue());
  });

  auto *storeStep = procMod->addProcStep("store");
  storeStep->body([&](mlir::OpBuilder &builder) {
    // Write to register
    auto val = UInt::constant(42, 32, builder, procMod->getLoc());
    regInst->callMethod("write", {val.getValue()}, builder);
    // Signal done
    auto done = UInt::constant(1, 1, builder, procMod->getLoc());
    storeStep->stepDone(done.getValue());
  });

  // Create a procedural rule
  auto *procRule = procMod->addProcRule("sequential");

  // Guard: always true
  procRule->guard([&](mlir::OpBuilder &builder) {
    auto trueVal = UInt::constant(1, 1, builder, procMod->getLoc());
    builder.create<circt::cmt2::ReturnOp>(
        procMod->getLoc(),
        mlir::ValueRange{trueVal.getValue()});
  });

  // Control: seq { enable @load; enable @store }
  procRule->control([&](ControlBuilder &ctrl) {
    ctrl.seq([&](ControlBuilder &seq) {
      seq.enable("load");
      seq.enable("store");
    });
  });

  // Print MLIR output
  llvm::outs() << circuit.emitMLIRString();
  llvm::outs() << "\n\nProc example completed!\n";

  return 0;
}
