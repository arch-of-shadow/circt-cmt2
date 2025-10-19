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
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
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

//===----------------------------------------------------------------------===//
// IfBuilder Implementation
//===----------------------------------------------------------------------===//

Signal IfBuilder::build() {
  // Determine if we have results based on the then function
  mlir::Value thenResult;
  mlir::Value elseResult;

  // Create the if operation
  mlir::OpBuilder::InsertionGuard guard(builder_);

  // First, we need to check if the branches will return values
  // We'll do this by building the branches in temporary regions first

  llvm::SmallVector<mlir::Type> resultTypes;

  // Create the if operation
  auto ifOp = builder_.create<circt::cmt2::IfOp>(
      loc_, resultTypes, condition_);

  // Build the then region
  {
    mlir::OpBuilder::InsertionGuard regionGuard(builder_);
    mlir::Block *thenBlock = new mlir::Block();
    ifOp.getThenRegion().push_back(thenBlock);
    builder_.setInsertionPointToStart(thenBlock);

    if (thenFn_) {
      thenResult = thenFn_(builder_);

      // Create yield operation
      if (thenResult) {
        builder_.create<circt::cmt2::YieldOp>(loc_, mlir::ValueRange{thenResult});
      } else {
        builder_.create<circt::cmt2::YieldOp>(loc_, mlir::ValueRange{});
      }
    } else {
      builder_.create<circt::cmt2::YieldOp>(loc_, mlir::ValueRange{});
    }
  }

  // Build the else region if present
  if (hasElse_ && elseFn_) {
    mlir::OpBuilder::InsertionGuard regionGuard(builder_);
    mlir::Block *elseBlock = new mlir::Block();
    ifOp.getElseRegion().push_back(elseBlock);
    builder_.setInsertionPointToStart(elseBlock);

    elseResult = elseFn_(builder_);

    // Create yield operation
    if (elseResult) {
      builder_.create<circt::cmt2::YieldOp>(loc_, mlir::ValueRange{elseResult});
    } else {
      builder_.create<circt::cmt2::YieldOp>(loc_, mlir::ValueRange{});
    }
  }

  // If both branches return results, update the if operation to have result types
  if (thenResult && elseResult) {
    // We need to recreate the operation with result types
    builder_.setInsertionPoint(ifOp);

    auto newIfOp = builder_.create<circt::cmt2::IfOp>(
        loc_, mlir::TypeRange{thenResult.getType()}, condition_);

    // Move regions from old op to new op
    newIfOp.getThenRegion().takeBody(ifOp.getThenRegion());
    newIfOp.getElseRegion().takeBody(ifOp.getElseRegion());

    // Erase old operation
    ifOp.erase();

    // Return the result wrapped in a Signal
    if (newIfOp.getNumResults() > 0) {
      return Signal(newIfOp.getResult(0), &builder_, loc_);
    }
  }

  // Return empty signal if no results
  return Signal(mlir::Value(), nullptr, loc_);
}
