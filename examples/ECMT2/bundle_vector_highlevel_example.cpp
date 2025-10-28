//===- bundle_vector_highlevel_example.cpp - High-Level Bundle/Vector ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates using Bundle and FVector types with the high-level
// ECMT2 API. It showcases the clean, declarative syntax with minimal boilerplate.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace cmt2::ecmt2;
using namespace cmt2::ecmt2::highlevel;
using namespace circt::firrtl;

//===----------------------------------------------------------------------===//
// Example 1: Simple Bundle Module
//===----------------------------------------------------------------------===//

class SimpleBundleModule : public Cmt2Module {
public:
  highlevel::Rule manipulateBundle;

  SimpleBundleModule() : Cmt2Module("SimpleBundleExample") {
    INIT_RULE(manipulateBundle)
        .guard([](mlir::OpBuilder &b) {
          return UIntConst(1, 1);  // Always fire
        })
        .body([](mlir::OpBuilder &b) {
          // ✨ Create bundle using high-level fluent API
          auto addrData = MakeBundle()
                              .addUInt("addr", 32)
                              .addUInt("data", 64)
                              .build();

          // ✨ Access fields using helper
          auto addr = GetField(addrData, "addr");
          auto data = GetField(addrData, "data");

          // Create constants and combine
          auto addrConst = UIntConst(0x1000, 32);
          auto dataConst = UIntConst(0xDEADBEEF, 64);

          (void)Or(addr, addrConst);
          (void)Or(data, dataConst);

          Return();
        });
  }

  void build() override {}
};

//===----------------------------------------------------------------------===//
// Example 2: Vector Module
//===----------------------------------------------------------------------===//

class VectorModule : public Cmt2Module {
public:
  highlevel::Rule accessVector;

  VectorModule() : Cmt2Module("VectorExample") {
    INIT_RULE(accessVector)
        .guard([](mlir::OpBuilder &b) {
          return UIntConst(1, 1);
        })
        .body([](mlir::OpBuilder &b) {
          auto ctx = b.getContext();

          // ✨ Create vector using helper
          auto elemType = UIntType::get(ctx, 32);
          auto registers = MakeVector(elemType, 4);

          // ✨ Access elements using helper
          auto reg0 = GetElement(registers, 0);
          auto reg1 = GetElement(registers, 1);
          auto reg2 = GetElement(registers, 2);
          auto reg3 = GetElement(registers, 3);

          // Perform operations using helpers
          auto sum01 = Add(reg0, reg1);
          auto sum23 = Add(reg2, reg3);
          (void)Add(sum01, sum23);

          Return();
        });
  }

  void build() override {}
};

//===----------------------------------------------------------------------===//
// Example 3: Nested Bundle with Vector
//===----------------------------------------------------------------------===//

class NestedBundleModule : public Cmt2Module {
public:
  highlevel::Rule accessNestedBundle;

  NestedBundleModule() : Cmt2Module("NestedExample") {
    INIT_RULE(accessNestedBundle)
        .guard([](mlir::OpBuilder &b) {
          return UIntConst(1, 1);
        })
        .body([](mlir::OpBuilder &b) {
          auto ctx = b.getContext();

          // ✨ Create nested bundle with vector field
          auto packet = MakeBundle()
                            .addVector("data", UIntType::get(ctx, 8), 4)
                            .addUInt("valid", 1)
                            .build();

          // ✨ Access nested structure
          auto dataVec = GetField(packet, "data");
          (void)GetElement(dataVec, 0);  // Access first byte

          Return();
        });
  }

  void build() override {}
};

//===----------------------------------------------------------------------===//
// Example 4: Vector of Bundles
//===----------------------------------------------------------------------===//

class VectorOfBundlesModule : public Cmt2Module {
public:
  highlevel::Rule computeFromRegister;

  VectorOfBundlesModule() : Cmt2Module("VectorOfBundlesExample") {
    INIT_RULE(computeFromRegister)
        .guard([](mlir::OpBuilder &b) {
          return UIntConst(1, 1);
        })
        .body([](mlir::OpBuilder &b) {
          auto ctx = b.getContext();

          // ✨ Create bundle type for vector elements
          auto bundleType = BundleType::get(
              ctx, MakeBundle()
                       .addUInt("x", 16)
                       .addUInt("y", 16)
                       .getElements());

          // ✨ Create vector of bundles
          auto registerFile = MakeVector(bundleType, 8);

          // ✨ Access element and fields
          auto reg0 = GetElement(registerFile, 0);
          auto x = GetField(reg0, "x");
          auto y = GetField(reg0, "y");

          // Compute sum using helpers
          (void)Add(x, y);

          Return();
        });
  }

