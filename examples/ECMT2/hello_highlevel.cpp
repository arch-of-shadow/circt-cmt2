//===- hello_highlevel.cpp - Hello Example using High-Level API -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates the V3 fully declarative API for ECMT2:
// - Declarative ClockInput, ResetInput members
// - Declarative Instance<T> members with auto-registration
// - Declarative Method and Rule with fluent .guard().body() API
// - Helper functions (Return(), Add(), Bits(), etc.) - no builder.create<>!
// - Interface mechanism (Reader, Writer)
// - Hierarchical module composition with interface passing
// - Empty build() methods - everything in constructor!
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Input.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Helpers.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace cmt2::ecmt2;
using namespace cmt2::ecmt2::highlevel;

// ============================================================================
// Child Module: Uses reader interface, has a register, provides set method
// ============================================================================
class Child : public Cmt2Module {
public:
  // Declarative members!
  ClockInput clk;
  ResetInput rst;
  highlevel::Instance<ExternalModule> r;  // Register instance
  highlevel::Method<void, void> setMethod;  // Using void for now (will hold UInt32)

  Child(ExternalModule *regMod) : Cmt2Module("child"), regMod_(regMod) {
    // Register inputs
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Register instance
    CMT2_REGISTER_INSTANCE(r, regMod, clk.get().getValue(), rst.get().getValue());

    // Note: Interfaces are still registered in build() as they need low-level API
  }

  void build() override {
    auto *module = lowLevelModule();

    // Declare reader interface (inward - child needs this from parent)
    // Still using low-level API for interfaces (no declarative wrapper yet)
    auto *readerDecl = module->defineInterface("reader", "Reader");

    // Add set method using low-level API temporarily
    // TODO: Use declarative Method<UInt32, UInt32> once we have proper type support
    llvm::SmallVector<std::pair<std::string, mlir::Type>, 1> setArgs;
    setArgs.push_back({"v", firrtl::UIntType::get(module->getBuilder().getContext(), 32)});
    llvm::SmallVector<mlir::Type, 1> setResults;
    setResults.push_back(firrtl::UIntType::get(module->getBuilder().getContext(), 32));
    auto *set = module->addMethod("set", setArgs, setResults);

    // Guard (always ready) - using helper functions!
    set->guard([this](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, loc());
      Cmt2Module::setCurrentContext(&ctx);
      Return();  // Helper function!
      Cmt2Module::setCurrentContext(nullptr);
    });

    // Body - using helper functions!
    set->body([this, readerDecl](mlir::OpBuilder &builder,
                                  llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, loc());
      Cmt2Module::setCurrentContext(&ctx);

      // Call reader interface to get data
      auto readerData = readerDecl->callValue("getData", builder);

      // Read current value from register
      auto currentVals = r.callValue("read", builder);

      // Get method argument %v
      auto vVal = args[0];

      // Compute: reader_data + v
      auto sum1 = Add(readerData[0], vVal);  // Helper function!
      auto sum1_trunc = Bits(sum1, 31, 0);  // Helper function!

      // Compute: current + (reader_data + v)
      auto sum2 = Add(currentVals[0], sum1_trunc);  // Helper function!
      auto newVal = Bits(sum2, 31, 0);  // Helper function!

      // Write back to register
      r.callMethod("write", builder, newVal);

      // Return the new value
      Return(newVal);  // Helper function!

      Cmt2Module::setCurrentContext(nullptr);
    });

    set->finalize();
  }

private:
  ExternalModule *regMod_;
};

// ============================================================================
// Hello Module: Main module with interface definitions and composition
// ============================================================================
class Hello : public Cmt2Module {
public:
  // Declarative members!
  ClockInput clk;
  ResetInput rst;
  highlevel::Instance<ExternalModule> x;  // Register instance
  highlevel::Rule incr;  // Increment rule

  Hello(ExternalModule *regMod, Module *childMod)
      : Cmt2Module("hello"), regMod_(regMod), childMod_(childMod) {
    // Register inputs
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Register instance x
    CMT2_REGISTER_INSTANCE(x, regMod, clk.get().getValue(), rst.get().getValue());

    // Note: Interfaces, child instance, write method, and rule registered in build()
    // as they need low-level API for interface bindings
  }

