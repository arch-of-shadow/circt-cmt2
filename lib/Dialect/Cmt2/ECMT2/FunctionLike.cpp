//===- FunctionLike.cpp - ECMT2 Function-Like Implementation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "mlir/IR/Builders.h"

using namespace circt;
using namespace cmt2;
using namespace cmt2::ecmt2;

//===----------------------------------------------------------------------===//
// FunctionLike
//===----------------------------------------------------------------------===//

FunctionLike::FunctionLike(mlir::Operation *op, mlir::OpBuilder &parentBuilder)
    : op_(op), parentBuilder_(parentBuilder) {
  // Builders will be initialized by derived classes
}

//===----------------------------------------------------------------------===//
// Rule
//===----------------------------------------------------------------------===//

Rule::Rule(llvm::StringRef name, Module *parent)
    : FunctionLike(nullptr, parent->getBuilder()) {
  auto &builder = parent->getBuilder();
  auto loc = parent->getLoc();

  // Create empty function type (no inputs/outputs)
  auto funcType = builder.getFunctionType({}, {});
  auto funcTypeAttr = mlir::TypeAttr::get(funcType);

  // Build argument names
  llvm::SmallVector<mlir::Attribute> argNames;
  auto argNamesAttr = builder.getArrayAttr(argNames);

  // Build result names (ok empty for now, rule does not return values)
  llvm::SmallVector<mlir::Attribute> resNames;
  auto resNamesAttr = builder.getArrayAttr(resNames);

  op_ = builder.create<RuleOp>(loc, builder.getStringAttr(name), funcTypeAttr,
                                argNamesAttr, resNamesAttr,
                                builder.getArrayAttr({}), builder.getArrayAttr({}));

  // Access guard and body regions
  auto &guardRegion = op_.getGuard();
  auto &bodyRegion = op_.getBody();

  auto *guardBlock = new mlir::Block();
  guardRegion.push_back(guardBlock);
  guardBuilder_ = std::make_unique<mlir::OpBuilder>(guardBlock, guardBlock->begin());

  auto *bodyBlock = new mlir::Block();
  bodyRegion.push_back(bodyBlock);
  bodyBuilder_ = std::make_unique<mlir::OpBuilder>(bodyBlock, bodyBlock->begin());
}

void Rule::finalize() {
  // Rules are already constructed, nothing to finalize
}

//===----------------------------------------------------------------------===//
// Method
//===----------------------------------------------------------------------===//

Method::Method(llvm::StringRef name,
               llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
               llvm::ArrayRef<mlir::Type> results, Module *parent)
    : FunctionLike(nullptr, parent->getBuilder()) {
  auto &builder = parent->getBuilder();
  auto loc = parent->getLoc();

  // Build function type
  llvm::SmallVector<mlir::Type> argTypes;
  for (auto &arg : args)
    argTypes.push_back(arg.second);

  auto funcType = builder.getFunctionType(argTypes, results);
  auto funcTypeAttr = mlir::TypeAttr::get(funcType);

  // Build argument names
  llvm::SmallVector<mlir::Attribute> argNames;
  for (auto &arg : args)
    argNames.push_back(builder.getStringAttr(arg.first));
  auto argNamesAttr = builder.getArrayAttr(argNames);

  // Build result names
  llvm::SmallVector<mlir::Attribute> resNames;
  for (size_t i = 0; i < results.size(); i++) {
    resNames.push_back(builder.getStringAttr(""));
  }
  auto resNamesAttr = builder.getArrayAttr(resNames);

  op_ = builder.create<MethodOp>(loc, builder.getStringAttr(name), funcTypeAttr,
                                  argNamesAttr, resNamesAttr,
                                  builder.getArrayAttr({}), builder.getArrayAttr({}));

  // Access guard and body regions
  auto &guardRegion = op_.getGuard();
  auto &bodyRegion = op_.getBody();

  auto *guardBlock = new mlir::Block();
  guardRegion.push_back(guardBlock);
  guardBuilder_ = std::make_unique<mlir::OpBuilder>(guardBlock, guardBlock->begin());

  auto *bodyBlock = new mlir::Block();
  bodyRegion.push_back(bodyBlock);
  bodyBuilder_ = std::make_unique<mlir::OpBuilder>(bodyBlock, bodyBlock->begin());

  // Add arguments
  for (auto argType : argTypes) {
    guardBlock->addArgument(argType, loc);
    bodyBlock->addArgument(argType, loc);
  }

  // Collect arguments
  for (auto arg : bodyBlock->getArguments())
    arguments_.push_back(arg);
}

