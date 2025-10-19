//===- Circuit.h - High-Level Circuit API ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// High-level circuit class that wraps low-level ecmt2::Circuit
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_CIRCUIT_H
#define CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_CIRCUIT_H

#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Module.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace circt {
namespace cmt2 {
namespace ecmt2 {
namespace highlevel {

/// High-level circuit class
/// Wraps low-level ecmt2::Circuit
class Circuit {
public:
  /// Constructor
  Circuit(llvm::StringRef topModule, mlir::MLIRContext &context);

  /// Destructor
  ~Circuit();

  /// Add a module by type
  /// Creates both high-level and low-level modules
  template <typename T>
  T *addModule() {
    static_assert(std::is_base_of<Cmt2Module, T>::value,
                  "T must inherit from Cmt2Module");

    // 1. Create high-level module object
    auto highLevelModule = std::make_unique<T>();
    T *ptr = highLevelModule.get();

    // 2. Create corresponding low-level module
    ecmt2::Module *lowLevelModule =
        lowLevelCircuit_->addModule(highLevelModule->name());

    // 3. Connect high-level to low-level
    highLevelModule->setLowLevelModule(lowLevelModule);

    // 4. Initialize all registered members (instances, values, methods, rules)
    highLevelModule->getRegistry().initializeAll(highLevelModule.get());

    // 5. Call user's build() method for additional customization
    highLevelModule->build();

    // 6. Apply precedence constraints
    if (!ptr->getPrecedenceConstraints().empty()) {
      lowLevelModule->setPrecedence(ptr->getPrecedenceConstraints());
    }

    // 7. Store the high-level module
    highLevelModules_.push_back(std::move(highLevelModule));

    return ptr;
  }

  /// Add a pre-constructed module (for modules with constructor arguments)
  /// Takes ownership of the module
  template <typename T>
  T *addModule(std::unique_ptr<T> module) {
    static_assert(std::is_base_of<Cmt2Module, T>::value,
                  "T must inherit from Cmt2Module");

    T *ptr = module.get();

    // Create corresponding low-level module
    ecmt2::Module *lowLevelModule =
        lowLevelCircuit_->addModule(module->name());

    // Connect high-level to low-level
    module->setLowLevelModule(lowLevelModule);

    // Initialize all registered members
    module->getRegistry().initializeAll(module.get());

    // Call user's build() method
    module->build();

    // Apply precedence constraints
    if (!ptr->getPrecedenceConstraints().empty()) {
      lowLevelModule->setPrecedence(ptr->getPrecedenceConstraints());
    }

    // Store the high-level module
    highLevelModules_.push_back(std::move(module));

    return ptr;
  }

  /// Add an interface definition to the circuit
  ecmt2::Interface *addInterface(llvm::StringRef name);

  /// Add an external module to the circuit
  ecmt2::ExternalModule *addExternalModule(llvm::StringRef name,
                                           llvm::StringRef firrtlModule);

  /// Add an external module with parameters to the circuit
  ecmt2::ExternalModule *addExternalModule(llvm::StringRef name,
                                           llvm::StringRef firrtlModule,
                                           const llvm::StringMap<int64_t> &params);

  /// Generate MLIR output
  std::string emitMLIRString();

  /// Run Cmt2 to FIRRTL conversion pipeline
  mlir::LogicalResult runCmt2ToFIRRTLPipeline();

  /// Generate FIRRTL output
  std::string emitFIRRTL();

  /// Access low-level circuit
  ecmt2::Circuit *lowLevelCircuit() { return lowLevelCircuit_.get(); }

  /// Get MLIR context
  mlir::MLIRContext &getContext() { return context_; }

private:
  mlir::MLIRContext &context_;
  std::string topModule_;

  /// Low-level circuit from ecmt2-EDSL.md
  std::unique_ptr<ecmt2::Circuit> lowLevelCircuit_;

  /// High-level modules
  std::vector<std::unique_ptr<Cmt2Module>> highLevelModules_;
};

} // namespace highlevel
} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_HIGHLEVEL_CIRCUIT_H
