//===- PassDetails.h - Cmt2 pass class details -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stuff shared between the different Cmt2 passes.
//
//===----------------------------------------------------------------------===//

// clang-tidy seems to expect the absolute path in the header guard on some
// systems, so just disable it.
// NOLINTNEXTLINE(llvm-header-guard)
#ifndef DIALECT_Cmt2_TRANSFORMS_PASSDETAILS_H
#define DIALECT_Cmt2_TRANSFORMS_PASSDETAILS_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/Pass/Pass.h"

namespace circt {

namespace hw {
class HWDialect;
}

namespace cmt2 {

#define GEN_PASS_CLASSES
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"

} // namespace cmt2
} // namespace circt

#endif // DIALECT_Cmt2_TRANSFORMS_PASSDETAILS_H