  void build() override {}
};

//===----------------------------------------------------------------------===//
// Example 5: Method with Bundle Argument (✨ HIGH-LEVEL API)
//===----------------------------------------------------------------------===//

class BundleMethodModule : public Cmt2Module {
public:
  // ✨ Declarative member using CustomMethod
  highlevel::CustomMethod transformCoord;

  BundleMethodModule() : Cmt2Module("BundleMethodExample") {
    // ✨ Fully declarative in constructor with type specifications
    INIT_CUSTOM_METHOD(transformCoord)
        .argType("coord", MakeBundleType()
                              .addUInt("x", 16)
                              .addUInt("y", 16))
        .returnType(MakeBundleType()
                        .addUInt("x", 16)
                        .addUInt("y", 16))
        .guard([](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
          Return();
        })
        .body([](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
          // Access input bundle
          auto inputCoord = args[0];
          auto x = GetField(inputCoord, "x");
          auto y = GetField(inputCoord, "y");

          // Transform: swap x and y, add offset
          auto offset = UIntConst(100, 16);
          auto newX = Add(y, offset);
          auto newY = x;

          // Create output bundle
          auto outputBundle = MakeBundle()
                                  .addUInt("x", 16)
                                  .addUInt("y", 16)
                                  .build();

          (void)newX;  // Suppress warning for unused newX/newY
          (void)newY;

          Return(outputBundle);
        });
  }

  void build() override {}  // ✨ Empty! Fully declarative!
};

//===----------------------------------------------------------------------===//
// Example 6: Value Returning a Vector (✨ HIGH-LEVEL API)
//===----------------------------------------------------------------------===//

class VectorReturnModule : public Cmt2Module {
public:
  // ✨ Declarative member using CustomValue
  highlevel::CustomValue getColorVector;

  VectorReturnModule() : Cmt2Module("VectorReturnExample") {
    // ✨ Fully declarative in constructor
    INIT_CUSTOM_VALUE(getColorVector)
        .returnType(MakeVectorType("uint", 8, 4))
        .guard([](mlir::OpBuilder &b) {
          Return();
        })
        .body([](mlir::OpBuilder &b) {
          auto ctx = b.getContext();

          // Create and return a vector
          auto colorVec = MakeVector(firrtl::UIntType::get(ctx, 8), 4);

          Return(colorVec);
        });
  }

  void build() override {}  // ✨ Empty! Fully declarative!
};

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<cmt2::Cmt2Dialect>();
  context.loadDialect<firrtl::FIRRTLDialect>();

  // Create circuit
  highlevel::Circuit circuit("BundleVectorHighLevelExample", context);

  // Add modules using the high-level API
  llvm::outs() << "=== Creating High-Level Bundle/Vector Modules ===\n";

  circuit.addModule(std::make_unique<SimpleBundleModule>());
  llvm::outs() << "✓ SimpleBundleExample module created\n";

  circuit.addModule(std::make_unique<VectorModule>());
  llvm::outs() << "✓ VectorExample module created\n";

  circuit.addModule(std::make_unique<NestedBundleModule>());
  llvm::outs() << "✓ NestedExample module created\n";

  circuit.addModule(std::make_unique<VectorOfBundlesModule>());
  llvm::outs() << "✓ VectorOfBundlesExample module created\n";

  circuit.addModule(std::make_unique<BundleMethodModule>());
  llvm::outs() << "✓ BundleMethodExample module created\n";

  circuit.addModule(std::make_unique<VectorReturnModule>());
  llvm::outs() << "✓ VectorReturnExample module created\n";

  // Generate MLIR output
  llvm::outs() << "\n=== Generated MLIR ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Attempt FIRRTL conversion
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  } else {
    llvm::outs() << "\n=== Cmt2 to FIRRTL Conversion Failed ===\n";
  }

  return 0;
}
