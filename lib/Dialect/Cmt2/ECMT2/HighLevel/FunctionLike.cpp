//===- FunctionLike.cpp - High-Level Function Implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"

using namespace circt;
using namespace cmt2::ecmt2::highlevel;

// Rule implementation
void Rule::init(Cmt2Module *parent, llvm::StringRef name) {
  name_ = name.str();

  // Create low-level rule
  auto *lowLevelRule = parent->lowLevelModule()->addRule(name);

  // Set guard if defined
  if (guardFn_) {
    lowLevelRule->guard([this, parent](mlir::OpBuilder &builder) {
      // Set up build context for implicit helper functions
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      guardFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  // Set body if defined
  if (bodyFn_) {
    lowLevelRule->body([this, parent](mlir::OpBuilder &builder) {
      // Set up build context for implicit helper functions
      BuildContext ctx(&builder, parent->loc());
      Cmt2Module::setCurrentContext(&ctx);
      bodyFn_(builder);
      Cmt2Module::setCurrentContext(nullptr);
    });
  }

  lowLevelRule->finalize();
}

// Template instantiations would go here for common types
// But since these use lambdas extensively, we keep them header-only
