//===- Instance.h - ECMT2 Instance and Call Support -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines instance and call support for the ECMT2 embedded DSL.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_INSTANCE_H
#define CIRCT_DIALECT_CMT2_ECMT2_INSTANCE_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace circt {
namespace cmt2 {
namespace ecmt2 {

class Instance {
public:
  Instance(llvm::StringRef name, ModuleBase *moduleType,
           llvm::ArrayRef<mlir::Value> args, Module *parent,
           llvm::ArrayRef<std::pair<std::string, std::string>>
               interfaceBindings = {});

  /// Call methods on this instance
  llvm::SmallVector<mlir::Value, 4>
  callMethod(llvm::StringRef method, llvm::ArrayRef<mlir::Value> args,
             mlir::OpBuilder &builder);

  /// Access values from this instance
  llvm::SmallVector<mlir::Value, 4> callValue(llvm::StringRef value,
                                              mlir::OpBuilder &builder);

  llvm::StringRef getName() const { return name_; }
  mlir::Operation *getOperation() const { return op_; }
  ModuleBase *getModuleType() const { return moduleType_; }

private:
  std::string name_;
  InstanceOp op_;
  ModuleBase *moduleType_;
  mlir::Location loc_;

  friend class CallBuilder;
};

/// Helper class for building cmt2.call operations
class CallBuilder {
public:
  static llvm::SmallVector<mlir::Value, 4>
  buildCall(Instance *instance, llvm::StringRef entity,
            llvm::ArrayRef<mlir::Value> args, mlir::OpBuilder &builder,
            mlir::Location loc);

  /// Build call to @this (private function)
  static llvm::SmallVector<mlir::Value, 4>
  buildThisCall(llvm::StringRef entity, llvm::ArrayRef<mlir::Value> args,
                mlir::OpBuilder &builder, mlir::Location loc);
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_INSTANCE_H
