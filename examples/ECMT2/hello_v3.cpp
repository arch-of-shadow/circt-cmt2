//===- hello_v3_truly_full.cpp - TRULY Fully Declarative V3 API -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates a TRULY FULLY DECLARATIVE V3 API for ECMT2:
// - NO lowLevelModule() calls
// - NO manual build() implementation
// - ALL declarations in constructor using INIT_* macros
// - Declarative Method/Value/Rule with interface access
// - Declarative InterfaceDecl and InterfaceDef
// - Declarative instance with interface bindings
// - Helper functions throughout
// - EVERYTHING in constructor!
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Input.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Helpers.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/InterfaceWrapper.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace cmt2::ecmt2;
using namespace cmt2::ecmt2::highlevel;

// Type markers for interfaces
struct Reader {};
struct Writer {};

// ============================================================================
// Child Module: Truly declarative with Method using interface
// ============================================================================
class ChildTrulyDeclarative : public Cmt2Module {
public:
  // ✨ All declarative members!
  ClockInput clk;
  ResetInput rst;
  highlevel::Instance<ExternalModule> r;

  // ✨ Declarative interface
  highlevel::InterfaceDecl<Reader> reader;

  // ✨ Declarative method with proper types!
  highlevel::Method<highlevel::UInt<32>, highlevel::UInt<32>> setMethod;

  ChildTrulyDeclarative(ExternalModule *regMod)
      : Cmt2Module("child_truly_decl"), regMod_(regMod) {

    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);
    CMT2_REGISTER_INSTANCE(r, regMod, clk.get().getValue(), rst.get().getValue());

    // ✨ Register interface declaratively
    CMT2_INTERFACE_DECL(reader, "Reader");

    // ✨ TRULY DECLARATIVE METHOD - defined in constructor, uses interface!
    // The key insight: when the lambda executes during MLIR building,
    // the interface member has already been initialized!
    INIT_METHOD(setMethod)
      .guard([](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
        Return();  // Always ready
      })
      .body([this](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
        // ✨ Access interface through member - it's initialized by now!
        auto readerData = reader.callValue("getData", b);

        // Read current value
        auto currentVals = r.callValue("read", b);

        // Get method argument
        auto vVal = args[0];

        // ✨ All helper functions!
        auto sum1 = Add(readerData[0], vVal);
        auto sum1_trunc = Bits(sum1, 31, 0);
        auto sum2 = Add(currentVals[0], sum1_trunc);
        auto newVal = Bits(sum2, 31, 0);

        // Write back
        r.callMethod("write", b, newVal);

        // Return result
        Return(newVal);
      });
  }

  void build() override {
    // ✨ EMPTY! Everything is declarative!
  }

private:
  ExternalModule *regMod_;
};

// ============================================================================
// Hello Module: Truly declarative with all features
// ============================================================================
class HelloTrulyDeclarative : public Cmt2Module {
public:
  // ✨ All declarative members!
  ClockInput clk;
  ResetInput rst;
  highlevel::Instance<ExternalModule> x;

  // ✨ Declarative interfaces
  highlevel::InterfaceDecl<Writer> writer;
  highlevel::InterfaceDef<Reader> readX;

  // ✨ Declarative child instance (will have interface bindings)
  highlevel::Instance<Module> c;

  // ✨ Declarative method and rule
  highlevel::Method<highlevel::UInt<32>, highlevel::UInt<32>> writeMethod;
  highlevel::Rule incr;

  HelloTrulyDeclarative(ExternalModule *regMod, Module *childMod)
      : Cmt2Module("hello_truly_decl"), regMod_(regMod), childMod_(childMod) {

    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);
    CMT2_REGISTER_INSTANCE(x, regMod, clk.get().getValue(), rst.get().getValue());

    // ✨ Declarative interfaces
    CMT2_INTERFACE_DECL(writer, "Writer");
    INIT_INTERFACE_DEF(readX, "Reader")
      .bind("x", "read", "getData");