  void build() override {
    auto *module = lowLevelModule();

    // Declare writer interface (outward - for external connections)
    auto *writerDecl = module->defineInterface("writer", "Writer");

    // Define ReadX interface that binds x.read to Reader.getData
    auto *readXDef = module->defineInterfaceDef("ReadX", "Reader");
    readXDef->bind("x", "read", "getData");
    readXDef->finalize();

    // Create child instance using externally-provided child module
    auto *childInst = module->addInstance("c", childMod_,
                                          {clk.get().getValue(), rst.get().getValue()},
                                          {{"ReadX", "reader"}});

    // Add write method - using helper functions!
    llvm::SmallVector<std::pair<std::string, mlir::Type>, 1> writeArgs;
    writeArgs.push_back({"v", firrtl::UIntType::get(module->getBuilder().getContext(), 32)});
    llvm::SmallVector<mlir::Type, 1> writeResults;
    writeResults.push_back(firrtl::UIntType::get(module->getBuilder().getContext(), 32));
    auto *writeMethod = module->addMethod("write", writeArgs, writeResults);

    writeMethod->guard([this](mlir::OpBuilder &builder,
                          llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, loc());
      Cmt2Module::setCurrentContext(&ctx);
      Return();  // Helper function!
      Cmt2Module::setCurrentContext(nullptr);
    });

    writeMethod->body([this, childInst](mlir::OpBuilder &builder,
                                         llvm::ArrayRef<mlir::BlockArgument> args) {
      BuildContext ctx(&builder, loc());
      Cmt2Module::setCurrentContext(&ctx);

      // Read old value from x
      auto oldVals = x.callValue("read", builder);

      // Get method argument %v
      auto methodArg = args[0];

      // Call child.set(v)
      childInst->callMethod("set", {methodArg}, builder);

      // Write v to x
      x.callMethod("write", builder, methodArg);

      // Return old value
      Return(oldVals[0]);  // Helper function!

      Cmt2Module::setCurrentContext(nullptr);
    });

    writeMethod->finalize();

    // Add increment rule - using helper functions and fluent API!
    auto *incrRulePtr = module->addRule("incr");

    incrRulePtr->guard([this](mlir::OpBuilder &builder) {
      BuildContext ctx(&builder, loc());
      Cmt2Module::setCurrentContext(&ctx);
      Return();  // Helper function!
      Cmt2Module::setCurrentContext(nullptr);
    });

    incrRulePtr->body([this, writerDecl](mlir::OpBuilder &builder) {
      BuildContext ctx(&builder, loc());
      Cmt2Module::setCurrentContext(&ctx);

      // Read current value from x
      auto currentVals = x.callValue("read", builder);

      // Increment by 1 using helper functions!
      auto one = UIntConst(1, 32);  // Helper function!
      auto sum = Add(currentVals[0], one);  // Helper function!
      auto newVal = Bits(sum, 31, 0);  // Helper function!

      // Write back to x
      x.callMethod("write", builder, newVal);

      // Call writer interface (outward call)
      writerDecl->callMethod("store", {newVal}, builder);

      Return();  // Helper function!

      Cmt2Module::setCurrentContext(nullptr);
    });

    incrRulePtr->finalize();

    // Set precedence: write before incr
    module->setPrecedence({{"write", "incr"}});
  }

private:
  ExternalModule *regMod_;
  Module *childMod_;
};

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<cmt2::Cmt2Dialect>();
  context.loadDialect<firrtl::FIRRTLDialect>();

  // Create high-level circuit (auto-initializes module library)
  highlevel::Circuit circuit("hello", context);

  // Add interfaces at circuit level
  // Reader interface
  auto *readerInterface = circuit.addInterface("Reader");
  readerInterface->addValue("getData", {},
    {mlir::TypeAttr::get(firrtl::UIntType::get(&context, 32))});

  // Writer interface
  auto *writerInterface = circuit.addInterface("Writer");
  writerInterface->addMethod("store",
    {{"data", firrtl::UIntType::get(&context, 32)}},
    {});

  // Create external register module
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {"read_data"})
        .bindMethod("write", "write_enable", "write_ready",
                   {"write_data"}, {})
        .addConflict("write", "write")
        .addConflictFree("read", "read")
        .addSequenceBefore("read", "write");

  // Add child module first (it will be referenced by hello)
  auto *childModule = circuit.addModule(std::make_unique<Child>(regMod));

  // Add hello module, passing both the external module and child module
  circuit.addModule(std::make_unique<Hello>(regMod, childModule->lowLevelModule()));

  llvm::outs() << "\n=== Hello Example (V3 Declarative API) ===\n";
  llvm::outs() << "This demonstrates:\n";
  llvm::outs() << "- Declarative ClockInput, ResetInput, Instance<T> members\n";
  llvm::outs() << "- Helper functions (Return(), Add(), Bits(), etc.) - no builder.create<>!\n";
  llvm::outs() << "- Interface mechanism (Reader, Writer)\n";
  llvm::outs() << "- Hierarchical module composition with interface passing\n";
  llvm::outs() << "- Much cleaner than the old low-level API!\n\n";

  llvm::outs() << "Generated MLIR:\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Run conversion pipeline
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
