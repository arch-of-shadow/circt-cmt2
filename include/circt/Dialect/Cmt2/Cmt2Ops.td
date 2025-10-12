//===- Cmt2Ops.td - Cmt2 Dialect Operators -------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cmt2 dialect operators.
//
//===----------------------------------------------------------------------===//
#ifndef CIRCT_DIALECT_Cmt2_Cmt2OPS_TD
#define CIRCT_DIALECT_Cmt2_Cmt2OPS_TD

// TODO: add this to HW interface
//       and do the failing check.
// def isHWModuleSymbol : AttrConstraint<
//     CPred<"!::mlir::SymbolTable::lookupNearestSymbolFrom("
//             "&$_op, $_self.cast<::mlir::FlatSymbolRefAttr>()) || "
//           "hw::isAnyModule(::mlir::SymbolTable::lookupNearestSymbolFrom("
//             "&$_op, $_self.cast<::mlir::FlatSymbolRefAttr>()))"
//     >, "is module like">;

// MethodSymbol is an attribute constraint. It has the form (instance, method).
def CircuitOp : Cmt2Op<
  "circuit", [
    IsolatedFromAbove,
    SingleBlock,
    SymbolTable,
    NoTerminator,
    NoRegionArguments
  ]> {
  let summary = "represent a Cmt2 circuit.";
  let description = [{
    The `cmt2.circuit` is the top level container for Cmt2 modules and
    external modules. 

    Arguments: None

    Results: None

    Regions:
    - `body`: This region holds the definitions of modules and
      external modules.
      
    Assembly format:
    ```mlir
    cmt2.circuit {
      cmt2.module @mod1 { ... }
      cmt2.module.extern @extmod1 { ... }
      ...
    }
    ```
  }];
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = [{
    $body attr-dict
  }];
}

def ExtModuleHwOp : Cmt2Op<
  "module.extern.hw", [
    HasParent<"CircuitOp">,
    IsolatedFromAbove,
    SingleBlock,
    Symbol,
    SymbolTable,
    NoTerminator,
    OpAsmOpInterface,
    // FunctionOpInterface,
    // DeclareOpInterfaceMethods<Cmt2ModuleLike>,
    //DeclareOpInterfaceMethods<HWModuleLike>,
  ]> {
  let summary = "Binding a `hw.module` to Cmt2 ExtModule.";
  let description = [{
    The `cmt2.module.extern.hw` binds a `hw.module` to a Cmt2 external module.
   
    Attributes:
    - `sequenceBefore` ('<'): A list of 2-tuples.
    - `conflict` ('<>'): A list of 2-tuples.
    - `conflictFree` ('/'): A list of 2-tuples.

    Arguments: 
    - `extHwModule`: The symbol name of the `hw.module` to bind to.
    // TODO: port signals

    Results: None

    Regions:
    - `body`: This region holds the `cmt2.bind.*` operations

    Assembly format:
    ```mlir
    cmt2.module.extern.hw @extmod1 : <@hwmod1> {
      cmt2.bind.bare ...
      cmt2.bind.value ...
      cmt2.bind.method ...
    } { sequenceBefore = [ ... ], conflict = [ ... ], conflictFree = [ ... ] }
    ```
  }];
  let regions = (region SizedRegion<1>:$body);
  let arguments = (ins 
    SymbolNameAttr:$sym_name,
    SymbolRefAttr:$ext_module_name
    // TypeAttrOf<FunctionType>:$function_type,
    // OptionalAttr<DictArrayAttr>:$arg_attrs,
    // OptionalAttr<DictArrayAttr>:$res_attrs
  );
  let assemblyFormat = [{
    $sym_name `:` $ext_module_name $body attr-dict
  }];
}

def BindBareOp : Cmt2Op<
  "bind.bare", [
    HasParent<"ExtModuleHwOp">
  ]> {
  let summary = "Binding a signal to a port";
  let description = [{
    The "cmt2.bind.bare" operation binds a signal to a port of the external module. It is commonly used for clock and reset signals that do not follow the ready-enable protocol.

    Arguments:
    - `signal`: The signal (value) to be bound to the port.
    - `port`: The port (attribute) of the external module to which the signal is bound.

    Results: None
    
    Assembly format:
    ```mlir
    cmt2.bind.bare %signal, @port : 
    ```
  }];
  let arguments = (ins 
    HWIntegerType:$signal,
    FlatSymbolRefAttr:$port
  );
  let assemblyFormat = [{
    $signal `,` $port attr-dict `:` qualified(type($signal))
  }];
}

