//===- InterfaceWrapper.cpp - High-Level Interface Wrappers -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/InterfaceWrapper.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

// InterfaceDeclBase implementation

void InterfaceDeclBase::init(Cmt2Module *parent, llvm::StringRef name,
                             llvm::StringRef interfaceType) {
  name_ = name.str();
  interfaceType_ = interfaceType.str();

  // Create low-level interface declaration
  lowLevelDecl_ = parent->lowLevelModule()->defineInterfaceDecl(name, interfaceType);
}

llvm::SmallVector<mlir::Value, 4>
InterfaceDeclBase::callMethod(llvm::StringRef method,
                              llvm::ArrayRef<mlir::Value> args,
                              mlir::OpBuilder &builder) {
  if (!lowLevelDecl_) {
    llvm::report_fatal_error("InterfaceDecl not initialized");
  }
  return lowLevelDecl_->callMethod(method, args, builder);
}

llvm::SmallVector<mlir::Value, 4>
InterfaceDeclBase::callValue(llvm::StringRef value, mlir::OpBuilder &builder) {
  if (!lowLevelDecl_) {
    llvm::report_fatal_error("InterfaceDecl not initialized");
  }
  return lowLevelDecl_->callValue(value, builder);
}

// InterfaceDefBase implementation

InterfaceDefBase &InterfaceDefBase::bind(llvm::StringRef instance,
                                         llvm::StringRef instanceMethod,
                                         llvm::StringRef interfaceMethod) {
  bindings_.push_back(std::make_tuple(instance.str(), instanceMethod.str(),
                                      interfaceMethod.str()));
  return *this;
}

void InterfaceDefBase::init(Cmt2Module *parent, llvm::StringRef name,
                           llvm::StringRef interfaceType) {
  name_ = name.str();
  interfaceType_ = interfaceType.str();

  // Create low-level interface definition
  lowLevelDef_ = parent->lowLevelModule()->defineInterfaceDef(name, interfaceType);

  // Apply all bindings
  for (const auto &[inst, instMethod, ifaceMethod] : bindings_) {
    lowLevelDef_->bind(inst, instMethod, ifaceMethod);
  }

  // Finalize the interface definition
  lowLevelDef_->finalize();
}

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt
