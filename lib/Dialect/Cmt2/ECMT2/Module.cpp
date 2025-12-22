//===- Module.cpp - ECMT2 Module Implementation -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Utils.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/FunctionLike.h"
#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/Cmt2/Cmt2OpInterfaces.h"

using namespace circt;
using namespace cmt2;
using namespace cmt2::ecmt2;

// ExternalModule class has been removed and unified into Module class.
// Use Module with external constructor instead.

namespace circt {
namespace cmt2 {
namespace ecmt2 {
    

mlir::FunctionType getFunctionTypeFromBinding(
  mlir::ModuleOp topModule,
  llvm::StringRef firrtlModuleName,
  llvm::ArrayRef<std::string> argPorts,
  llvm::ArrayRef<std::string> resPorts,
  OpBuilder &builder
) {
  llvm::SmallVector<mlir::Type> argumentTypes;
  llvm::SmallVector<mlir::Type> resultTypes;

  // Walk both FModuleOp and FExtModuleOp to find the FIRRTL module
  topModule->walk([&](circt::firrtl::FModuleOp firrtlMod) {
    if (firrtlMod.getModuleName() == firrtlModuleName) {
      // For each argument port, find its type in the FIRRTL module
      for (auto portName: argPorts) {
        for (size_t i = 0; i < firrtlMod.getNumPorts(); ++i) {
          if (firrtlMod.getPortName(i) == portName) {
            argumentTypes.push_back(firrtlMod.getPortType(i));
            break;
          }
        }
      }
      // Also for each result port
      for (auto portName: resPorts) {
        for (size_t i = 0; i < firrtlMod.getNumPorts(); ++i) {
          if (firrtlMod.getPortName(i) == portName) {
            resultTypes.push_back(firrtlMod.getPortType(i));
            break;
          }
        }
      }
      return mlir::WalkResult::interrupt();
    }
    return mlir::WalkResult::advance();
  });

  // If not found in FModuleOp, try FExtModuleOp
  if (argumentTypes.empty() && resultTypes.empty()) {
    topModule->walk([&](circt::firrtl::FExtModuleOp firrtlMod) {
      if (firrtlMod.getModuleName() == firrtlModuleName) {
        for (auto portName: argPorts) {
          for (size_t i = 0; i < firrtlMod.getNumPorts(); ++i) {
            if (firrtlMod.getPortName(i) == portName) {
              argumentTypes.push_back(firrtlMod.getPortType(i));
              break;
            }
          }
        }
        for (auto portName: resPorts) {
          for (size_t i = 0; i < firrtlMod.getNumPorts(); ++i) {
            if (firrtlMod.getPortName(i) == portName) {
              resultTypes.push_back(firrtlMod.getPortType(i));
              break;
            }
          }
        }
        return mlir::WalkResult::interrupt();
      }
      return mlir::WalkResult::advance();
    });
  }

  return builder.getFunctionType(argumentTypes, resultTypes);
}

} // namespace ecmt2
} // namespace cmt2
} // namespace circt

//===----------------------------------------------------------------------===//
// Module
//===----------------------------------------------------------------------===//

Module::Module(llvm::StringRef name, mlir::OpBuilder &builder,
               mlir::Location loc)
    : name_(name.str()), builder_(builder), loc_(loc), isExternal_(false) {

  // Create ModuleOp
  auto nameAttr = builder.getStringAttr(name);
  auto argNamesAttr = builder.getArrayAttr({});

  op_ = builder.create<cmt2::ModuleOp>(loc, nameAttr, argNamesAttr,
                                        /*external=*/nullptr,
                                        /*ext_module_name=*/nullptr);

  // Set insertion point inside the module
  auto *block = new mlir::Block();
  op_.getBody().push_back(block);
  builder_.setInsertionPointToEnd(block);
}

Module::Module(llvm::StringRef name, llvm::StringRef firrtlModuleName,
               mlir::OpBuilder &builder, mlir::Location loc)
    : name_(name.str()), builder_(builder), loc_(loc), isExternal_(true) {

  // Create ModuleOp with external attribute
  auto nameAttr = builder.getStringAttr(name);
  auto argNamesAttr = builder.getArrayAttr({});
  auto externalAttr = builder.getUnitAttr();
  auto extModuleNameAttr = mlir::FlatSymbolRefAttr::get(builder.getContext(), firrtlModuleName);

  op_ = builder.create<cmt2::ModuleOp>(loc, nameAttr, argNamesAttr,
                                        externalAttr, extModuleNameAttr);

  // Set insertion point inside the module
  auto *block = new mlir::Block();
  op_.getBody().push_back(block);
  builder_.setInsertionPointToEnd(block);
}