def BindValueOp : Cmt2Op<
  "bind.value", [
    HasParent<"ExtModuleHwOp">,
    // DeclareOpInterfaceMethods<Cmt2FunctionLike>,
    // DeclareOpInterfaceMethods<Cmt2ValueLike>,
    Symbol
  ]> {
  let summary = "Binding a value method";
  let description = [{
    The "cmt2.bind.value" operations binds a value method to hw.module logic.
    
    Note: The value method cannot have input arguments.

    Arguments:
    - `sym_name`: The symbol name of the value method being bound.
    - `function_type`: The function type of the value method.
    - `ready`: The signal (attribute) representing the ready output of the value method.
    - `data`: The signal (attribute) representing the data output of the value method.

    Results: None

    Assembly format:
    ```mlir
    cmt2.bind.value @valMethod : (i1) -> (i32) [
      ready = %readySignal,
      data = %dataSignal
    ]
    ```
  }];

  let arguments = (ins 
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    OptionalAttr<FlatSymbolRefAttr>:$ready,
    FlatSymbolRefArrayAttr:$data
  );

  let assemblyFormat = [{
    $sym_name `:` $function_type `[` (`ready` `=` $ready^ `,`)? `data` `=` $data `]` attr-dict
  }];
}

def BindMethodOp : Cmt2Op<
  "bind.method", [
    HasParent<"ExtModuleHwOp">,
    // DeclareOpInterfaceMethods<Cmt2FunctionLike>,
    // DeclareOpInterfaceMethods<Cmt2MethodLike>,
    Symbol,
    NoTerminator,
    IsolatedFromAbove
  ]> {
  let summary = "Binding a method";
  let description = [{
    The "cmt2.bind.method" operation binds a method to hw.module logic. 
    The method follows the ready-enable protocol.

    Arguments:
    - `sym_name`: The symbol name of the method being bound.
    - `function_type`: The function type of the method.
    - `enable`: The signal (attribute) representing the enable input of the method.
    - `ready`: The signal (attribute) representing the ready output of the method.
    - `inputs`: The signals (attributes) representing the input arguments of the method.
    - `outputs`: The signals (attributes) representing the output results of the method.

    Results: None

    Assembly format:
    ```mlir
    cmt2.bind.method @methodName : (i1, i32) -> (i1, i32) [
      enable = %enableSignal,
      ready = %readySignal,
      inputs = [%input1, %input2],
      outputs = [%output1, %output2]
    ]
    ```
  }];

  let arguments = (ins 
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    OptionalAttr<FlatSymbolRefAttr>:$enable,
    OptionalAttr<FlatSymbolRefAttr>:$ready,
    FlatSymbolRefArrayAttr:$inputs,
    FlatSymbolRefArrayAttr:$outputs
  );
  let assemblyFormat = [{
    $sym_name `:` $function_type `[` 
      (`enable` `=` $enable^ `,`)? 
      (`ready` `=` $ready^ `,`)? 
      `inputs` `=` $inputs `,` 
      `outputs` `=` $outputs 
    `]` attr-dict
  }];
}

def ModuleOp : Cmt2Op<
  "module", [
    HasParent<"CircuitOp">,
    IsolatedFromAbove,
    SingleBlock,
    Symbol,
    SymbolTable,
    NoTerminator,
    // DeclareOpInterfaceMethods<Cmt2ModuleLike>,
    //DeclareOpInterfaceMethods<HWModuleLike>,
  ]> {
  let summary = "Defining a Cmt2 Module.";
  let description = [{
    The `cmt2.module` defines a Cmt2 module, which can contain rules,
    methods, values and instances to construct the module hierarchy.
    It can also contain primitive modules which have the `ready`-`enable`
    contract, such as FIFO, RAM, etc.
    The module can be instantiated by `cmt2.instance` in other Cmt2 modules.

    Attributes:
    - `sequenceBefore` ('<'): A list of 2-tuples.
    - `conflict` ('<>'): A list of 2-tuples.
    - `conflictFree` ('/'): A list of 2-tuples.

    Arguments: None

    Results: None

    Regions:
    - `body`: This region holds the definitions of rules, methods, values,
      and instances that make up the module's functionality.

    Assembly format:
    ```mlir
    cmt2.module @mod1 {
      cmt2.rule @rule1 { ... }
      cmt2.method @method1 { ... }
      cmt2.value @value1 { ... }
      cmt2.instance @inst1 : @otherMod { ... }
      ...
    } { sequenceBefore = [ ... ], conflict = [ ... ], conflictFree = [ ... ] }
    ```
  }];
  let regions = (region SizedRegion<1>:$body);
  let arguments = (ins 
    SymbolNameAttr:$sym_name
    // OptionalAttr<DictArrayAttr>:$arg_attrs,
    // OptionalAttr<DictArrayAttr>:$res_attrs
  );

  let assemblyFormat = [{
    $sym_name $body attr-dict
  }];

}

