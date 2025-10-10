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

def Cmt2ModuleLike : OpInterface<"Cmt2ModuleLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = "Provide base module interface.";
  let methods = [
    //===------------------------------------------------------------------===//
    // Module Name
    //===------------------------------------------------------------------===//

    InterfaceMethod<"Get the module name",
    "StringRef", "moduleName", (ins),
    /*methodBody=*/[{ return $_op.getName(); }]>,

    InterfaceMethod<"Get the module name",
    "StringAttr", "moduleNameAttr", (ins),
    /*methodBody=*/[{ return $_op.getNameAttr(); }]>,

    //===------------------------------------------------------------------===//
    // Instance
    //===------------------------------------------------------------------===//
    // InterfaceMethod<"Get instances for this Module.",
    // "llvm::SmallVector<StringAttr>", "instances", (ins),
    // /*methodBody=*/[{ return $_op.getInstances(); }]>,
  ];
  let verify = [{}];
}

// BindMethod and Method
def Cmt2MethodLike : OpInterface<"Cmt2MethodLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = "Provide method interface.";
}

// BindValue and Value
def Cmt2ValueLike : OpInterface<"Cmt2ValueLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = "Provide value interface.";
}

def Cmt2RuleLike : OpInterface<"Cmt2RuleLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = "Provide rule interface.";
  let methods = [
    //===------------------------------------------------------------------===//
    // Symbol Name Handle
    //===------------------------------------------------------------------===//

    InterfaceMethod<"Get the rule name",
    "StringRef", "ruleName", (ins),
    /*methodBody=*/[{ return $_op.getName(); }]>,

    InterfaceMethod<"Get the rule name",
    "StringAttr", "ruleNameAttr", (ins),
    /*methodBody=*/[{ return $_op.getNameAttr(); }]>,
  ];
  let verify = [{}];
}

def Cmt2FunctionLike : OpInterface<"Cmt2FunctionLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = "Provide method and value interface to modules.";
  let methods = [
    //===------------------------------------------------------------------===//
    // Symbol Name Handle
    //===------------------------------------------------------------------===//

    InterfaceMethod<"Get the function name",
    "StringRef", "functionName", (ins),
    /*methodBody=*/[{ return $_op.getName(); }]>,

    InterfaceMethod<"Get the function name",
    "StringAttr", "functionNameAttr", (ins),
    /*methodBody=*/[{ return $_op.getNameAttr(); }]>,
  ];

}

def Cmt2CallLike : OpInterface<"Cmt2CallLike"> {
  let cppNamespace = "::circt::cmt2";
  let description = "Provide functions to call method and value";
  let methods = [
    //===------------------------------------------------------------------===//
    // Instance
    //===------------------------------------------------------------------===//
    InterfaceMethod<"Which instance is called.",
    "SymbolRefAttr", "instanceName", (ins),
    [{}],
    /*defaultImplementation=*/[{
      return $_op->template
        getAttrOfType<SymbolRefAttr>("instanceName");
    }]
    >,

    //===------------------------------------------------------------------===//
    // Methods
    //===------------------------------------------------------------------===//
    InterfaceMethod<"Which FunctionLikeOp is called.",
    "SymbolRefAttr", "functionName", (ins),
    [{}],
    /*defaultImplementation=*/[{
      return $_op->template
        getAttrOfType<SymbolRefAttr>("functionName");
    }]
    >,
  ];
}