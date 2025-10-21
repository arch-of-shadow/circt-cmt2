//===- Cmt2OpInterfaces.td - Cmt2 Dialect OpInterfaces -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cmt2 dialect operator interfaces.
//
//===----------------------------------------------------------------------===//
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"


// Cmt2FunctionLike interface for Rule, Method, and Value operations
def Cmt2FunctionLike : OpInterface<"Cmt2FunctionLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = [{
    Provide common interface for function-like operations (Rule/Method/Value).
    This interface applies to RuleOp, MethodOp, ValueOp, BindMethodOp, and BindValueOp.

    This interface provides FunctionOpInterface-compatible features for Cmt2 operations,
    including:
    - Symbol name handling (functionName, functionNameAttr)
    - Function kind identification (getFunctionKind)
    - CallableOpInterface compatibility (getCallableRegion)
    - Type queries (getArgumentTypes, getResultTypes, getFunctionType)
    - Type manipulation (setFunctionTypeAttr, cloneTypeWith)
    - Body/region handling (isExternal, getFunctionBody)
    - Argument/result counts (getNumArguments, getNumResults)
    - Argument/result attribute access (getAllArgAttrs, getArgAttrDict, getArgAttr, etc.)
    - Optional signal name attributes (readyName, enableName) for hardware generation

    Note: Unlike FunctionOpInterface which requires single-region operations,
    Cmt2FunctionLike supports two-region operations (guard and body regions).
    For BindMethodOp and BindValueOp, which have no regions, some methods
    (like getFunctionBody) should not be called.

    Optional attributes:
    - readyName: String attribute specifying the name for the ready signal
    - enableName: String attribute specifying the name for the enable signal
  }];
  let methods = [
    //===------------------------------------------------------------------===//
    // Symbol Name Handle
    //===------------------------------------------------------------------===//

    InterfaceMethod<"Get the function name",
    "::llvm::StringRef", "functionName", (ins),
    /*methodBody=*/[{ return $_op.getName(); }]>,

    InterfaceMethod<"Get the function name",
    "::mlir::StringAttr", "functionNameAttr", (ins),
    /*methodBody=*/[{ return $_op.getNameAttr(); }]>,

    //===------------------------------------------------------------------===//
    // Function Kind
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Get the kind of this function (Rule, Method, or Value).
      This method must be implemented by each operation that implements this interface.
    }],
    "::circt::cmt2::FunctionKind", "getFunctionKind", (ins)>,

    //===------------------------------------------------------------------===//
    // FunctionOpInterface compatibility methods
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Get the callable region (body region) for CallableOpInterface.
      For Cmt2 function-like ops with two regions, return the body region.
      For BindMethodOp/BindValueOp, return nullptr.
    }],
    "::mlir::Region *", "getCallableRegion", (ins)>,

    InterfaceMethod<[{
      Get the function type attribute.
    }],
    "::mlir::FunctionType", "getFunctionType", (ins)>,

    InterfaceMethod<[{
      Get the argument types from the function type.
    }],
    "::llvm::ArrayRef<::mlir::Type>", "getArgumentTypes", (ins)>,

    InterfaceMethod<[{
      Get the result types from the function type.
    }],
    "::llvm::ArrayRef<::mlir::Type>", "getResultTypes", (ins)>,

    //===------------------------------------------------------------------===//
    // FunctionOpInterface-like type manipulation
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Set the type of the function. This performs an unsafe modification to
      the function type; it does not update argument or result attributes.
    }],
    "void", "setFunctionTypeAttr", (ins "::mlir::TypeAttr":$type),
    /*methodBody=*/[{ $_op->setAttr($_op.getFunctionTypeAttrName(), type); }]>,

    InterfaceMethod<[{
      Returns a clone of the function type with the given argument and result types.
    }],
    "::mlir::Type", "cloneTypeWith", (ins
      "::mlir::TypeRange":$inputs, "::mlir::TypeRange":$results
    ),
    /*methodBody=*/[{ return $_op.getFunctionType().clone(inputs, results); }]>,

    //===------------------------------------------------------------------===//
    // Body and region handling
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Returns true if this function is external (has no body).
      For Cmt2 operations, this checks if the body region is empty.
      For BindMethodOp/BindValueOp, always returns true.
    }],
    "bool", "isExternal", (ins)>,

    InterfaceMethod<[{
      Return the function body region. For Cmt2 two-region operations, this
      returns the body region (not the guard region).
      For BindMethodOp/BindValueOp, this should not be called.
    }],
    "::mlir::Region &", "getFunctionBody", (ins)>,

    //===------------------------------------------------------------------===//
    // Argument and result counts
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Returns the number of function arguments.
    }],
    "unsigned", "getNumArguments", (ins),
    /*methodBody=*/[{ return $_op.getArgNames().size(); }]>,

    InterfaceMethod<[{
      Returns the number of function results.
    }],
    "unsigned", "getNumResults", (ins),
    /*methodBody=*/[{ return $_op.getBodyResNames().size(); }]>,

    //===------------------------------------------------------------------===//
    // Argument and result names
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Returns the name of the i-th function argument
    }],
    "std::string", "getArgumentName", (ins "unsigned":$index),
    /*methodBody=*/[{ return llvm::cast<mlir::StringAttr>($_op.getArgNames()[index]).getValue().str(); }]>,

    InterfaceMethod<[{
      Returns the name of the i-th function results
    }],
    "std::string", "getResultName", (ins "unsigned":$index),
    /*methodBody=*/[{ return llvm::cast<mlir::StringAttr>($_op.getBodyResNames()[index]).getValue().str(); }]>,


    //===------------------------------------------------------------------===//
    // Argument attributes
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Return an ArrayAttr containing all argument attribute dictionaries,
      or nullptr if no arguments have attributes.
    }],
    "::mlir::ArrayAttr", "getAllArgAttrs", (ins),
    /*methodBody=*/[{ return $_op.getArgAttrsAttr(); }]>,

    InterfaceMethod<[{
      Return the attribute dictionary for the argument at 'index'.
    }],
    "::mlir::DictionaryAttr", "getArgAttrDict", (ins "unsigned":$index),
    /*methodBody=*/[{
      auto argAttrs = $_op.getArgAttrsAttr();
      return argAttrs ? llvm::cast<::mlir::DictionaryAttr>(argAttrs[index])
                      : ::mlir::DictionaryAttr();
    }]>,

    InterfaceMethod<[{
      Return the specified attribute, if present, for the argument at 'index'.
    }],
    "::mlir::Attribute", "getArgAttr", (ins "unsigned":$index, "::mlir::StringAttr":$name),
    /*methodBody=*/[{
      auto argAttrs = $_op.getArgAttrsAttr();
      auto argDict = argAttrs ? llvm::cast<::mlir::DictionaryAttr>(argAttrs[index])
                              : ::mlir::DictionaryAttr();
      return argDict ? argDict.get(name) : nullptr;
    }]>,

    //===------------------------------------------------------------------===//
    // Result attributes
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Return an ArrayAttr containing all result attribute dictionaries,
      or nullptr if no results have attributes.
    }],
    "::mlir::ArrayAttr", "getAllResAttrs", (ins),
    /*methodBody=*/[{ return $_op.getResAttrsAttr(); }]>,

    InterfaceMethod<[{
      Return the attribute dictionary for the result at 'index'.
    }],
    "::mlir::DictionaryAttr", "getResAttrDict", (ins "unsigned":$index),
    /*methodBody=*/[{
      auto resAttrs = $_op.getResAttrsAttr();
      return resAttrs ? llvm::cast<::mlir::DictionaryAttr>(resAttrs[index])
                      : ::mlir::DictionaryAttr();
    }]>,

    InterfaceMethod<[{
      Return the specified attribute, if present, for the result at 'index'.
    }],
    "::mlir::Attribute", "getResAttr", (ins "unsigned":$index, "::mlir::StringAttr":$name),
    /*methodBody=*/[{
      auto resAttrs = $_op.getResAttrsAttr();
      auto resDict = resAttrs ? llvm::cast<::mlir::DictionaryAttr>(resAttrs[index])
                              : ::mlir::DictionaryAttr();
      return resDict ? resDict.get(name) : nullptr;
    }]>,

    //===------------------------------------------------------------------===//
    // Optional attributes for ready/enable signal names
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Get the ready signal name, if specified.
      Returns StringAttr if set, nullptr otherwise.
    }],
    "::mlir::StringAttr", "getReadyNameAttr", (ins),
    /*methodBody=*/[{
      return ::llvm::dyn_cast_or_null<::mlir::StringAttr>($_op->getAttr("readyName"));
    }]>,

    InterfaceMethod<[{
      Get the ready signal name as a StringRef.
      Returns empty string if not set.
    }],
    "::llvm::StringRef", "getReadyName", (ins),
    /*methodBody=*/[{
      auto attr = ::llvm::dyn_cast_or_null<::mlir::StringAttr>($_op->getAttr("readyName"));
      return attr ? attr.getValue() : ::llvm::StringRef();
    }]>,

    InterfaceMethod<[{
      Set the ready signal name.
    }],
    "void", "setReadyNameAttr", (ins "::mlir::StringAttr":$name),
    /*methodBody=*/[{
      $_op->setAttr("readyName", name);
    }]>,

    InterfaceMethod<[{
      Remove the ready signal name attribute.
    }],
    "void", "removeReadyName", (ins),
    /*methodBody=*/[{
      $_op->removeAttr("readyName");
    }]>,

    InterfaceMethod<[{
      Get the enable signal name, if specified.
      Returns StringAttr if set, nullptr otherwise.
    }],
    "::mlir::StringAttr", "getEnableNameAttr", (ins),
    /*methodBody=*/[{
      return ::llvm::dyn_cast_or_null<::mlir::StringAttr>($_op->getAttr("enableName"));
    }]>,

    InterfaceMethod<[{
      Get the enable signal name as a StringRef.
      Returns empty string if not set.
    }],
    "::llvm::StringRef", "getEnableName", (ins),
    /*methodBody=*/[{
      auto attr = ::llvm::dyn_cast_or_null<::mlir::StringAttr>($_op->getAttr("enableName"));
      return attr ? attr.getValue() : ::llvm::StringRef();
    }]>,

    InterfaceMethod<[{
      Set the enable signal name.
    }],
    "void", "setEnableNameAttr", (ins "::mlir::StringAttr":$name),
    /*methodBody=*/[{
      $_op->setAttr("enableName", name);
    }]>,

    InterfaceMethod<[{
      Remove the enable signal name attribute.
    }],
    "void", "removeEnableName", (ins),
    /*methodBody=*/[{
      $_op->removeAttr("enableName");
    }]>,
  ];
  let verify = [{}];
}

