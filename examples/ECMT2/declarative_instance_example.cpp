//===- declarative_instance_example.cpp - Declarative Instance -*- C++ -*-===//
//
// Demonstrates the declarative Instance<T> API with V2 improvements
//
//===----------------------------------------------------------------------===//

// Single unified header for high-level API
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Input.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Helpers.h"

// Dialects
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace cmt2::ecmt2;
using namespace cmt2::ecmt2::highlevel;

// ============================================================================
// Declarative Counter Module - Using declarative Instance<T> and Rule
// ============================================================================
class DeclarativeCounter : public Cmt2Module {
public:
  // Declarative members!
  ClockInput clk;
  ResetInput rst;
  highlevel::Instance<ExternalModule> countReg;  // Declarative instance!
  highlevel::Rule increment;  // Declarative rule!

  DeclarativeCounter(ExternalModule *regMod) : Cmt2Module("DeclarativeCounter") {
    // Register inputs
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Register instance declaratively!
    CMT2_REGISTER_INSTANCE(countReg, regMod, clk.get().getValue(), rst.get().getValue());

    // Register and define rule with fluent API!
    // Using helper functions for cleaner syntax!
    INIT_RULE(increment)
      .guard([this](mlir::OpBuilder &b) {
        auto guardVal = UIntConst(1, 1);  // Helper function!
        Return(guardVal);  // Helper function!
      })
      .body([this](mlir::OpBuilder &b) {
        // Use the declarative instance!
        auto currentVals = countReg.callValue("read", b);

        // Increment by 1 using helper functions
        auto one = UIntConst(1, 32);  // Helper function!
        auto sum = Add(currentVals[0], one);  // Helper function!
        auto newVal = Bits(sum, 31, 0);  // Helper function!

        // Write back using declarative instance
        countReg.callMethod("write", b, newVal);

        Return();  // Helper function!
      });
  }

  void build() override {
    // Empty! Everything is declarative in constructor!
  }
};

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<cmt2::Cmt2Dialect>();
  context.loadDialect<firrtl::FIRRTLDialect>();

  // Create high-level circuit
  highlevel::Circuit circuit("DeclarativeCounterTop", context);

  // Create external register module
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("reg", "FIRRTLReg", regParams);

  // Configure the register module
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {"read_data"})
        .bindMethod("write", "write_enable", "write_ready",
                   {"write_data"}, {})
        .addConflict("write", "write")
        .addConflictFree("read", "read")
        .addSequenceBefore("read", "write");

  // Add module using declarative API
  circuit.addModule(std::make_unique<DeclarativeCounter>(regMod));

  llvm::outs() << "\n=== Declarative Instance & Rule Example ===\n";
  llvm::outs() << "This example demonstrates the V3 fully declarative API:\n";
  llvm::outs() << "1. Declarative Instance<T> members at class scope\n";
  llvm::outs() << "2. Declarative Rule members at class scope\n";
  llvm::outs() << "3. CMT2_REGISTER_INSTANCE() and INIT_RULE() macros\n";
  llvm::outs() << "4. Fluent .guard().body() API for rules\n";
  llvm::outs() << "5. Empty build() method - everything in constructor!\n";
  llvm::outs() << "6. No lowLevelModule()->addRule() calls!\n\n";

  llvm::outs() << "Generated MLIR:\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Run conversion pipeline
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
