//===- Circuit.cpp - High-Level Circuit Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"

using namespace circt::cmt2::ecmt2::highlevel;

Circuit::Circuit(llvm::StringRef topModule, mlir::MLIRContext &context)
    : context_(context), topModule_(topModule.str()) {
  // Create low-level circuit
  lowLevelCircuit_ = std::make_unique<circt::cmt2::ecmt2::Circuit>(topModule, context);
}

Circuit::~Circuit() = default;

circt::cmt2::ecmt2::Interface *Circuit::addInterface(llvm::StringRef name) {
  return lowLevelCircuit_->addInterface(name);
}

std::string Circuit::emitMLIRString() {
  return lowLevelCircuit_->emitMLIRString();
}

mlir::LogicalResult Circuit::runCmt2ToFIRRTLPipeline() {
  return lowLevelCircuit_->runCmt2ToFIRRTLPipeline();
}

std::string Circuit::emitFIRRTL() {
  return lowLevelCircuit_->emitFIRRTL();
}
