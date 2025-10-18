//===- Module.cpp - High-Level Module Implementation ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"

using namespace circt;
using namespace cmt2::ecmt2::highlevel;

// Define thread-local current context
thread_local BuildContext *Cmt2Module::currentContext_ = nullptr;
