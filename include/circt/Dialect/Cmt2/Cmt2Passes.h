//===- Cmt2Passes.h - Cmt2 pass entry points ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes that expose pass constructors.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_Cmt2_Cmt2PASSES_H
#define CIRCT_DIALECT_Cmt2_Cmt2PASSES_H

#include "mlir/Pass/PassRegistry.h"

namespace mlir {
class Pass;
} // namespace mlir

namespace circt {
namespace cmt2 {

std::unique_ptr<mlir::Pass> createTestPass();
// std::unique_ptr<mlir::Pass> createGenerateConflictMatrix();
// std::unique_ptr<mlir::Pass> createReferRules();

/// Generate pass declarations.
#define GEN_PASS_DECL
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"

/// Generate the code for registering passes.
#define GEN_PASS_REGISTRATION
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_Cmt2_Cmt2PASSES_H