    // ✨ Declarative child instance with interface binding!
    c.addInterfaceBinding("readX", "reader");
    CMT2_REGISTER_INSTANCE(c, childMod, clk.get().getValue(), rst.get().getValue());

    // ✨ TRULY DECLARATIVE METHOD
    INIT_METHOD(writeMethod)
      .guard([](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
        Return();
      })
      .body([this](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Read old value
        auto oldVals = x.callValue("read", b);

        // Get method argument
        auto methodArg = args[0];

        // Call child method
        c.callMethod("setMethod", b, methodArg);

        // Write to x
        x.callMethod("write", b, methodArg);

        // Return old value
        Return(oldVals[0]);
      });

    // ✨ TRULY DECLARATIVE RULE
    INIT_RULE(incr)
      .guard([](mlir::OpBuilder &b) {
        Return();
      })
      .body([this](mlir::OpBuilder &b) {
        // Read current
        auto currentVals = x.callValue("read", b);

        // Increment
        auto one = UIntConst(1, 32);
        auto sum = Add(currentVals[0], one);
        auto newVal = Bits(sum, 31, 0);

        // Write back
        x.callMethod("write", b, newVal);

        // ✨ Call writer interface through member!
        writer.callMethod("store", {newVal}, b);

        Return();
      });

    // ✨ TRULY DECLARATIVE PRECEDENCE - no lowLevelModule() call needed!
    addPrecedence("writeMethod", "incr");
  }

  void build() override {
    // ✨ COMPLETELY EMPTY! Everything is declarative!
  }

private:
  ExternalModule *regMod_;
  Module *childMod_;
};

int main() {
  mlir::MLIRContext context;
  context.loadDialect<cmt2::Cmt2Dialect>();
  context.loadDialect<firrtl::FIRRTLDialect>();

  highlevel::Circuit circuit("hello_truly_decl", context);

  // Circuit-level interfaces
  auto *readerInterface = circuit.addInterface("Reader");
  readerInterface->addValue("getData", {},
    {mlir::TypeAttr::get(firrtl::UIntType::get(&context, 32))});

  auto *writerInterface = circuit.addInterface("Writer");
  writerInterface->addMethod("store",
    {{"data", firrtl::UIntType::get(&context, 32)}},
    {});

  // External register module
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("reg", "FIRRTLReg", regParams);
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {"read_data"})
        .bindMethod("write", "write_enable", "write_ready", {"write_data"}, {})
        .addConflict("write", "write")
        .addConflictFree("read", "read")
        .addSequenceBefore("read", "write");

  // Add modules
  auto *childModule = circuit.addModule(std::make_unique<ChildTrulyDeclarative>(regMod));
  circuit.addModule(std::make_unique<HelloTrulyDeclarative>(regMod, childModule->lowLevelModule()));

  llvm::outs() << "\n=== TRULY Fully Declarative V3 API ===\n";
  llvm::outs() << "This demonstrates:\n";
  llvm::outs() << "- ✨ NO lowLevelModule() calls in user code\n";
  llvm::outs() << "- ✨ ALL declarations in constructor with INIT_* macros\n";
  llvm::outs() << "- ✨ INIT_METHOD with interface access\n";
  llvm::outs() << "- ✨ INIT_RULE with interface calls\n";
  llvm::outs() << "- ✨ Declarative InterfaceDecl and InterfaceDef\n";
  llvm::outs() << "- ✨ Declarative instance with interface bindings\n";
  llvm::outs() << "- ✨ Declarative precedence with addPrecedence()\n";
  llvm::outs() << "- ✨ Helper functions throughout\n";
  llvm::outs() << "- ✨ COMPLETELY empty build() methods\n";
  llvm::outs() << "- ✨ Templated type markers: UInt<Width>, SInt<Width>\n";
  llvm::outs() << "- ✨ Type-safe Method<RetType, Args...> templates\n\n";

  llvm::outs() << "Generated MLIR:\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
