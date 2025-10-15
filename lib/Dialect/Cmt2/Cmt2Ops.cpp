//===- Cmt2Ops.cpp - Handshake MLIR Operations -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Cmt2 operations struct.
//
//===----------------------------------------------------------------------===//
#include "circt/Dialect/Cmt2/Cmt2Ops.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/IR/PatternMatch.h"

using namespace mlir;
using namespace circt::cmt2;
using namespace circt::igraph;

namespace circt {
namespace cmt2 {

//===----------------------------------------------------------------------===//
// Module-like Operations (ModuleOp, ExtModuleFirrtlOp)
//===----------------------------------------------------------------------===//

static ParseResult parseModuleLikeOp(OpAsmParser &parser,
                                      OperationState &result,
                                      bool isExtModule = false) {
  auto builder = parser.getBuilder();

  // Parse the symbol name
  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, SymbolTable::getSymbolAttrName(),
                             result.attributes))
    return failure();

  // For ext module, parse : @extModuleName
  if (isExtModule) {
    FlatSymbolRefAttr extModNameAttr;
    if (parser.parseColon() ||
        parser.parseAttribute(extModNameAttr, "ext_module_name", result.attributes))
      return failure();
  }

  // Parse the argument list using MLIR's built-in parser
  SmallVector<OpAsmParser::Argument> args;
  if (parser.parseArgumentList(args, OpAsmParser::Delimiter::OptionalParen,
                                /*allowType=*/true, /*allowAttrs=*/false))
    return failure();

  // Extract argument names
  SmallVector<StringRef> argNames;
  for (auto &arg : args) {
    argNames.push_back(arg.ssaName.name.drop_front());
  }

  // Store argument names
  result.addAttribute("argNames", builder.getStrArrayAttr(argNames));

  // Parse the optional attribute dict before body
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes))
    return failure();

  // Parse the body region
  auto *body = result.addRegion();
  if (parser.parseRegion(*body, args))
    return failure();

  // Parse the optional trailing attribute dict after body (without keyword)
  if (parser.parseOptionalAttrDict(result.attributes))
    return failure();

  return success();
}

static void printModuleLikeOp(OpAsmPrinter &p, Operation *op,
                               ArrayAttr argNames, Region &body,
                               ArrayRef<StringRef> elidedAttrs) {
  // Print port list
  Block &bodyBlock = body.front();
  if (!bodyBlock.getArguments().empty()) {
    p << '(';
    llvm::interleaveComma(llvm::zip(argNames, bodyBlock.getArguments()), p,
                         [&](auto tuple) {
                           auto [name, arg] = tuple;
                           p.printOperand(arg);
                           p << ": ";
                           p.printType(arg.getType());
                         });
    p << ')';
  }

  // Print body
  p << ' ';
  p.printRegion(body, /*printEntryBlockArgs=*/false);

  // Print trailing attributes after body
  p.printOptionalAttrDictWithKeyword(op->getAttrs(), elidedAttrs);
}

static void getAsmBlockArgumentNamesImpl(ArrayAttr argNames, Region &region,
                                          OpAsmSetValueNameFn setNameFn) {
  if (region.empty())
    return;
  auto *block = &region.front();
  for (auto [idx, arg] : llvm::enumerate(block->getArguments())) {
    if (idx < argNames.size())
      setNameFn(arg, cast<StringAttr>(argNames[idx]).getValue());
  }
}

//===----------------------------------------------------------------------===//
// ModuleOp
//===----------------------------------------------------------------------===//

ParseResult ModuleOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseModuleLikeOp(parser, result, false);
}

void ModuleOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getSymName());

  SmallVector<StringRef> elidedAttrs = {"sym_name", "argNames"};
  printModuleLikeOp(p, *this, getArgNames(), getBody(), elidedAttrs);
}

void ModuleOp::getAsmBlockArgumentNames(Region &region,
                                         OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

//===----------------------------------------------------------------------===//
// ExtModuleFirrtlOp
//===----------------------------------------------------------------------===//

ParseResult ExtModuleFirrtlOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseModuleLikeOp(parser, result, true);
}

void ExtModuleFirrtlOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getSymName());
  p << " : ";
  p.printSymbolName(getExtModuleName());

  SmallVector<StringRef> elidedAttrs = {"sym_name", "ext_module_name", "argNames"};
  printModuleLikeOp(p, *this, getArgNames(), getBody(), elidedAttrs);
}

