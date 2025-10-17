//===- Module.h - High-Level Module API ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// High-level class-based API for Cmt2 modules
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_MODULE_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_MODULE_H

#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include <string>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

class Circuit;

/// Base class for all high-level Cmt2 modules
/// Wraps the low-level ecmt2::Module class
class Cmt2Module {
public:
  Cmt2Module(llvm::StringRef name) : name_(name.str()) {}
  virtual ~Cmt2Module() = default;

  /// Override this to define module structure
  virtual void build() = 0;

  /// Access to underlying low-level module
  ecmt2::Module *lowLevelModule() { return lowLevelModule_; }
  const ecmt2::Module *lowLevelModule() const { return lowLevelModule_; }

  /// Module name
  llvm::StringRef name() const { return name_; }

protected:
  /// Called by Circuit to set up low-level module
  void setLowLevelModule(ecmt2::Module *module) {
    lowLevelModule_ = module;
  }

  /// Convenience accessors
  mlir::OpBuilder &builder() { return lowLevelModule_->getBuilder(); }
  mlir::Location loc() const { return lowLevelModule_->getLoc(); }

private:
  std::string name_;
  ecmt2::Module *lowLevelModule_ = nullptr;

  friend class Circuit;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_MODULE_H
