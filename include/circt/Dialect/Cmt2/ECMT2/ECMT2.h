//===- ECMT2.h - ECMT2 Embedded DSL Main Header ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the main header file for the ECMT2 embedded DSL. Include this file
// to access all ECMT2 functionality (both low-level and high-level APIs).
//
// Usage:
//   #include "circt/Dialect/Cmt2/ECMT2/ECMT2.h"
//   using namespace circt::cmt2::ecmt2;           // For low-level API
//   using namespace circt::cmt2::ecmt2::highlevel; // For high-level API
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_H
#define CIRCT_DIALECT_CMT2_ECMT2_H

// Low-level API
#include "circt/Dialect/Cmt2/ECMT2/Utils.h"
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/Signal.h"
#include "circt/Dialect/Cmt2/ECMT2/SignalHelpers.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "circt/Dialect/Cmt2/ECMT2/STLLibrary.h"

// High-level API
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/ECMT2.h"

#endif // CIRCT_DIALECT_CMT2_ECMT2_H
