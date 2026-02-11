//===- ModuleInterpreter.cpp - External Module Interpreters -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the built-in module interpreters for CMT2.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Interpreter/ModuleInterpreter.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-module-interp"

using namespace circt;
using namespace cmt2;
using namespace interp;

//===----------------------------------------------------------------------===//
// Helper functions
//===----------------------------------------------------------------------===//

static unsigned getIntParam(llvm::ArrayRef<mlir::NamedAttribute> params,
                            llvm::StringRef name, unsigned defaultValue) {
  for (const auto &attr : params) {
    if (attr.getName().strref() == name) {
      if (auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(attr.getValue()))
        return intAttr.getInt();
    }
  }
  return defaultValue;
}

/// Helper to convert APInt to a string for JSON serialization.
static std::string apIntToString(const llvm::APInt &val, unsigned radix = 10,
                                 bool isSigned = false) {
  llvm::SmallString<32> str;
  val.toString(str, radix, isSigned);
  return std::string(str.str());
}

//===----------------------------------------------------------------------===//
// RegInterpreter Implementation
//===----------------------------------------------------------------------===//

void RegInterpreter::initializeInstance(
    llvm::StringRef instanceName, llvm::ArrayRef<mlir::NamedAttribute> params) {
  RegState state;
  state.width = getIntParam(params, "width", 32);
  state.value = llvm::APInt(state.width, 0);
  state.resetValue = llvm::APInt(state.width, getIntParam(params, "init", 0));
  state.pendingWrite = std::nullopt;

  instances_[instanceName] = std::move(state);
  LLVM_DEBUG(llvm::dbgs() << "RegInterpreter: initialized instance '"
                          << instanceName << "' width=" << state.width << "\n");
}

void RegInterpreter::resetInstance(llvm::StringRef instanceName) {
  auto it = instances_.find(instanceName);
  if (it != instances_.end()) {
    it->second.value = it->second.resetValue;
    it->second.pendingWrite = std::nullopt;
    LLVM_DEBUG(llvm::dbgs() << "RegInterpreter: reset instance '"
                            << instanceName << "'\n");
  }
}

void RegInterpreter::commitCycle(llvm::StringRef instanceName) {
  auto it = instances_.find(instanceName);
  if (it != instances_.end() && it->second.pendingWrite) {
    it->second.value = *it->second.pendingWrite;
    it->second.pendingWrite = std::nullopt;
    LLVM_DEBUG(llvm::dbgs() << "RegInterpreter: committed write to '"
                            << instanceName << "'\n");
  }
}

bool RegInterpreter::checkMethodGuard(llvm::StringRef instanceName,
                                      llvm::StringRef methodName,
                                      llvm::ArrayRef<InterpValue> args) {
  auto it = instances_.find(instanceName);
  if (it == instances_.end())
    return false;

  // read is always ready
  if (methodName == "read")
    return true;

  // write is ready when no pending write (one write per cycle)
  if (methodName == "write")
    return !it->second.pendingWrite.has_value();

  return false;
}

std::optional<std::vector<InterpValue>>
RegInterpreter::callMethodBody(llvm::StringRef instanceName,
                               llvm::StringRef methodName,
                               llvm::ArrayRef<InterpValue> args) {
  auto it = instances_.find(instanceName);
  if (it == instances_.end())
    return std::nullopt;

  if (methodName == "read") {
    LLVM_DEBUG(llvm::dbgs() << "RegInterpreter: read from '" << instanceName
                            << "' = " << it->second.value << "\n");
    return std::vector<InterpValue>{it->second.value};
  }

  if (methodName == "write" && !args.empty()) {
    // Queue write for end of cycle
    InterpValue writeVal = args[0];
    // Truncate or extend to match register width
    if (writeVal.getBitWidth() > it->second.width)
      writeVal = writeVal.trunc(it->second.width);
    else if (writeVal.getBitWidth() < it->second.width)
      writeVal = writeVal.zext(it->second.width);

    it->second.pendingWrite = writeVal;
    LLVM_DEBUG(llvm::dbgs() << "RegInterpreter: queued write to '"
                            << instanceName << "' = " << writeVal << "\n");
    return std::vector<InterpValue>{};
  }

  return std::nullopt;
}

llvm::json::Value
RegInterpreter::getInstanceState(llvm::StringRef instanceName) const {
  auto it = instances_.find(instanceName);
  if (it == instances_.end())
    return llvm::json::Object{{"error", "instance not found"}};

  llvm::json::Object obj;
  obj["type"] = "Reg";
  obj["width"] = static_cast<int64_t>(it->second.width);
  obj["value"] = apIntToString(it->second.value);
  obj["resetValue"] = apIntToString(it->second.resetValue);
  obj["hasPendingWrite"] = it->second.pendingWrite.has_value();
  if (it->second.pendingWrite)
    obj["pendingWrite"] = apIntToString(*it->second.pendingWrite);
  return obj;
}