def InstanceOp : Cmt2Op<
  "instance", [
    HasParent<"ModuleOp">,
    Symbol,
  //  DeclareOpInterfaceMethods<HWInstanceLike, ["instanceName"]>,
  ]> {
  let summary = "Instantiate a Cmt2 Module or Cmt2 ExtModule";
  let description = [{
    The `cmt2.instance` instantiates a Cmt2 module or Cmt2 external module.
    The instantiated module can be a primitive module which has the
    `ready`-`enable` contract, such as FIFO, RAM, etc.

    Attributes:

    Arguments:
    - `sym_name`: The name of the instance.
    - `args`: The list of signals (values) to be connected to the instance's
      input ports.
    - `module_name`: The symbol name of the module or external module to be
      instantiated.
    - `interface_binds`: (optional) The list of interface bindings, each is a tuple of (interface def name, interface decl name).

    Results: None

    Assembly format:
    ```mlir
    cmt2.instance @inst1 of @mod1 ( %arg1, %arg2, ... : type1, type2, ... ) 
      with [ [ @ifaceDef1, @ifaceDecl1 ], [ @ifaceDef2, @ifaceDecl2 ], ... ]
      attr-dict
    ```
  }];
  // arguments which need to pass to non-scheduler modules.
  let arguments = (ins 
    SymbolNameAttr:$sym_name,
    Variadic<HWIntegerType>:$args,
    FlatSymbolRefAttr:$module_name,
    OptionalAttr<ArrayAttr>:$interface_binds
  );
  let assemblyFormat = [{
    $sym_name `=` $module_name  ( `(` $args^ `)` `:` type($args))?  (`with` $interface_binds^)? attr-dict
  }];
}

def ReturnOp : Cmt2Op<
  "return", [
    Terminator,
    ReturnLike,
    ParentOneOf<["RuleOp", "MethodOp", "ValueOp"]>,
  ]> {
  let summary = "Return from a value/action/rule.";
  let description = [{
    The `cmt2.return` operation is used to return from a `RuleOp`, `MethodOp`, or `ValueOp`.
    It signifies the end of the operation's body and provides the necessary return
    values as per the operation's contract.

    Arguments:
    - `outputs`: values to be returned, if any.

    Results: None 
    Assembly format:
    ```mlir
    cmt2.return %outputs1, %outputs2, ... : <type>
    ```
  }];

  let arguments = (ins
    Variadic<HWIntegerType>:$outputs
  );

  let assemblyFormat = [{
    attr-dict ($outputs^ `:` qualified(type($outputs)))?
  }];

  let builders = [OpBuilder<(ins), [{
    build($_builder, $_state, mlir::ValueRange());
  }]>];


}

def RuleOp : Cmt2Op<
  "rule", [
    IsolatedFromAbove,
    HasParent<"ModuleOp">,
    Symbol,
    // DeclareOpInterfaceMethods<Cmt2RuleLike>,
    SingleBlockImplicitTerminator<"cmt2::ReturnOp">,
  ]> {
  let summary = "Define a rule";
  let description = [{
    The `cmt2.rule` defines a rule, which is the top level scheduling unit
    in Cmt2. The rule can contain method calls, value calls and combinational
    logic to define the behavior of the rule. The rule is scheduled by the
    Cmt2 scheduler, which generates firing logic of rules and the `enable` signals for the methods
    called by the rule.

    Attributes:
    - `explicit`: A boolean attribute indicating whether the rule is explicit.
      An explicit rule is one that have the `ready` signal registered and returned (as the first output).

    Arguments: None

    Results: None

    Regions:
    - `body`: This region holds the operations that define the behavior of the rule.

    Assembly format:
    ```mlir
    cmt2.rule @rule1 { ... }
    ```
  }];
  let regions = (region 
    SizedRegion<1>:$guard,
    SizedRegion<1>:$body
  );
  let arguments = (ins SymbolNameAttr:$sym_name);
  let assemblyFormat = [{
    $sym_name $guard $body attr-dict
  }];
}

