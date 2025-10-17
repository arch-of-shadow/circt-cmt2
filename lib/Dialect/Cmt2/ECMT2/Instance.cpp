//===- Instance.cpp - ECMT2 Instance Implementation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Instance.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "mlir/IR/BuiltinOps.h"

using namespace circt;
using namespace cmt2::ecmt2;

//===----------------------------------------------------------------------===//
// Instance
//===----------------------------------------------------------------------===//

Instance::Instance(llvm::StringRef name, ModuleBase *moduleType,
                   llvm::ArrayRef<mlir::Value> args, Module *parent,
                   llvm::ArrayRef<std::pair<std::string, std::string>>
                       interfaceBindings)
    : name_(name.str()), moduleType_(moduleType), loc_(parent->getLoc()) {

  auto &builder = parent->getBuilder();

  // Build interface bindings attribute
  // Format: [[@interfaceDef, @interfaceDecl], ...]
  llvm::SmallVector<mlir::Attribute> bindingAttrs;
  for (auto &binding : interfaceBindings) {
    auto bindingArray = builder.getArrayAttr({
        mlir::FlatSymbolRefAttr::get(builder.getContext(), binding.first),
        mlir::FlatSymbolRefAttr::get(builder.getContext(), binding.second)
    });
    bindingAttrs.push_back(bindingArray);
  }

  // Create instance operation
  auto nameAttr = builder.getStringAttr(name);
  auto moduleNameAttr =
      mlir::FlatSymbolRefAttr::get(builder.getContext(),
                                    moduleType->getName());

  // Determine result types (empty for now, will be inferred)
  llvm::SmallVector<mlir::Type> resultTypes;

  op_ = builder.create<cmt2::InstanceOp>(
      loc_, nameAttr, args, moduleNameAttr,
      builder.getArrayAttr(bindingAttrs));
}

llvm::SmallVector<mlir::Value, 4>
Instance::callMethod(llvm::StringRef method, llvm::ArrayRef<mlir::Value> args,
                     mlir::OpBuilder &builder) {
  return CallBuilder::buildCall(this, method, args, builder, loc_);
}

llvm::SmallVector<mlir::Value, 4>
Instance::callValue(llvm::StringRef value, mlir::OpBuilder &builder) {
  return CallBuilder::buildCall(this, value, {}, builder, loc_);
}

//===----------------------------------------------------------------------===//
// CallBuilder
//===----------------------------------------------------------------------===//

llvm::SmallVector<mlir::Value, 4>
CallBuilder::buildCall(Instance *instance, llvm::StringRef entity,
                       llvm::ArrayRef<mlir::Value> args,
                       mlir::OpBuilder &builder, mlir::Location loc) {

  // Build callee symbol reference: @instance
  auto instanceSym = mlir::SymbolRefAttr::get(builder.getContext(),
                                                instance->getName());

  // Build method/value symbol reference: @entity
  auto entitySym = mlir::SymbolRefAttr::get(builder.getContext(), entity);

  // Determine result types by looking up the bind operation and FIRRTL module
  llvm::SmallVector<mlir::Type> resultTypes;

  // Check if the module is an external FIRRTL module by checking the operation type
  auto moduleOp = instance->moduleType_->getOperation();
  if (auto extModFirrtl = mlir::dyn_cast<cmt2::ExtModuleFirrtlOp>(moduleOp)) {
    // Walk the body to find the BindMethodOp or BindValueOp
    auto entityAttr = builder.getStringAttr(entity);

    // Get the FIRRTL module name to look up port types
    llvm::StringRef firrtlModuleName = extModFirrtl.getExtModuleName();

    for (auto &bodyOp : extModFirrtl.getBodyRegion().front()) {
      if (auto bindMethod = mlir::dyn_cast<cmt2::BindMethodOp>(bodyOp)) {
        if (bindMethod.getSymNameAttr() == entityAttr) {
          // For BindMethodOp, result count = number of outputs
          auto outputsAttr = bindMethod.getOutputs();

          // Look up the FIRRTL module to get actual port types
          mlir::Operation *topModule = extModFirrtl->template getParentOfType<mlir::ModuleOp>();
          if (topModule) {
            topModule->walk([&](circt::firrtl::FModuleOp firrtlMod) {
              if (firrtlMod.getModuleName() == firrtlModuleName) {
                // For each output port, find its type in the FIRRTL module
                for (auto outputAttr : outputsAttr) {
                  auto portName = mlir::cast<mlir::FlatSymbolRefAttr>(outputAttr).getAttr();
                  // Find the port in the FIRRTL module
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
          break;
        }
      } else if (auto bindValue = mlir::dyn_cast<cmt2::BindValueOp>(bodyOp)) {
        if (bindValue.getSymNameAttr() == entityAttr) {
          // For BindValueOp, result count = number of data ports
          auto dataAttr = bindValue.getData();

          // Look up the FIRRTL module to get actual port types
          mlir::Operation *topModule = extModFirrtl->template getParentOfType<mlir::ModuleOp>();
          if (topModule) {
            topModule->walk([&](circt::firrtl::FModuleOp firrtlMod) {
              if (firrtlMod.getModuleName() == firrtlModuleName) {
                // For each data port, find its type in the FIRRTL module
                for (auto dataPortAttr : dataAttr) {
                  auto portName = mlir::cast<mlir::FlatSymbolRefAttr>(dataPortAttr).getAttr();
                  // Find the port in the FIRRTL module
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
          break;
        }
      }
    }
  }
  // If module type is not ExternalModule or bind op not found, resultTypes remains empty
  // which might be acceptable for regular Cmt2 modules

  // Create call operation
  // CallOp signature: (TypeRange outputs, ValueRange inputs, callee, methodOrValue, arg_attrs, res_attrs)
  auto callOp =
      builder.create<cmt2::CallOp>(loc, resultTypes, args, instanceSym, entitySym,
                                   builder.getArrayAttr({}), builder.getArrayAttr({}));

  // Return results
  llvm::SmallVector<mlir::Value, 4> results;
  for (auto result : callOp.getResults())
    results.push_back(result);
  return results;
}

llvm::SmallVector<mlir::Value, 4>
CallBuilder::buildThisCall(llvm::StringRef entity,
                           llvm::ArrayRef<mlir::Value> args,
                           mlir::OpBuilder &builder, mlir::Location loc) {

  // Build callee symbol reference: @this
  auto thisSym =
      mlir::SymbolRefAttr::get(builder.getContext(), "this");

  // Build entity symbol reference: @entity
  auto entitySym = mlir::SymbolRefAttr::get(builder.getContext(), entity);

  // Create call operation
  // CallOp signature: (TypeRange outputs, ValueRange inputs, callee, methodOrValue, arg_attrs, res_attrs)
  llvm::SmallVector<mlir::Type> resultTypes;  // Empty, will be inferred
  auto callOp =
      builder.create<cmt2::CallOp>(loc, resultTypes, args, thisSym, entitySym,
                                   builder.getArrayAttr({}), builder.getArrayAttr({}));

  // Return results
  llvm::SmallVector<mlir::Value, 4> results;
  for (auto result : callOp.getResults())
    results.push_back(result);
  return results;
}
