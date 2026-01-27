//===- AddRuleFiringPort.cpp - Add debug ports for rule firing --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AddRuleFiringPort pass for the Cmt2 dialect.
// It adds output ports that indicate when each rule fires, useful for debugging.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-add-rule-firing-port"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_ADDRULEFIRINGPORT
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// AddRuleFiringPort Pass Implementation
//===----------------------------------------------------------------------===//

struct AddRuleFiringPortPass
    : public circt::cmt2::impl::AddRuleFiringPortBase<AddRuleFiringPortPass> {
  using AddRuleFiringPortBase::AddRuleFiringPortBase;

  void runOnOperation() override;

private:
  /// Process a single module to add firing ports for its rules.
  void processModule(cmt2::ModuleOp module);
};

} // end anonymous namespace

void AddRuleFiringPortPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  LLVM_DEBUG(llvm::dbgs() << "=== AddRuleFiringPort Pass ===\n");
  LLVM_DEBUG(llvm::dbgs() << "Prefix: " << prefix << "\n");

  // Process each module in the circuit
  for (auto module : circuit.getOps<cmt2::ModuleOp>()) {
    processModule(module);
  }
}

void AddRuleFiringPortPass::processModule(cmt2::ModuleOp module) {
  OpBuilder builder(module.getContext());

  // Collect all rules in this module
  SmallVector<RuleOp> rules;
  for (auto rule : module.getOps<RuleOp>()) {
    rules.push_back(rule);
  }

  if (rules.empty()) {
    LLVM_DEBUG(llvm::dbgs() << "Module " << module.getName()
                            << ": no rules, skipping\n");
    return;
  }

  LLVM_DEBUG(llvm::dbgs() << "Module " << module.getName() << ": "
                          << rules.size() << " rules\n");

  // Build list of firing port names
  SmallVector<std::string> firingPortNames;
  for (auto rule : rules) {
    std::string portName = prefix + rule.getSymName().str() + "_firing";
    firingPortNames.push_back(portName);

    // Add attribute to the rule indicating its firing port name
    rule->setAttr("debug.firing_port",
                  builder.getStringAttr(portName));

    LLVM_DEBUG(llvm::dbgs() << "  Rule " << rule.getSymName()
                            << " -> port " << portName << "\n");
  }

  // Add module-level attribute listing all debug firing ports
  SmallVector<Attribute> portAttrs;
  for (const auto &name : firingPortNames) {
    portAttrs.push_back(builder.getStringAttr(name));
  }
  module->setAttr("debug.firing_ports", builder.getArrayAttr(portAttrs));
}
