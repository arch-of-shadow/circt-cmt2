//===- InferRules.cpp - InferRules Pass ----C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Use InstanceGraph and CallGraph to illustrate the method call information,
//
//
//===----------------------------------------------------------------------===//

#include "PassDetails.h"
#include "circt/Dialect/Cmt2/CallInfo.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2Passes.h"
#include "circt/Dialect/Cmt2/InstanceGraph.h"
#include "circt/Support/LLVM.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace circt;
using namespace mlir;
using namespace cmt2;
using namespace hw;

class InferRules : public InferRulesBase<InferRules> {
  void runOnOperation() override;

private:
  struct StackElement {
    StackElement(InstanceGraphNode *node)
        : node(node), iterator(node->begin()), viewed(false) {}
    InstanceGraphNode *node;
    InstanceGraphNode::iterator iterator;
    bool viewed;
  };
  llvm::SmallVector<SmallVector<Attribute>> rules;
};

void InferRules::runOnOperation() {
  auto circuit = OperationPass<CircuitOp>::getOperation();
  auto builder = OpBuilder(circuit->getBlock(), ++Block::iterator(circuit));

  // visiting all module to inspect the call map for each module.
  // get the instance graph of the circuit.
  auto *instanceGraph = &getAnalysis<InstanceGraph>();
  InstanceGraphNode *top = instanceGraph->getTopLevelNode();

  // add Method in top as method:
  if (auto module = llvm::dyn_cast<circt::cmt2::ModuleOp>(top->getModule())) {
    for (auto method : getMethods(module)) {
      hw::InnerRefAttr::getFromOperation(method, method.getNameAttr(), module.moduleNameAttr());
    }
  }

  // DFS the circuit from the top to analyse the primitive call of each rule.
  // for the methods in the top module, they are regarded as rule, the interface
  // is out-ready->cmt2 cmt2->enable->out.
  SmallVector<StackElement> instancePath;
  instancePath.emplace_back(top);
  while (!instancePath.empty()) {
    auto &element = instancePath.back();
    auto &node = element.node;
    auto &iterator = element.iterator;
    if (!element.viewed) {
      auto module = node->getModule();
      // regard methods in the top being rule
      if (node == top) {
        //        GlobalRefOp::build(OpBuilder::atBlockBegin()):
        rules;
      }
      auto moduleSymbolRef =
          mlir::SymbolRefAttr::get(module.getContext(), module.moduleName());
    }
    element.viewed = true;

    if (iterator == node->end()) {
      instancePath.pop_back();
      continue;
    }
    auto *instanceNode = *iterator++;

    instancePath.emplace_back(instanceNode->getTarget());
  }
  return;
}

std::unique_ptr<mlir::Pass> circt::cmt2::createInferRules() {
  return std::make_unique<InferRules>();
}