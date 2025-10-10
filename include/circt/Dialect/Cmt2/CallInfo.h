/===- CallInfo.h - CallInfo for each Cmt2Module -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Cmt2 CallInfo, which analyse a Module, to show each rule
// method or value calling a local instance function(method, value).
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_Cmt2_Cmt2CALLINFO_H
#define CIRCT_DIALECT_Cmt2_Cmt2CALLINFO_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator.h"

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
// #include "circt/Dialect/HW/InstanceGraphBase.h"
#include "circt/Support/LLVM.h"

namespace circt {
namespace cmt2 {

class CallInfo {
public:
  explicit CallInfo(Operation *operation);
  llvm::SmallVector<std::pair<SymbolRefAttr, SymbolRefAttr>, 4>
  getAllCallee(llvm::StringRef symbolName);
  llvm::SmallVector<std::pair<SymbolRefAttr, SymbolRefAttr>, 4>
  getAllMethodCallee(llvm::StringRef symbolName);
  llvm::SmallVector<std::pair<SymbolRefAttr, SymbolRefAttr>, 4>
  getAllValueCallee(llvm::StringRef symbolName);
  llvm::SmallVector<std::pair<SymbolRefAttr, SymbolRefAttr>, 4>
  getAllRuleCallee(llvm::StringRef symbolName);
  llvm::SmallVector<llvm::StringRef> getAllCaller(llvm::StringRef instanceName,
                                                  llvm::StringRef functionName);

private:
  using CallCache =
      DenseMap<llvm::StringRef,
               llvm::SmallVector<std::pair<SymbolRefAttr, SymbolRefAttr>, 4>>;
  CallCache methodsCache = CallCache{};
  CallCache valuesCache = CallCache{};
  CallCache rulesCache = CallCache{};
};
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_Cmt2_Cmt2CALLINFO_H