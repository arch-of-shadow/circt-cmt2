//===- ModuleInliner.cpp - Cmt2 module inlining pass ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// This file implements module inlining for the Cmt2 dialect.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/Transforms/CallInfo.h"
#include "circt/Dialect/Cmt2/Transforms/InstanceGraph.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cmt2-module-inliner"

namespace circt {
namespace cmt2 {
#define GEN_PASS_DEF_MODULEINLINER
#include "circt/Dialect/Cmt2/Cmt2Passes.h.inc"
} // namespace cmt2
} // namespace circt

using namespace circt;
using namespace cmt2;
using namespace mlir;

namespace {

/// Module inliner pass implementation
struct ModuleInlinerPass
    : public circt::cmt2::impl::ModuleInlinerBase<ModuleInlinerPass> {

  void runOnOperation() override;

private:
  /// Check if a module should NOT be inlined
  bool shouldNotInline(Cmt2ModuleLike module);

  /// Inline an instance into its parent module
  LogicalResult inlineInstance(InstanceOp instance);

  /// Replace calls to an instance with inlined body
  void inlineCalls(InstanceOp instance, Cmt2ModuleLike targetModule,
                   IRMapping &mapper,
                   DenseMap<StringAttr, StringAttr> &instanceNameMap,
                   DenseMap<StringAttr, StringAttr> &interfaceBindingMap);

  InstanceGraph *instanceGraph = nullptr;
  CallInfoView *callInfo = nullptr;
  CircuitOp circuit;
};

} // end anonymous namespace

bool ModuleInlinerPass::shouldNotInline(Cmt2ModuleLike module) {
  // Don't inline external modules
  if (isa<ExtModuleFirrtlOp>(module.getOperation()))
    return true;

  // Don't inline modules with synthesis=true
  if (auto synthesisAttr = module->getAttrOfType<BoolAttr>("synthesis"))
    if (synthesisAttr.getValue())
      return true;

  // Don't inline modules that have no uses (top-level modules)
  auto *node = instanceGraph->lookup(module.moduleNameAttr());
  if (node && node->noUses())
    return true;

  return false;
}

LogicalResult ModuleInlinerPass::inlineInstance(InstanceOp instance) {
  // Get the target module
  auto targetModule = instance.getReferencedModule();
  if (!targetModule) {
    instance.emitError("cannot find referenced module");
    return failure();
  }

  // Get the parent module
  auto parentModule = instance->getParentOfType<cmt2::ModuleOp>();
  if (!parentModule) {
    instance.emitError("instance not within a module");
    return failure();
  }

  LLVM_DEBUG(llvm::dbgs() << "Inlining instance: " << instance.getInstanceName()
                          << " of module " << targetModule.moduleName()
                          << " into " << parentModule.getModuleName() << "\n");

  OpBuilder builder(instance);
  IRMapping mapper;

  // Map the target module's block arguments to the instance's operands
  // This is crucial: when we inline Leaf into Top, Leaf's %clk argument
  // should map to the %clk value passed to the @leaf instance
  if (auto targetModuleOp = dyn_cast<cmt2::ModuleOp>(targetModule.getOperation())) {
    Block &targetBody = targetModuleOp.getBody().front();
    for (auto [targetArg, instanceOperand] :
         llvm::zip(targetBody.getArguments(), instance.getOperands())) {
      mapper.map(targetArg, instanceOperand);
      LLVM_DEBUG(llvm::dbgs() << "  Mapped argument to operand\n");
    }
  }

  // Build interface binding map: interfaceDecl -> interfaceDef
  // This maps the child module's interface declarations to the parent's interface definitions
  DenseMap<StringAttr, StringAttr> interfaceBindingMap;
  if (auto interfaceBindsAttr = instance.getInterfaceBinds()) {
    for (auto binding : *interfaceBindsAttr) {
      auto bindingArray = llvm::cast<mlir::ArrayAttr>(binding);
      if (bindingArray.size() >= 2) {
        // Format: [@interfaceDef, @interfaceDecl]
        auto interfaceDefRef = llvm::cast<mlir::SymbolRefAttr>(bindingArray[0]);
        auto interfaceDeclRef = llvm::cast<mlir::SymbolRefAttr>(bindingArray[1]);
        interfaceBindingMap[interfaceDeclRef.getLeafReference()] = interfaceDefRef.getLeafReference();
        LLVM_DEBUG(llvm::dbgs() << "  Interface binding: @" << interfaceDeclRef.getLeafReference().getValue()
                                << " -> @" << interfaceDefRef.getLeafReference().getValue() << "\n");
      }
    }
  }

  // Step 1: Clone subinstances from target module to parent module
  // We also need to track symbol name mappings for updating CallOps
  // Rename cloned instances with hierarchical names: parent.child
  DenseMap<StringAttr, StringAttr> instanceNameMap;
  MLIRContext *ctx = instance.getContext();
  StringAttr parentInstanceName = instance.getInstanceNameAttr();

  targetModule->walk([&](InstanceOp subInstance) {
    auto *cloned = builder.clone(*subInstance.getOperation(), mapper);
    auto clonedInstance = cast<InstanceOp>(cloned);

    // Create hierarchical name: parent.child using nested SymbolRefAttr
    // The nested name represents hierarchy [parent, child]
    StringAttr oldName = subInstance.getInstanceNameAttr();
    StringAttr newName = StringAttr::get(ctx,
        (parentInstanceName.getValue() + "." + oldName.getValue()).str());

    // Update the cloned instance's name
    clonedInstance.setSymNameAttr(newName);

    // Map old instance name to new hierarchical name
    instanceNameMap[oldName] = newName;

    LLVM_DEBUG(llvm::dbgs() << "  Cloned subinstance: "
                            << subInstance.getInstanceName()
                            << " -> " << newName.getValue() << "\n");
  });

  // Step 2: Inline calls to this instance's methods/values
  inlineCalls(instance, targetModule, mapper, instanceNameMap, interfaceBindingMap);

  // Step 3: Remove the original instance
  instance.erase();

  return success();
}