Module::~Module() = default;

//===----------------------------------------------------------------------===//
// Module - External binding methods
//===----------------------------------------------------------------------===//

Module &Module::bindClock(llvm::StringRef argName, llvm::StringRef port) {
  // Add clock argument
  auto clockType = firrtl::ClockType::get(builder_.getContext());
  auto arg = addArgument(argName, clockType);

  // Create bind.bare operation
  auto savedIP = builder_.saveInsertionPoint();
  builder_.setInsertionPointToEnd(&op_.getBody().front());

  auto portRef = mlir::FlatSymbolRefAttr::get(builder_.getContext(), port);
  builder_.create<cmt2::BindBareOp>(loc_, arg, portRef);

  builder_.restoreInsertionPoint(savedIP);
  return *this;
}

Module &Module::bindReset(llvm::StringRef argName, llvm::StringRef port) {
  // Add reset argument
  auto resetType = firrtl::UIntType::get(builder_.getContext(), 1);
  auto arg = addArgument(argName, resetType);

  // Create bind.bare operation
  auto savedIP = builder_.saveInsertionPoint();
  builder_.setInsertionPointToEnd(&op_.getBody().front());

  auto portRef = mlir::FlatSymbolRefAttr::get(builder_.getContext(), port);
  builder_.create<cmt2::BindBareOp>(loc_, arg, portRef);

  builder_.restoreInsertionPoint(savedIP);
  return *this;
}

Module &Module::bindMethod(llvm::StringRef name, llvm::StringRef enablePort,
                           llvm::StringRef readyPort,
                           llvm::ArrayRef<std::string> argPorts,
                           llvm::ArrayRef<std::string> resPorts) {
  // Create actual BindMethodOp in the body
  auto savedIP = builder_.saveInsertionPoint();
  builder_.setInsertionPointToEnd(&op_.getBody().front());

  // Create StringAttr for each port
  auto enableAttr = enablePort.empty() ? mlir::StringAttr() :
                    mlir::StringAttr::get(builder_.getContext(), enablePort);
  auto readyAttr = readyPort.empty() ? mlir::StringAttr() :
                   mlir::StringAttr::get(builder_.getContext(), readyPort);

  llvm::SmallVector<mlir::Attribute> argNames;
  for (auto &arg : argPorts)
    argNames.push_back(builder_.getStringAttr(arg));

  llvm::SmallVector<mlir::Attribute> bodyResNames;
  for (auto &res : resPorts)
    bodyResNames.push_back(builder_.getStringAttr(res));

  // Create function type (inputs -> outputs)
  auto topModule = op_->getParentOfType<mlir::ModuleOp>();
  auto firrtlModuleName = op_.getExternalModuleName();
  auto functionType = getFunctionTypeFromBinding(topModule, firrtlModuleName, argPorts, resPorts, builder_);

  // Create empty arg_attrs and res_attrs
  auto emptyArrayAttr = builder_.getArrayAttr({});

  builder_.create<cmt2::BindMethodOp>(
      loc_,
      builder_.getStringAttr(name),
      mlir::TypeAttr::get(functionType),
      enableAttr,
      readyAttr,
      builder_.getArrayAttr(argNames),
      builder_.getArrayAttr(bodyResNames),
      emptyArrayAttr,
      emptyArrayAttr);

  builder_.restoreInsertionPoint(savedIP);

  return *this;
}

