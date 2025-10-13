//===- Cmt2CallInfo.h - Call information for Cmt2 --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CallInfo analysis for the Cmt2 dialect.
// It tracks which methods/values are called by each rule/method/value.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_CMT2CALLINFO_H
#define CIRCT_DIALECT_CMT2_CMT2CALLINFO_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace circt {
namespace cmt2 {

/// Enumeration for the type of the callee (what is being called)
enum class CallType {
  MethodCall, // Calling a MethodOp
  ValueCall   // Calling a ValueOp
};

/// Information about a single call operation
struct CallInfo {
  mlir::SymbolRefAttr calleeInstance; // The instance being called (@x, @y, @this)
  mlir::SymbolRefAttr calleeEntity;   // The method/value being called (@read, @write)
  CallType callType;                  // Type of call (rule/method/value)

  CallInfo(mlir::SymbolRefAttr instance, mlir::SymbolRefAttr entity, CallType type)
      : calleeInstance(instance), calleeEntity(entity), callType(type) {}
};

/// View of call information for a single entity (rule/method/value)
using EntityCallInfo = llvm::SmallVector<CallInfo, 4>;

/// View of call information for a single module
/// Maps entity name -> list of calls
using ModuleCallInfo = llvm::DenseMap<mlir::SymbolRefAttr, EntityCallInfo>;

/// The main CallInfoView analysis result
/// Maps module name -> module call info
class CallInfoView {
public:
  explicit CallInfoView(CircuitOp circuit);

  /// Get call info for a specific module
  const ModuleCallInfo *getModuleCallInfo(mlir::StringRef moduleName) const;

  /// Get all module names
  llvm::SmallVector<mlir::StringRef, 4> getModuleNames() const;

  /// Print the call info in a human-readable format
  void print(llvm::raw_ostream &os) const;

private:
  /// Build the call info for a single module
  void buildModuleCallInfo(ModuleOp module);

  /// Process a rule/method/value operation to extract call information
  void processEntity(mlir::Operation *entity, mlir::SymbolRefAttr entityName,
                     ModuleOp currentModule, ModuleCallInfo &moduleInfo);

  /// Determine the type of the callee (Method or Value)
  CallType determineCalleeType(CallOp callOp, ModuleOp currentModule);

  /// The circuit operation for symbol lookup
  CircuitOp circuit;

  /// The main data structure: module name -> module call info
  llvm::DenseMap<mlir::StringRef, ModuleCallInfo> callInfoMap;
};

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_CMT2CALLINFO_H
