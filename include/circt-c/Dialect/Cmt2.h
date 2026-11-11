//===- Cmt2.h - C interface for the Cmt2 dialect ------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_C_DIALECT_CMT2_H
#define CIRCT_C_DIALECT_CMT2_H

#include "mlir-c/IR.h"

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Dialect API
//===----------------------------------------------------------------------===//

MLIR_DECLARE_CAPI_DIALECT_REGISTRATION(Cmt2, cmt2);

/// Register all CMT2 passes
MLIR_CAPI_EXPORTED void registerCmt2Passes(void);

//===----------------------------------------------------------------------===//
// Attribute API
//===----------------------------------------------------------------------===//

/// Creates a CMT2 IntervalAttr with the given number of cycles.
MLIR_CAPI_EXPORTED MlirAttribute circtCmt2IntervalAttrGet(MlirContext ctx,
                                                          int64_t cycles);

/// Returns true if the given attribute is a CMT2 IntervalAttr.
MLIR_CAPI_EXPORTED bool circtCmt2IntervalAttrIsA(MlirAttribute attr);

/// Returns the number of cycles from a CMT2 IntervalAttr.
MLIR_CAPI_EXPORTED int64_t circtCmt2IntervalAttrGetCycles(MlirAttribute attr);

/// Creates a CMT2 LatencyAttr with the given number of cycles.
MLIR_CAPI_EXPORTED MlirAttribute circtCmt2LatencyAttrGet(MlirContext ctx,
                                                         int64_t cycles);

/// Returns true if the given attribute is a CMT2 LatencyAttr.
MLIR_CAPI_EXPORTED bool circtCmt2LatencyAttrIsA(MlirAttribute attr);

/// Returns the number of cycles from a CMT2 LatencyAttr.
MLIR_CAPI_EXPORTED int64_t circtCmt2LatencyAttrGetCycles(MlirAttribute attr);

/// Creates a CMT2 TimingIntervalAttr with the given start and end cycles.
MLIR_CAPI_EXPORTED MlirAttribute circtCmt2TimingIntervalAttrGet(MlirContext ctx,
                                                                 int64_t start,
                                                                 int64_t end);

/// Returns true if the given attribute is a CMT2 TimingIntervalAttr.
MLIR_CAPI_EXPORTED bool circtCmt2TimingIntervalAttrIsA(MlirAttribute attr);

/// Returns the start cycle from a CMT2 TimingIntervalAttr.
MLIR_CAPI_EXPORTED int64_t
circtCmt2TimingIntervalAttrGetStart(MlirAttribute attr);

/// Returns the end cycle from a CMT2 TimingIntervalAttr.
MLIR_CAPI_EXPORTED int64_t
circtCmt2TimingIntervalAttrGetEnd(MlirAttribute attr);

#ifdef __cplusplus
}
#endif

#endif // CIRCT_C_DIALECT_CMT2_H
