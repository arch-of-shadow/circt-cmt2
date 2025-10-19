//===- two_counter_if_highlevel.cpp - Two Counter High-Level -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates conditional execution using cmt2.if with the
// high-level declarative ECMT2 API.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace cmt2::ecmt2;
using namespace cmt2::ecmt2::highlevel;

// Type alias to work around nested template syntax
using UInt1 = highlevel::UInt<1>;
using UInt32 = highlevel::UInt<32>;

/// Example showing If support with high-level declarative API
class TwoCounterIfExample : public Cmt2Module {
public:
  // ✨ Declarative members
  ClockInput clk;
  ResetInput rst;
  highlevel::Instance<ExternalModule> counter1;
  highlevel::Instance<ExternalModule> counter2;

  // ✨ Declarative method using If
  highlevel::Method<UInt32, UInt1> selectAndIncrement;

  // ✨ Declarative rule with If
  highlevel::Rule conditionalIncrement;

  TwoCounterIfExample(ExternalModule *regMod) : Cmt2Module("TwoCounterIfExample") {
    // Register inputs
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Register instances
    CMT2_REGISTER_INSTANCE(counter1, regMod, clk.get().getValue(), rst.get().getValue());
    CMT2_REGISTER_INSTANCE(counter2, regMod, clk.get().getValue(), rst.get().getValue());

    // ✨ Method demonstrating If with helper functions
    INIT_METHOD(selectAndIncrement)
      .guard([](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
        Return();  // Always ready
      })
      .body([this](mlir::OpBuilder &b, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Get the select argument
        Signal selectSig(args[0], &b, loc());

        // Read both counters
        auto counter1Vals = counter1.callValue("read", b);
        auto counter2Vals = counter2.callValue("read", b);

        // Use If to select which counter to increment
        auto result = If(selectSig,
          // Then branch: increment counter1
          [&](mlir::OpBuilder &builder) -> Signal {
            auto one = UIntConst(1, 32);
            auto sum = Add(counter1Vals[0], one);
            auto newVal = Bits(sum, 31, 0);
            counter1.callMethod("write", builder, newVal);
            return Signal(newVal, &builder, loc());
          },
          // Else branch: increment counter2
          [&](mlir::OpBuilder &builder) -> Signal {
            auto one = UIntConst(1, 32);
            auto sum = Add(counter2Vals[0], one);
            auto newVal = Bits(sum, 31, 0);
            counter2.callMethod("write", builder, newVal);
            return Signal(newVal, &builder, loc());
          },
          b, loc());

        Return(result.getValue());
      });

    // ✨ Rule demonstrating If in rule body
    INIT_RULE(conditionalIncrement)
      .guard([](mlir::OpBuilder &b) {
        Return();
      })
      .body([this](mlir::OpBuilder &b) {
        // Read counter1
        auto counter1Vals = counter1.callValue("read", b);

        // Check if counter1 is even (bit 0 == 0)
        auto bit0 = Bits(counter1Vals[0], 0, 0);
        auto zero = UIntConst(0, 1);
        auto isEven = Eq(bit0, zero);

        Signal isEvenSig(isEven, &b, loc());

        // If even, increment counter2, else do nothing
        If(isEvenSig,
          [&](mlir::OpBuilder &builder) -> Signal {
            auto counter2Vals = counter2.callValue("read", builder);
            auto one = UIntConst(1, 32);
            auto sum = Add(counter2Vals[0], one);
            auto newVal = Bits(sum, 31, 0);
            counter2.callMethod("write", builder, newVal);
            return Signal(newVal, &builder, loc());
          },
          b, loc());

        Return();
      });
  }

  void build() override {
    // ✨ Empty! Everything is declarative!
  }
};

int main() {
  mlir::MLIRContext context;
  context.loadDialect<cmt2::Cmt2Dialect>();
  context.loadDialect<firrtl::FIRRTLDialect>();

  highlevel::Circuit circuit("TwoCounterIfExample", context);

  // External register module
  llvm::StringMap<int64_t> regParams;
  regParams["width"] = 32;
  auto *regMod = circuit.addExternalModule("FIRRTLReg", regParams);
  regMod->bindClock("clk", "clock")
        .bindReset("rst", "reset")
        .bindValue("read", "read_ready", {"read_data"})
        .bindMethod("write", "write_enable", "write_ready", {"write_data"}, {})
        .addConflict("write", "write")
        .addConflictFree("read", "read")
        .addSequenceBefore("read", "write");

  // Add module
  circuit.addModule(std::make_unique<TwoCounterIfExample>(regMod));

  llvm::outs() << "\n=== High-Level Declarative If Example ===\n";
  llvm::outs() << "This demonstrates:\n";
  llvm::outs() << "- ✨ Declarative Cmt2Module with INIT_METHOD and INIT_RULE\n";
  llvm::outs() << "- ✨ If() helper function with Then/Else lambdas\n";
  llvm::outs() << "- ✨ Helper functions: Return(), Add(), Bits(), Eq()\n";
  llvm::outs() << "- ✨ Method with If for conditional counter selection\n";
  llvm::outs() << "- ✨ Rule with If for conditional execution\n";
  llvm::outs() << "- ✨ Empty build() method\n\n";

  llvm::outs() << "Generated MLIR:\n";
  llvm::outs() << circuit.emitMLIRString() << "\n";

  return 0;
}
