//===- ModuleGenerator.h - Generate storage modules in passes ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleGenerator utility class for generating
// storage modules (Reg, ShiftReg, FIFO) during transformation passes.
//
// ModuleGenerator uses the ModuleLibrary to load pre-built FIRRTL modules
// and creates ExtModuleFirrtlOp bindings in the CMT2 circuit.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_TRANSFORMS_MODULEGENERATOR_H
#define CIRCT_DIALECT_CMT2_TRANSFORMS_MODULEGENERATOR_H

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"

namespace circt {
namespace cmt2 {

/// Utility class for generating storage modules in transformation passes.
///
/// This class provides a pass-friendly API for creating storage modules
/// (Reg, ShiftReg, FIFO) and their instances. It uses the ModuleLibrary
/// to load pre-built FIRRTL modules and creates ExtModuleFirrtlOp bindings.
///
/// Usage:
/// ```cpp
/// void MyPass::runOnOperation() {
///   auto circuit = getOperation();
///   ModuleGenerator gen(circuit);
///
///   // Get or create a ShiftReg module (returns ExtModuleFirrtlOp)
///   auto regMod = gen.getOrCreateShiftRegModule(32, 2);
///
///   // Create an instance
///   auto inst = gen.createStorageInstance(loc, "storage0", regMod,
///                                         clk, rst, builder);
///
///   // Generate a call to write
///   gen.createStorageCall(loc, inst, "write", {data}, builder);
/// }
/// ```
class ModuleGenerator {
public:
  explicit ModuleGenerator(CircuitOp circuit);

  /// Initialize the ModuleLibrary. Must be called before using generator.
  /// @param manifestPath Path to the module library manifest file
  /// @return success if manifest loaded, failure otherwise
  mlir::LogicalResult initialize(StringRef manifestPath);

  /// Get or create a simple Reg module with given data width.
  /// The module provides read() and write() methods.
  /// Uses ModuleLibrary to load the actual FIRRTL implementation.
  /// @param dataWidth The width of the register in bits
  /// @return The ExtModuleFirrtlOp binding, created if not already present
  ExtModuleFirrtlOp getOrCreateRegModule(unsigned dataWidth);

  /// Get or create a ShiftReg module with given data width and depth.
  /// ShiftReg provides write(), valid(), peek(), and advance() methods.
  /// This is a compound module built from Reg modules.
  /// @param dataWidth The width of data in bits
  /// @param depth The number of stages (pipeline depth)
  /// @return The ModuleOp (ShiftReg is built from Reg instances)
  cmt2::ModuleOp getOrCreateShiftRegModule(unsigned dataWidth, unsigned depth);

  /// Get or create a FIFO module with given data width and depth.
  /// FIFO provides enq(), deq(), first(), notEmpty(), notFull() methods.
  /// @param dataWidth The width of data in bits
  /// @param depth The FIFO depth (number of entries)
  /// @return The module, created if not already present
  Cmt2ModuleLike getOrCreateFIFOModule(unsigned dataWidth, unsigned depth);

  /// Create an instance of a storage module.
  /// @param loc Source location for the instance
  /// @param instanceName Name for the new instance
  /// @param storageModule The module to instantiate (ModuleOp or ExtModuleFirrtlOp)
  /// @param clk Clock signal for the instance
  /// @param rst Reset signal for the instance
  /// @param builder OpBuilder positioned at insertion point
  /// @return The created instance operation
  InstanceOp createStorageInstance(Location loc, StringRef instanceName,
                                   Cmt2ModuleLike storageModule, Value clk,
                                   Value rst, OpBuilder &builder);

  /// Create a call to a storage instance method.
  /// @param loc Source location for the call
  /// @param instance The storage instance to call
  /// @param methodName The method to call ("read", "write", "valid", etc.)
  /// @param args Arguments to the method
  /// @param builder OpBuilder positioned at insertion point
  /// @return The result values from the call
  SmallVector<Value> createStorageCall(Location loc, InstanceOp instance,
                                       StringRef methodName, ValueRange args,
                                       OpBuilder &builder);

  /// Get the circuit this generator is working with.
  CircuitOp getCircuit() const { return circuit_; }

  /// Check if the generator has been initialized with ModuleLibrary.
  bool isInitialized() const { return initialized_; }

private:
  /// Create an ExtModuleFirrtlOp for a Reg module from the library.
  ExtModuleFirrtlOp createRegExtModule(unsigned dataWidth);

  /// Create a ShiftReg CMT2 module from Reg instances.
  cmt2::ModuleOp createShiftRegModule(StringRef name, unsigned dataWidth,
                                      unsigned depth);

  /// Create a FIFO module.
  Cmt2ModuleLike createFIFOModule(StringRef name, unsigned dataWidth,
                                  unsigned depth);

  /// Look up an external module by name in the circuit.
  ExtModuleFirrtlOp findExtModule(StringRef name);

  /// Look up a CMT2 module by name in the circuit.
  cmt2::ModuleOp findModule(StringRef name);

  /// The circuit being modified.
  CircuitOp circuit_;

  /// Whether the generator has been initialized.
  bool initialized_ = false;

  /// Cache of created ExtModuleFirrtlOp by name.
  llvm::StringMap<ExtModuleFirrtlOp> extModuleCache_;

  /// Cache of created ModuleOp by name.
  llvm::StringMap<cmt2::ModuleOp> moduleCache_;
};

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_TRANSFORMS_MODULEGENERATOR_H
