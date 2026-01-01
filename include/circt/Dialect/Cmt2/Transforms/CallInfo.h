//===- CallInfo.h - Call information analysis for Cmt2 --------*- C++ -*-===//
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

#ifndef CIRCT_DIALECT_CMT2_TRANSFORMS_CALLINFO_H
#define CIRCT_DIALECT_CMT2_TRANSFORMS_CALLINFO_H

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

  /// Determine the type of the callee with explicit instance and method refs
  CallType determineCalleeType(CallOp callOp, ModuleOp currentModule,
                                mlir::SymbolRefAttr calleeInstance,
                                mlir::SymbolRefAttr calleeEntity);

  /// The circuit operation for symbol lookup
  CircuitOp circuit;

  /// The main data structure: module name -> module call info
  llvm::DenseMap<mlir::StringRef, ModuleCallInfo> callInfoMap;
};

//===----------------------------------------------------------------------===//
// Step Call Collection Utilities
//===----------------------------------------------------------------------===//

/// Collect all method calls within a procedural step operation.
/// This is used for backpressure analysis and static step validation.
///
/// @param step The ProcStepOp or ProcStaticStepOp to analyze
/// @param module The containing module for symbol resolution
/// @param circuit The circuit for cross-module lookups
/// @return Vector of CallInfo for all method calls in the step
llvm::SmallVector<CallInfo, 4> collectStepCalls(mlir::Operation *step,
                                                  cmt2::ModuleOp module,
                                                  CircuitOp circuit);

/// Check if any call in the given step has potential conflicts with other
/// functions in the module.
///
/// @param step The step operation to check
/// @param module The containing module
/// @param conflictMatrix The conflict matrix for analysis
/// @return true if any method call has potential conflicts
bool stepHasConflictingCalls(mlir::Operation *step, cmt2::ModuleOp module,
                             const class ModuleConflictMatrix &conflictMatrix);

/// Get all functions that could fire concurrently with a given step.
/// This includes rules and methods that are not in a sequential relationship
/// with the step's parent proc rule.
///
/// @param step The step operation
/// @param module The containing module
/// @return Vector of function names that could be concurrent
llvm::SmallVector<mlir::StringAttr, 8>
getConcurrentFunctions(mlir::Operation *step, cmt2::ModuleOp module);

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_TRANSFORMS_CALLINFO_H
