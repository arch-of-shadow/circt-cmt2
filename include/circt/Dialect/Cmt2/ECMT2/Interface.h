//===- Interface.h - ECMT2 Interface Support --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines interface support for the ECMT2 embedded DSL.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_INTERFACE_H
#define CIRCT_DIALECT_CMT2_ECMT2_INTERFACE_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/SmallVector.h"
#include <string>
#include <tuple>

namespace circt {
namespace cmt2 {
namespace ecmt2 {

// Forward declarations
class Module;
class Circuit;

/// Circuit-level interface definition
class Interface {
public:
  Interface(llvm::StringRef name, Circuit *parent);

  /// Add a method to the interface
  Interface &addMethod(llvm::StringRef name,
                      llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
                      llvm::ArrayRef<mlir::Type> results);

  /// Add a value to the interface
  Interface &addValue(llvm::StringRef name,
                     llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
                     llvm::ArrayRef<mlir::TypeAttr> results);

  llvm::StringRef getName() const { return name_; }
  InterfaceOp getOp() const { return op_; }

private:
  std::string name_;
  InterfaceOp op_;
  mlir::OpBuilder &builder_;
  mlir::Location loc_;
};

class InterfaceDecl {
public:
  InterfaceDecl(llvm::StringRef name, llvm::StringRef type, Module *parent);

  /// Call methods through the interface
  llvm::SmallVector<mlir::Value, 4>
  callMethod(llvm::StringRef method, llvm::ArrayRef<mlir::Value> args,
             mlir::OpBuilder &builder);

  llvm::SmallVector<mlir::Value, 4> callValue(llvm::StringRef value,
                                              mlir::OpBuilder &builder);

  llvm::StringRef getName() const { return name_; }
  llvm::StringRef getType() const { return type_; }

private:
  std::string name_;
  std::string type_;
  InterfaceDeclOp op_;
  mlir::Location loc_;
};

class InterfaceDef {
public:
  InterfaceDef(llvm::StringRef name, llvm::StringRef type, Module *parent);

  /// Bind instance methods to interface methods
  InterfaceDef &bind(llvm::StringRef instance, llvm::StringRef instanceMethod,
                    llvm::StringRef interfaceMethod);

  void finalize();

private:
  std::string name_;
  std::string type_;
  InterfaceDefOp op_;
  llvm::SmallVector<std::tuple<std::string, std::string, std::string>, 4>
      bindings_;
  mlir::OpBuilder &builder_;
  mlir::Location loc_;
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_INTERFACE_H