def ValueOp : Cmt2Op<
  "value", [
    ParentOneOf<["ModuleOp", "InterfaceOp"]>,
    IsolatedFromAbove,
    Symbol,
    // DeclareOpInterfaceMethods<Cmt2FunctionLike>,
    // DeclareOpInterfaceMethods<Cmt2ValueLike>,
    SingleBlockImplicitTerminator<"cmt2::ReturnOp">
  ]> {
  let summary = "Define a value method";
  let description = [{
    The `cmt2.value` defines a value method, which can be used in `RuleOp`,
    `MethodOp` and `ValueOp`. The value method follows the `ready`-`data`
    contract (without `enable`).
    The value method can contain value calls and combinational
    logic to define the behavior of the value method.

    Attributes:
    - `explicit`: A boolean attribute indicating whether the value method is explicit.
      An explicit value method is one that have the `ready` signal registered and returned (as the first output).

    Arguments:
    - `sym_name`: The symbol name of the value method being defined.
    - `function_type`: The function type of the value method.
    
    Results: None

    Regions:
    - `body`: This region holds the operations that define the behavior of the value method.

    Assembly format:
    ```mlir
    cmt2.value @value1 :<function_type> { ... }
    ```
  }];
  let regions = (region 
    SizedRegion<1>:$guard,
    SizedRegion<1>:$body
  );
  let arguments = (ins 
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type
  );
  let assemblyFormat = [{
    $sym_name `:` $function_type $guard $body attr-dict
  }];
}