Module &Module::bindValue(llvm::StringRef name, llvm::StringRef readyPort,
                          llvm::ArrayRef<std::string> argPorts,
                          llvm::ArrayRef<std::string> resPorts) {
  // Create actual BindValueOp in the body
  auto savedIP = builder_.saveInsertionPoint();
  builder_.setInsertionPointToEnd(&op_.getBody().front());

  // Create FlatSymbolRefAttr for each port
  auto readyAttr = readyPort.empty() ? mlir::StringAttr() :
                   mlir::StringAttr::get(builder_.getContext(), readyPort);

  // Update argNames attribute
  llvm::SmallVector<mlir::Attribute> argNames;
  for (auto &arg : argPorts)
    argNames.push_back(builder_.getStringAttr(arg));

  llvm::SmallVector<mlir::Attribute> bodyResNames;
  for (auto &res : resPorts)
    bodyResNames.push_back(builder_.getStringAttr(res));

  // Create function type (no inputs -> outputs)
  auto topModule = op_->getParentOfType<mlir::ModuleOp>();
  auto firrtlModuleName = op_.getExternalModuleName();
  auto functionType = getFunctionTypeFromBinding(topModule, firrtlModuleName, argPorts, resPorts, builder_);

  // Create empty arg_attrs and res_attrs
  auto emptyArrayAttr = builder_.getArrayAttr({});

  builder_.create<cmt2::BindValueOp>(
      loc_,
      builder_.getStringAttr(name),
      mlir::TypeAttr::get(functionType),
      readyAttr,
      builder_.getArrayAttr(argNames),
      builder_.getArrayAttr(bodyResNames),
      emptyArrayAttr,
      emptyArrayAttr);

  builder_.restoreInsertionPoint(savedIP);

  return *this;
}

Module &Module::addConflict(llvm::StringRef a, llvm::StringRef b) {
  // Format: conflict = [[@a, @b], ...]
  auto pairArray = builder_.getArrayAttr({
      mlir::FlatSymbolRefAttr::get(builder_.getContext(), a),
      mlir::FlatSymbolRefAttr::get(builder_.getContext(), b),
  });

  auto conflictAttr = op_->getAttr("conflict");
  llvm::SmallVector<mlir::Attribute> conflicts;
  if (conflictAttr) {
    auto arr = mlir::cast<mlir::ArrayAttr>(conflictAttr);
    conflicts.append(arr.begin(), arr.end());
  }
  conflicts.push_back(pairArray);
  op_->setAttr("conflict", builder_.getArrayAttr(conflicts));

  return *this;
}

Module &Module::addConflictFree(llvm::StringRef a, llvm::StringRef b) {
  // Format: conflictFree = [[@a, @b], ...]
  auto pairArray = builder_.getArrayAttr({
      mlir::FlatSymbolRefAttr::get(builder_.getContext(), a),
      mlir::FlatSymbolRefAttr::get(builder_.getContext(), b),
  });

  auto conflictFreeAttr = op_->getAttr("conflictFree");
  llvm::SmallVector<mlir::Attribute> conflictFrees;
  if (conflictFreeAttr) {
    auto arr = mlir::cast<mlir::ArrayAttr>(conflictFreeAttr);
    conflictFrees.append(arr.begin(), arr.end());
  }
  conflictFrees.push_back(pairArray);
  op_->setAttr("conflictFree", builder_.getArrayAttr(conflictFrees));

  return *this;
}

Module &Module::addSequenceBefore(llvm::StringRef before, llvm::StringRef after) {
  // Format: sequenceBefore = [[@before, @after], ...]
  // Meaning: before < after (before must be scheduled before after)
  auto pairArray = builder_.getArrayAttr({
      mlir::FlatSymbolRefAttr::get(builder_.getContext(), before),
      mlir::FlatSymbolRefAttr::get(builder_.getContext(), after),
  });

  auto sequenceBeforeAttr = op_->getAttr("sequenceBefore");
  llvm::SmallVector<mlir::Attribute> sequenceBefores;
  if (sequenceBeforeAttr) {
    auto arr = mlir::cast<mlir::ArrayAttr>(sequenceBeforeAttr);
    sequenceBefores.append(arr.begin(), arr.end());
  }
  sequenceBefores.push_back(pairArray);
  op_->setAttr("sequenceBefore", builder_.getArrayAttr(sequenceBefores));

  return *this;
}

mlir::BlockArgument Module::addArgument(llvm::StringRef name, mlir::Type type) {
  // Add block argument to the module body
  auto *block = &op_.getBody().front();
  auto arg = block->addArgument(type, loc_);

  // Update argNames attribute
  llvm::SmallVector<mlir::Attribute> argNames;
  if (auto existingNames = op_.getArgNames()) {
    argNames.append(existingNames.begin(), existingNames.end());
  }
  argNames.push_back(builder_.getStringAttr(name));
  op_.setArgNamesAttr(builder_.getArrayAttr(argNames));

  return arg;
}

