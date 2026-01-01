//===- Diagnostics.cpp - CMT2 Diagnostic Utilities --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements diagnostic utilities for the CMT2 dialect.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/Diagnostics.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FormatVariadic.h"

#define DEBUG_TYPE "cmt2-diagnostics"

using namespace circt;
using namespace cmt2;
using namespace mlir;

//===----------------------------------------------------------------------===//
// Location Utilities
//===----------------------------------------------------------------------===//

/// Check if a filename indicates a Python source file.
static bool isPythonFile(StringRef filename) {
  return filename.ends_with(".py") || filename.ends_with(".pyw");
}

/// Format a single location as a string.
static std::string formatSingleLocation(Location loc) {
  std::string result;
  llvm::raw_string_ostream os(result);

  if (auto fileLoc = dyn_cast<FileLineColLoc>(loc)) {
    os << fileLoc.getFilename().getValue() << ":" << fileLoc.getLine();
    if (fileLoc.getColumn() > 0)
      os << ":" << fileLoc.getColumn();
  } else if (auto nameLoc = dyn_cast<NameLoc>(loc)) {
    os << nameLoc.getName().getValue();
    Location childLoc = nameLoc.getChildLoc();
    if (!isa<UnknownLoc>(childLoc))
      os << " at " << formatSingleLocation(childLoc);
  } else if (auto callSiteLoc = dyn_cast<CallSiteLoc>(loc)) {
    os << formatSingleLocation(callSiteLoc.getCallee());
    os << " called from " << formatSingleLocation(callSiteLoc.getCaller());
  } else if (isa<UnknownLoc>(loc)) {
    os << "<unknown>";
  } else {
    os << "<location>";
  }

  return result;
}

Location circt::cmt2::extractPythonLocation(Location loc) {
  // Check if this is a fused location
  if (auto fusedLoc = dyn_cast<FusedLoc>(loc)) {
    // Look for a Python file location in the fused locations
    for (Location subLoc : fusedLoc.getLocations()) {
      if (auto fileLoc = dyn_cast<FileLineColLoc>(subLoc)) {
        if (isPythonFile(fileLoc.getFilename().getValue()))
          return subLoc;
      }
      // Recursively check nested fused locations
      Location nested = extractPythonLocation(subLoc);
      if (nested != subLoc)
        return nested;
    }
  }

  // Check if this is directly a Python file location
  if (auto fileLoc = dyn_cast<FileLineColLoc>(loc)) {
    if (isPythonFile(fileLoc.getFilename().getValue()))
      return loc;
  }

  // Return original location if no Python source found
  return loc;
}

std::string circt::cmt2::formatLocationChain(Location loc) {
  std::string result;
  llvm::raw_string_ostream os(result);

  if (auto fusedLoc = dyn_cast<FusedLoc>(loc)) {
    os << "Location chain:\n";
    for (auto [i, subLoc] : llvm::enumerate(fusedLoc.getLocations())) {
      os << "  [" << i << "] " << formatSingleLocation(subLoc) << "\n";
    }
  } else {
    os << formatSingleLocation(loc);
  }

  return result;
}

bool circt::cmt2::hasPythonSourceInfo(Location loc) {
  if (auto fusedLoc = dyn_cast<FusedLoc>(loc)) {
    for (Location subLoc : fusedLoc.getLocations()) {
      if (hasPythonSourceInfo(subLoc))
        return true;
    }
    return false;
  }

  if (auto fileLoc = dyn_cast<FileLineColLoc>(loc)) {
    return isPythonFile(fileLoc.getFilename().getValue());
  }

  return false;
}

Location circt::cmt2::fusedLocation(MLIRContext *ctx,
                                     ArrayRef<Location> locs) {
  if (locs.empty())
    return UnknownLoc::get(ctx);
  if (locs.size() == 1)
    return locs[0];
  return FusedLoc::get(ctx, locs);
}

//===----------------------------------------------------------------------===//
// Cmt2Diagnostic Implementation
//===----------------------------------------------------------------------===//

