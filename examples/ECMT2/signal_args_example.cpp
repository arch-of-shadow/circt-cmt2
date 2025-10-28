//===- signal_args_example.cpp - Signal Arguments Example ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This example demonstrates using the Signal-based arguments API for methods,
// which provides a cleaner and more powerful interface than raw BlockArguments.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
#include "llvm/Support/raw_ostream.h"

using namespace circt;
using namespace cmt2::ecmt2;
using namespace cmt2::ecmt2::highlevel;

// Type aliases to work around nested template syntax limitations
using UInt32 = highlevel::UInt<32>;

/// Example module showing Signal-based argument handling
class SignalArgsExample : public Cmt2Module {
public:
  ClockInput clk;
  ResetInput rst;

  // Method with Signal arguments - much cleaner API!
  highlevel::Method<UInt32, UInt32, UInt32> addMethod;

  SignalArgsExample() : Cmt2Module("SignalArgsExample") {
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // Using bodySignal/guardSignal for Signal-based arguments
    INIT_METHOD(addMethod)
      .guardSignal([](mlir::OpBuilder &builder, const std::vector<Signal> &args) {
        // With Signal arguments, we can use Signal operators directly!
        Return();
      })
      .bodySignal([](mlir::OpBuilder &builder, const std::vector<Signal> &args) {
        // args are Signal objects, so we can use operators!
        Signal a = args[0];
        Signal b_val = args[1];

        // ✨ Use Signal operators for cleaner code
        Signal sum = a + b_val;
        Signal result = sum.bits(31, 0);  // Truncate to 32 bits

        Return(result.getValue());
      });
  }

  void build() override {}
};

/// Comparison: Old way vs New way
class ComparisonExample : public Cmt2Module {
public:
  ClockInput clk;
  ResetInput rst;

  // Method using old BlockArgument API
  highlevel::Method<UInt32, UInt32, UInt32> oldWay;

  // Method using new Signal API
  highlevel::Method<UInt32, UInt32, UInt32> newWay;

  ComparisonExample() : Cmt2Module("ComparisonExample") {
    CMT2_ARG_CLOCK(clk);
    CMT2_ARG_RESET(rst);

    // ❌ Old way: raw BlockArguments - verbose and error-prone
    INIT_METHOD(oldWay)
      .guard([](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        Return();
      })
      .body([](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
        // Need to manually wrap in Signal for operations
        Signal a(args[0], &builder, builder.getUnknownLoc());
        Signal b_val(args[1], &builder, builder.getUnknownLoc());

        auto sum = Add(a.getValue(), b_val.getValue());
        auto result = Bits(sum, 31, 0);

        Return(result);
      });

    // ✅ New way: Signal arguments - clean and intuitive!
    INIT_METHOD(newWay)
      .guardSignal([](mlir::OpBuilder &builder, const std::vector<Signal> &args) {
        Return();
      })
      .bodySignal([](mlir::OpBuilder &builder, const std::vector<Signal> &args) {
        // Arguments are already Signal objects!
        Signal a = args[0];
        Signal b_val = args[1];

        // Direct operator usage
        Signal sum = a + b_val;
        Signal result = sum.bits(31, 0);

        Return(result.getValue());
      });
  }

  void build() override {}
};

int main() {
  mlir::MLIRContext context;
  context.loadDialect<cmt2::Cmt2Dialect>();
  context.loadDialect<firrtl::FIRRTLDialect>();

  llvm::outs() << "=== Signal Arguments Example ===\n\n";

  {
    highlevel::Circuit circuit("SignalArgsExample", context);
    circuit.addModule<SignalArgsExample>();

    llvm::outs() << "This example demonstrates the new Signal-based argument API:\n\n";
    llvm::outs() << "Generated MLIR:\n";
    llvm::outs() << circuit.emitMLIRString() << "\n\n";
  }

  llvm::outs() << "\n=== Comparison: Old vs New API ===\n\n";

  {
    highlevel::Circuit circuit("ComparisonExample", context);
    circuit.addModule<ComparisonExample>();

    llvm::outs() << "This shows the difference between:\n";
    llvm::outs() << "- Old: .guard()/.body() with llvm::ArrayRef<mlir::BlockArgument>\n";
    llvm::outs() << "- New: .guardSignal()/.bodySignal() with std::vector<Signal>\n\n";

    llvm::outs() << "Benefits of Signal arguments:\n";
    llvm::outs() << "✅ No manual wrapping needed\n";
    llvm::outs() << "✅ Can use Signal operators (+, -, *, etc.)\n";
    llvm::outs() << "✅ Can use Signal methods (.bits(), .cat(), etc.)\n";
    llvm::outs() << "✅ Less boilerplate code\n";
    llvm::outs() << "✅ Type-safe and intuitive\n\n";

    llvm::outs() << "Generated MLIR:\n";
    llvm::outs() << circuit.emitMLIRString() << "\n";
  }

  return 0;
}
