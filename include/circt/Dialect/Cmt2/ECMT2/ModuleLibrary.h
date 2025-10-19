//===- ModuleLibrary.h - ECMT2 Module Library ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleLibrary singleton for managing reusable FIRRTL
// modules in ECMT2.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_ECMT2_MODULELIBRARY_H
#define CIRCT_DIALECT_CMT2_ECMT2_MODULELIBRARY_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/LogicalResult.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace circt {
namespace cmt2 {
namespace ecmt2 {

/// Module library manager - singleton pattern
class ModuleLibrary {
public:
  static ModuleLibrary &getInstance();

  /// Initialize library from manifest
  mlir::LogicalResult loadManifest(llvm::StringRef path);

  /// Query if a module exists
  bool hasModule(llvm::StringRef name) const;

  /// Module metadata structure
  struct ModuleInfo {
    std::string name;
    std::string description;
    enum Type { Static, Chisel } type;
    std::string path;

    // Port information (port name -> type string)
    llvm::StringMap<std::string> ports;

    // Method/Value information
    struct MethodInfo {
      std::string name;
      bool hasReady;
      bool hasEnable;
      std::vector<std::string> inputs;  // "name: type" strings
      std::vector<std::string> outputs; // "name: type" strings
    };
    std::vector<MethodInfo> methods;

    // Conflict matrix
    struct ConflictEntry {
      std::string func1, func2;
      enum Relation { Conflict, ConflictFree, SequentialBefore } relation;
    };
    std::vector<ConflictEntry> conflictMatrix;

    // Parameters (for Chisel modules)
    struct ParamInfo {
      std::string name;
      std::string type;
      std::optional<int64_t> defaultValue;
      bool required;
    };
    std::vector<ParamInfo> parameters;

    // Build configuration (for Chisel modules)
    struct BuildInfo {
      std::string command;
      std::vector<std::string> args;
      std::string outputPattern;
      bool useCache;
    };
    std::optional<BuildInfo> buildInfo;
  };

  std::optional<ModuleInfo> getModuleInfo(llvm::StringRef name) const;

  /// Load/generate FIRRTL module MLIR
  /// For static modules: loads from file
  /// For Chisel modules: builds with parameters (cached)
  mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
  loadModule(llvm::StringRef name,
             const llvm::StringMap<int64_t> &params,
             mlir::MLIRContext &context);

  /// Insert module into circuit
  /// This is called automatically by Circuit::addExternalModule
  /// Returns the actual FIRRTL module name (with parameters) via actualModuleName
  mlir::LogicalResult insertModuleIntoCircuit(llvm::StringRef name,
                                              const llvm::StringMap<int64_t> &params,
                                              mlir::OpBuilder &builder,
                                              mlir::Location loc,
                                              std::string &actualModuleName);

  /// Get the actual FIRRTL module name that would be generated (without building)
  /// This is used for deduplication - to check if a module already exists
  mlir::LogicalResult getActualModuleName(llvm::StringRef name,
                                          const llvm::StringMap<int64_t> &params,
                                          std::string &actualModuleName) const;

  /// Set library base path (for testing)
  void setLibraryPath(llvm::StringRef path) { libraryBasePath_ = path.str(); }

private:
  ModuleLibrary() = default;

  // Module catalog
  llvm::StringMap<ModuleInfo> modules_;

  // Library base path
  std::string libraryBasePath_;

  // Cache for generated modules
  struct CacheKey {
    std::string moduleName;
    std::map<std::string, int64_t> params;
    bool operator<(const CacheKey &other) const;
  };
  std::map<CacheKey, std::string> cache_; // CacheKey -> cached file path

  // Helper methods
  mlir::LogicalResult loadStaticModule(const ModuleInfo &info,
                                       mlir::MLIRContext &context,
                                       mlir::OwningOpRef<mlir::ModuleOp> &result);

  mlir::LogicalResult buildChiselModule(const ModuleInfo &info,
                                        const llvm::StringMap<int64_t> &params,
                                        mlir::MLIRContext &context,
                                        mlir::OwningOpRef<mlir::ModuleOp> &result);

  std::string getCachePath(const std::string &moduleName,
                          const llvm::StringMap<int64_t> &params) const;

  std::string substituteParams(const std::string &pattern,
                               const llvm::StringMap<int64_t> &params) const;
};

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_ECMT2_MODULELIBRARY_H
