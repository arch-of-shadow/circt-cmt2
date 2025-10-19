//===- hello_simple_highlevel.cpp - Clean High-Level API Demo -*- C++ -*-===//
//
// Demonstrates the V3 fully declarative API:
// - Declarative members (ClockInput, ResetInput, Instance<T>)
// - Declarative Method/Value/Rule with fluent API
// - Auto-registration macros
// - No lowLevelModule()->addMethod/addRule calls!
// - Clean fluent .guard().body() syntax!
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Input.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Helpers.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace cmt2::ecmt2;
using namespace cmt2::ecmt2::highlevel;

// ============================================================================
// SimpleHello Module - Fully declarative style!
// ============================================================================
class SimpleHello : public Cmt2Module {
public:
  // Declarative members
  ClockInput clk;
  ResetInput rst;
  highlevel::Instance<ExternalModule> x;

  // Declarative rule - no lowLevelModule()->addRule!
  highlevel::Rule incr;

  SimpleHello(ExternalModule *regMod) : Cmt2Module("simple_hello") {
    // Register inputs
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Register instance
    CMT2_REGISTER_INSTANCE(x, regMod, clk.get().getValue(), rst.get().getValue());

    // Register and define increment rule with fluent API!
    // No more lowLevelModule()->addRule()!
    // No more rule->guard(), rule->body(), rule->finalize()!
    // No more explicit builder.create<> calls!
    // Just clean helper functions!
    INIT_RULE(incr)
      .guard([](mlir::OpBuilder &b) {
        // Always fire
        Return();  // Helper function - no explicit b.create!
      })
      .body([this](mlir::OpBuilder &b) {
        // Read, increment, write back
        auto currentVals = x.callValue("read", b);
        auto one = UIntConst(1, 32);  // Helper function!
        auto sum = Add(currentVals[0], one);  // Helper function!
        auto newVal = Bits(sum, 31, 0);  // Helper function!
        x.callMethod("write", b, newVal);
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

  // Create circuit
  highlevel::Circuit circuit("SimpleHelloTop", context);

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

  // Add module
  circuit.addModule(std::make_unique<SimpleHello>(regMod));

  llvm::outs() << "\n=== Simple Hello High-Level Example ===\n";
  llvm::outs() << "This demonstrates the V3 fully declarative API:\n";
  llvm::outs() << "- Declarative members (ClockInput, ResetInput, Instance<T>, Rule)\n";
  llvm::outs() << "- Auto-registration macros (CMT2_ARG_CLOCK, CMT2_REGISTER_INSTANCE, INIT_RULE)\n";
  llvm::outs() << "- Fluent .guard().body() API - no lowLevelModule()->addRule()!\n";
  llvm::outs() << "- Helper functions (Return(), UIntConst(), Add(), Bits()) - no builder.create<>()!\n";
  llvm::outs() << "- Empty build() method - everything in constructor!\n\n";

  llvm::outs() << "Generated MLIR:\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Run conversion pipeline
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
