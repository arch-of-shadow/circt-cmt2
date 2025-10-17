//===- Interface.cpp - ECMT2 Interface Implementation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Cmt2/ECMT2/Interface.h"
#include "circt/Dialect/Cmt2/ECMT2/Module.h"
#include "circt/Dialect/Cmt2/ECMT2/Circuit.h"
#include "circt/Dialect/Cmt2/Cmt2Ops.h"

using namespace circt;
using namespace cmt2::ecmt2;

//===----------------------------------------------------------------------===//
// InterfaceDecl
//===----------------------------------------------------------------------===//

InterfaceDecl::InterfaceDecl(llvm::StringRef name, llvm::StringRef type,
                             Module *parent)
    : name_(name.str()), type_(type.str()), loc_(parent->getLoc()) {

  auto &builder = parent->getBuilder();

  // Create InterfaceDeclOp
  // InterfaceDeclOp::build expects (interface, sym_name)
  auto interfaceAttr = mlir::SymbolRefAttr::get(builder.getContext(), type);
  auto nameAttr = builder.getStringAttr(name);

  op_ = builder.create<cmt2::InterfaceDeclOp>(loc_, interfaceAttr, nameAttr);
}

llvm::SmallVector<mlir::Value, 4>
InterfaceDecl::callMethod(llvm::StringRef method,
                          llvm::ArrayRef<mlir::Value> args,
                          mlir::OpBuilder &builder) {

  // Build callee symbol reference: @interface
  auto ifaceSym = mlir::SymbolRefAttr::get(builder.getContext(), name_);

  // Build method symbol reference: @method
  auto methodSym = mlir::SymbolRefAttr::get(builder.getContext(), method);

  // Look up result types from the interface definition
  llvm::SmallVector<mlir::Type> resultTypes;

  // Find the InterfaceOp by walking up to the top-level module
  auto topModule = op_.getOperation()->getParentOfType<mlir::ModuleOp>();
  if (topModule) {
    topModule->walk([&](cmt2::InterfaceOp ifaceOp) {
      if (ifaceOp.getSymName() == type_) {
        // Found the interface - now look for the method/value
        ifaceOp.walk([&](mlir::Operation *op) {
          if (auto methodOp = mlir::dyn_cast<cmt2::MethodOp>(op)) {
            if (methodOp.getSymName() == method) {
              auto funcType = methodOp.getFunctionType();
              resultTypes.append(funcType.getResults().begin(),
                                 funcType.getResults().end());
              return mlir::WalkResult::interrupt();
            }
          } else if (auto valueOp = mlir::dyn_cast<cmt2::ValueOp>(op)) {
            if (valueOp.getSymName() == method) {
              auto funcType = valueOp.getFunctionType();
              resultTypes.append(funcType.getResults().begin(),
                                 funcType.getResults().end());
              return mlir::WalkResult::interrupt();
            }
          }
          return mlir::WalkResult::advance();
        });
        return mlir::WalkResult::interrupt();
      }
      return mlir::WalkResult::advance();
    });
  }

  // Create call operation with proper result types
  auto callOp =
      builder.create<cmt2::CallOp>(loc_, resultTypes, args, ifaceSym, methodSym,
                                   builder.getArrayAttr({}), builder.getArrayAttr({}));

  // Return results
  llvm::SmallVector<mlir::Value, 4> results;
  for (auto result : callOp.getResults())
    results.push_back(result);
  return results;
}

llvm::SmallVector<mlir::Value, 4>
InterfaceDecl::callValue(llvm::StringRef value, mlir::OpBuilder &builder) {
  return callMethod(value, {}, builder);
}

//===----------------------------------------------------------------------===//
// InterfaceDef
//===----------------------------------------------------------------------===//

InterfaceDef::InterfaceDef(llvm::StringRef name, llvm::StringRef type,
                           Module *parent)
    : name_(name.str()), type_(type.str()), builder_(parent->getBuilder()),
      loc_(parent->getLoc()) {

  // Create InterfaceDefOp
  // InterfaceDefOp::build expects (interface, sym_name, methods)
  auto interfaceAttr = mlir::SymbolRefAttr::get(builder_.getContext(), type);
  auto nameAttr = builder_.getStringAttr(name);

  // Create with empty bindings initially
  op_ = builder_.create<cmt2::InterfaceDefOp>(loc_, interfaceAttr, nameAttr,
                                               builder_.getArrayAttr({}));
}

InterfaceDef &InterfaceDef::bind(llvm::StringRef instance,
                                llvm::StringRef instanceMethod,
                                llvm::StringRef interfaceMethod) {
  // Store binding for finalize
  bindings_.push_back(
      std::make_tuple(instance.str(), instanceMethod.str(), interfaceMethod.str()));
  return *this;
}

void InterfaceDef::finalize() {
  // Build bindings array attribute
  // Format: [[@instance, @method, @interfaceMethod], ...]
  llvm::SmallVector<mlir::Attribute> bindingAttrs;
  for (auto &binding : bindings_) {
    auto bindingArray = builder_.getArrayAttr({
        mlir::FlatSymbolRefAttr::get(builder_.getContext(), std::get<0>(binding)),
        mlir::FlatSymbolRefAttr::get(builder_.getContext(), std::get<1>(binding)),
        mlir::FlatSymbolRefAttr::get(builder_.getContext(), std::get<2>(binding)),
    });
    bindingAttrs.push_back(bindingArray);
  }

  // Update the operation
  op_.setMethodsAttr(builder_.getArrayAttr(bindingAttrs));
}

