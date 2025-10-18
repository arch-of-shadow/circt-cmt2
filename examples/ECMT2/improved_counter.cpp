//===- improved_counter.cpp - Improved Counter Example --------*- C++ -*-===//
//
// Demonstrates the improved high-level API with:
// - Implicit build context (no need to pass builder and location)
// - Simplified helpers (Return(), Call(), Add(), etc.)
// - Auto-registering arguments
// - Cleaner macros
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
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

/// Counter module with improved syntax
class ImprovedCounter : public Cmt2Module {
public:
  // Declare inputs at class scope
  ClockInput clk;
  ResetInput rst;

  // Declare rules at class scope
  highlevel::Rule incrementRule;
  highlevel::Rule resetRule;

  ImprovedCounter() : Cmt2Module("ImprovedCounter") {
    // Auto-register arguments - simplified!
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Register rules using fluent API with INIT_RULE
    INIT_RULE(incrementRule)
        .guard([&](mlir::OpBuilder &b) {
          // Guard logic: always enabled in this example
          return UIntConst(1, 1);  // Using helper function!
        })
        .body([&](mlir::OpBuilder &b) {
          // Body: Empty for this example - would increment counter
          Return();  // Simplified return - no need for b.create<>!
        });

    INIT_RULE(resetRule)
        .guard([&](mlir::OpBuilder &b) {
          // Guard: check reset signal
          // In full version would use: return rst;
          return UIntConst(0, 1);  // Disabled for this example
        })
        .body([&](mlir::OpBuilder &b) {
          // Body: Reset counter to 0
          Return();  // Simplified!
        });
  }

  void build() override {
    // Build method can be empty if everything is done in constructor!
    // This is much cleaner than before!
  }
};

/// Even more concise version using declaration macros
class UltraCleanCounter : public Cmt2Module {
public:
  // Declare inputs
  ClockInput clk;
  ResetInput rst;

  // Declare rules - using optional declaration macro for clarity
  CMT2_DECL_RULE(tick);

  UltraCleanCounter() : Cmt2Module("UltraCleanCounter") {
    // Register everything in one place
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Define and register rule with fluent API
    INIT_RULE(tick)
        .guard([&](mlir::OpBuilder &b) {
          return UIntConst(1, 1);
        })
        .body([&](mlir::OpBuilder &b) {
          Return();
        });
  }

  void build() override {
    // Nothing needed here!
  }
};

int main() {
  // Initialize MLIR
  mlir::MLIRContext context;
  context.loadDialect<circt::cmt2::Cmt2Dialect>();
  context.loadDialect<circt::firrtl::FIRRTLDialect>();

  // Create circuit
  highlevel::Circuit circuit("ImprovedCounterTop", context);

  // Add module - all initialization happens automatically!
  circuit.addModule<ImprovedCounter>();

  llvm::outs() << "\n=== Improved Counter Example ===\n";
  llvm::outs() << "This example demonstrates:\n";
  llvm::outs() << "1. Auto-registering arguments with CMT2_ARG_CLOCK/RESET\n";
  llvm::outs() << "2. Simplified Return() instead of b.create<ReturnOp>()\n";
  llvm::outs() << "3. Helper functions like UIntConst() instead of manual creation\n";
  llvm::outs() << "4. INIT_RULE() macro for registration + fluent API\n";
  llvm::outs() << "5. Empty build() method - everything in constructor!\n\n";

  llvm::outs() << "Generated MLIR:\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  return 0;
}
