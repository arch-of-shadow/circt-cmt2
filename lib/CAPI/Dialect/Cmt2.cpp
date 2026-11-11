//===- Cmt2.cpp - C interface for the Cmt2 dialect -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt-c/Dialect/Cmt2.h"
#include "circt/Dialect/Cmt2/Cmt2Attributes.h"
#include "circt/Dialect/Cmt2/Cmt2Dialect.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"

#include "mlir/CAPI/IR.h"
#include "mlir/CAPI/Registration.h"
#include "mlir/CAPI/Support.h"
#include "mlir/Pass/Pass.h"

using namespace circt::cmt2;

MLIR_DEFINE_CAPI_DIALECT_REGISTRATION(Cmt2, cmt2, circt::cmt2::Cmt2Dialect)

void registerCmt2Passes() { circt::cmt2::registerPasses(); }

//===----------------------------------------------------------------------===//
// Attribute API
//===----------------------------------------------------------------------===//

MlirAttribute circtCmt2IntervalAttrGet(MlirContext ctx, int64_t cycles) {
  return wrap(IntervalAttr::get(unwrap(ctx), cycles));
}

bool circtCmt2IntervalAttrIsA(MlirAttribute attr) {
  return llvm::isa<IntervalAttr>(unwrap(attr));
}

int64_t circtCmt2IntervalAttrGetCycles(MlirAttribute attr) {
  return llvm::cast<IntervalAttr>(unwrap(attr)).getCycles();
}

MlirAttribute circtCmt2LatencyAttrGet(MlirContext ctx, int64_t cycles) {
  return wrap(LatencyAttr::get(unwrap(ctx), cycles));
}

bool circtCmt2LatencyAttrIsA(MlirAttribute attr) {
  return llvm::isa<LatencyAttr>(unwrap(attr));
}

int64_t circtCmt2LatencyAttrGetCycles(MlirAttribute attr) {
  return llvm::cast<LatencyAttr>(unwrap(attr)).getCycles();
}

MlirAttribute circtCmt2TimingIntervalAttrGet(MlirContext ctx, int64_t start,
                                             int64_t end) {
  return wrap(TimingIntervalAttr::get(unwrap(ctx), start, end));
}

bool circtCmt2TimingIntervalAttrIsA(MlirAttribute attr) {
  return llvm::isa<TimingIntervalAttr>(unwrap(attr));
}

int64_t circtCmt2TimingIntervalAttrGetStart(MlirAttribute attr) {
  return llvm::cast<TimingIntervalAttr>(unwrap(attr)).getStart();
}

int64_t circtCmt2TimingIntervalAttrGetEnd(MlirAttribute attr) {
  return llvm::cast<TimingIntervalAttr>(unwrap(attr)).getEnd();
}