Clock Module::addClockArgument(llvm::StringRef name) {
  auto clockType = firrtl::ClockType::get(builder_.getContext());
  auto arg = addArgument(name, clockType);
  return Clock(arg);
}

Reset Module::addResetArgument(llvm::StringRef name) {
  auto resetType = firrtl::UIntType::get(builder_.getContext(), 1);
  auto arg = addArgument(name, resetType);
  return Reset(arg, &builder_, loc_);
}

Instance *
Module::addInstance(llvm::StringRef name, ModuleBase *moduleType,
                    llvm::ArrayRef<mlir::Value> args,
                    llvm::ArrayRef<std::pair<std::string, std::string>>
                        interfaceBindings) {
  auto inst = std::make_unique<Instance>(name, moduleType, args, this,
                                         interfaceBindings);
  auto *ptr = inst.get();
  instances_.push_back(std::move(inst));
  return ptr;
}

Rule *Module::addRule(llvm::StringRef name) {
  auto rule = std::make_unique<Rule>(name, this);
  auto *ptr = rule.get();
  rules_.push_back(std::move(rule));
  return ptr;
}

Method *
Module::addMethod(llvm::StringRef name,
                  llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
                  llvm::ArrayRef<mlir::Type> results) {
  auto method = std::make_unique<Method>(name, args, results, this);
  auto *ptr = method.get();
  methods_.push_back(std::move(method));
  return ptr;
}

cmt2::ecmt2::Value *Module::addValue(llvm::StringRef name,
                        llvm::ArrayRef<mlir::Type> results) {
  auto value = std::make_unique<cmt2::ecmt2::Value>(name, results, this);
  auto *ptr = value.get();
  values_.push_back(std::move(value));
  return ptr;
}

InterfaceDecl *Module::defineInterfaceDecl(llvm::StringRef name,
                                      llvm::StringRef type) {
  auto iface = std::make_unique<InterfaceDecl>(name, type, this);
  auto *ptr = iface.get();
  interfaces_.push_back(std::move(iface));
  return ptr;
}

InterfaceDef *Module::defineInterfaceDef(llvm::StringRef name,
                                        llvm::StringRef type) {
  auto ifaceDef = std::make_unique<InterfaceDef>(name, type, this);
  auto *ptr = ifaceDef.get();
  interfaceDefs_.push_back(std::move(ifaceDef));
  return ptr;
}

void Module::setPrecedence(
    llvm::ArrayRef<std::pair<std::string, std::string>> pairs) {
  // Build precedence array attribute
  // Format: [[@first, @second], [@third, @fourth], ...]
  llvm::SmallVector<mlir::Attribute> precedenceAttrs;

  // First, get existing precedence pairs if any
  if (auto existingPrecedence = op_->getAttrOfType<mlir::ArrayAttr>("precedence")) {
    for (auto attr : existingPrecedence) {
      precedenceAttrs.push_back(attr);
    }
  }

  // Add new pairs, checking if they already exist
  for (auto &pair : pairs) {
    bool alreadyExists = false;

    // Check if this pair already exists in precedenceAttrs
    for (auto attr : precedenceAttrs) {
      if (auto pairArray = dyn_cast<mlir::ArrayAttr>(attr)) {
        if (pairArray.size() == 2) {
          auto first = cast<mlir::FlatSymbolRefAttr>(pairArray[0]).getValue().str();
          auto second = cast<mlir::FlatSymbolRefAttr>(pairArray[1]).getValue().str();
          if (first == pair.first && second == pair.second) {
            alreadyExists = true;
            break;
          }
        }
      }
    }

    // Only add if it doesn't already exist
    if (!alreadyExists) {
      auto pairArray = builder_.getArrayAttr({
          mlir::FlatSymbolRefAttr::get(builder_.getContext(), pair.first),
          mlir::FlatSymbolRefAttr::get(builder_.getContext(), pair.second),
      });
      precedenceAttrs.push_back(pairArray);
    }
  }

  // Set the precedence attribute on the ModuleOp
  op_->setAttr("precedence", builder_.getArrayAttr(precedenceAttrs));
}
