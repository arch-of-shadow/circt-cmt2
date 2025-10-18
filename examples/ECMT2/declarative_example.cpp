//===- declarative_example.cpp - Declarative High-Level API ----*- C++ -*-===//
//
// Demonstrates declarative module definition with registration macros
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Registry.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt::cmt2::ecmt2;
using namespace circt::cmt2::ecmt2::highlevel;

/// Declarative counter module
class DeclarativeCounter : public Cmt2Module {
public:
  // Declare rules as members (must come before constructor)
  highlevel::Rule incrementRule;
  highlevel::Rule resetRule;

  DeclarativeCounter() : Cmt2Module("DeclarativeCounter") {
    // Register members for automatic initialization
    CMT2_REGISTER(incrementRule);
    CMT2_REGISTER(resetRule);
  }

  void build() override {
    auto *module = lowLevelModule();
    auto &builder = module->getBuilder();
    auto loc = module->getLoc();

    // Add clock and reset arguments
    Clock clk = module->addClockArgument("clk");
    Reset rst = module->addResetArgument("rst");

    // Define rule behavior using lambdas
    incrementRule.guard([&](mlir::OpBuilder &b) {
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    incrementRule.body([&](mlir::OpBuilder &b) {
      // Increment logic would go here
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    resetRule.guard([&](mlir::OpBuilder &b) {
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    resetRule.body([&](mlir::OpBuilder &b) {
      // Reset logic would go here
      b.create<circt::cmt2::ReturnOp>(loc, mlir::ValueRange{});
    });

    llvm::outs() << "DeclarativeCounter built with "
                 << "automatic member registration\n";
  }
};

int main() {
  // Initialize MLIR context
  mlir::MLIRContext context;
  context.loadDialect<circt::cmt2::Cmt2Dialect>();
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  // Create high-level circuit
  highlevel::Circuit circuit("DeclarativeCounter", context);

  // Add module - members are automatically initialized!
  circuit.addModule<DeclarativeCounter>();

  llvm::outs() << "\n=== Generated MLIR (Declarative API) ===\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  // Run conversion pipeline
  if (circuit.runCmt2ToFIRRTLPipeline().succeeded()) {
    llvm::outs() << "\n=== After Cmt2 to FIRRTL Conversion ===\n";
    llvm::outs() << circuit.emitFIRRTL() << "\n";
  }

  return 0;
}