Method &Method::setEnableName(llvm::StringRef name) {
  op_->setAttr("enableName", mlir::StringAttr::get(op_->getContext(), name));
  return *this;
}

Method &Method::setReadyName(llvm::StringRef name) {
  op_->setAttr("readyName", mlir::StringAttr::get(op_->getContext(), name));
  return *this;
}

void Method::finalize() {
  // Methods are already constructed, nothing to finalize
}

//===----------------------------------------------------------------------===//
// ecmt2::Value
//===----------------------------------------------------------------------===//

ecmt2::Value::Value(llvm::StringRef name, llvm::ArrayRef<mlir::Type> results,
                    Module *parent)
    : FunctionLike(nullptr, parent->getBuilder()) {
  auto &builder = parent->getBuilder();
  auto loc = parent->getLoc();

  // Create function type (no inputs, only outputs)
  auto funcType = builder.getFunctionType({}, results);
  auto funcTypeAttr = mlir::TypeAttr::get(funcType);

  // Build empty arg names and result names
  llvm::SmallVector<mlir::Attribute> argNames;
  auto argNamesAttr = builder.getArrayAttr(argNames);

  llvm::SmallVector<mlir::Attribute> resNames;
  for (size_t i = 0; i < results.size(); ++i)
    resNames.push_back(builder.getStringAttr(""));
  auto resNamesAttr = builder.getArrayAttr(resNames);

  op_ = builder.create<ValueOp>(loc, builder.getStringAttr(name), funcTypeAttr,
                                 argNamesAttr, resNamesAttr,
                                 builder.getArrayAttr({}), builder.getArrayAttr({}));

  // Access guard and body regions
  auto &guardRegion = op_.getGuard();
  auto &bodyRegion = op_.getBody();

  auto *guardBlock = new mlir::Block();
  guardRegion.push_back(guardBlock);
  guardBuilder_ = std::make_unique<mlir::OpBuilder>(guardBlock, guardBlock->begin());

  auto *bodyBlock = new mlir::Block();
  bodyRegion.push_back(bodyBlock);
  bodyBuilder_ = std::make_unique<mlir::OpBuilder>(bodyBlock, bodyBlock->begin());
}

ecmt2::Value &ecmt2::Value::setReadyName(llvm::StringRef name) {
  op_->setAttr("readyName", mlir::StringAttr::get(op_->getContext(), name));
  return *this;
}

void ecmt2::Value::finalize() {
  // Values are already constructed, nothing to finalize
}

//===----------------------------------------------------------------------===//
// ProcGroup
//===----------------------------------------------------------------------===//

ProcGroup::ProcGroup(llvm::StringRef name, Module *parent) : name_(name.str()) {
  auto &builder = parent->getBuilder();
  auto loc = parent->getLoc();

  op_ = builder.create<ProcGroupOp>(loc, builder.getStringAttr(name));

  auto *block = new mlir::Block();
  op_.getBodyRegion().push_back(block);
  bodyBuilder_ = std::make_unique<mlir::OpBuilder>(block, block->begin());
}

void ProcGroup::groupDone(mlir::Value condition) {
  bodyBuilder_->create<ProcGroupDoneOp>(bodyBuilder_->getUnknownLoc(),
                                        condition);
}

//===----------------------------------------------------------------------===//
// ProcStaticGroup
//===----------------------------------------------------------------------===//

ProcStaticGroup::ProcStaticGroup(llvm::StringRef name, uint64_t latency,
                                 Module *parent)
    : name_(name.str()) {
  auto &builder = parent->getBuilder();
  auto loc = parent->getLoc();

  op_ = builder.create<ProcStaticGroupOp>(loc, builder.getStringAttr(name),
                                          latency);

  auto *block = new mlir::Block();
  op_.getBodyRegion().push_back(block);
  bodyBuilder_ = std::make_unique<mlir::OpBuilder>(block, block->begin());
}

//===----------------------------------------------------------------------===//
// ControlBuilder
//===----------------------------------------------------------------------===//

ControlBuilder::ControlBuilder(mlir::OpBuilder &builder, mlir::Location loc)
    : builder_(builder), loc_(loc) {}

