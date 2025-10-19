//===- Cmt2ToFIRRTL.h - Cmt2 to FIRRTL conversion pass ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Cmt2 to FIRRTL conversion pass.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_CONVERSION_CMT2TOFIRRTL_H
#define CIRCT_CONVERSION_CMT2TOFIRRTL_H

#include <memory>

namespace mlir {
class Pass;
} // namespace mlir

namespace circt {

/// Create a Cmt2 to FIRRTL conversion pass.
std::unique_ptr<mlir::Pass> createLowerCmt2ToFIRRTLPass();

} // namespace circt

#endif // CIRCT_CONVERSION_CMT2TOFIRRTL_H
