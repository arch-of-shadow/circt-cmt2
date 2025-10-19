//===- bundle_vector_example.cpp - Bundle/Vector Example --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates using Bundle and FVector types with the ECMT2 DSL.
// It shows the improved helper APIs for cleaner bundle/vector creation.
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
using namespace circt::firrtl;

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<circt::cmt2::Cmt2Dialect>();
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  // Create circuit
  Circuit circuit("BundleVectorExample", context);

  // Example 1: Simple Bundle Usage with BundleBuilder
  llvm::outs() << "=== Example 1: Simple Bundle ===\n";
  {
    auto *mod = circuit.addModule("SimpleBundleExample");
    auto loc = mod->getLoc();

    // Add a rule that manipulates the bundle
    auto *rule = mod->addRule("manipulateBundle");
    rule->guard([&](mlir::OpBuilder &b) {
      auto trueVal = UInt::constant(1, 1, b, loc);
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{trueVal.getValue()});
    });

    rule->body([&](mlir::OpBuilder &b) {
      // ✨ Create a bundle using BundleBuilder - much cleaner!
      auto addrData = BundleBuilder(&context)
                          .addUInt("addr", 32)
                          .addUInt("data", 64)
                          .build(b, loc);

      // Access bundle fields (same as before)
      Signal addr = addrData["addr"];
      Signal data = addrData["data"];

      // Create some constants
      auto addrConst = UInt::constant(0x1000, 32, b, loc);
      auto dataConst = UInt::constant(0xDEADBEEF, 64, b, loc);

      // Combine using OR (just for demonstration)
      Signal addrResult = addr | addrConst;
      Signal dataResult = data | dataConst;

      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    rule->finalize();
  }

  // Example 2: Vector of Integers
  llvm::outs() << "=== Example 2: Vector of Integers ===\n";
  {
    auto *mod = circuit.addModule("VectorExample");
    auto loc = mod->getLoc();

    // Add a rule that accesses vector elements
    auto *rule = mod->addRule("accessVector");
    rule->guard([&](mlir::OpBuilder &b) {
      auto trueVal = UInt::constant(1, 1, b, loc);
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{trueVal.getValue()});
    });

    rule->body([&](mlir::OpBuilder &b) {
      // Create a vector of 4 x 32-bit unsigned integers inside the body
      auto elemType = UIntType::get(&context, 32);
      FVector registers(elemType, 4, b, loc);

      // Access individual elements
      Signal reg0 = registers[0];
      Signal reg1 = registers[1];
      Signal reg2 = registers[2];
      Signal reg3 = registers[3];

      // Create constants for each register
      auto val0 = UInt::constant(0, 32, b, loc);
      auto val1 = UInt::constant(1, 32, b, loc);
      auto val2 = UInt::constant(2, 32, b, loc);
      auto val3 = UInt::constant(3, 32, b, loc);

      // Perform some operations
      Signal sum01 = reg0 + reg1;
      Signal sum23 = reg2 + reg3;
      Signal total = sum01 + sum23;

      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    rule->finalize();
  }

  // Example 3: Nested Structures - Bundle containing a Vector
  llvm::outs() << "=== Example 3: Nested Bundle with Vector ===\n";
  {
    auto *mod = circuit.addModule("NestedExample");
    auto loc = mod->getLoc();

    // Add a value method that returns a specific byte from the packet
    auto uint8Type = UIntType::get(&context, 8);
    auto *getValue = mod->addValue("getByte0", {uint8Type});

    getValue->guard([&](mlir::OpBuilder &b) {
      auto trueVal = UInt::constant(1, 1, b, loc);
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{trueVal.getValue()});
    });

    getValue->body([&](mlir::OpBuilder &b) {
      // ✨ Create nested bundle using BundleBuilder with addVector
      auto packet = BundleBuilder(&context)
                        .addVector("data", UIntType::get(&context, 8), 4)
                        .addUInt("valid", 1)
                        .build(b, loc);

      // Extract the vector field and use AsVector helper
      Signal dataVec = packet["data"];

      // ✨ Use AsVector helper for clean type conversion
      FVector dataVector = AsVector(dataVec, &b, loc);

      // Access the first element
      Signal byte0 = dataVector[0];

      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{byte0.getValue()});
    });

    getValue->finalize();
  }

  // Example 4: Vector of Bundles
  llvm::outs() << "=== Example 4: Vector of Bundles ===\n";
  {
    auto *mod = circuit.addModule("VectorOfBundlesExample");
    auto loc = mod->getLoc();

    // Add a method that computes sum of x and y from register 0
    auto uint16Type = UIntType::get(&context, 16);
    auto *computeSum = mod->addValue("computeSumReg0", {uint16Type});

    computeSum->guard([&](mlir::OpBuilder &b) {
      auto trueVal = UInt::constant(1, 1, b, loc);
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{trueVal.getValue()});
    });

    computeSum->body([&](mlir::OpBuilder &b) {
      // ✨ Create bundle type using BundleBuilder, then get type for vector
      auto bundleType = BundleType::get(
          &context, BundleBuilder(&context)
                        .addUInt("x", 16)
                        .addUInt("y", 16)
                        .getElements());

      // Create a vector of 8 bundles
      FVector registerFile(bundleType, 8, b, loc);

      // Access register 0
      Signal reg0 = registerFile[0];

      // ✨ Use AsBundle helper for clean type conversion
      Bundle reg0Bundle = AsBundle(reg0, &b, loc);

      // Access x and y fields
      Signal x = reg0Bundle["x"];
      Signal y = reg0Bundle["y"];

      // Compute sum
      Signal sum = x + y;
      Signal result = sum.bits(15, 0);  // Truncate to 16 bits

      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{result.getValue()});
    });

    computeSum->finalize();
  }

  // Example 5: Method with Bundle Argument and Return
  llvm::outs() << "=== Example 5: Method with Bundle Argument ===\n";
  {
    auto *mod = circuit.addModule("BundleMethodExample");
    auto loc = mod->getLoc();

    // Create a bundle type for arguments (x, y coordinates)
    auto coordBundleType = BundleType::get(
        &context, BundleBuilder(&context)
                      .addUInt("x", 16)
                      .addUInt("y", 16)
                      .getElements());

    // Create a method that takes a bundle and returns a modified bundle
    llvm::SmallVector<std::pair<std::string, mlir::Type>> args;
    args.push_back({"coord", coordBundleType});

    auto *transformMethod = mod->addMethod("transformCoord", args, {coordBundleType});

    transformMethod->guard([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> blockArgs) {
      auto trueVal = UInt::constant(1, 1, b, loc);
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{trueVal.getValue()});
    });

    transformMethod->body([&](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> blockArgs) {
      // ✨ Access input bundle argument
      auto inputCoord = blockArgs[0];
      Bundle inputBundle(inputCoord, &b, loc);

      // Extract x and y
      Signal x = inputBundle["x"];
      Signal y = inputBundle["y"];

      // Transform: swap and add offset
      auto offset = UInt::constant(100, 16, b, loc);
      Signal newX = y + offset;  // New X is old Y + 100
      Signal newY = x;            // New Y is old X

      // Create output bundle
      auto outputBundle = BundleBuilder(&context)
                              .addUInt("x", 16)
                              .addUInt("y", 16)
                              .build(b, loc);

      // Assign to output bundle fields (this is conceptual - actual assignment
      // would use connects in real hardware)
      (void)newX;
      (void)newY;

      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{outputBundle.getValue()});
    });

    transformMethod->finalize();
  }

  // Example 6: Value Returning a Vector
  llvm::outs() << "=== Example 6: Value Returning a Vector ===\n";
  {
    auto *mod = circuit.addModule("VectorReturnExample");
    auto loc = mod->getLoc();

    // Create a vector type
    auto vecType = FVectorType::get(UIntType::get(&context, 8), 4);

    auto *getColorVec = mod->addValue("getColorVector", {vecType});

    getColorVec->guard([&](mlir::OpBuilder &b) {
      auto trueVal = UInt::constant(1, 1, b, loc);
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{trueVal.getValue()});
    });

    getColorVec->body([&](mlir::OpBuilder &b) {
      // ✨ Create and return a vector of color components (R, G, B, A)
      FVector colorVec(UIntType::get(&context, 8), 4, b, loc);

      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{colorVec.getValue()});
    });

    getColorVec->finalize();
  }

  // Generate MLIR output
  llvm::outs() << "\n=== Generated MLIR ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Attempt FIRRTL conversion (if pipeline is available)
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  } else {
    llvm::outs() << "\n=== Cmt2 to FIRRTL Conversion Failed ===\n";
  }

  return 0;
}