ControlBuilder &ControlBuilder::enable(llvm::StringRef groupName) {
  builder_.create<ProcEnableOp>(loc_,
                                FlatSymbolRefAttr::get(builder_.getContext(),
                                                       groupName));
  return *this;
}

mlir::Value ControlBuilder::invoke(llvm::StringRef instance,
                                   llvm::StringRef method,
                                   llvm::ArrayRef<mlir::Value> args,
                                   llvm::ArrayRef<mlir::Type> results) {
  auto instanceAttr = FlatSymbolRefAttr::get(builder_.getContext(), instance);
  auto methodAttr = FlatSymbolRefAttr::get(builder_.getContext(), method);

  auto invokeOp = builder_.create<ProcInvokeOp>(loc_, results, instanceAttr,
                                                methodAttr, args);
  if (results.empty())
    return mlir::Value();
  return invokeOp.getResult(0);
}

void ControlBuilder::end() {
  builder_.create<ProcControlEndOp>(loc_);
}

//===----------------------------------------------------------------------===//
// ProcRule
//===----------------------------------------------------------------------===//

ProcRule::ProcRule(llvm::StringRef name, Module *parent)
    : name_(name.str()), loc_(parent->getLoc()) {
  auto &builder = parent->getBuilder();

  // Create empty function type (no inputs/outputs)
  auto funcType = builder.getFunctionType({}, {});
  auto funcTypeAttr = mlir::TypeAttr::get(funcType);

  // Empty argument names
  auto argNamesAttr = builder.getArrayAttr({});

  op_ = builder.create<ProcRuleOp>(loc_, builder.getStringAttr(name),
                                   funcTypeAttr, argNamesAttr,
                                   builder.getArrayAttr({}),
                                   builder.getArrayAttr({}));

  // Create guard region
  auto *guardBlock = new mlir::Block();
  op_.getGuard().push_back(guardBlock);
  guardBuilder_ =
      std::make_unique<mlir::OpBuilder>(guardBlock, guardBlock->begin());

  // Create control region
  auto *controlBlock = new mlir::Block();
  op_.getControl().push_back(controlBlock);
  controlBuilder_ =
      std::make_unique<mlir::OpBuilder>(controlBlock, controlBlock->begin());
}

//===----------------------------------------------------------------------===//
// ProcMethod
//===----------------------------------------------------------------------===//

ProcMethod::ProcMethod(llvm::StringRef name,
                       llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
                       llvm::ArrayRef<mlir::Type> results, Module *parent)
    : name_(name.str()), loc_(parent->getLoc()) {
  auto &builder = parent->getBuilder();

  // Build function type
  llvm::SmallVector<mlir::Type> argTypes;
  for (auto &arg : args)
    argTypes.push_back(arg.second);

  auto funcType = builder.getFunctionType(argTypes, results);
  auto funcTypeAttr = mlir::TypeAttr::get(funcType);

  // Build argument names
  llvm::SmallVector<mlir::Attribute> argNames;
  for (auto &arg : args)
    argNames.push_back(builder.getStringAttr(arg.first));
  auto argNamesAttr = builder.getArrayAttr(argNames);

  // Build result names
  llvm::SmallVector<mlir::Attribute> resNames;
  for (size_t i = 0; i < results.size(); i++)
    resNames.push_back(builder.getStringAttr(""));
  auto resNamesAttr = builder.getArrayAttr(resNames);

  op_ = builder.create<ProcMethodOp>(loc_, builder.getStringAttr(name),
                                     funcTypeAttr, argNamesAttr, resNamesAttr,
                                     builder.getArrayAttr({}),
                                     builder.getArrayAttr({}));

  // Create guard region with arguments
  auto *guardBlock = new mlir::Block();
  op_.getGuard().push_back(guardBlock);
  for (auto argType : argTypes)
    guardBlock->addArgument(argType, loc_);
  guardBuilder_ =
      std::make_unique<mlir::OpBuilder>(guardBlock, guardBlock->begin());

  // Create control region with arguments
  auto *controlBlock = new mlir::Block();
  op_.getControl().push_back(controlBlock);
  for (auto argType : argTypes)
    controlBlock->addArgument(argType, loc_);
  controlBuilder_ =
      std::make_unique<mlir::OpBuilder>(controlBlock, controlBlock->begin());

  // Collect arguments from control block
  for (auto arg : controlBlock->getArguments())
    arguments_.push_back(arg);
}
