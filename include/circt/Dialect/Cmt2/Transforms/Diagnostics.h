//===- Diagnostics.h - CMT2 Diagnostic Utilities ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines diagnostic utilities for the CMT2 dialect that provide
// multi-level reporting (error, warning, info, debug) with complete source
// location tracking through Python and MLIR locations.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_CMT2_TRANSFORMS_DIAGNOSTICS_H
#define CIRCT_DIALECT_CMT2_TRANSFORMS_DIAGNOSTICS_H

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace circt {
namespace cmt2 {

//===----------------------------------------------------------------------===//
// Diagnostic Levels
//===----------------------------------------------------------------------===//

/// Diagnostic severity levels for CMT2.
enum class DiagnosticLevel {
  Error,   // Fatal errors that prevent compilation
  Warning, // Non-fatal issues that may affect correctness
  Info,    // Informational messages about compilation progress
  Debug    // Detailed debugging information
};

//===----------------------------------------------------------------------===//
// Location Utilities
//===----------------------------------------------------------------------===//

/// Extract Python source location from a fused location.
/// Returns the FileLineColLoc component if found, otherwise returns the
/// original location.
mlir::Location extractPythonLocation(mlir::Location loc);

/// Get a formatted string representation of a location chain.
/// For fused locations, shows all component locations.
std::string formatLocationChain(mlir::Location loc);

/// Check if a location contains Python source information.
/// Returns true if the location or any fused component is a FileLineColLoc
/// with a .py file extension.
bool hasPythonSourceInfo(mlir::Location loc);

/// Create a fused location combining multiple locations.
/// Useful for tracking transformations through passes.
mlir::Location fusedLocation(mlir::MLIRContext *ctx,
                              llvm::ArrayRef<mlir::Location> locs);

//===----------------------------------------------------------------------===//
// Diagnostic Builder
//===----------------------------------------------------------------------===//

/// A builder class for constructing rich CMT2 diagnostics with source tracking.
///
/// Example usage:
/// ```cpp
/// Cmt2Diagnostic::error(op, "method conflict detected")
///     .note(method1Loc, "first conflicting method")
///     .note(method2Loc, "second conflicting method")
///     .hint("consider using explicit scheduling")
///     .emit();
/// ```
class Cmt2Diagnostic {
public:
  /// Create an error diagnostic.
  static Cmt2Diagnostic error(mlir::Operation *op, llvm::StringRef message);
  static Cmt2Diagnostic error(mlir::Location loc, llvm::StringRef message);

  /// Create a warning diagnostic.
  static Cmt2Diagnostic warning(mlir::Operation *op, llvm::StringRef message);
  static Cmt2Diagnostic warning(mlir::Location loc, llvm::StringRef message);

  /// Create an info diagnostic (remark).
  static Cmt2Diagnostic info(mlir::Operation *op, llvm::StringRef message);
  static Cmt2Diagnostic info(mlir::Location loc, llvm::StringRef message);

  /// Create a debug diagnostic (only emitted when debug mode is enabled).
  static Cmt2Diagnostic debug(mlir::Operation *op, llvm::StringRef message);
  static Cmt2Diagnostic debug(mlir::Location loc, llvm::StringRef message);

  /// Add a note with optional location.
  Cmt2Diagnostic &note(llvm::StringRef message);
  Cmt2Diagnostic &note(mlir::Location loc, llvm::StringRef message);
  Cmt2Diagnostic &note(mlir::Operation *op, llvm::StringRef message);

  /// Add a hint for how to fix the issue.
  Cmt2Diagnostic &hint(llvm::StringRef message);

  /// Add source context showing the Python location if available.
  Cmt2Diagnostic &withPythonSource();

  /// Add the full location chain for debugging.
  Cmt2Diagnostic &withLocationChain();

  /// Emit the diagnostic and return failure for errors.
  mlir::LogicalResult emit();

  /// Check if this is an error level diagnostic.
  bool isError() const { return level_ == DiagnosticLevel::Error; }

private:
  Cmt2Diagnostic(DiagnosticLevel level, mlir::Location loc,
                 llvm::StringRef message);

  DiagnosticLevel level_;
  mlir::Location loc_;
  std::string message_;
  llvm::SmallVector<std::pair<mlir::Location, std::string>> notes_;
  std::string hint_;
  bool includePythonSource_ = false;
  bool includeLocationChain_ = false;
};

//===----------------------------------------------------------------------===//
// Conversion Error Reporting
//===----------------------------------------------------------------------===//

/// Report an error during CMT2 to FIRRTL conversion with full source trace.
///
/// This function extracts Python source locations from fused locations and
/// provides a detailed error message with:
/// - The conversion error message
/// - The Python source location where the construct was defined
/// - The MLIR location chain showing the transformation history
///
/// Example:
/// ```cpp
/// return reportConversionError(op, "unsupported operation in rule body")
///     .note(ruleLoc, "in rule defined here")
///     .emit();
/// ```
Cmt2Diagnostic reportConversionError(mlir::Operation *op,
                                      llvm::StringRef message);

/// Report a type mismatch error with expected vs actual types.
Cmt2Diagnostic reportTypeMismatch(mlir::Operation *op, mlir::Type expected,
                                   mlir::Type actual,
                                   llvm::StringRef context = "");

/// Report a missing definition error (method, value, module not found).
Cmt2Diagnostic reportMissingDefinition(mlir::Operation *op,
                                         llvm::StringRef kind,
                                         llvm::StringRef name,
                                         llvm::StringRef container = "");

/// Report a scheduling conflict between methods.
Cmt2Diagnostic reportSchedulingConflict(mlir::Operation *op,
                                          llvm::StringRef method1,
                                          llvm::StringRef method2,
                                          llvm::StringRef reason);

//===----------------------------------------------------------------------===//
// Pass Diagnostic Utilities
//===----------------------------------------------------------------------===//

/// A diagnostic handler that can be installed in passes to capture and
/// format diagnostics with Python source information.
class Cmt2DiagnosticHandler {
public:
  /// Create a handler for the given context.
  explicit Cmt2DiagnosticHandler(mlir::MLIRContext *ctx);

  /// Destructor unregisters the handler.
  ~Cmt2DiagnosticHandler();

  /// Get statistics about diagnostics emitted.
  struct Stats {
    unsigned errors = 0;
    unsigned warnings = 0;
    unsigned infos = 0;
    unsigned debugs = 0;
  };
  const Stats &getStats() const { return stats_; }

  /// Check if any errors were emitted.
  bool hadErrors() const { return stats_.errors > 0; }

private:
  mlir::MLIRContext *ctx_;
  mlir::DiagnosticEngine::HandlerID handlerId_;
  Stats stats_;
};

//===----------------------------------------------------------------------===//
// Debug Logging Utilities
//===----------------------------------------------------------------------===//

/// Log a debug message with location context.
/// These are only emitted when LLVM_DEBUG is enabled.
void debugLog(mlir::Operation *op, llvm::StringRef message);
void debugLog(mlir::Location loc, llvm::StringRef message);

/// Log an info message with location context.
void infoLog(mlir::Operation *op, llvm::StringRef message);
void infoLog(mlir::Location loc, llvm::StringRef message);

//===----------------------------------------------------------------------===//
// Diagnostic Macros
//===----------------------------------------------------------------------===//

/// Emit a CMT2 error and return failure.
#define CMT2_ERROR(op, msg)                                                    \
  return ::circt::cmt2::Cmt2Diagnostic::error(op, msg).emit()

/// Emit a CMT2 error with Python source context and return failure.
#define CMT2_ERROR_WITH_SOURCE(op, msg)                                        \
  return ::circt::cmt2::Cmt2Diagnostic::error(op, msg)                         \
      .withPythonSource()                                                      \
      .emit()

/// Emit a CMT2 warning.
#define CMT2_WARNING(op, msg)                                                  \
  ::circt::cmt2::Cmt2Diagnostic::warning(op, msg).emit()

/// Emit a CMT2 warning with Python source context.
#define CMT2_WARNING_WITH_SOURCE(op, msg)                                      \
  ::circt::cmt2::Cmt2Diagnostic::warning(op, msg).withPythonSource().emit()

/// Debug logging (only in debug builds with LLVM_DEBUG enabled).
#define CMT2_DEBUG(op, msg)                                                    \
  LLVM_DEBUG(::circt::cmt2::debugLog(op, msg))

} // namespace cmt2
} // namespace circt

#endif // CIRCT_DIALECT_CMT2_TRANSFORMS_DIAGNOSTICS_H
