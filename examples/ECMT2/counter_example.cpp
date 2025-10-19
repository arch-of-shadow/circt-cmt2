//===- counter_example.cpp - Counter Example using ECMT2 DSL ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates a simple counter module using the ECMT2 embedded DSL.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
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
  Circuit circuit("Counter", context);

  // Create a register external module from library with width=32
  // Name is auto-generated from parameters: Reg_width32_init0
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {"read_data"})
        .bindMethod("write", "write_enable", "write_ready",
                   {"write_data"}, {})
        .addSequenceBefore("read", "write");

  // Create counter module
  auto *counterMod = circuit.addModule("Counter");

  // Add clock and reset as module arguments
  Clock clk = counterMod->addClockArgument("clk");
  Reset rst = counterMod->addResetArgument("rst");

  // Add a register instance for counter value
  auto *countReg = counterMod->addInstance(
      "count", regMod,
      {clk.getValue(), rst.getValue()});

  // Add a rule to increment the counter
  auto *incrementRule = counterMod->addRule("increment");

  // Guard: always ready
  incrementRule->guard([&](mlir::OpBuilder &builder) {
    // Guard is always true (counter always increments)
    auto trueVal = UInt::constant(1, 1, builder, counterMod->getLoc());
    builder.create<circt::cmt2::ReturnOp>(
        counterMod->getLoc(),
        mlir::ValueRange{trueVal.getValue()});
  });

  // Body: increment counter
  incrementRule->body([&](mlir::OpBuilder &builder) {
    // Read current value
    auto currentVals = countReg->callValue("read", builder);
    if (!currentVals.empty()) {
      Signal current(currentVals[0], &builder, counterMod->getLoc());

      // Increment by 1
      auto one = UInt::constant(1, 32, builder, counterMod->getLoc());
      Signal sum = current + one;

      // Truncate to 32 bits (FIRRTL add produces width+1 bits)
      Signal nextVal = sum.bits(31, 0);

      // Write back
      countReg->callMethod("write", {nextVal.getValue()}, builder);
    }

    builder.create<circt::cmt2::ReturnOp>(
        counterMod->getLoc(),
        mlir::ValueRange{});
  });

  incrementRule->finalize();

  // Generate MLIR output
  llvm::outs() << "=== Generated MLIR ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Attempt FIRRTL conversion (if pipeline is available)
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
