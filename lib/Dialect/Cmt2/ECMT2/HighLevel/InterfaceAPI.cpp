//===- InterfaceAPI.cpp - Declarative Interface Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/InterfaceAPI.h"
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"

using namespace circt;
using namespace cmt2::ecmt2::highlevel;

void Cmt2Interface::init(cmt2::ecmt2::Circuit *circuit) {
  circuit_ = circuit;
  context_ = &circuit->getContext();

  // Create low-level interface through circuit
  lowLevelInterface_ = circuit->addInterface(name_);

  // Call derived class build() to add methods/values
  build();
}