void ExtModuleFirrtlOp::getAsmBlockArgumentNames(Region &region,
                                               OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

//===----------------------------------------------------------------------===//
// Function-like Operations (RuleOp, MethodOp, ValueOp)
//===----------------------------------------------------------------------===//

// Helper function to parse function-like operations with two regions
static ParseResult parseFunctionLikeOp(OpAsmParser &parser,
                                        OperationState &result,
                                        bool hasBodyResults) {
  auto builder = parser.getBuilder();

  // Parse the symbol name
  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, SymbolTable::getSymbolAttrName(),
                             result.attributes))
    return failure();

  // Parse the argument list
  SmallVector<OpAsmParser::Argument> args;
  if (parser.parseArgumentList(args, OpAsmParser::Delimiter::OptionalParen,
                                /*allowType=*/true, /*allowAttrs=*/false))
    return failure();

  // Extract argument names and types
  SmallVector<StringRef> argNames;
  SmallVector<Type> argTypes;
  for (auto &arg : args) {
    argNames.push_back(arg.ssaName.name.drop_front());
    argTypes.push_back(arg.type);
  }

  // Parse `->` and result types (for body region)
  SmallVector<Type> bodyResTypes;
  if (parser.parseArrow())
    return failure();

  if (hasBodyResults) {
    // Parse result types in parentheses (may be empty)
    if (parser.parseLParen())
      return failure();
    // Try to parse optional type list - if it fails, list is empty
    if (parser.parseOptionalRParen()) {
      // Not empty, parse the type list
      if (parser.parseTypeList(bodyResTypes) || parser.parseRParen())
        return failure();
    }
  } else {
    // Parse single result type
    if (parser.parseType(bodyResTypes.emplace_back()))
      return failure();
  }

  // Store argument names and function type
  result.addAttribute("argNames", builder.getStrArrayAttr(argNames));

  // The function_type represents the shared arguments
  auto funcType = builder.getFunctionType(argTypes, bodyResTypes);
  result.addAttribute("function_type", TypeAttr::get(funcType));

  // Initialize empty bodyResNames
  if (hasBodyResults) {
    SmallVector<Attribute> resNames;
    for (size_t i = 0; i < bodyResTypes.size(); ++i)
      resNames.push_back(builder.getStringAttr("res" + std::to_string(i)));
    result.addAttribute("bodyResNames", builder.getArrayAttr(resNames));
  }

  // Parse optional attribute dict
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes))
    return failure();

  // Parse guard region (with shared arguments)
  auto *guardRegion = result.addRegion();
  if (parser.parseRegion(*guardRegion, args))
    return failure();

  // Parse body region (with shared arguments)
  auto *bodyRegion = result.addRegion();
  if (parser.parseRegion(*bodyRegion, args))
    return failure();

  // Ensure both regions have blocks with implicit terminators
  OpBuilder opBuilder(builder.getContext());

  // Handle guard region
  if (guardRegion->empty()) {
    // Create block if region is empty
    Block *guardBlock = new Block();
    guardBlock->addArguments(
        argTypes, SmallVector<Location>(argTypes.size(), result.location));
    guardRegion->push_back(guardBlock);
  }

  // Add implicit terminator to guard block if it doesn't have one
  Block &guardBlock = guardRegion->front();
  if (guardBlock.empty() || !guardBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&guardBlock);
    opBuilder.create<ReturnOp>(result.location);
  }

  // Handle body region
  if (bodyRegion->empty()) {
    // Create block if region is empty
    Block *bodyBlock = new Block();
    bodyBlock->addArguments(
        argTypes, SmallVector<Location>(argTypes.size(), result.location));
    bodyRegion->push_back(bodyBlock);
  }

  // Add implicit terminator to body block if it doesn't have one
  Block &bodyBlock = bodyRegion->front();
  if (bodyBlock.empty() || !bodyBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&bodyBlock);
    opBuilder.create<ReturnOp>(result.location);
  }

  return success();
}

