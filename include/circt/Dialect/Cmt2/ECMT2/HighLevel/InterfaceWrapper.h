//===- InterfaceWrapper.h - High-Level Interface Wrappers -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declarative V3 API wrappers for Cmt2 interfaces
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INTERFACEWRAPPER_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INTERFACEWRAPPER_H

#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/Builders.h"
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

// Forward declaration
class Cmt2Module;

/// Declarative interface declaration (module requires this interface)
/// Wraps low-level ecmt2::InterfaceDecl
class InterfaceDeclBase {
public:
  InterfaceDeclBase() = default;
  virtual ~InterfaceDeclBase() = default;

  /// Initialize with parent module
  void init(Cmt2Module *parent, llvm::StringRef name, llvm::StringRef interfaceType);

  /// Call methods through the interface
  llvm::SmallVector<mlir::Value, 4>
  callMethod(llvm::StringRef method, llvm::ArrayRef<mlir::Value> args,
             mlir::OpBuilder &builder);

  llvm::SmallVector<mlir::Value, 4> callValue(llvm::StringRef value,
                                              mlir::OpBuilder &builder);

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

  void setInterfaceType(llvm::StringRef type) { interfaceType_ = type.str(); }
  llvm::StringRef getInterfaceType() const { return interfaceType_; }

protected:
  std::string name_;
  std::string interfaceType_;
  ecmt2::InterfaceDecl *lowLevelDecl_ = nullptr;
};

/// Templated interface declaration for type safety
template <typename InterfaceType>
class InterfaceDecl : public InterfaceDeclBase {
public:
  InterfaceDecl() = default;
};

/// Declarative interface definition (module provides implementation)
/// Wraps low-level ecmt2::InterfaceDef
class InterfaceDefBase {
public:
  InterfaceDefBase() = default;
  virtual ~InterfaceDefBase() = default;

  /// Bind instance method to interface method
  InterfaceDefBase &bind(llvm::StringRef instance, llvm::StringRef instanceMethod,
                        llvm::StringRef interfaceMethod);

  /// Initialize with parent module
  void init(Cmt2Module *parent, llvm::StringRef name, llvm::StringRef interfaceType);

  void setName(llvm::StringRef name) { name_ = name.str(); }
  llvm::StringRef getName() const { return name_; }

  void setInterfaceType(llvm::StringRef type) { interfaceType_ = type.str(); }
  llvm::StringRef getInterfaceType() const { return interfaceType_; }

protected:
  std::string name_;
  std::string interfaceType_;
  std::vector<std::tuple<std::string, std::string, std::string>> bindings_;
  ecmt2::InterfaceDef *lowLevelDef_ = nullptr;
};

/// Templated interface definition for type safety
template <typename InterfaceType>
class InterfaceDef : public InterfaceDefBase {
public:
  InterfaceDef() = default;

  InterfaceDef &bind(llvm::StringRef instance, llvm::StringRef instanceMethod,
                    llvm::StringRef interfaceMethod) {
    InterfaceDefBase::bind(instance, instanceMethod, interfaceMethod);
    return *this;
  }
};

/// Registration macros for interfaces

// Register an interface declaration (inward or outward)
#define CMT2_INTERFACE_DECL(member, interfaceTypeName)                         \
  do {                                                                          \
    member.setInterfaceType(interfaceTypeName);                                \
    getRegistry().registerMember(#member, [this](auto *mod) {                  \
      this->member.init(mod, #member, interfaceTypeName);                      \
    });                                                                         \
  } while (0)

// Register and initialize an interface definition
#define INIT_INTERFACE_DEF(member, interfaceTypeName)                          \
  [&]() -> auto & {                                                            \
    member.setInterfaceType(interfaceTypeName);                                \
    getRegistry().registerMember(#member, [this](auto *mod) {                  \
      this->member.init(mod, #member, interfaceTypeName);                      \
    });                                                                         \
    return member;                                                              \
  }()

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_INTERFACEWRAPPER_H
