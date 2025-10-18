//===- highlevel_counter.cpp - Counter Example using High-Level API -*-C++-===//
//
// Demonstrates the high-level class-based API for ECMT2
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt::cmt2::ecmt2;
using namespace circt::cmt2::ecmt2::highlevel;

/// Counter module using high-level API
class Counter : public Cmt2Module {
public:
  Counter() : Cmt2Module("Counter") {}

  void build() override {
    // Get location and module
    auto loc = this->loc();
    auto *module = lowLevelModule();

    // Add clock and reset as module arguments
    Clock clk = module->addClockArgument("clk");
    Reset rst = module->addResetArgument("rst");

    // Get the register module from library
    auto &library = ModuleLibrary::getInstance();
    llvm::StringMap<int64_t> params;
    params["width"] = 32;

    // Create external module using low-level API
    auto *regMod = new ExternalModule("reg", "FIRRTLReg",
                                      module->getBuilder(), loc);

    // Bind clock, reset, and ports
    regMod->bindClock("clk", "clock")
          .bindReset("rst", "reset")
          .bindValue("read", "read_ready", {"read_data"})
          .bindMethod("write", "write_enable", "write_ready",
                     {"write_data"}, {})
          .addConflictFree("read", "read")
          .addConflict("write", "write")
          .addSequenceBefore("read", "write");

    // Create register instance
    auto *countReg = module->addInstance(
        "count_reg", regMod,
        {clk.getValue(), rst.getValue()});

    // Add increment rule
    auto *incrRule = module->addRule("incr");

    incrRule->guard([](mlir::OpBuilder &b) {
      auto loc = b.getUnknownLoc();
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    incrRule->body([&, countReg](mlir::OpBuilder &b) {
      auto loc = b.getUnknownLoc();

      // Read current value
      auto currentVals = countReg->callValue("read", b);
      Signal current(currentVals[0], &b, loc);

      // Increment by 1
      auto one = UInt::constant(1, 32, b, loc);
      Signal sum = current + one;
      Signal newVal = sum.bits(31, 0);

      // Write back
      countReg->callMethod("write", {newVal.getValue()}, b);

      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    incrRule->finalize();

    llvm::outs() << "Counter module built using high-level API\n";
  }
};

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<circt::cmt2::Cmt2Dialect>();
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  // Initialize module library
  auto &library = ModuleLibrary::getInstance();
  std::string manifestPath = "lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml";
  if (library.loadManifest(manifestPath).failed()) {
    llvm::errs() << "Warning: Failed to load module library manifest\n";
  }

  // Create high-level circuit (use explicit namespace to avoid ambiguity)
  highlevel::Circuit circuit("Counter", context);

  // Add counter module using high-level API
  auto *counter = circuit.addModule<Counter>();

  llvm::outs() << "=== Generated MLIR (High-Level API) ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Run conversion pipeline
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