void ModuleInlinerPass::inlineCalls(InstanceOp instance,
                                    Cmt2ModuleLike targetModule,
                                    IRMapping &mapper,
                                    DenseMap<StringAttr, StringAttr> &instanceNameMap,
                                    DenseMap<StringAttr, StringAttr> &interfaceBindingMap) {
  auto parentModule = instance->getParentOfType<cmt2::ModuleOp>();
  OpBuilder builder(parentModule.getContext());

  // Find all calls to this instance
  LLVM_DEBUG(llvm::dbgs() << "  Looking for calls to instance @"
                          << instance.getInstanceNameAttr().getValue() << "\n");
  parentModule->walk([&](CallOp call) {
    LLVM_DEBUG(llvm::dbgs() << "    Found call: @" << call.getCalleeAttr().getLeafReference().getValue()
                            << " @" << call.getMethodOrValueAttr().getLeafReference().getValue() << "\n");
    // Check if this call targets our instance
    // getCallee() returns SymbolRefAttr, need to get the leaf reference and compare
    if (call.getCalleeAttr().getLeafReference() != instance.getInstanceNameAttr()) {
      LLVM_DEBUG(llvm::dbgs() << "      Skipping (doesn't match)\n");
      return;
    }

    LLVM_DEBUG(llvm::dbgs() << "  Inlining call to " << call.getCalleeAttr().getLeafReference().getValue()
                            << "." << call.getMethodOrValueAttr().getLeafReference().getValue() << "\n");

    // Look up the target method/value in the target module
    // Need to convert SymbolRefAttr to StringAttr
    StringAttr methodName = call.getMethodOrValueAttr().getLeafReference();
    auto targetFunc = targetModule.lookupFunctionLike(methodName);
    if (!targetFunc) {
      call.emitError("cannot find method/value in target module");
      return;
    }

    // For now, we'll use a simplified approach:
    // Just clone the body region of the target function
    builder.setInsertionPoint(call);

    // Clone the body region
    Region *bodyRegion = nullptr;
    if (auto methodOp = dyn_cast<MethodOp>(targetFunc.getOperation()))
      bodyRegion = &methodOp.getBody();
    else if (auto valueOp = dyn_cast<ValueOp>(targetFunc.getOperation()))
      bodyRegion = &valueOp.getBody();

    if (!bodyRegion || bodyRegion->empty()) {
      LLVM_DEBUG(llvm::dbgs() << "    Skipping - no body region\n");
      return;
    }

    // Create a mapping from function arguments to call inputs
    IRMapping localMapper = mapper; // Start with existing mappings
    Block &entryBlock = bodyRegion->front();
    for (auto [arg, input] :
         llvm::zip(entryBlock.getArguments(), call.getInputs())) {
      localMapper.map(arg, input);
    }

    // Clone the operations from the body
    SmallVector<Value> clonedResults;
    for (Operation &op : entryBlock) {
      if (auto returnOp = dyn_cast<ReturnOp>(op)) {
        // Collect the results
        for (Value result : returnOp.getOperands()) {
          clonedResults.push_back(localMapper.lookupOrDefault(result));
        }
        break;
      }
      auto *clonedOp = builder.clone(op, localMapper);

      // If this is a CallOp, we need to update the callee reference
      // if it references a subinstance that was cloned or an interface that needs binding
      if (auto clonedCall = dyn_cast<CallOp>(clonedOp)) {
        SymbolRefAttr oldCalleeRef = clonedCall.getCalleeAttr();
        StringAttr oldCallee = oldCalleeRef.getLeafReference();

        // First, check if this is an interface call that needs remapping
        auto interfaceIt = interfaceBindingMap.find(oldCallee);
        if (interfaceIt != interfaceBindingMap.end()) {
          // This call references an interface declaration - remap to interface definition
          SymbolRefAttr newCalleeRef = SymbolRefAttr::get(interfaceIt->second);
          clonedCall.setCalleeAttr(newCalleeRef);
          LLVM_DEBUG(llvm::dbgs() << "    Remapped interface call from @" << oldCallee.getValue()
                                  << " to @" << interfaceIt->second.getValue() << "\n");
          oldCallee = interfaceIt->second; // Update for potential further remapping
        }

        // Then, check if it references a subinstance that was cloned
        auto instanceIt = instanceNameMap.find(oldCallee);
        if (instanceIt != instanceNameMap.end()) {
          // Update the callee to point to the cloned instance
          SymbolRefAttr newCalleeRef = SymbolRefAttr::get(instanceIt->second);
          clonedCall.setCalleeAttr(newCalleeRef);
          LLVM_DEBUG(llvm::dbgs() << "    Remapped instance call from @" << oldCallee.getValue()
                                  << " to @" << instanceIt->second.getValue() << "\n");
        }
      }
    }

    // Replace the call's results
    if (!clonedResults.empty())
      call.replaceAllUsesWith(clonedResults);

    // Erase the call
    call.erase();
  });
}

