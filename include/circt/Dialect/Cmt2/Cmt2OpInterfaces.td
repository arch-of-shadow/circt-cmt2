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
    }]>


    //===------------------------------------------------------------------===//
    // Instance
    //===------------------------------------------------------------------===//
    // InterfaceMethod<"Get instances for this Module.",
    // "llvm::SmallVector<StringAttr>", "instances", (ins),
    // /*methodBody=*/[{ return $_op.getInstances(); }]>,
  ];
  let verify = [{}];
}