Cmt2Diagnostic::Cmt2Diagnostic(DiagnosticLevel level, Location loc,
                               StringRef message)
    : level_(level), loc_(loc), message_(message.str()) {}

Cmt2Diagnostic Cmt2Diagnostic::error(Operation *op, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Error, op->getLoc(), message);
}

Cmt2Diagnostic Cmt2Diagnostic::error(Location loc, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Error, loc, message);
}

Cmt2Diagnostic Cmt2Diagnostic::warning(Operation *op, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Warning, op->getLoc(), message);
}

Cmt2Diagnostic Cmt2Diagnostic::warning(Location loc, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Warning, loc, message);
}

Cmt2Diagnostic Cmt2Diagnostic::info(Operation *op, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Info, op->getLoc(), message);
}

Cmt2Diagnostic Cmt2Diagnostic::info(Location loc, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Info, loc, message);
}

Cmt2Diagnostic Cmt2Diagnostic::debug(Operation *op, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Debug, op->getLoc(), message);
}

Cmt2Diagnostic Cmt2Diagnostic::debug(Location loc, StringRef message) {
  return Cmt2Diagnostic(DiagnosticLevel::Debug, loc, message);
}

Cmt2Diagnostic &Cmt2Diagnostic::note(StringRef message) {
  notes_.push_back({UnknownLoc::get(loc_.getContext()), message.str()});
  return *this;
}

Cmt2Diagnostic &Cmt2Diagnostic::note(Location loc, StringRef message) {
  notes_.push_back({loc, message.str()});
  return *this;
}

Cmt2Diagnostic &Cmt2Diagnostic::note(Operation *op, StringRef message) {
  notes_.push_back({op->getLoc(), message.str()});
  return *this;
}

Cmt2Diagnostic &Cmt2Diagnostic::hint(StringRef message) {
  hint_ = message.str();
  return *this;
}

Cmt2Diagnostic &Cmt2Diagnostic::withPythonSource() {
  includePythonSource_ = true;
  return *this;
}

Cmt2Diagnostic &Cmt2Diagnostic::withLocationChain() {
  includeLocationChain_ = true;
  return *this;
}

/// Helper to emit the diagnostic with all notes and hints.
static void emitDiagnosticWithNotes(
    InFlightDiagnostic &diag, DiagnosticLevel level, Location loc,
    bool includePythonSource, bool includeLocationChain,
    ArrayRef<std::pair<Location, std::string>> notes, StringRef hint) {
  // Add Python source context if requested
  if (includePythonSource && hasPythonSourceInfo(loc)) {
    Location pythonLoc = extractPythonLocation(loc);
    if (pythonLoc != loc) {
      diag.attachNote(pythonLoc) << "defined in Python source";
    }
  }

  // Add location chain if requested
  if (includeLocationChain) {
    diag.attachNote() << formatLocationChain(loc);
  }

  // Add all notes
  for (const auto &[noteLoc, noteMsg] : notes) {
    if (isa<UnknownLoc>(noteLoc)) {
      diag.attachNote() << noteMsg;
    } else {
      diag.attachNote(noteLoc) << noteMsg;
    }
  }

  // Add hint if provided
  if (!hint.empty()) {
    diag.attachNote() << "hint: " << hint;
  }
}

LogicalResult Cmt2Diagnostic::emit() {
  // Debug diagnostics only emit when LLVM_DEBUG is enabled
  if (level_ == DiagnosticLevel::Debug) {
    LLVM_DEBUG({
      llvm::dbgs() << "[CMT2 DEBUG] " << formatSingleLocation(loc_) << ": "
                   << message_ << "\n";
      for (const auto &[noteLoc, noteMsg] : notes_) {
        llvm::dbgs() << "  note: " << noteMsg << "\n";
      }
    });
    return success();
  }

  // Create and emit the appropriate MLIR diagnostic
  switch (level_) {
  case DiagnosticLevel::Error: {
    InFlightDiagnostic diag = emitError(loc_, message_);
    emitDiagnosticWithNotes(diag, level_, loc_, includePythonSource_,
                            includeLocationChain_, notes_, hint_);
    return failure();
  }
  case DiagnosticLevel::Warning: {
    InFlightDiagnostic diag = emitWarning(loc_, message_);
    emitDiagnosticWithNotes(diag, level_, loc_, includePythonSource_,
                            includeLocationChain_, notes_, hint_);
    return success();
  }
  case DiagnosticLevel::Info: {
    InFlightDiagnostic diag = emitRemark(loc_, message_);
    emitDiagnosticWithNotes(diag, level_, loc_, includePythonSource_,
                            includeLocationChain_, notes_, hint_);
    return success();
  }
  default:
    llvm_unreachable("Unhandled diagnostic level");
  }
}

