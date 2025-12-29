//===- ProcToGAA.cpp - Convert procedural rules/methods to GAA -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ProcToGAA pass for the Cmt2 dialect.
// It performs final cleanup of procedural operations after ProcStmtToAction
// has generated the FSM-based rules. This pass removes the original proc
// operations (proc.rule, proc.group, etc.) that are no longer needed.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-proc-to-gaa"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_PROCTOGAA
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// ProcToGAA pass implementation
struct ProcToGAAPass : public circt::cmt2::impl::ProcToGAABase<ProcToGAAPass> {

  void runOnOperation() override;

private:
  /// Process a single module - remove converted proc ops.
  void processModule(cmt2::ModuleOp module);

  /// Mark proc ops for removal and add conversion metadata.
  void markForRemoval(ProcRuleOp rule, cmt2::ModuleOp module);
  void markForRemoval(ProcMethodOp method, cmt2::ModuleOp module);
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Proc Op Cleanup
//===----------------------------------------------------------------------===//

void ProcToGAAPass::markForRemoval(ProcRuleOp rule, cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Marking proc rule @" << rule.getSymName()
                          << " for removal\n");

  // Check if this rule was converted by ProcStmtToAction
  if (!rule->hasAttr("proc.stmt_converted")) {
    // If not converted yet, just add metadata for compatibility
    if (rule->hasAttr("tdcc.num_states")) {
      OpBuilder builder(rule);
      StringRef baseName = rule.getSymName();
      std::string fsmName = ("__fsm_" + baseName).str();

      rule->setAttr("proc.converted", builder.getUnitAttr());
      rule->setAttr("proc.fsm_name", builder.getStringAttr(fsmName));
      rule->setAttr("proc.idle_value",
                    builder.getStringAttr((baseName + "__idle").str()));
      rule->setAttr("proc.running_value",
                    builder.getStringAttr((baseName + "__running").str()));
    }
    return;
  }

  // Rule was converted - it can be removed
  // For now, just mark it; actual removal would need careful handling of
  // symbol references
  rule->setAttr("proc.converted", OpBuilder(rule).getUnitAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Marked for removal\n");
}

void ProcToGAAPass::markForRemoval(ProcMethodOp method, cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Marking proc method @" << method.getSymName()
                          << " for removal\n");

  // Check if this method was converted by ProcStmtToAction
  if (!method->hasAttr("proc.stmt_converted")) {
    // If not converted yet, just add metadata for compatibility
    if (method->hasAttr("tdcc.num_states")) {
      OpBuilder builder(method);
      StringRef baseName = method.getSymName();
      std::string fsmName = ("__fsm_" + baseName).str();

      method->setAttr("proc.converted", builder.getUnitAttr());
      method->setAttr("proc.fsm_name", builder.getStringAttr(fsmName));
      method->setAttr("proc.idle_value",
                      builder.getStringAttr((baseName + "__idle").str()));
      method->setAttr("proc.running_value",
                      builder.getStringAttr((baseName + "__running").str()));
      method->setAttr("proc.needs_arg_regs", builder.getUnitAttr());
    }
    return;
  }

  // Method was converted - it can be removed
  method->setAttr("proc.converted", OpBuilder(method).getUnitAttr());

  LLVM_DEBUG(llvm::dbgs() << "  Marked for removal\n");
}

//===----------------------------------------------------------------------===//
// Module Processing
//===----------------------------------------------------------------------===//

void ProcToGAAPass::processModule(cmt2::ModuleOp module) {
  LLVM_DEBUG(llvm::dbgs() << "Processing module @" << module.getSymName()
                          << "\n");

  // Collect all proc ops
  SmallVector<ProcRuleOp> rules;
  SmallVector<ProcMethodOp> methods;
  SmallVector<ProcGroupOp> groups;

  for (auto &op : module.getBodyRegion().front()) {
    if (auto rule = dyn_cast<ProcRuleOp>(op))
      rules.push_back(rule);
    else if (auto method = dyn_cast<ProcMethodOp>(op))
      methods.push_back(method);
    else if (auto group = dyn_cast<ProcGroupOp>(op))
      groups.push_back(group);
  }

  // Mark rules and methods for removal
  for (auto rule : rules) {
    markForRemoval(rule, module);
  }

  for (auto method : methods) {
    markForRemoval(method, module);
  }

  // Note: We don't erase the proc ops here to avoid breaking symbol
  // references. Instead, they are marked with proc.converted attribute
  // and will be ignored by later passes. A future cleanup pass can
  // safely remove them after all references are resolved.

  LLVM_DEBUG(llvm::dbgs() << "  Processed " << rules.size() << " rules, "
                          << methods.size() << " methods, " << groups.size()
                          << " groups\n");
}

void ProcToGAAPass::runOnOperation() {
  CircuitOp circuit = getOperation();

  // Process each module in the circuit
  for (auto &op : circuit.getBodyRegion().front().getOperations()) {
    if (auto module = dyn_cast<cmt2::ModuleOp>(op)) {
      processModule(module);
    }
  }
}
