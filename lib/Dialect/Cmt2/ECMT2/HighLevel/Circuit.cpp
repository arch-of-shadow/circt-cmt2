//===- Circuit.cpp - High-Level Circuit Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/HighLevel/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/Cmt2/ECMT2/ModuleLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace circt::cmt2::ecmt2::highlevel;

Circuit::Circuit(llvm::StringRef topModule, mlir::MLIRContext &context)
    : context_(context), topModule_(topModule.str()) {
  // Create low-level circuit
  lowLevelCircuit_ = std::make_unique<circt::cmt2::ecmt2::Circuit>(topModule, context);

  // Initialize module library
  auto &library = circt::cmt2::ecmt2::ModuleLibrary::getInstance();

  // Try to find the module library manifest
  // First try: relative to build directory
  llvm::SmallString<256> manifestPath;
  llvm::sys::path::append(manifestPath, "lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml");

  if (!llvm::sys::fs::exists(manifestPath)) {
    // Second try: relative to executable directory (for installed binaries)
    std::string exePath = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
    if (!exePath.empty()) {
      manifestPath = llvm::sys::path::parent_path(exePath);
      llvm::sys::path::append(manifestPath, "../lib/Dialect/Cmt2/ModuleLibrary/manifest.yaml");
    }
  }

  // Set library base path (parent of manifest)
  if (llvm::sys::fs::exists(manifestPath)) {
    llvm::SmallString<256> libraryPath = llvm::sys::path::parent_path(manifestPath);
    library.setLibraryPath(libraryPath);

    // Load the manifest
    if (mlir::failed(library.loadManifest(manifestPath))) {
      llvm::errs() << "Warning: Failed to load module library manifest from "
                   << manifestPath << "\n";
    }
  } else {
    llvm::errs() << "Warning: Module library manifest not found. "
                 << "External FIRRTL modules may not work correctly.\n";
  }
}

Circuit::~Circuit() = default;

circt::cmt2::ecmt2::Interface *Circuit::addInterface(llvm::StringRef name) {
  return lowLevelCircuit_->addInterface(name);
}

circt::cmt2::ecmt2::ExternalModule *
Circuit::addExternalModule(llvm::StringRef firrtlModule, llvm::StringRef name) {
  return lowLevelCircuit_->addExternalModule(firrtlModule, name);
}

circt::cmt2::ecmt2::ExternalModule *
Circuit::addExternalModule(llvm::StringRef firrtlModule,
                           const llvm::StringMap<int64_t> &params,
                           llvm::StringRef name) {
  return lowLevelCircuit_->addExternalModule(firrtlModule, params, name);
}

std::string Circuit::emitMLIRString() {
  return lowLevelCircuit_->emitMLIRString();
}

mlir::LogicalResult Circuit::runCmt2ToFIRRTLPipeline() {
  return lowLevelCircuit_->runCmt2ToFIRRTLPipeline();
}

std::string Circuit::emitFIRRTL() {
  return lowLevelCircuit_->emitFIRRTL();
}