//===----------------------------------------------------------------------===//
// Interface (Circuit-level)
//===----------------------------------------------------------------------===//

Interface::Interface(llvm::StringRef name, Circuit *parent)
    : name_(name.str()), builder_(parent->getBuilder()),
      loc_(parent->getLoc()) {

  // Create InterfaceOp
  auto nameAttr = builder_.getStringAttr(name);
  op_ = builder_.create<cmt2::InterfaceOp>(loc_, nameAttr);

  // Create the body block for the interface
  auto *bodyBlock = new mlir::Block();
  op_.getBody().push_back(bodyBlock);

  // Set insertion point inside the interface
  builder_.setInsertionPointToEnd(bodyBlock);
}

Interface &Interface::addMethod(
    llvm::StringRef name,
    llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
    llvm::ArrayRef<mlir::Type> results) {

  auto savedIP = builder_.saveInsertionPoint();
  builder_.setInsertionPointToEnd(&op_.getBody().front());

  // Build argument types and names
  llvm::SmallVector<mlir::Type> argTypes;
  llvm::SmallVector<mlir::Attribute> argNames;
  for (auto &arg : args) {
    argTypes.push_back(arg.second);
    argNames.push_back(builder_.getStringAttr(arg.first));
  }

  // Build result names (empty for interface methods)
  llvm::SmallVector<mlir::Attribute> resultNames;
  for (size_t i = 0; i < results.size(); ++i) {
    resultNames.push_back(builder_.getStringAttr(""));
  }

  // Build function type
  auto funcType = builder_.getFunctionType(argTypes, results);

  // Create MethodOp stub (empty guard and body will be created automatically)
  auto nameAttr = builder_.getStringAttr(name);
  auto methodOp = builder_.create<cmt2::MethodOp>(
      loc_, nameAttr, mlir::TypeAttr::get(funcType),
      builder_.getArrayAttr(argNames),
      mlir::StringAttr(),  // guardResName (optional)
      builder_.getArrayAttr(resultNames),  // bodyResNames
      builder_.getArrayAttr({}),  // arg_attrs
      builder_.getArrayAttr({})); // res_attrs

  // Create empty guard and body regions with proper terminator
  auto &guardRegion = methodOp.getGuard();
  auto *guardBlock = builder_.createBlock(&guardRegion);
  builder_.setInsertionPointToEnd(guardBlock);
  builder_.create<cmt2::ReturnOp>(loc_, mlir::ValueRange{});

  auto &bodyRegion = methodOp.getBody();
  auto *bodyBlock = builder_.createBlock(&bodyRegion);
  for (auto argType : argTypes) {
    bodyBlock->addArgument(argType, loc_);
  }
  builder_.setInsertionPointToEnd(bodyBlock);
  builder_.create<cmt2::ReturnOp>(loc_, mlir::ValueRange{});

  builder_.restoreInsertionPoint(savedIP);
  return *this;
}

Interface &Interface::addValue(
    llvm::StringRef name,
    llvm::ArrayRef<std::pair<std::string, mlir::Type>> args,
    llvm::ArrayRef<mlir::TypeAttr> results) {

  auto savedIP = builder_.saveInsertionPoint();
  builder_.setInsertionPointToEnd(&op_.getBody().front());

  // Build argument types and names
  llvm::SmallVector<mlir::Type> argTypes;
  llvm::SmallVector<mlir::Attribute> argNames;
  for (auto &arg : args) {
    argTypes.push_back(arg.second);
    argNames.push_back(builder_.getStringAttr(arg.first));
  }

  // Extract result types from TypeAttrs
  llvm::SmallVector<mlir::Type> resultTypes;
  llvm::SmallVector<mlir::Attribute> resultNames;
  for (auto typeAttr : results) {
    resultTypes.push_back(typeAttr.getValue());
    resultNames.push_back(builder_.getStringAttr(""));
  }

  // Build function type
  auto funcType = builder_.getFunctionType(argTypes, resultTypes);

  // Create ValueOp stub
  auto nameAttr = builder_.getStringAttr(name);
  auto valueOp = builder_.create<cmt2::ValueOp>(
      loc_, nameAttr, mlir::TypeAttr::get(funcType),
      builder_.getArrayAttr(argNames),
      mlir::StringAttr(),  // guardResName (optional)
      builder_.getArrayAttr(resultNames),  // bodyResNames
      builder_.getArrayAttr({}),  // arg_attrs
      builder_.getArrayAttr({})); // res_attrs

  // Create empty guard and body regions with proper terminator
  auto &guardRegion = valueOp.getGuard();
  auto *guardBlock = builder_.createBlock(&guardRegion);
  builder_.setInsertionPointToEnd(guardBlock);
  builder_.create<cmt2::ReturnOp>(loc_, mlir::ValueRange{});

  auto &bodyRegion = valueOp.getBody();
  auto *bodyBlock = builder_.createBlock(&bodyRegion);
  for (auto argType : argTypes) {
    bodyBlock->addArgument(argType, loc_);
  }
  builder_.setInsertionPointToEnd(bodyBlock);

  // Interface stubs have empty bodies - just return
  builder_.create<cmt2::ReturnOp>(loc_, mlir::ValueRange{});

  builder_.restoreInsertionPoint(savedIP);
  return *this;
}
