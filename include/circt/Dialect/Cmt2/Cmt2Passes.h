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

#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"

namespace mlir {
class Pass;
} // namespace mlir

namespace circt {

// Forward declaration for the conversion pass
std::unique_ptr<mlir::Pass> createLowerCmt2ToFIRRTLPass();

namespace cmt2 {

std::unique_ptr<mlir::Pass> createTestPass();
// std::unique_ptr<mlir::Pass> createGenerateConflictMatrix();
// std::unique_ptr<mlir::Pass> createReferRules();

/// Populate a pass manager with the complete pipeline for converting Cmt2 to FIRRTL.
/// This includes:
/// 1. Inlining private functions
/// 2. Verifying all private functions have been inlined
/// 3. Verifying call sequences respect conflict matrix constraints
/// 4. Converting Cmt2 to FIRRTL
void populateCmt2ToFIRRTLPipeline(mlir::OpPassManager &pm);

/// Generate pass declarations.
#define GEN_PASS_DECL
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"

/// Generate the code for registering passes.
#define GEN_PASS_REGISTRATION
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_Cmt2_Cmt2PASSES_H