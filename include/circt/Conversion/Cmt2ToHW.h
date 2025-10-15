//===- Cmt2ToHW.h - Cmt2 to HW conversion pass ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares passes which together will lower the Cmt2 dialect to the
// HW dialect.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_CONVERSION_CMT2TOHW_CMT2TOHW_H
#define CIRCT_CONVERSION_CMT2TOHW_CMT2TOHW_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Support/LLVM.h"
#include <memory>

namespace mlir {
class Pass;
} // namespace mlir

namespace circt {

#define GEN_PASS_DECL_CMT2TOHW
#include "circt/Conversion/Passes.h.inc"

std::unique_ptr<mlir::Pass> createCmt2ToHWPass();

} // namespace circt

#endif // CIRCT_CONVERSION_CMT2TOHW_CMT2TOHW_H
