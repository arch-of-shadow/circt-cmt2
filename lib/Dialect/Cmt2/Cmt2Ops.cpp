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

  // Print trailing attributes after body (without "attributes" keyword)
  p.printOptionalAttrDict(op->getAttrs(), elidedAttrs);
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

  SmallVector<StringRef> elidedAttrs = {"sym_name", "ext_module_name", "argNames", "methods", "values"};
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

  // Parse optional `->` and result types (for body region)
  SmallVector<Type> bodyResTypes;
  if (succeeded(parser.parseOptionalArrow())) {
    // Arrow present, parse result types in parentheses (always)
    if (parser.parseLParen())
      return failure();
    // Try to parse optional type list - if it fails, list is empty
    if (parser.parseOptionalRParen()) {
      // Not empty, parse the type list
      if (parser.parseTypeList(bodyResTypes) || parser.parseRParen())
        return failure();
    }
  }
  // No arrow present means empty results (bodyResTypes stays empty)
  // This is allowed for all function-like ops (rules, methods, values)

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

  // Print result types (always in parentheses for consistency)
  p << " -> (";
  llvm::interleaveComma(resTypes, p, [&](Type type) {
    p.printType(type);
  });
  p << ')';

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
  p << ' ';
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
  p << ' ';
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
  p << ' ';
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

//===----------------------------------------------------------------------===//
// Interface-related Helper Functions
//===----------------------------------------------------------------------===//

/// Get all InterfaceDefOp operations in a module
llvm::SmallVector<InterfaceDefOp, 4> getInterfaceDefs(ModuleOp module) {
  llvm::SmallVector<InterfaceDefOp, 4> interfaceDefs;
  for (auto &op : module.getOps()) {
    if (auto defOp = llvm::dyn_cast<InterfaceDefOp>(op)) {
      interfaceDefs.push_back(defOp);
    }
  }
  return interfaceDefs;
}

/// Get all InterfaceDeclOp operations in a module
llvm::SmallVector<InterfaceDeclOp, 4> getInterfaceDecls(ModuleOp module) {
  llvm::SmallVector<InterfaceDeclOp, 4> interfaceDecls;
  for (auto &op : module.getOps()) {
    if (auto declOp = llvm::dyn_cast<InterfaceDeclOp>(op)) {
      interfaceDecls.push_back(declOp);
    }
  }
  return interfaceDecls;
}

/// Look up an InterfaceDefOp by symbol name in a module
InterfaceDefOp lookupInterfaceDef(ModuleOp module, mlir::StringRef name) {
  return mlir::SymbolTable::lookupNearestSymbolFrom<InterfaceDefOp>(
      module, mlir::StringAttr::get(module.getContext(), name));
}

/// Look up an InterfaceDeclOp by symbol name in a module
InterfaceDeclOp lookupInterfaceDecl(ModuleOp module, mlir::StringRef name) {
  return mlir::SymbolTable::lookupNearestSymbolFrom<InterfaceDeclOp>(
      module, mlir::StringAttr::get(module.getContext(), name));
}

/// Look up an InterfaceOp by symbol name in a circuit
InterfaceOp lookupInterface(CircuitOp circuit, mlir::StringRef name) {
  return mlir::SymbolTable::lookupNearestSymbolFrom<InterfaceOp>(
      circuit, mlir::StringAttr::get(circuit.getContext(), name));
}

/// Get the InterfaceOp that a decl refers to
InterfaceOp getInterfaceForDecl(InterfaceDeclOp decl) {
  auto circuit = decl->getParentOfType<CircuitOp>();
  if (!circuit)
    return nullptr;
  return lookupInterface(circuit, decl.getInterface().getLeafReference());
}

/// Get the InterfaceOp that a def refers to
InterfaceOp getInterfaceForDef(InterfaceDefOp def) {
  auto circuit = def->getParentOfType<CircuitOp>();
  if (!circuit)
    return nullptr;
  return lookupInterface(circuit, def.getInterface().getLeafReference());
}

/// Resolve an interface call to the actual instance and method/value
/// Returns a pair of (instance symbol, method/value symbol) or (nullptr, nullptr) if not found
std::pair<mlir::SymbolRefAttr, mlir::SymbolRefAttr>
resolveInterfaceCall(ModuleOp module, mlir::SymbolRefAttr interfaceDefName,
                     mlir::SymbolRefAttr interfaceMethodName) {
  auto interfaceDef = lookupInterfaceDef(module, interfaceDefName.getLeafReference());
  if (!interfaceDef)
    return {nullptr, nullptr};

  // InterfaceDefOp has methods attribute: [[@inst, @instMethod, @ifaceMethod], ...]
  auto methodsAttr = interfaceDef.getMethods();
  for (auto methodEntry : methodsAttr) {
    auto arrayAttr = llvm::cast<mlir::ArrayAttr>(methodEntry);
    if (arrayAttr.size() >= 3) {
      // Format: [@instance, @instanceMethod, @interfaceMethod]
      auto ifaceMethodRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[2]);
      if (ifaceMethodRef.getLeafReference() == interfaceMethodName.getLeafReference()) {
        // Found the mapping
        auto instanceRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[0]);
        auto instanceMethodRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[1]);
        return {instanceRef, instanceMethodRef};
      }
    }
  }

  return {nullptr, nullptr};
}

/// Check if a CallOp is calling through an interface (i.e., callee is an InterfaceDefOp)
bool isInterfaceCall(CallOp callOp) {
  auto parentModule = callOp->getParentOfType<ModuleOp>();
  if (!parentModule)
    return false;

  auto calleeAttr = callOp.getCalleeAttr();
  return lookupInterfaceDef(parentModule, calleeAttr.getLeafReference()) != nullptr;
}

/// Get interface bindings for an instance
/// Returns a map from interface decl name to interface def name
llvm::DenseMap<mlir::StringAttr, mlir::StringAttr>
getInterfaceBindings(InstanceOp instance) {
  llvm::DenseMap<mlir::StringAttr, mlir::StringAttr> bindings;

  if (auto interfaceBinds = instance.getInterfaceBinds()) {
    for (auto bindAttr : *interfaceBinds) {
      auto arrayAttr = llvm::cast<mlir::ArrayAttr>(bindAttr);
      if (arrayAttr.size() >= 2) {
        // Format: [@interfaceDefName, @interfaceDeclName]
        auto defRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[0]);
        auto declRef = llvm::cast<mlir::SymbolRefAttr>(arrayAttr[1]);
        bindings[declRef.getLeafReference()] = defRef.getLeafReference();
      }
    }
  }

  return bindings;
}

/// Get all function-like operations (RuleOp, MethodOp, ValueOp) in a module
llvm::SmallVector<Cmt2FunctionLike, 4> getFunctions(ModuleOp module) {
  llvm::SmallVector<Cmt2FunctionLike, 4> functions;
  for (auto &op : module.getOps()) {
    if (auto func = llvm::dyn_cast<Cmt2FunctionLike>(op)) {
      functions.push_back(func);
    }
  }
  return functions;
}

/// Get all instances in a module
llvm::SmallVector<InstanceOp, 4> getInstances(ModuleOp module) {
  llvm::SmallVector<InstanceOp, 4> instances;
  for (auto &op : module.getOps()) {
    if (auto instance = llvm::dyn_cast<InstanceOp>(op)) {
      instances.push_back(instance);
    }
  }
  return instances;
}

// Commented out legacy helper functions
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