// Helper function to print function-like operations
static void printFunctionLikeOp(OpAsmPrinter &p, Operation *op,
                                 ArrayAttr argNames, FunctionType funcType,
                                 Region &guard, Region &body) {
  p << ' ';

  // Get argument types from function type
  auto argTypes = funcType.getInputs();
  auto resTypes = funcType.getResults();

  // Print arguments
  if (!argTypes.empty()) {
    Block &guardBlock = guard.front();
    p << '(';
    llvm::interleaveComma(llvm::zip(argNames, guardBlock.getArguments()), p,
                         [&](auto tuple) {
                           auto [name, arg] = tuple;
                           p.printOperand(arg);
                           p << ": ";
                           p.printType(arg.getType());
                         });
    p << ')';
  } else {
    p << "()";
  }

  // Print result types
  p << " -> ";
  if (resTypes.size() == 1) {
    p.printType(resTypes[0]);
  } else {
    p << '(';
    llvm::interleaveComma(resTypes, p, [&](Type type) {
      p.printType(type);
    });
    p << ')';
  }

  // Print attributes (excluding the ones we handle specially)
  SmallVector<StringRef> elidedAttrs = {"sym_name", "function_type", "argNames",
                                         "guardResName", "bodyResNames"};
  p.printOptionalAttrDictWithKeyword(op->getAttrs(), elidedAttrs);

  // Print regions
  p << ' ';
  p.printRegion(guard, /*printEntryBlockArgs=*/false);
  p << ' ';
  p.printRegion(body, /*printEntryBlockArgs=*/false);
}

//===----------------------------------------------------------------------===//
// RuleOp
//===----------------------------------------------------------------------===//

ParseResult RuleOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseFunctionLikeOp(parser, result, /*hasBodyResults=*/false);
}

void RuleOp::print(OpAsmPrinter &p) {
  p.printSymbolName(getSymName());
  printFunctionLikeOp(p, *this, getArgNames(), getFunctionType(),
                      getGuard(), getBody());
}

void RuleOp::getAsmBlockArgumentNames(Region &region,
                                       OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

// Cmt2FunctionLike methods for RuleOp
Region *RuleOp::getCallableRegion() { return &getBody(); }
ArrayRef<Type> RuleOp::getArgumentTypes() { return getFunctionType().getInputs(); }
ArrayRef<Type> RuleOp::getResultTypes() { return getFunctionType().getResults(); }
bool RuleOp::isExternal() { return getBody().empty(); }
Region &RuleOp::getFunctionBody() { return getBody(); }

//===----------------------------------------------------------------------===//
// MethodOp
//===----------------------------------------------------------------------===//

ParseResult MethodOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseFunctionLikeOp(parser, result, /*hasBodyResults=*/true);
}

void MethodOp::print(OpAsmPrinter &p) {
  p.printSymbolName(getSymName());
  printFunctionLikeOp(p, *this, getArgNames(), getFunctionType(),
                      getGuard(), getBody());
}

void MethodOp::getAsmBlockArgumentNames(Region &region,
                                         OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

// Cmt2FunctionLike methods for MethodOp
Region *MethodOp::getCallableRegion() { return &getBody(); }
ArrayRef<Type> MethodOp::getArgumentTypes() { return getFunctionType().getInputs(); }
ArrayRef<Type> MethodOp::getResultTypes() { return getFunctionType().getResults(); }
bool MethodOp::isExternal() { return getBody().empty(); }
Region &MethodOp::getFunctionBody() { return getBody(); }

//===----------------------------------------------------------------------===//
// ValueOp
//===----------------------------------------------------------------------===//

ParseResult ValueOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseFunctionLikeOp(parser, result, /*hasBodyResults=*/true);
}

void ValueOp::print(OpAsmPrinter &p) {
  p.printSymbolName(getSymName());
  printFunctionLikeOp(p, *this, getArgNames(), getFunctionType(),
                      getGuard(), getBody());
}

