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
#include "circt/Dialect/Cmt2/Cmt2Types.h"
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
                                        OperationState &result) {
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
  SmallVector<Attribute> resNames;
  for (size_t i = 0; i < bodyResTypes.size(); ++i)
    resNames.push_back(builder.getStringAttr("res" + std::to_string(i)));
  result.addAttribute("bodyResNames", builder.getArrayAttr(resNames));

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
  auto builder = parser.getBuilder();

  // Parse the symbol name
  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, SymbolTable::getSymbolAttrName(),
                             result.attributes))
    return failure();

  // Parse the regular argument list
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

  // Parse optional tokens_in
  SmallVector<OpAsmParser::Argument> tokenArgs;
  SmallVector<StringRef> tokenInNames;
  SmallVector<Type> tokenInTypes;
  if (succeeded(parser.parseOptionalKeyword("tokens_in"))) {
    if (parser.parseArgumentList(tokenArgs, OpAsmParser::Delimiter::Paren,
                                  /*allowType=*/true, /*allowAttrs=*/false))
      return failure();
    for (auto &arg : tokenArgs) {
      tokenInNames.push_back(arg.ssaName.name.drop_front());
      tokenInTypes.push_back(arg.type);
    }
  }

  // Parse optional tokens_out
  SmallVector<Type> tokenOutTypes;
  if (succeeded(parser.parseOptionalKeyword("tokens_out"))) {
    if (parser.parseLParen())
      return failure();
    if (parser.parseOptionalRParen()) {
      if (parser.parseTypeList(tokenOutTypes) || parser.parseRParen())
        return failure();
    }
  }

  // Parse optional `->` and result types (for body region)
  SmallVector<Type> bodyResTypes;
  if (succeeded(parser.parseOptionalArrow())) {
    if (parser.parseLParen())
      return failure();
    if (parser.parseOptionalRParen()) {
      if (parser.parseTypeList(bodyResTypes) || parser.parseRParen())
        return failure();
    }
  }

  // Store argument names and function type
  result.addAttribute("argNames", builder.getStrArrayAttr(argNames));

  // The function_type represents the shared arguments
  auto funcType = builder.getFunctionType(argTypes, bodyResTypes);
  result.addAttribute("function_type", TypeAttr::get(funcType));

  // Initialize empty bodyResNames
  SmallVector<Attribute> resNames;
  for (size_t i = 0; i < bodyResTypes.size(); ++i)
    resNames.push_back(builder.getStringAttr("res" + std::to_string(i)));
  result.addAttribute("bodyResNames", builder.getArrayAttr(resNames));

  // Store token attributes if present
  if (!tokenInTypes.empty()) {
    result.addAttribute("token_in_types", builder.getTypeArrayAttr(tokenInTypes));
    result.addAttribute("token_in_names", builder.getStrArrayAttr(tokenInNames));
  }
  if (!tokenOutTypes.empty()) {
    result.addAttribute("token_out_types", builder.getTypeArrayAttr(tokenOutTypes));
  }

  // Parse optional attribute dict
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes))
    return failure();

  // Combine regular args and token args for region parsing
  SmallVector<OpAsmParser::Argument> allArgs;
  allArgs.append(args.begin(), args.end());
  allArgs.append(tokenArgs.begin(), tokenArgs.end());

  // All argument types for block creation
  SmallVector<Type> allArgTypes;
  allArgTypes.append(argTypes.begin(), argTypes.end());
  allArgTypes.append(tokenInTypes.begin(), tokenInTypes.end());

  // Parse guard region
  auto *guardRegion = result.addRegion();
  if (parser.parseRegion(*guardRegion, allArgs))
    return failure();

  // Parse body region
  auto *bodyRegion = result.addRegion();
  if (parser.parseRegion(*bodyRegion, allArgs))
    return failure();

  // Ensure both regions have blocks with implicit terminators
  OpBuilder opBuilder(builder.getContext());

  // Handle guard region
  if (guardRegion->empty()) {
    Block *guardBlock = new Block();
    guardBlock->addArguments(
        allArgTypes, SmallVector<Location>(allArgTypes.size(), result.location));
    guardRegion->push_back(guardBlock);
  }

  Block &guardBlock = guardRegion->front();
  if (guardBlock.empty() || !guardBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&guardBlock);
    opBuilder.create<ReturnOp>(result.location);
  }

  // Handle body region
  if (bodyRegion->empty()) {
    Block *bodyBlock = new Block();
    bodyBlock->addArguments(
        allArgTypes, SmallVector<Location>(allArgTypes.size(), result.location));
    bodyRegion->push_back(bodyBlock);
  }

  Block &bodyBlock = bodyRegion->front();
  if (bodyBlock.empty() || !bodyBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&bodyBlock);
    opBuilder.create<ReturnOp>(result.location);
  }

  return success();
}

void RuleOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getSymName());

  // Get types
  auto argTypes = getFunctionType().getInputs();
  auto resTypes = getFunctionType().getResults();
  Block &guardBlock = getGuard().front();

  // Print regular arguments
  p << '(';
  size_t numRegularArgs = argTypes.size();
  for (size_t i = 0; i < numRegularArgs; ++i) {
    if (i > 0) p << ", ";
    p.printOperand(guardBlock.getArgument(i));
    p << ": ";
    p.printType(argTypes[i]);
  }
  p << ')';

  // Print tokens_in if present
  if (hasTokenInputs()) {
    p << " tokens_in(";
    auto tokenInTypes = getTokenInTypes();
    for (size_t i = 0; i < getNumTokenInputs(); ++i) {
      if (i > 0) p << ", ";
      p.printOperand(guardBlock.getArgument(numRegularArgs + i));
      p << ": ";
      p.printType(cast<TypeAttr>((*tokenInTypes)[i]).getValue());
    }
    p << ')';
  }

  // Print tokens_out if present
  if (hasTokenOutputs()) {
    p << " tokens_out(";
    auto tokenOutTypes = getTokenOutTypes();
    llvm::interleaveComma(*tokenOutTypes, p, [&](Attribute attr) {
      p.printType(cast<TypeAttr>(attr).getValue());
    });
    p << ')';
  }

  // Print result types
  p << " -> (";
  llvm::interleaveComma(resTypes, p, [&](Type type) {
    p.printType(type);
  });
  p << ')';

  // Print attributes (excluding the ones we handle specially)
  SmallVector<StringRef> elidedAttrs = {"sym_name", "function_type", "argNames",
                                         "guardResName", "bodyResNames",
                                         "token_in_types", "token_in_names",
                                         "token_out_types"};
  p.printOptionalAttrDictWithKeyword((*this)->getAttrs(), elidedAttrs);

  // Print regions
  p << ' ';
  p.printRegion(getGuard(), /*printEntryBlockArgs=*/false);
  p << ' ';
  p.printRegion(getBody(), /*printEntryBlockArgs=*/false);
}

void RuleOp::getAsmBlockArgumentNames(Region &region,
                                       OpAsmSetValueNameFn setNameFn) {
  // Set names for regular arguments
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);

  // Set names for token arguments
  if (hasTokenInputs()) {
    auto tokenNames = getTokenInNames();
    size_t numRegularArgs = getFunctionType().getNumInputs();
    Block &block = region.front();
    for (size_t i = 0; i < getNumTokenInputs(); ++i) {
      if (numRegularArgs + i < block.getNumArguments()) {
        setNameFn(block.getArgument(numRegularArgs + i),
                  cast<StringAttr>((*tokenNames)[i]).getValue());
      }
    }
  }
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
  return parseFunctionLikeOp(parser, result);
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
  return parseFunctionLikeOp(parser, result);
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

//===----------------------------------------------------------------------===//
// Procedural Operations (ProcRuleOp, ProcMethodOp)
//===----------------------------------------------------------------------===//

// Helper function to parse procedural function-like operations
// These have guard + control regions, where control doesn't take arguments
static ParseResult parseProcFunctionLikeOp(OpAsmParser &parser,
                                            OperationState &result,
                                            bool hasBodyResNames = false) {
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

  // Parse optional `->` and result types
  SmallVector<Type> resTypes;
  if (succeeded(parser.parseOptionalArrow())) {
    if (parser.parseLParen())
      return failure();
    if (parser.parseOptionalRParen()) {
      if (parser.parseTypeList(resTypes) || parser.parseRParen())
        return failure();
    }
  }

  // Store argument names and function type
  result.addAttribute("argNames", builder.getStrArrayAttr(argNames));
  auto funcType = builder.getFunctionType(argTypes, resTypes);
  result.addAttribute("function_type", TypeAttr::get(funcType));

  // Initialize bodyResNames if needed
  if (hasBodyResNames) {
    SmallVector<Attribute> resNames;
    for (size_t i = 0; i < resTypes.size(); ++i)
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

  // Parse "control" keyword and control region (no arguments)
  if (parser.parseKeyword("control"))
    return failure();

  auto *controlRegion = result.addRegion();
  if (parser.parseRegion(*controlRegion, {}))
    return failure();

  // Handle guard region - add implicit terminator if needed
  OpBuilder opBuilder(builder.getContext());
  if (guardRegion->empty()) {
    Block *guardBlock = new Block();
    guardBlock->addArguments(
        argTypes, SmallVector<Location>(argTypes.size(), result.location));
    guardRegion->push_back(guardBlock);
  }
  Block &guardBlock = guardRegion->front();
  if (guardBlock.empty() || !guardBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&guardBlock);
    opBuilder.create<ReturnOp>(result.location);
  }

  // Handle control region - ensure it's not empty and has a terminator
  if (controlRegion->empty()) {
    Block *controlBlock = new Block();
    controlRegion->push_back(controlBlock);
  }
  Block &controlBlock = controlRegion->front();
  if (controlBlock.empty() || !controlBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&controlBlock);
    opBuilder.create<ProcControlEndOp>(result.location);
  }

  return success();
}

// Helper function to print procedural function-like operations
static void printProcFunctionLikeOp(OpAsmPrinter &p, Operation *op,
                                     ArrayAttr argNames, FunctionType funcType,
                                     Region &guard, Region &control) {
  p << ' ';

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
  p << " -> (";
  llvm::interleaveComma(resTypes, p, [&](Type type) {
    p.printType(type);
  });
  p << ')';

  // Print attributes
  SmallVector<StringRef> elidedAttrs = {"sym_name", "function_type", "argNames",
                                         "bodyResNames"};
  p.printOptionalAttrDictWithKeyword(op->getAttrs(), elidedAttrs);

  // Print guard region
  p << ' ';
  p.printRegion(guard, /*printEntryBlockArgs=*/false);

  // Print control keyword and region
  p << " control ";
  p.printRegion(control, /*printEntryBlockArgs=*/true);
}