void ModuleInlinerPass::runOnOperation() {
  circuit = getOperation();

  // Get analyses - we only use them for shouldNotInline check
  instanceGraph = &getAnalysis<InstanceGraph>();
  callInfo = &getAnalysis<CallInfoView>();

  LLVM_DEBUG(llvm::dbgs() << "=== Module Inliner Pass ===\n");

  // Process modules (we process in multiple rounds since inlining can be hierarchical)
  bool changed = true;
  while (changed) {
    changed = false;

    // Collect all modules in current state
    SmallVector<Cmt2ModuleLike> modulesToProcess;
    circuit.walk([&](Cmt2ModuleLike module) {
      modulesToProcess.push_back(module);
    });

    for (auto module : modulesToProcess) {
      // Skip if module was already removed
      if (!module->getParentOp())
        continue;

      // Skip modules that should not be inlined
      if (shouldNotInline(module)) {
        LLVM_DEBUG(llvm::dbgs() << "Skipping module: " << module.moduleName()
                                << " (should not inline)\n");
        continue;
      }

      LLVM_DEBUG(llvm::dbgs() << "Processing module: " << module.moduleName()
                              << "\n");

      // Find all instances of this module by walking the IR
      SmallVector<InstanceOp> instancesToInline;
      circuit.walk([&](InstanceOp instance) {
        auto refModule = instance.getReferencedModule();
        if (refModule && refModule == module)
          instancesToInline.push_back(instance);
      });

      // Inline all instances
      if (!instancesToInline.empty()) {
        changed = true;
        for (auto instance : instancesToInline) {
          if (failed(inlineInstance(instance))) {
            signalPassFailure();
            return;
          }
        }
      }
    }
  }

  // Clean up: Remove modules that are no longer used
  SmallVector<Cmt2ModuleLike> allModules;
  circuit.walk([&](Cmt2ModuleLike module) {
    allModules.push_back(module);
  });

  for (auto module : allModules) {
    // Skip if module was already removed
    if (!module->getParentOp())
      continue;

    // Skip modules that should not be removed
    if (shouldNotInline(module))
      continue;

    // Check if module has any uses left by walking the IR
    bool hasUses = false;
    circuit.walk([&](InstanceOp instance) {
      auto refModule = instance.getReferencedModule();
      if (refModule && refModule == module) {
        hasUses = true;
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });

    if (!hasUses) {
      LLVM_DEBUG(llvm::dbgs() << "Removing unused module: "
                              << module.moduleName() << "\n");
      module->erase();
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "=== Module Inliner Pass Complete ===\n");
}

