//===- two_counter_if_example.cpp - Two Counter with If Example -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates conditional execution using cmt2.if in the ECMT2
// embedded DSL. It implements a two-counter module that conditionally increments
// one of two counters based on a parameter.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/SignalHelpers.h"
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
  Circuit circuit("twoCounter", context);

  // Create a register external module from library with width=32
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {}, {"read_data"})
        .bindMethod("write", "write_enable", "write_ready",
                   {"write_data"}, {})
        .addSequenceBefore("read", "write")
        .addConflict("write", "write")
        .addConflictFree("read", "read");

  // Create two-counter module
  auto *twoCounterMod = circuit.addModule("twoCounter");

  // Add clock and reset as module arguments
  Clock clk = twoCounterMod->addClockArgument("clk");
  Reset rst = twoCounterMod->addResetArgument("rst");

  // Add two register instances (x and y)
  auto *xReg = twoCounterMod->addInstance(
      "x", regMod,
      {clk.getValue(), rst.getValue()});

  auto *yReg = twoCounterMod->addInstance(
      "y", regMod,
      {clk.getValue(), rst.getValue()});

  // Add incr method: conditionally increment x or y based on parameter 'a'
  auto *incrMethod = twoCounterMod->addMethod(
      "incr",
      {{"a", circt::firrtl::UIntType::get(&context, 1)}},
      {circt::firrtl::UIntType::get(&context, 32)});

  // Guard: always ready
  incrMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    auto trueVal = UInt::constant(1, 1, builder, twoCounterMod->getLoc());
    builder.create<circt::cmt2::ReturnOp>(
        twoCounterMod->getLoc(),
        mlir::ValueRange{});
  });

  // Body: Use if to conditionally write to x or y
  incrMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
    // Get the 'a' parameter
    Signal aParam(args[0], &builder, twoCounterMod->getLoc());

    // Read current values from both registers
    auto xVals = xReg->callValue("read", builder);
    auto yVals = yReg->callValue("read", builder);

    Signal xCurrent(xVals[0], &builder, twoCounterMod->getLoc());
    Signal yCurrent(yVals[0], &builder, twoCounterMod->getLoc());

    // Calculate sum for conditional write
    Signal sum = xCurrent + yCurrent;
    Signal sumTrunc = sum.bits(31, 0);

    // Use if to conditionally write and return
    // If a is true, write to x and return x's old value
    // Otherwise, write to y and return y's old value
    auto result = IfBuilder(aParam, builder, twoCounterMod->getLoc())
        .Then([&](mlir::OpBuilder &b) -> mlir::Value {
          // Write sumTrunc to x
          xReg->callMethod("write", {sumTrunc.getValue()}, b);
          return xCurrent.getValue();
        })
        .Else([&](mlir::OpBuilder &b) -> mlir::Value {
          // Write sumTrunc to y
          yReg->callMethod("write", {sumTrunc.getValue()}, b);
          return yCurrent.getValue();
        })
        .build();

    // Return the result
    builder.create<circt::cmt2::ReturnOp>(
        twoCounterMod->getLoc(),
        mlir::ValueRange{result.getValue()});
  });

  // Add incrementX rule: always increment x by 1
  auto *incrementXRule = twoCounterMod->addRule("incrementX");

  // Guard: always ready
  incrementXRule->guard([&](mlir::OpBuilder &builder) {
    builder.create<circt::cmt2::ReturnOp>(
        twoCounterMod->getLoc(),
        mlir::ValueRange{});
  });

  // Body: increment x
  incrementXRule->body([&](mlir::OpBuilder &builder) {
    // Read current value from x
    auto xVals = xReg->callValue("read", builder);
    Signal xCurrent(xVals[0], &builder, twoCounterMod->getLoc());

    // Increment by 1
    auto one = UInt::constant(1, 32, builder, twoCounterMod->getLoc());
    Signal sum = xCurrent + one;
    Signal nextVal = sum.bits(31, 0);

    // Write back to x
    xReg->callMethod("write", {nextVal.getValue()}, builder);

    builder.create<circt::cmt2::ReturnOp>(
        twoCounterMod->getLoc(),
        mlir::ValueRange{});
  });

  // Set precedence: incr should execute before incrementX
  twoCounterMod->setPrecedence({{std::string("incr"), std::string("incrementX")}});

  // Emit MLIR
  llvm::outs() << "\n=== Generated Cmt2 IR ===\n";
  llvm::outs() << circuit.emitMLIRString();

  llvm::outs() << "\n\nExample demonstrates:\n";
  llvm::outs() << "- Using IfBuilder to create conditional execution\n";
  llvm::outs() << "- Then/Else branches that return values\n";
  llvm::outs() << "- Conditional method calls based on parameters\n";
  llvm::outs() << "- Integration with existing register instances\n";

  return 0;
}