std::vector<std::string> RegInterpreter::getInstanceNames() const {
  std::vector<std::string> names;
  for (const auto &entry : instances_)
    names.push_back(entry.first().str());
  return names;
}

bool RegInterpreter::hasInstance(llvm::StringRef instanceName) const {
  return instances_.count(instanceName) > 0;
}

std::optional<InterpValue>
RegInterpreter::readRegister(llvm::StringRef instanceName) const {
  auto it = instances_.find(instanceName);
  if (it != instances_.end())
    return it->second.value;
  return std::nullopt;
}

void RegInterpreter::writeRegister(llvm::StringRef instanceName,
                                   const InterpValue &value) {
  auto it = instances_.find(instanceName);
  if (it != instances_.end())
    it->second.pendingWrite = value;
}

//===----------------------------------------------------------------------===//
// WireInterpreter Implementation
//===----------------------------------------------------------------------===//

void WireInterpreter::initializeInstance(
    llvm::StringRef instanceName, llvm::ArrayRef<mlir::NamedAttribute> params) {
  WireState state;
  state.width = getIntParam(params, "width", 32);
  state.value = llvm::APInt(state.width, 0);

  instances_[instanceName] = std::move(state);
  LLVM_DEBUG(llvm::dbgs() << "WireInterpreter: initialized instance '"
                          << instanceName << "' width=" << state.width << "\n");
}

void WireInterpreter::resetInstance(llvm::StringRef instanceName) {
  auto it = instances_.find(instanceName);
  if (it != instances_.end()) {
    it->second.value = llvm::APInt(it->second.width, 0);
    LLVM_DEBUG(llvm::dbgs() << "WireInterpreter: reset instance '"
                            << instanceName << "'\n");
  }
}

void WireInterpreter::commitCycle(llvm::StringRef instanceName) {
  // Wire has no pending state - writes are immediate
  // Reset to 0 at end of cycle (wire holds value only within cycle)
  auto it = instances_.find(instanceName);
  if (it != instances_.end()) {
    it->second.value = llvm::APInt(it->second.width, 0);
  }
}

bool WireInterpreter::checkMethodGuard(llvm::StringRef instanceName,
                                       llvm::StringRef methodName,
                                       llvm::ArrayRef<InterpValue> args) {
  // Wire read/write are always ready
  return instances_.count(instanceName) > 0;
}

std::optional<std::vector<InterpValue>>
WireInterpreter::callMethodBody(llvm::StringRef instanceName,
                                llvm::StringRef methodName,
                                llvm::ArrayRef<InterpValue> args) {
  auto it = instances_.find(instanceName);
  if (it == instances_.end())
    return std::nullopt;

  if (methodName == "read") {
    LLVM_DEBUG(llvm::dbgs() << "WireInterpreter: read from '" << instanceName
                            << "' = " << it->second.value << "\n");
    return std::vector<InterpValue>{it->second.value};
  }

  if (methodName == "write" && !args.empty()) {
    // Immediate write (visible to subsequent reads in same cycle)
    InterpValue writeVal = args[0];
    // Truncate or extend to match wire width
    if (writeVal.getBitWidth() > it->second.width)
      writeVal = writeVal.trunc(it->second.width);
    else if (writeVal.getBitWidth() < it->second.width)
      writeVal = writeVal.zext(it->second.width);

    it->second.value = writeVal;
    LLVM_DEBUG(llvm::dbgs() << "WireInterpreter: write to '" << instanceName
                            << "' = " << writeVal << "\n");
    return std::vector<InterpValue>{};
  }

  return std::nullopt;
}

llvm::json::Value
WireInterpreter::getInstanceState(llvm::StringRef instanceName) const {
  auto it = instances_.find(instanceName);
  if (it == instances_.end())
    return llvm::json::Object{{"error", "instance not found"}};

  llvm::json::Object obj;
  obj["type"] = "Wire";
  obj["width"] = static_cast<int64_t>(it->second.width);
  obj["value"] = apIntToString(it->second.value);
  return obj;
}

std::vector<std::string> WireInterpreter::getInstanceNames() const {
  std::vector<std::string> names;
  for (const auto &entry : instances_)
    names.push_back(entry.first().str());
  return names;
}

bool WireInterpreter::hasInstance(llvm::StringRef instanceName) const {
  return instances_.count(instanceName) > 0;
}

// MemoryInterpreter implementation removed - use MLIR-based behavioral models
// See docs/Cmt2/features/Interpreter.md for details
