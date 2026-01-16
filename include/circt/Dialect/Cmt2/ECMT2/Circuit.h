//===- Circuit.h - ECMT2 Circuit and Code Generation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Circuit class for the ECMT2 embedded DSL.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_CIRCUIT_H
#define CIRCT_DIALECT_CMT2_ECMT2_CIRCUIT_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Support/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace ecmt2 {

// Forward declarations
class Interface;

class Circuit {
public:
  enum class FileFormat { MLIR, FIRRTL, Verilog };

  Circuit(llvm::StringRef topModule, mlir::MLIRContext &context);
  ~Circuit();

  /// Module management
  Module *addModule(llvm::StringRef name);
  mlir::FailureOr<Module *> getModule(llvm::StringRef name);

  /// Add external FIRRTL module
  /// @param firrtlModule - Module name from library manifest (e.g., "FIRRTLReg")
  /// @param name - Optional CMT2 module name. If empty, uses actual FIRRTL module name
  /// @param params - Optional parameters for parameterized modules
  ExternalModule *addExternalModule(llvm::StringRef firrtlModule,
                                   llvm::StringRef name = "");
  ExternalModule *addExternalModule(llvm::StringRef firrtlModule,
                                   const llvm::StringMap<int64_t> &params,
                                   llvm::StringRef name = "");
  ExternalModule *hasExternalModule(llvm::StringRef firrtlModule,
                                   const llvm::StringMap<int64_t> &params);

  /// Interface management
  Interface *addInterface(llvm::StringRef name);

  /// Code generation
  mlir::OwningOpRef<mlir::ModuleOp> generateMLIR();
  std::string emitMLIRString();

  /// Conversion pipeline
  mlir::LogicalResult runCmt2ToFIRRTLPipeline();
  std::string emitFIRRTL();

  /// Using firtool
  std::string emitVerilog();

  /// File I/O
  mlir::LogicalResult saveToFile(llvm::StringRef filename,
                                FileFormat format = FileFormat::MLIR);

  /// Get context
  mlir::MLIRContext &getContext() { return context_; }
  mlir::OpBuilder &getBuilder() { return builder_; }
  mlir::Location getLoc() const { return loc_; }

private:
  mlir::MLIRContext &context_;
  mlir::OpBuilder builder_;
  mlir::Location loc_;
  std::string topModule_;

  mlir::OwningOpRef<mlir::ModuleOp> mlirModule_;
  CircuitOp circuitOp_;

  std::vector<std::unique_ptr<Module>> modules_;
  std::vector<std::unique_ptr<ExternalModule>> externalModules_;
  std::vector<std::unique_ptr<Interface>> interfaces_;

  llvm::StringMap<size_t> modulesMap_;
  llvm::StringMap<size_t> externalModulesMap_;
  llvm::StringMap<size_t> interfacesMap_;
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_CIRCUIT_H