void ValueOp::getAsmBlockArgumentNames(Region &region,
                                        OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

// Cmt2FunctionLike methods for ValueOp
Region *ValueOp::getCallableRegion() { return &getBody(); }
ArrayRef<Type> ValueOp::getArgumentTypes() { return getFunctionType().getInputs(); }
ArrayRef<Type> ValueOp::getResultTypes() { return getFunctionType().getResults(); }
bool ValueOp::isExternal() { return getBody().empty(); }
Region &ValueOp::getFunctionBody() { return getBody(); }

//===----------------------------------------------------------------------===//
// BindMethodOp
//===----------------------------------------------------------------------===//

// Cmt2FunctionLike methods for BindMethodOp
Region *BindMethodOp::getCallableRegion() { return nullptr; }
ArrayRef<Type> BindMethodOp::getArgumentTypes() { return getFunctionType().getInputs(); }
ArrayRef<Type> BindMethodOp::getResultTypes() { return getFunctionType().getResults(); }

// BindMethodOp doesn't have a body since it's a binding to external HW
bool BindMethodOp::isExternal() { return true; }
Region &BindMethodOp::getFunctionBody() {
  llvm_unreachable("BindMethodOp has no body region");
}

//===----------------------------------------------------------------------===//
// BindValueOp
//===----------------------------------------------------------------------===//

// Cmt2FunctionLike methods for BindValueOp
Region *BindValueOp::getCallableRegion() { return nullptr; }
ArrayRef<Type> BindValueOp::getArgumentTypes() { return getFunctionType().getInputs(); }
ArrayRef<Type> BindValueOp::getResultTypes() { return getFunctionType().getResults(); }

// BindValueOp doesn't have a body since it's a binding to external HW
bool BindValueOp::isExternal() { return true; }
Region &BindValueOp::getFunctionBody() {
  llvm_unreachable("BindValueOp has no body region");
}

//===----------------------------------------------------------------------===//
// CallOp
//===----------------------------------------------------------------------===//

// CallOpInterface methods for CallOp
CallInterfaceCallable CallOp::getCallableForCallee() {
  // Return the methodOrValue symbol as the callee
  return getMethodOrValueAttr();
}

void CallOp::setCalleeFromCallable(CallInterfaceCallable callee) {
  // Set the methodOrValue attribute from the callable
  if (auto symbolRef = callee.dyn_cast<SymbolRefAttr>())
    setMethodOrValueAttr(symbolRef);
}

Operation::operand_range CallOp::getArgOperands() {
  return getInputs();
}

MutableOperandRange CallOp::getArgOperandsMutable() {
  return getInputsMutable();
}

//===----------------------------------------------------------------------===//
// InstanceOp
//===----------------------------------------------------------------------===//

// Get the referenced module (Cmt2ModuleLike) for this instance
Cmt2ModuleLike InstanceOp::getReferencedModule() {
  auto circuit = getOperation()->getParentOfType<CircuitOp>();
  if (!circuit)
    return nullptr;
  return circuit.lookupSymbol<Cmt2ModuleLike>(getModuleNameAttr().getAttr());
}

// Cmt2ModuleLike getReferenceModule(InstanceOp instance) {
//   auto circuit =
//       instance.getOperation()->getParentOfType<circt::cmt2::CircuitOp>();
//   if (!circuit)
//     return nullptr;
//   return circuit.lookupSymbol<Cmt2ModuleLike>(instance.moduleNameAttr());
// }
// llvm::SmallVector<Cmt2FunctionLike, 4> getFunctions(Cmt2ModuleLike module) {
//   llvm::SmallVector<Cmt2FunctionLike, 4> functions;
//   module.getOperation()->walk(
//       [&](Cmt2FunctionLike function) { functions.push_back(function); });
//   return functions;
// }
// llvm::SmallVector<MethodOp, 4> getMethods(ModuleOp module) {
//   llvm::SmallVector<MethodOp, 4> methods;
//   module.getOperation()->walk(
//       [&](MethodOp method) { methods.push_back(method); });
//   return methods;
// }

// llvm::SmallVector<ValueOp, 4> getValues(ModuleOp module) {
//   llvm::SmallVector<ValueOp, 4> values;
//   module.getOperation()->walk([&](ValueOp value) { values.push_back(value); });
//   return values;
// }

// llvm::SmallVector<RuleOp, 4> getRules(ModuleOp module) {
//   llvm::SmallVector<RuleOp, 4> rules;
//   module.getOperation()->walk([&](RuleOp rule) { rules.push_back(rule); });
//   return rules;
// }

// llvm::SmallVector<InstanceOp, 4> getInstances(ModuleOp module) {
//   llvm::SmallVector<InstanceOp, 4> instances;
//   module.getOperation()->walk(
//       [&](InstanceOp instance) { instances.push_back(instance); });
//   return instances;
// }

// llvm::SmallVector<BindMethodOp, 4> getMethods(ExtModuleOp module) {
//   llvm::SmallVector<BindMethodOp, 4> methods;
//   module.getOperation()->walk(
//       [&](BindMethodOp method) { methods.push_back(method); });
//   return methods;
// }

// llvm::SmallVector<BindValueOp, 4> getValues(ExtModuleOp module) {
//   llvm::SmallVector<BindValueOp, 4> values;
//   module.getOperation()->walk(
//       [&](BindValueOp value) { values.push_back(value); });
//   return values;
// }

// llvm::SmallVector<InstanceOp, 4> getInstances(Cmt2ModuleLike module) {
//   llvm::SmallVector<InstanceOp, 4> instances;
//   module.getOperation()->walk(
//       [&](InstanceOp instance) { instances.push_back(instance); });
//   return instances;
// }

} // namespace cmt2
} // namespace circt

// Provide the autogenerated implementation guts for the Op classes.
#define GET_OP_CLASSES
#include "circt/Dialect/Cmt2/Cmt2.cpp.inc"