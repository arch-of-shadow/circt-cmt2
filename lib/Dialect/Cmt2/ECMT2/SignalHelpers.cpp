//===- SignalHelpers.cpp - ECMT2 Signal Helper Functions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of helper functions for ECMT2 Signal, Bundle, and FVector.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/SignalHelpers.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Attributes.h"

using namespace circt::cmt2::ecmt2;
using namespace circt::firrtl;

//===----------------------------------------------------------------------===//
// BundleBuilder Implementation
//===----------------------------------------------------------------------===//

BundleBuilder &BundleBuilder::addUInt(llvm::StringRef name, unsigned width,
                                       bool isFlip) {
  auto nameAttr = mlir::StringAttr::get(context_, name);
  auto type = UIntType::get(context_, width);
  elements_.push_back(BundleType::BundleElement(nameAttr, isFlip, type));
  return *this;
}

BundleBuilder &BundleBuilder::addSInt(llvm::StringRef name, unsigned width,
                                       bool isFlip) {
  auto nameAttr = mlir::StringAttr::get(context_, name);
  auto type = SIntType::get(context_, width);
  elements_.push_back(BundleType::BundleElement(nameAttr, isFlip, type));
  return *this;
}

BundleBuilder &BundleBuilder::addVector(llvm::StringRef name,
                                         mlir::Type elemType, size_t numElems,
                                         bool isFlip) {
  auto nameAttr = mlir::StringAttr::get(context_, name);
  auto vecType = FVectorType::get(mlir::cast<FIRRTLBaseType>(elemType), numElems);
  elements_.push_back(BundleType::BundleElement(nameAttr, isFlip, vecType));
  return *this;
}

BundleBuilder &BundleBuilder::addField(llvm::StringRef name, mlir::Type type,
                                        bool isFlip) {
  auto nameAttr = mlir::StringAttr::get(context_, name);
  auto firrtlType = mlir::cast<FIRRTLBaseType>(type);
  elements_.push_back(BundleType::BundleElement(nameAttr, isFlip, firrtlType));
  return *this;
}

Bundle BundleBuilder::build(mlir::OpBuilder &builder, mlir::Location loc) {
  return Bundle(elements_, builder, loc);
}
