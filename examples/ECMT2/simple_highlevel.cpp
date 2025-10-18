//===- simple_highlevel.cpp - Simple High-Level API Demo -------*- C++ -*-===//
//
// Demonstrates the high-level Cmt2Module class wrapper
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt::cmt2::ecmt2;
using namespace circt::cmt2::ecmt2::highlevel;

/// Simple test module using high-level API
class SimpleModule : public Cmt2Module {
public:
  SimpleModule() : Cmt2Module("SimpleModule") {}

  void build() override {
    auto *module = lowLevelModule();
    auto &builder = module->getBuilder();
    auto loc = module->getLoc();

    // Add clock and reset arguments
    module->addClockArgument("clk");
    module->addResetArgument("rst");

    // Add a simple rule
    auto *testRule = module->addRule("test");

    testRule->guard([&](mlir::OpBuilder &b) {
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    testRule->body([&](mlir::OpBuilder &b) {
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    testRule->finalize();

    llvm::outs() << "SimpleModule built using high-level API\n";
  }
};

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<circt::cmt2::Cmt2Dialect>();
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  // Create high-level circuit
  highlevel::Circuit circuit("SimpleModule", context);

  // Add module using high-level API
  circuit.addModule<SimpleModule>();

  llvm::outs() << "\n=== Generated MLIR (High-Level API) ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Run conversion pipeline
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