def Cmt2ModuleLike : OpInterface<"Cmt2ModuleLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = "Provide base module interface.";
  let methods = [
    //===------------------------------------------------------------------===//
    // Module Name
    //===------------------------------------------------------------------===//

    InterfaceMethod<"Get the module name",
    "::llvm::StringRef", "moduleName", (ins),
    /*methodBody=*/[{ return $_op.getName(); }]>,

    InterfaceMethod<"Get the module name",
    "::mlir::StringAttr", "moduleNameAttr", (ins),
    /*methodBody=*/[{ return $_op.getNameAttr(); }]>,

    //===------------------------------------------------------------------===//
    // Symbol Lookup
    //===------------------------------------------------------------------===//

    InterfaceMethod<[{
      Look up a Cmt2FunctionLike by name in this module.
      Returns the Cmt2FunctionLike if found, nullptr otherwise.
    }],
    "Cmt2FunctionLike", "lookupFunctionLike",
    (ins "::mlir::StringAttr":$name),
    /*methodBody=*/[{
      return llvm::dyn_cast_or_null<Cmt2FunctionLike>(::mlir::SymbolTable::lookupSymbolIn($_op, name));
    }]>,

    //===------------------------------------------------------------------===//
    // Body
    //===------------------------------------------------------------------===//
    InterfaceMethod<"Body region",
    "::mlir::Region&", "body", (ins),
    [{ return $_op.getBodyRegion(); }]>

    //===------------------------------------------------------------------===//
    // Instance
    //===------------------------------------------------------------------===//
    // InterfaceMethod<"Get instances for this Module.",
    // "llvm::SmallVector<StringAttr>", "instances", (ins),
    // /*methodBody=*/[{ return $_op.getInstances(); }]>,
  ];
  let verify = [{}];
}