//===----------------------------------------------------------------------===//
// Conversion Error Reporting
//===----------------------------------------------------------------------===//

Cmt2Diagnostic circt::cmt2::reportConversionError(Operation *op,
                                                   StringRef message) {
  return Cmt2Diagnostic::error(op, message).withPythonSource();
}

Cmt2Diagnostic circt::cmt2::reportTypeMismatch(Operation *op, Type expected,
                                                Type actual, StringRef context) {
  std::string msg;
  llvm::raw_string_ostream os(msg);
  os << "type mismatch";
  if (!context.empty())
    os << " in " << context;
  os << ": expected " << expected << ", got " << actual;

  return Cmt2Diagnostic::error(op, msg).withPythonSource();
}

Cmt2Diagnostic circt::cmt2::reportMissingDefinition(Operation *op,
                                                     StringRef kind,
                                                     StringRef name,
                                                     StringRef container) {
  std::string msg;
  llvm::raw_string_ostream os(msg);
  os << kind << " '" << name << "' not found";
  if (!container.empty())
    os << " in " << container;

  return Cmt2Diagnostic::error(op, msg).withPythonSource();
}

Cmt2Diagnostic circt::cmt2::reportSchedulingConflict(Operation *op,
                                                      StringRef method1,
                                                      StringRef method2,
                                                      StringRef reason) {
  std::string msg;
  llvm::raw_string_ostream os(msg);
  os << "scheduling conflict between '" << method1 << "' and '" << method2
     << "'";
  if (!reason.empty())
    os << ": " << reason;

  return Cmt2Diagnostic::warning(op, msg).withPythonSource();
}

//===----------------------------------------------------------------------===//
// Cmt2DiagnosticHandler Implementation
//===----------------------------------------------------------------------===//

Cmt2DiagnosticHandler::Cmt2DiagnosticHandler(MLIRContext *ctx) : ctx_(ctx) {
  // Register a diagnostic handler that tracks statistics
  handlerId_ = ctx_->getDiagEngine().registerHandler(
      [this](Diagnostic &diag) -> LogicalResult {
        // Track statistics
        switch (diag.getSeverity()) {
        case DiagnosticSeverity::Error:
          stats_.errors++;
          break;
        case DiagnosticSeverity::Warning:
          stats_.warnings++;
          break;
        case DiagnosticSeverity::Note:
          // Notes are attached to other diagnostics, don't count separately
          break;
        case DiagnosticSeverity::Remark:
          stats_.infos++;
          break;
        }

        // Don't consume the diagnostic - let other handlers process it
        return failure();
      });
}

Cmt2DiagnosticHandler::~Cmt2DiagnosticHandler() {
  ctx_->getDiagEngine().eraseHandler(handlerId_);
}

//===----------------------------------------------------------------------===//
// Debug Logging Utilities
//===----------------------------------------------------------------------===//

void circt::cmt2::debugLog(Operation *op, StringRef message) {
  debugLog(op->getLoc(), message);
}

void circt::cmt2::debugLog(Location loc, StringRef message) {
  LLVM_DEBUG({
    llvm::dbgs() << "[CMT2] " << formatSingleLocation(loc) << ": " << message
                 << "\n";
  });
}

void circt::cmt2::infoLog(Operation *op, StringRef message) {
  infoLog(op->getLoc(), message);
}

void circt::cmt2::infoLog(Location loc, StringRef message) {
  emitRemark(loc, message);
}