def MethodOp : Cmt2Op<
  "method", [
    ParentOneOf<["ModuleOp", "InterfaceOp"]>,
    // DeclareOpInterfaceMethods<Cmt2FunctionLike>,
    // DeclareOpInterfaceMethods<Cmt2MethodLike>,
    Symbol,
    SingleBlockImplicitTerminator<"ReturnOp">,
    //FunctionOpInterface
  ]> {
  let summary = "Define a method, can be used in both Cmt2 ExtModule and Module.";
  let description = [{
    The "cmt2.method" defines an action method, which has the `ready`-`enable` contract. 

    Attributes:
    - `explicit`: A boolean attribute indicating whether the method is explicit. 
    An explicit method should have a `enable` argument as the first input, and a `ready` output as the first output.

    Arguments:
    - `sym_name`: The symbol name of the method being defined.
    - `function_type`: The function type of the method.

    Results: None

    Assembly format:
    ```mlir
    cmt2.method @method1 : <function_type> { ... }
  }];

  let arguments = (ins 
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type
  );

  let regions = (region 
    SizedRegion<1>:$guard,
    SizedRegion<1>:$body
  );

  let assemblyFormat = [{
    $sym_name `:` $function_type $guard $body attr-dict
  }];

  let extraClassDeclaration = [{

    // methods need to be defined by FunctionOpInterface

    // using mlir::detail::FunctionOpInterfaceTrait<MethodOp>::front;
    // using mlir::detail::FunctionOpInterfaceTrait<MethodOp>::getBody;

    // /// Returns the type of this function.
    // FunctionType getFunctionType() {
    //   return getFunctionTypeAttr().getValue().cast<FunctionType>();
    // }

    // /// Returns the argument types of this function.
    // ArrayRef<Type> getArgumentTypes() { return getFunctionType().getInputs(); }

    // /// Returns the result types of this function.
    // ArrayRef<Type> getResultTypes() { return getFunctionType().getResults(); }

    // /// Verify the type attribute of this function. Returns failure and emits
    // /// an error if the attribute is invalid.
    // LogicalResult verifyType() {
    //   auto type = getFunctionTypeAttr().getValue();
    //   if (!type.isa<FunctionType>())
    //     return emitOpError("requires '" + getTypeAttrName() +
    //                        "' attribute of function type");
    //   return success();
    // }
  }];
}

def CallOp : Cmt2Op<
  "call", [
    ParentOneOf<["MethodOp", "RuleOp", "ValueOp"]>,
    // DeclareOpInterfaceMethods<Cmt2CallLike>
  ]> {
  let summary = "Call Method/Value.";
  let description = [{
    The `cmt2.call` operation is used to invoke a `MethodOp` or `ValueOp` within a `RuleOp`, `MethodOp`, or `ValueOp`.

    Attributes: None

    Arguments:
      - `inputs`: The list of input signals (values) to be passed to the method or value being called.
      - `callee`: The symbol name of the method or value being called.
    
    Results:
      - `outputs`: The list of output signals (values) returned by the method or value being called.
  
    Assembly format:
    
    ```mlir
    %output1, %output2 = cmt2.call @methodName(%input1, %input2, ...) : <function_type>
    ```
  }];
  let arguments = (ins 
    Variadic<HWIntegerType>:$inputs,
    SymbolRefAttr:$callee,
    SymbolRefAttr:$methodOrValue
  );
  let results = (outs Variadic<HWIntegerType>:$outputs);
  let assemblyFormat = [{
    $callee $methodOrValue `(` $inputs `)` attr-dict `:` functional-type($inputs, results)
  }];
}

def InterfaceOp : Cmt2Op<
  "interface", [
    IsolatedFromAbove,
    SingleBlock,
    Symbol,
    SymbolTable,
    NoTerminator,
    NoRegionArguments
  ]> {
  let summary = "represent a Cmt2 interface.";
  let description = [{
    The `cmt2.interface` is a container to group methods.

    Arguments:
    - `sym_name`: The symbol name of the interface.

    Results: None

    Regions:
    - `body`: This region holds the definitions of included methods.
      
    Assembly format:
    ```mlir
    cmt2.interface @iface1 {
      cmt2.method @method1 { ... }
      cmt2.value @value1 { ... }
      ...
    }
    ```
  }];
  let arguments = (ins 
    SymbolNameAttr:$sym_name
  );
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = [{
    $sym_name $body attr-dict
  }];
}

def InterfaceDefOp : Cmt2Op<
  "interface.def", [
    HasParent<"ModuleOp">,
    Symbol,
    NoTerminator
  ]> {
  let summary = "Include an interface in a module.";
  let description = [{
    The `cmt2.interface.def` operation defines an interface instance in a Cmt2 module

    Arguments:
    - `interface`: The symbol name of the interface to be defined.
    - `sym_name`: The symbol name of the interface instance.
    - `methods`: The list of method instances, each is a tuple of (instance name, method name, interface method name)

    Results: None

    Assembly format:
    ```mlir
    cmt2.interface.def @sym_name : @interface [
      ( @inst1, @method1, @ifaceMethod1 ),
      ( @inst2, @method2, @ifaceMethod2 ),
      ...
    ] attr-dict
    ```
  }];
  let arguments = (ins 
    SymbolRefAttr:$interface,
    SymbolNameAttr:$sym_name,
    ArrayAttr:$methods
  );
  let assemblyFormat = [{
    $sym_name `:` $interface $methods attr-dict
  }];
}

def InterfaceDeclOp : Cmt2Op<
  "interface.decl", [
    HasParent<"ModuleOp">,
    Symbol,
    NoTerminator
  ]> {
  let summary = "Declare an interface instance in a module.";
  let description = [{
    The `cmt2.interface.decl` operation declares an interface instance in a Cmt2 module.
    The instance can be bound during instantiation of the module or left unbound for a top module.

    Arguments:
    - `interface`: The symbol name of the interface to be declared.
    - `sym_name`: The symbol name of the interface instance.

    Results: None

    Assembly format:
    ```mlir
    cmt2.interface.decl @sym_name : @interface attr-dict
    ```
  }];
  let arguments = (ins 
    SymbolRefAttr:$interface,
    SymbolNameAttr:$sym_name
  );
  let assemblyFormat = [{
    $sym_name `:` $interface attr-dict
  }];
}

#endif // CIRCT_DIALECT_Cmt2_Cmt2OPS_TD