//===----------------------------------------------------------------------===//
// ProcRuleOp
//===----------------------------------------------------------------------===//

ParseResult ProcRuleOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseProcFunctionLikeOp(parser, result, /*hasBodyResNames=*/false);
}

void ProcRuleOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getSymName());
  printProcFunctionLikeOp(p, *this, getArgNames(), getFunctionType(),
                          getGuard(), getControl());
}

void ProcRuleOp::getAsmBlockArgumentNames(Region &region,
                                           OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

//===----------------------------------------------------------------------===//
// ProcMethodOp
//===----------------------------------------------------------------------===//

ParseResult ProcMethodOp::parse(OpAsmParser &parser, OperationState &result) {
  auto builder = parser.getBuilder();

  // Parse the symbol name
  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, SymbolTable::getSymbolAttrName(),
                             result.attributes))
    return failure();

  // Parse optional `static<latency>`
  if (succeeded(parser.parseOptionalKeyword("static"))) {
    if (parser.parseLess())
      return failure();
    int64_t latency;
    if (parser.parseInteger(latency))
      return failure();
    if (parser.parseGreater())
      return failure();
    result.addAttribute("static_latency", builder.getI64IntegerAttr(latency));
  }

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

  // Parse optional `->` and result types
  SmallVector<Type> resTypes;
  if (succeeded(parser.parseOptionalArrow())) {
    if (parser.parseLParen())
      return failure();
    if (parser.parseOptionalRParen()) {
      if (parser.parseTypeList(resTypes) || parser.parseRParen())
        return failure();
    }
  }

  // Store argument names and function type
  result.addAttribute("argNames", builder.getStrArrayAttr(argNames));
  auto funcType = builder.getFunctionType(argTypes, resTypes);
  result.addAttribute("function_type", TypeAttr::get(funcType));

  // Initialize bodyResNames
  SmallVector<Attribute> resNames;
  for (size_t i = 0; i < resTypes.size(); ++i)
    resNames.push_back(builder.getStringAttr("res" + std::to_string(i)));
  result.addAttribute("bodyResNames", builder.getArrayAttr(resNames));

  // Parse optional attribute dict
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes))
    return failure();

  // Parse guard region (with shared arguments)
  auto *guardRegion = result.addRegion();
  if (parser.parseRegion(*guardRegion, args))
    return failure();

  // Parse "control" keyword and control region (no arguments)
  if (parser.parseKeyword("control"))
    return failure();

  auto *controlRegion = result.addRegion();
  if (parser.parseRegion(*controlRegion, {}))
    return failure();

  // Handle guard region - add implicit terminator if needed
  OpBuilder opBuilder(builder.getContext());
  if (guardRegion->empty()) {
    Block *guardBlock = new Block();
    guardBlock->addArguments(
        argTypes, SmallVector<Location>(argTypes.size(), result.location));
    guardRegion->push_back(guardBlock);
  }
  Block &guardBlock = guardRegion->front();
  if (guardBlock.empty() || !guardBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&guardBlock);
    opBuilder.create<ReturnOp>(result.location);
  }

  // Handle control region - ensure it's not empty and has a terminator
  if (controlRegion->empty()) {
    Block *controlBlock = new Block();
    controlRegion->push_back(controlBlock);
  }
  Block &controlBlock = controlRegion->front();
  if (controlBlock.empty() || !controlBlock.back().hasTrait<OpTrait::IsTerminator>()) {
    opBuilder.setInsertionPointToEnd(&controlBlock);
    opBuilder.create<ProcControlEndOp>(result.location);
  }

  return success();
}

void ProcMethodOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getSymName());

  // Print optional `static<latency>`
  if (auto latency = getStaticLatency()) {
    p << " static<" << *latency << ">";
  }

  auto funcType = getFunctionType();
  auto argTypes = funcType.getInputs();
  auto resTypes = funcType.getResults();

  // Print arguments
  if (!argTypes.empty()) {
    Block &guardBlock = getGuard().front();
    p << '(';
    llvm::interleaveComma(llvm::zip(getArgNames(), guardBlock.getArguments()), p,
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
  p << " -> (";
  llvm::interleaveComma(resTypes, p, [&](Type type) {
    p.printType(type);
  });
  p << ')';

  // Print attributes (excluding those printed specially)
  SmallVector<StringRef> elidedAttrs = {"sym_name", "function_type", "argNames",
                                         "bodyResNames", "static_latency"};
  p.printOptionalAttrDictWithKeyword((*this)->getAttrs(), elidedAttrs);

  // Print guard region
  p << ' ';
  p.printRegion(getGuard(), /*printEntryBlockArgs=*/false);

  // Print control keyword and region
  p << " control ";
  p.printRegion(getControl(), /*printEntryBlockArgs=*/true);
}

void ProcMethodOp::getAsmBlockArgumentNames(Region &region,
                                             OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

//===----------------------------------------------------------------------===//
// IfOp
//===----------------------------------------------------------------------===//

LogicalResult IfOp::verify() {
  // Check if the condition type is a 1-bit FIRRTL type
  auto conditionType = getCondition().getType();
  if (auto uintType = llvm::dyn_cast<circt::firrtl::UIntType>(conditionType)) {
    if (!uintType.getWidth() || uintType.getWidth().value() != 1) {
      return emitOpError("condition must be a 1-bit unsigned integer type");
    }
  } else if (auto sintType = llvm::dyn_cast<circt::firrtl::SIntType>(conditionType)) {
    if (!sintType.getWidth() || sintType.getWidth().value() != 1) {
      return emitOpError("condition must be a 1-bit type");
    }
  } else {
    return emitOpError("condition must be a FIRRTL integer type");
  }

  // If the operation has results, both regions must be present and yield matching types
  if (!getResults().empty()) {
    if (getElseRegion().empty()) {
      return emitOpError("must have an else region if it has results");
    }

    // Check that both regions end with a yield operation
    auto checkYield = [&](Region &region, StringRef regionName) -> LogicalResult {
      if (region.empty() || region.front().empty()) {
        return emitOpError(regionName + " region must not be empty");
      }

      auto *terminator = region.front().getTerminator();
      auto yieldOp = llvm::dyn_cast_or_null<YieldOp>(terminator);
      if (!yieldOp) {
        return emitOpError(regionName + " region must end with cmt2.yield");
      }

      // Check yield types match if results
      if (yieldOp.getResults().size() != getResults().size()) {
        return emitOpError(regionName + " region yields " +
                          std::to_string(yieldOp.getResults().size()) +
                          " values but " + std::to_string(getResults().size()) +
                          " expected");
      }

      for (auto [yieldType, resultType] : llvm::zip(
              yieldOp.getResults().getTypes(), getResults().getTypes())) {
        if (yieldType != resultType) {
          return emitOpError(regionName + " region yield type mismatch");
        }
      }
      return success();
    };

    if (failed(checkYield(getThenRegion(), "then")))
      return failure();
    if (failed(checkYield(getElseRegion(), "else")))
      return failure();
  }

  return success();
}

//===----------------------------------------------------------------------===//
// ProcStaticRepeatOp
//===----------------------------------------------------------------------===//

LogicalResult ProcStaticRepeatOp::verify() {
  // Check that count is positive
  if (getCount() <= 0) {
    return emitOpError("count must be greater than 0");
  }

  // Check that body is not empty
  if (getBody().empty() || getBody().front().empty()) {
    return emitOpError("body region must not be empty");
  }

  // If body_latency is specified, it must be non-negative
  if (getBodyLatency() && *getBodyLatency() < 0) {
    return emitOpError("body_latency must be non-negative");
  }

  return success();
}

//===----------------------------------------------------------------------===//
// ProcStaticIfOp
//===----------------------------------------------------------------------===//

LogicalResult ProcStaticIfOp::verify() {
  // Check if the condition type is a 1-bit FIRRTL type
  auto conditionType = getCond().getType();
  if (auto uintType = llvm::dyn_cast<circt::firrtl::UIntType>(conditionType)) {
    if (!uintType.getWidth() || uintType.getWidth().value() != 1) {
      return emitOpError("condition must be a 1-bit unsigned integer type");
    }
  } else if (auto sintType = llvm::dyn_cast<circt::firrtl::SIntType>(conditionType)) {
    if (!sintType.getWidth() || sintType.getWidth().value() != 1) {
      return emitOpError("condition must be a 1-bit type");
    }
  } else {
    return emitOpError("condition must be a FIRRTL integer type");
  }

  // Check that thenRegion is not empty
  if (getThenRegion().empty() || getThenRegion().front().empty()) {
    return emitOpError("then region must not be empty");
  }

  // If latencies are specified, they must be non-negative
  if (getThenLatency() && *getThenLatency() < 0) {
    return emitOpError("then_latency must be non-negative");
  }
  if (getElseLatency() && *getElseLatency() < 0) {
    return emitOpError("else_latency must be non-negative");
  }

  // If both latencies are specified or neither, that's valid
  // But if only one is specified, that's an error
  bool hasThenLatency = getThenLatency().has_value();
  bool hasElseLatency = getElseLatency().has_value();
  if (hasThenLatency != hasElseLatency) {
    return emitOpError("must specify both then_latency and else_latency, or neither");
  }

  return success();
}

//===----------------------------------------------------------------------===//
// MethodOp Verification
//===----------------------------------------------------------------------===//

LogicalResult MethodOp::verify() {
  // Atomic methods do NOT support timing attributes.
  // Timing is only valid for procedural methods (ProcMethodOp) which execute
  // over multiple cycles. Atomic methods are always single-cycle.
  if (getOperation()->hasAttr("static_latency")) {
    return emitOpError("atomic methods cannot have timing attributes; "
                       "use cmt2.proc.method for multi-cycle methods with "
                       "static_latency");
  }
  if (getOperation()->hasAttr("interval")) {
    return emitOpError("atomic methods cannot have timing attributes; "
                       "use cmt2.proc.method for multi-cycle methods with "
                       "interval");
  }
  return success();
}

//===----------------------------------------------------------------------===//
// CallOp Timing Verification
//===----------------------------------------------------------------------===//

LogicalResult CallOp::verify() {
  // Verify call_ty if present (used by lowering to distinguish per-cycle clones).
  if (auto callTy = (*this)->getAttrOfType<StringAttr>("call_ty")) {
    StringRef v = callTy.getValue();
    if (v != "Enable" && v != "GetRes")
      return emitOpError("call_ty must be one of \"Enable\" or \"GetRes\"");
  }

  // Verify arg_timing array size matches inputs
  if (auto argTiming = getArgTiming()) {
    if (argTiming->size() != getInputs().size()) {
      return emitOpError("arg_timing array size (")
             << argTiming->size() << ") must match number of inputs ("
             << getInputs().size() << ")";
    }

    // Verify timing bounds: start >= 0 and end > start
    for (size_t i = 0; i < argTiming->size(); ++i) {
      if (auto timing = dyn_cast<TimingIntervalAttr>((*argTiming)[i])) {
        if (timing.getStart() < 0) {
          return emitOpError("arg_timing[")
                 << i << "] start (" << timing.getStart()
                 << ") must be non-negative";
        }
      }
    }
  }

  // Verify result_timing array size matches outputs
  if (auto resultTiming = getResultTiming()) {
    if (resultTiming->size() != getOutputs().size()) {
      return emitOpError("result_timing array size (")
             << resultTiming->size() << ") must match number of outputs ("
             << getOutputs().size() << ")";
    }

    // Verify timing bounds: start >= 0 and end > start
    for (size_t i = 0; i < resultTiming->size(); ++i) {
      if (auto timing = dyn_cast<TimingIntervalAttr>((*resultTiming)[i])) {
        if (timing.getStart() < 0) {
          return emitOpError("result_timing[")
                 << i << "] start (" << timing.getStart()
                 << ") must be non-negative";
        }
      }
    }
  }

  // Timing attributes are only meaningful inside ProcStaticStepOp.
  // If specified elsewhere, emit a warning (timing will be ignored).
  bool hasTimingAttrs = getCallTiming() || getArgTiming() || getResultTiming();
  auto staticStep = getOperation()->getParentOfType<ProcStaticStepOp>();

  if (hasTimingAttrs && !staticStep) {
    // Timing specified outside static step - this is likely an error.
    // The timing will be ignored during lowering.
    return emitOpError(
        "timing attributes (call_timing/arg_timing/result_timing) are only "
        "valid inside cmt2.proc.static_step; timing on this call will be "
        "ignored during lowering");
  }

  // If inside a static step, verify timing is within step bounds
  if (staticStep) {
    int64_t stepLatency = staticStep.getLatency();

    if (auto callTy = (*this)->getAttrOfType<StringAttr>("call_ty")) {
      return emitOpError(
          "call_ty is a lowering-only tag and must not appear inside "
          "cmt2.proc.static_step");
    }

    // Default call_timing is [0, 1).
    TimingIntervalAttr callTiming =
        getCallTiming().value_or(TimingIntervalAttr::get(getContext(), 0, 1));

    if (callTiming.getStart() < 0)
      return emitOpError("call_timing start (")
             << callTiming.getStart() << ") must be non-negative";
    if (callTiming.getEnd() > stepLatency)
      return emitOpError("call_timing end (")
             << callTiming.getEnd() << ") exceeds step latency (" << stepLatency
             << ")";
    // Restriction (initial): call_timing is a single-cycle issue point.
    if (callTiming.getEnd() != callTiming.getStart() + 1)
      return emitOpError("call_timing must be a single-cycle interval; got [")
             << callTiming.getStart() << ", " << callTiming.getEnd() << ")";

    if (auto argTiming = getArgTiming()) {
      for (size_t i = 0; i < argTiming->size(); ++i) {
        if (auto timing = dyn_cast<TimingIntervalAttr>((*argTiming)[i])) {
          if (timing.getEnd() > stepLatency) {
            return emitOpError("arg_timing[")
                   << i << "] end (" << timing.getEnd()
                   << ") exceeds step latency (" << stepLatency << ")";
          }
          // Restriction (initial): arguments must be valid at call issue time.
          if (timing.getStart() != callTiming.getStart() ||
              timing.getEnd() != callTiming.getEnd()) {
            return emitOpError("arg_timing[")
                   << i << "] must match call_timing ["
                   << callTiming.getStart() << ", " << callTiming.getEnd()
                   << "); got [" << timing.getStart() << ", " << timing.getEnd()
                   << ")";
          }
        }
      }
    }

    if (auto resultTiming = getResultTiming()) {
      TimingIntervalAttr common;
      bool haveCommon = false;
      for (size_t i = 0; i < resultTiming->size(); ++i) {
        if (auto timing = dyn_cast<TimingIntervalAttr>((*resultTiming)[i])) {
          if (timing.getEnd() > stepLatency) {
            return emitOpError("result_timing[")
                   << i << "] end (" << timing.getEnd()
                   << ") exceeds step latency (" << stepLatency << ")";
          }
          // Restriction (initial): results are captured in a single cycle.
          if (timing.getEnd() != timing.getStart() + 1) {
            return emitOpError("result_timing[")
                   << i << "] must be a single-cycle interval; got ["
                   << timing.getStart() << ", " << timing.getEnd() << ")";
          }
          // Restriction (initial): all results share the same capture time.
          if (!haveCommon) {
            common = timing;
            haveCommon = true;
          } else if (timing.getStart() != common.getStart() ||
                     timing.getEnd() != common.getEnd()) {
            return emitOpError("result_timing[")
                   << i << "] must match result_timing[0] ["
                   << common.getStart() << ", " << common.getEnd() << "); got ["
                   << timing.getStart() << ", " << timing.getEnd() << ")";
          }
        }
      }
    }
  }

  // If parent is ProcWhileOp, verify we're in the condition region, not body
  if (auto whileOp = dyn_cast<ProcWhileOp>(getOperation()->getParentOp())) {
    // Check if we're in the condition region (first region) or body (second)
    Region *parentRegion = getOperation()->getParentRegion();
    if (parentRegion == &whileOp.getBody()) {
      return emitOpError("cmt2.call is not allowed in the body region of "
                         "cmt2.proc.while; use the condition region instead");
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// ProcCondIfOp
//===----------------------------------------------------------------------===//

mlir::Value ProcCondIfOp::getCond() {
  // Get the condition from the terminator of the condition region
  if (getCondRegion().empty())
    return nullptr;
  Block &condBlock = getCondRegion().front();
  if (condBlock.empty())
    return nullptr;
  // The terminator should be ProcCondIfYieldOp
  if (auto yieldOp = dyn_cast<ProcCondIfYieldOp>(condBlock.getTerminator()))
    return yieldOp.getCond();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// ProcWhileOp
//===----------------------------------------------------------------------===//

mlir::Value ProcWhileOp::getCond() {
  // Get the condition from the terminator of the condition region
  if (getCondRegion().empty())
    return nullptr;
  Block &condBlock = getCondRegion().front();
  if (condBlock.empty())
    return nullptr;
  // The terminator should be ProcWhileCondYieldOp
  if (auto yieldOp = dyn_cast<ProcWhileCondYieldOp>(condBlock.getTerminator()))
    return yieldOp.getCond();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// BindMethodOp Timing Verification
//===----------------------------------------------------------------------===//

LogicalResult BindMethodOp::verify() {
  // If static_latency is specified, it must be positive
  if (auto latency = getStaticLatency()) {
    if (*latency <= 0) {
      return emitOpError("static_latency must be positive, got ") << *latency;
    }
  }

  // If interval is specified, static_latency must also be specified
  if (getInterval() && !getStaticLatency()) {
    return emitOpError("interval requires static_latency to be specified");
  }

  // If interval is specified, it must be <= static_latency
  if (auto interval = getInterval()) {
    if (auto latency = getStaticLatency()) {
      if (interval->getCycles() > *latency) {
        return emitOpError("interval (")
               << interval->getCycles() << ") must be <= static_latency ("
               << *latency << ")";
      }
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// ProcStaticStepOp Timing Verification
//===----------------------------------------------------------------------===//

LogicalResult ProcStaticStepOp::verify() {
  // Latency must be positive
  if (getLatency() <= 0) {
    return emitOpError("latency must be positive, got ") << getLatency();
  }

  // If interval is specified, it must be <= latency
  if (auto interval = getInterval()) {
    if (interval->getCycles() > getLatency()) {
      return emitOpError("interval (")
             << interval->getCycles() << ") must be <= latency ("
             << getLatency() << ")";
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// Token Operations
//===----------------------------------------------------------------------===//

LogicalResult TokenValidOp::verify() {
  // The result should be a 1-bit uint
  auto resultType = getValid().getType();
  if (auto uintType = dyn_cast<firrtl::UIntType>(resultType)) {
    if (uintType.getWidth().has_value() && uintType.getWidth().value() != 1) {
      return emitOpError("result must be a 1-bit uint, got width ")
             << uintType.getWidth().value();
    }
  }
  return success();
}

LogicalResult TokenDataOp::verify() {
  auto tokenType = cast<SyncTokenType>(getToken().getType());
  if (!tokenType.hasData()) {
    return emitOpError("token must carry data, but got ") << tokenType;
  }
  // Check that the result type matches the token's data type
  if (tokenType.getDataType() != getData().getType()) {
    return emitOpError("result type ")
           << getData().getType() << " must match token data type "
           << tokenType.getDataType();
  }
  return success();
}

LogicalResult TokenCreateOp::verify() {
  auto tokenType = cast<SyncTokenType>(getToken().getType());
  if (getData()) {
    // If data is provided, token must have data type
    if (!tokenType.hasData()) {
      return emitOpError("token type must have data when data operand is provided");
    }
    // Check that data type matches
    if (tokenType.getDataType() != getData().getType()) {
      return emitOpError("data type ")
             << getData().getType() << " must match token data type "
             << tokenType.getDataType();
    }
  } else {
    // If no data provided, token should not have data type
    if (tokenType.hasData()) {
      return emitOpError("token type has data but no data operand provided");
    }
  }
  return success();
}

// Custom assembly format for TokenCreateOp
// Without data: %tok = cmt2.token.create : !cmt2.sync_token
// With data: %tok = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
ParseResult TokenCreateOp::parse(OpAsmParser &parser, OperationState &result) {
  OpAsmParser::UnresolvedOperand dataOperand;
  Type dataType;
  Type tokenType;

  // Try to parse an operand
  auto parseOperandResult = parser.parseOptionalOperand(dataOperand);
  if (parseOperandResult.has_value()) {
    if (parser.parseColonType(dataType) || parser.parseArrow() ||
        parser.parseType(tokenType)) {
      return failure();
    }
    if (parser.resolveOperand(dataOperand, dataType, result.operands)) {
      return failure();
    }
  } else {
    if (parser.parseColonType(tokenType)) {
      return failure();
    }
  }

  result.addTypes(tokenType);
  return success();
}

void TokenCreateOp::print(OpAsmPrinter &p) {
  p << " ";
  if (getData()) {
    p << getData() << " : " << getData().getType() << " -> ";
  } else {
    p << ": ";
  }
  p << getToken().getType();
}

LogicalResult TokenJoinOp::verify() {
  if (getTokens().empty()) {
    return emitOpError("must have at least one input token");
  }
  return success();
}

//===----------------------------------------------------------------------===//
// Dataflow Operations
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// ProcDataflowOp
//===----------------------------------------------------------------------===//

ParseResult ProcDataflowOp::parse(OpAsmParser &parser, OperationState &result) {
  auto builder = parser.getBuilder();

  // Parse the symbol name
  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, mlir::SymbolTable::getSymbolAttrName(),
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

  // Parse optional `->` and result types
  SmallVector<Type> resTypes;
  if (succeeded(parser.parseOptionalArrow())) {
    if (parser.parseLParen())
      return failure();
    if (parser.parseOptionalRParen()) {
      if (parser.parseTypeList(resTypes) || parser.parseRParen())
        return failure();
    }
  }

  // Store attributes
  result.addAttribute("argNames", builder.getStrArrayAttr(argNames));
  auto funcType = builder.getFunctionType(argTypes, resTypes);
  result.addAttribute("function_type", TypeAttr::get(funcType));

  // Parse optional attribute dict
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes))
    return failure();

  // Parse the body region
  auto *bodyRegion = result.addRegion();
  if (parser.parseRegion(*bodyRegion, args))
    return failure();

  return success();
}

void ProcDataflowOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getSymName());

  auto funcType = getFunctionType();
  auto argTypes = funcType.getInputs();
  auto resTypes = funcType.getResults();

  // Print arguments
  if (!argTypes.empty()) {
    Block &bodyBlock = getBody().front();
    p << '(';
    llvm::interleaveComma(llvm::zip(getArgNames(), bodyBlock.getArguments()), p,
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
  if (!resTypes.empty()) {
    p << " -> (";
    llvm::interleaveComma(resTypes, p, [&](Type type) {
      p.printType(type);
    });
    p << ')';
  }

  // Print attributes
  SmallVector<StringRef> elidedAttrs = {"sym_name", "function_type", "argNames"};
  p.printOptionalAttrDictWithKeyword((*this)->getAttrs(), elidedAttrs);

  // Print body
  p << ' ';
  p.printRegion(getBody(), /*printEntryBlockArgs=*/false);
}

void ProcDataflowOp::getAsmBlockArgumentNames(Region &region,
                                               OpAsmSetValueNameFn setNameFn) {
  getAsmBlockArgumentNamesImpl(getArgNames(), region, setNameFn);
}

LogicalResult ProcDataflowOp::verify() {
  // Check that the body is not empty
  if (getBody().empty()) {
    return emitOpError("body region must not be empty");
  }

  // Check that interval is positive if specified
  if (auto interval = getInterval()) {
    if (*interval <= 0) {
      return emitOpError("interval must be positive, got ") << *interval;
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// DataflowTaskOp
//===----------------------------------------------------------------------===//

ParseResult DataflowTaskOp::parse(OpAsmParser &parser, OperationState &result) {
  auto builder = parser.getBuilder();

  // Parse the symbol name
  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, mlir::SymbolTable::getSymbolAttrName(),
                             result.attributes))
    return failure();

  // Parse optional ()
  if (succeeded(parser.parseOptionalLParen())) {
    if (parser.parseRParen())
      return failure();
  }

  // Parse optional tokens_in
  SmallVector<OpAsmParser::UnresolvedOperand> tokenOperands;
  SmallVector<Type> tokenTypes;
  SmallVector<StringRef> tokenNames;
  if (succeeded(parser.parseOptionalKeyword("tokens_in"))) {
    if (parser.parseLParen())
      return failure();

    // Parse comma-separated list of "name: type" pairs
    if (parser.parseOptionalRParen()) {
      do {
        OpAsmParser::UnresolvedOperand operand;
        Type type;
        if (parser.parseOperand(operand) || parser.parseColonType(type))
          return failure();
        tokenOperands.push_back(operand);
        tokenTypes.push_back(type);
        tokenNames.push_back(operand.name.drop_front());
      } while (succeeded(parser.parseOptionalComma()));
      if (parser.parseRParen())
        return failure();
    }
  }

  // Store token_in_names
  result.addAttribute("token_in_names", builder.getStrArrayAttr(tokenNames));

  // Resolve token operands
  if (parser.resolveOperands(tokenOperands, tokenTypes, parser.getCurrentLocation(),
                             result.operands))
    return failure();

  // Parse optional `->` and result types (token outputs)
  SmallVector<Type> resTypes;
  if (succeeded(parser.parseOptionalArrow())) {
    if (parser.parseLParen())
      return failure();
    if (parser.parseOptionalRParen()) {
      if (parser.parseTypeList(resTypes) || parser.parseRParen())
        return failure();
    }
  }

  // Add result types
  result.addTypes(resTypes);

  // Parse optional attribute dict BEFORE region (uses "attributes" keyword)
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes))
    return failure();

  // Parse the body region (no block arguments - tokens are accessed as operands)
  auto *bodyRegion = result.addRegion();
  if (parser.parseRegion(*bodyRegion, {}))
    return failure();

  // Ensure body block has a terminator
  if (bodyRegion->empty()) {
    bodyRegion->emplaceBlock();
  }

  return success();
}

void DataflowTaskOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getSymName());
  p << "()";

  // Print tokens_in if present
  if (!getTokenInputs().empty()) {
    p << " tokens_in(";
    llvm::interleaveComma(llvm::enumerate(getTokenInputs()), p,
                         [&](auto enumVal) {
                           p.printOperand(enumVal.value());
                           p << ": ";
                           p.printType(enumVal.value().getType());
                         });
    p << ')';
  }

  // Print result types (token outputs)
  if (!getTokenOutputs().empty()) {
    p << " -> (";
    llvm::interleaveComma(getTokenOutputs().getTypes(), p, [&](Type type) {
      p.printType(type);
    });
    p << ')';
  }

  // Print attributes
  SmallVector<StringRef> elidedAttrs = {"sym_name", "token_in_names"};
  p.printOptionalAttrDictWithKeyword((*this)->getAttrs(), elidedAttrs);

  // Print body
  p << ' ';
  p.printRegion(getBody(), /*printEntryBlockArgs=*/false);
}

void DataflowTaskOp::getAsmBlockArgumentNames(Region &region,
                                               OpAsmSetValueNameFn setNameFn) {
  // DataflowTaskOp has no block arguments - token inputs are operands
  // accessed from outer scope (not IsolatedFromAbove)
}

LogicalResult DataflowTaskOp::verify() {
  // Check that the body is not empty
  if (getBody().empty()) {
    return emitOpError("body region must not be empty");
  }

  // Check that token_in_names matches token_inputs count
  if (getTokenInNames().size() != getTokenInputs().size()) {
    return emitOpError("token_in_names count (")
           << getTokenInNames().size() << ") must match token_inputs count ("
           << getTokenInputs().size() << ")";
  }

  // Check that terminator is either DataflowYieldOp or DataflowReturnOp
  Block &block = getBody().front();
  if (block.empty()) {
    return emitOpError("body block must not be empty");
  }

  auto *terminator = block.getTerminator();
  if (!isa<DataflowYieldOp, DataflowReturnOp>(terminator)) {
    return emitOpError("body must end with cmt2.dataflow.yield or "
                       "cmt2.dataflow.return");
  }

  // If terminator is DataflowYieldOp, check that token counts match
  if (auto yieldOp = dyn_cast<DataflowYieldOp>(terminator)) {
    if (yieldOp.getTokens().size() != getTokenOutputs().size()) {
      return emitOpError("dataflow.yield token count (")
             << yieldOp.getTokens().size() << ") must match task result count ("
             << getTokenOutputs().size() << ")";
    }
    // Check token types match
    for (auto [yieldType, resultType] :
         llvm::zip(yieldOp.getTokens().getTypes(), getTokenOutputs().getTypes())) {
      if (yieldType != resultType) {
        return emitOpError("yield token type ")
               << yieldType << " does not match result type " << resultType;
      }
    }
  }

  // If terminator is DataflowReturnOp, this should be a final task with no token outputs
  if (auto returnOp = dyn_cast<DataflowReturnOp>(terminator)) {
    if (!getTokenOutputs().empty()) {
      return emitOpError("task with dataflow.return cannot have token outputs");
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// DataflowYieldOp
//===----------------------------------------------------------------------===//

LogicalResult DataflowYieldOp::verify() {
  // Verify parent is DataflowTaskOp
  auto taskOp = dyn_cast<DataflowTaskOp>(getOperation()->getParentOp());
  if (!taskOp) {
    return emitOpError("must be inside a cmt2.dataflow.task");
  }
  return success();
}

//===----------------------------------------------------------------------===//
// DataflowReturnOp
//===----------------------------------------------------------------------===//

LogicalResult DataflowReturnOp::verify() {
  // Verify parent is DataflowTaskOp
  auto taskOp = dyn_cast<DataflowTaskOp>(getOperation()->getParentOp());
  if (!taskOp) {
    return emitOpError("must be inside a cmt2.dataflow.task");
  }

  // Get the enclosing ProcDataflowOp to check result types
  auto dataflowOp = taskOp->getParentOfType<ProcDataflowOp>();
  if (!dataflowOp) {
    return emitOpError("task must be inside a cmt2.proc.dataflow");
  }

  // Check that return types match dataflow result types
  auto dataflowResults = dataflowOp.getResultTypes();
  if (getResults().size() != dataflowResults.size()) {
    return emitOpError("return count (")
           << getResults().size() << ") must match dataflow result count ("
           << dataflowResults.size() << ")";
  }

  for (auto [returnType, expectedType] :
       llvm::zip(getResults().getTypes(), dataflowResults)) {
    if (returnType != expectedType) {
      return emitOpError("return type ")
             << returnType << " does not match expected type " << expectedType;
    }
  }

  return success();
}

} // namespace cmt2
} // namespace circt

// Provide the autogenerated implementation guts for the Op classes.
#define GET_OP_CLASSES
#include "circt/Dialect/Cmt2/Cmt2.cpp.inc"
