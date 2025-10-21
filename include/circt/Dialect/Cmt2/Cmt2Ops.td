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

include "circt/Support/InstanceGraphInterface.td"
include "mlir/Interfaces/CallInterfaces.td"

// Define FIRRTL type constraint for use in operation definitions
def FIRRTLBaseType : Type<CPred<"llvm::isa<::circt::firrtl::FIRRTLBaseType>($_self)">,
                          "a FIRRTL base type", "::circt::firrtl::FIRRTLBaseType">;

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

def ExtModuleFirrtlOp : Cmt2Op<
  "module.extern.firrtl", [
    HasParent<"CircuitOp">,
    IsolatedFromAbove,
    SingleBlock,
    Symbol,
    SymbolTable,
    NoTerminator,
    OpAsmOpInterface,
    DeclareOpInterfaceMethods<InstanceGraphModuleOpInterface>,
    Cmt2ModuleLike,
  ]> {
  let summary = "Binding a `firrtl.module` to Cmt2 ExtModule.";
  let description = [{
    The `cmt2.module.extern.firrtl` binds a `firrtl.module` to a Cmt2 external module.

    Attributes:
    - `sequenceBefore` ('<'): A list of 2-tuples.
    - `conflict` ('<>'): A list of 2-tuples.
    - `conflictFree` ('/'): A list of 2-tuples.

    Arguments:
    - `ext_module_name`: The symbol name of the `firrtl.module` to bind to.
    - Input ports (as block arguments): clock, reset, and other signals

    Results: None

    Regions:
    - `body`: This region holds the `cmt2.bind.*` operations

    Assembly format:
    ```mlir
    cmt2.module.extern.firrtl @extmod1 : @firrtlmod1(in %clk: !firrtl.clock, in %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.value ...
      cmt2.bind.method ...
    } {sequenceBefore = [...], conflict = [...], conflictFree = [...]}
    ```
  }];
  let regions = (region SizedRegion<1>:$body);
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    FlatSymbolRefAttr:$ext_module_name,
    StrArrayAttr:$argNames
  );

  let hasCustomAssemblyFormat = 1;

  let extraClassDeclaration = [{
    // Provide getNameAttr for ModuleOpInterface default implementation
    mlir::StringAttr getNameAttr() { return getSymNameAttr(); }

    void getAsmBlockArgumentNames(mlir::Region &region,
                                  mlir::OpAsmSetValueNameFn setNameFn);
  }];
}

def BindBareOp : Cmt2Op<
  "bind.bare", [
    HasParent<"ExtModuleFirrtlOp">
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
    cmt2.bind.bare %signal, @port : !firrtl.uint<1>
    ```
  }];
  let arguments = (ins
    FIRRTLBaseType:$signal,
    FlatSymbolRefAttr:$port
  );
  let assemblyFormat = [{
    $signal `,` $port attr-dict `:` qualified(type($signal))
  }];
}

def BindValueOp : Cmt2Op<
  "bind.value", [
    HasParent<"ExtModuleFirrtlOp">,
    DeclareOpInterfaceMethods<Cmt2FunctionLike, ["getCallableRegion", "getArgumentTypes", "getResultTypes", "isExternal", "getFunctionBody"]>,
    Symbol
  ]> {
  let summary = "Binding a value method";
  let description = [{
    The "cmt2.bind.value" operations binds a value method to firrtl.module logic.
    
    Note: The value method cannot have input arguments.

    Arguments:
    - `sym_name`: The symbol name of the value method being bound.
    - `function_type`: The function type of the value method.
    - `readyName`: The signal (attribute) representing the ready output of the value method.
    - `argNames`: Names of the shared input arguments.
    - `bodyResNames`: Names of the body region results.

    Results: None

    Assembly format:
    ```mlir
    cmt2.bind.value @valMethod : (i1) -> (i32) [
      ready = %readySignal,
      arguments = [%input1, %input2],
      results = [%output1, %output2]
    ]
    ```
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    OptionalAttr<StrAttr>:$readyName,
    StrArrayAttr:$argNames,
    StrArrayAttr:$bodyResNames,
    OptionalAttr<ArrayAttr>:$arg_attrs,
    OptionalAttr<ArrayAttr>:$res_attrs
  );

  let assemblyFormat = [{
    $sym_name `:` $function_type `[` 
      (`ready` `=` $readyName^ `,`)? 
      `arguments` `=` $argNames `,` 
      `results` `=` $bodyResNames 
    `]` attr-dict
  }];

  let extraClassDefinition = [{
    FunctionKind $cppClass::getFunctionKind() {
      return FunctionKind::Value;
    }
  }];
}

def BindMethodOp : Cmt2Op<
  "bind.method", [
    HasParent<"ExtModuleFirrtlOp">,
    DeclareOpInterfaceMethods<Cmt2FunctionLike, ["getCallableRegion", "getArgumentTypes", "getResultTypes", "isExternal", "getFunctionBody"]>,
    Symbol,
    NoTerminator,
    IsolatedFromAbove
  ]> {
  let summary = "Binding a method";
  let description = [{
    The "cmt2.bind.method" operation binds a method to firrtl.module logic. 
    The method follows the ready-enable protocol.

    Arguments:
    - `sym_name`: The symbol name of the method being bound.
    - `function_type`: The function type of the method.
    - `enableName`: The signal (attribute) representing the enable input of the method.
    - `readyName`: The signal (attribute) representing the ready output of the method.
    - `argNames`: Names of the shared input arguments.
    - `bodyResNames`: Names of the body region results.

    Results: None

    Assembly format:
    ```mlir
    cmt2.bind.method @methodName : (i1, i32) -> (i1, i32) [
      enable = %enableSignal,
      ready = %readySignal,
      arguments = [%input1, %input2],
      results = [%output1, %output2]
    ]
    ```
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    OptionalAttr<StrAttr>:$enableName,
    OptionalAttr<StrAttr>:$readyName,
    StrArrayAttr:$argNames,
    StrArrayAttr:$bodyResNames,
    OptionalAttr<ArrayAttr>:$arg_attrs,
    OptionalAttr<ArrayAttr>:$res_attrs
  );

  let assemblyFormat = [{
    $sym_name `:` $function_type `[` 
      (`enable` `=` $enableName^ `,`)? 
      (`ready` `=` $readyName^ `,`)? 
      `arguments` `=` $argNames `,` 
      `results` `=` $bodyResNames 
    `]` attr-dict
  }];

  let extraClassDefinition = [{
    FunctionKind $cppClass::getFunctionKind() {
      return FunctionKind::Method;
    }
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
    OpAsmOpInterface,
    DeclareOpInterfaceMethods<InstanceGraphModuleOpInterface>,
    Cmt2ModuleLike,
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

    Arguments:
    - Input ports (as block arguments): clock, reset, and other signals

    Results: None

    Regions:
    - `body`: This region holds the definitions of rules, methods, values,
      and instances that make up the module's functionality.

    Assembly format:
    ```mlir
    cmt2.module @mod1(in %clk: i1, in %rst: i1) {
      cmt2.rule @rule1 { ... }
      cmt2.method @method1 { ... }
      cmt2.value @value1 { ... }
      cmt2.instance @inst1 : @otherMod { ... }
      ...
    } {sequenceBefore = [...], conflict = [...], conflictFree = [...]}
    ```
  }];
  let regions = (region SizedRegion<1>:$body);
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    StrArrayAttr:$argNames
  );

  let hasCustomAssemblyFormat = 1;

  let extraClassDeclaration = [{
    // Provide getNameAttr for ModuleOpInterface default implementation
    mlir::StringAttr getNameAttr() { return getSymNameAttr(); }

    void getAsmBlockArgumentNames(mlir::Region &region,
                                  mlir::OpAsmSetValueNameFn setNameFn);
  }];

}

def InstanceOp : Cmt2Op<
  "instance", [
    HasParent<"ModuleOp">,
    Symbol,
    DeclareOpInterfaceMethods<InstanceGraphInstanceOpInterface>,
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
    Variadic<FIRRTLBaseType>:$args,
    FlatSymbolRefAttr:$module_name,
    OptionalAttr<ArrayAttr>:$interface_binds
  );
  let assemblyFormat = [{
    $sym_name `=` $module_name  ( `(` $args^ `)` `:` type($args))?  (`with` $interface_binds^)? attr-dict
  }];

  let extraClassDeclaration = [{
    // Provide getReferencedModuleNameAttr for InstanceOpInterface default implementation
    mlir::StringAttr getReferencedModuleNameAttr() {
      return getModuleNameAttr().getAttr();
    }

    /// Get the referenced module (Cmt2ModuleLike) for this instance
    Cmt2ModuleLike getReferencedModule();
  }];

  let extraClassDefinition = [{
    llvm::StringRef $cppClass::getInstanceName() {
      return getSymName();
    }

    mlir::StringAttr $cppClass::getInstanceNameAttr() {
      return getSymNameAttr();
    }
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
    Variadic<FIRRTLBaseType>:$outputs
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
    DeclareOpInterfaceMethods<Cmt2FunctionLike, ["getCallableRegion", "getArgumentTypes", "getResultTypes", "isExternal", "getFunctionBody"]>,
    SingleBlockImplicitTerminator<"cmt2::ReturnOp">,
    OpAsmOpInterface,
  ]> {
  let summary = "Define a rule";
  let description = [{
    The `cmt2.rule` defines a rule, which is the top level scheduling unit
    in Cmt2. The rule can contain method calls, value calls and combinational
    logic to define the behavior of the rule. The rule is scheduled by the
    Cmt2 scheduler, which generates firing logic of rules and the `enable` signals for the methods
    called by the rule.

    Arguments:
    - `sym_name`: The symbol name of the rule being defined.
    - `function_type`: The function type of the rule (typically no arguments).
    - `argNames`: Names of the shared input arguments (typically empty for rules).
    - `bodyResNames`: Names of the body region results.

    Results: None

    Regions:
    - `guard`: The guard region that determines when the rule can fire.
    - `body`: The body region that contains the rule's actions.

    Assembly format:
    ```mlir
    cmt2.rule @rule1() -> (i1) { guard } { body }
    ```
  }];
  let regions = (region
    SizedRegion<1>:$guard,
    SizedRegion<1>:$body
  );
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    StrArrayAttr:$argNames,
    StrArrayAttr:$bodyResNames,
    OptionalAttr<ArrayAttr>:$arg_attrs,
    OptionalAttr<ArrayAttr>:$res_attrs
  );

  let hasCustomAssemblyFormat = 1;

  let extraClassDeclaration = [{
    void getAsmBlockArgumentNames(mlir::Region &region,
                                  mlir::OpAsmSetValueNameFn setNameFn);
  }];

  let extraClassDefinition = [{
    FunctionKind $cppClass::getFunctionKind() {
      return FunctionKind::Rule;
    }
  }];

}

def ValueOp : Cmt2Op<
  "value", [
    ParentOneOf<["ModuleOp", "InterfaceOp"]>,
    IsolatedFromAbove,
    Symbol,
    DeclareOpInterfaceMethods<Cmt2FunctionLike, ["getCallableRegion", "getArgumentTypes", "getResultTypes", "isExternal", "getFunctionBody"]>,
    SingleBlockImplicitTerminator<"cmt2::ReturnOp">,
    OpAsmOpInterface,
  ]> {
  let summary = "Define a value method";
  let description = [{
    The `cmt2.value` defines a value method, which can be used in `RuleOp`,
    `MethodOp` and `ValueOp`. The value method follows the `ready`-`data`
    contract (without `enable`).
    The value method can contain value calls and combinational
    logic to define the behavior of the value method.

    Arguments:
    - `sym_name`: The symbol name of the value method being defined.
    - `function_type`: The function type of the value method (shared arguments).
    - `argNames`: Names of the shared input arguments.
    - `bodyResNames`: Names of the body region results.

    Results: None

    Regions:
    - `guard`: The guard region that determines when the value is ready.
    - `body`: The body region that contains the value computation.

    Assembly format:
    ```mlir
    cmt2.value @value1(%arg0: i32) -> (i32) { guard } { body }
    ```
  }];
  let regions = (region
    SizedRegion<1>:$guard,
    SizedRegion<1>:$body
  );
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    StrArrayAttr:$argNames,
    StrArrayAttr:$bodyResNames,
    OptionalAttr<ArrayAttr>:$arg_attrs,
    OptionalAttr<ArrayAttr>:$res_attrs
  );

  let hasCustomAssemblyFormat = 1;

  let extraClassDeclaration = [{
    void getAsmBlockArgumentNames(mlir::Region &region,
                                  mlir::OpAsmSetValueNameFn setNameFn);
  }];

  let extraClassDefinition = [{
    FunctionKind $cppClass::getFunctionKind() {
      return FunctionKind::Value;
    }
  }];
}

def MethodOp : Cmt2Op<
  "method", [
    ParentOneOf<["ModuleOp", "InterfaceOp"]>,
    IsolatedFromAbove,
    DeclareOpInterfaceMethods<Cmt2FunctionLike, ["getCallableRegion", "getArgumentTypes", "getResultTypes", "isExternal", "getFunctionBody"]>,
    Symbol,
    SingleBlockImplicitTerminator<"ReturnOp">,
    OpAsmOpInterface,
  ]> {
  let summary = "Define a method, can be used in both Cmt2 ExtModule and Module.";
  let description = [{
    The "cmt2.method" defines an action method, which has the `ready`-`enable` contract.

    Arguments:
    - `sym_name`: The symbol name of the method being defined.
    - `function_type`: The function type of the method (shared arguments).
    - `argNames`: Names of the shared input arguments.
    - `bodyResNames`: Names of the body region results.

    Results: None

    Assembly format:
    ```mlir
    cmt2.method @method1(%arg0: i32, %arg1: i32) -> () { guard } { body }
    ```
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    StrArrayAttr:$argNames,
    StrArrayAttr:$bodyResNames,
    OptionalAttr<ArrayAttr>:$arg_attrs,
    OptionalAttr<ArrayAttr>:$res_attrs
  );

  let regions = (region
    SizedRegion<1>:$guard,
    SizedRegion<1>:$body
  );

  let hasCustomAssemblyFormat = 1;

  let extraClassDeclaration = [{
    void getAsmBlockArgumentNames(mlir::Region &region,
                                  mlir::OpAsmSetValueNameFn setNameFn);
  }];

  let extraClassDefinition = [{
    FunctionKind $cppClass::getFunctionKind() {
      return FunctionKind::Method;
    }
  }];

}

def CallOp : Cmt2Op<
  "call", [
    ParentOneOf<["MethodOp", "RuleOp", "ValueOp", "IfOp"]>,
    DeclareOpInterfaceMethods<CallOpInterface>
  ]> {
  let summary = "Call Method/Value.";
  let description = [{
    The `cmt2.call` operation is used to invoke a `MethodOp` or `ValueOp` within a `RuleOp`, `MethodOp`, or `ValueOp`.

    This operation implements CallOpInterface, providing standard call-like operation features:
    - getCallableForCallee(): Returns the callee symbol reference
    - getArgOperands(): Returns the input operands
    - resolveCallable(): Resolves the callable operation

    Arguments:
      - `inputs`: The list of input signals (values) to be passed to the method or value being called.
      - `callee`: The symbol name of the instance being called. A special case is the `@this` callee, which refers to the current module itself.
      - `methodOrValue`: The symbol name of the method or value being called.

    Results:
      - `outputs`: The list of output signals (values) returned by the method or value being called.

    Assembly format:

    ```mlir
    %output1, %output2 = cmt2.call @callee @methodName(%input1, %input2, ...) : <function_type>
    ```
  }];
  let arguments = (ins
    Variadic<FIRRTLBaseType>:$inputs,
    SymbolRefAttr:$callee,
    SymbolRefAttr:$methodOrValue,
    OptionalAttr<ArrayAttr>:$arg_attrs,
    OptionalAttr<ArrayAttr>:$res_attrs
  );
  let results = (outs Variadic<FIRRTLBaseType>:$outputs);
  let assemblyFormat = [{
    $callee $methodOrValue `(` $inputs `)` attr-dict `:` functional-type($inputs, results)
  }];

  let extraClassDeclaration = [{
    /// Get the methodOrValue symbol as the primary callee
    mlir::SymbolRefAttr getMethodOrValueSymbol() {
      return getMethodOrValueAttr();
    }
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

def IfOp : Cmt2Op<
  "if", [
    ParentOneOf<["MethodOp", "RuleOp", "ValueOp", "IfOp"]>,
    NoRegionArguments
  ]> {
  let summary = "Conditional execution with if-else branches.";
  let description = [{
    The `cmt2.if` operation provides conditional execution within rules, methods,
    and values. It has a required `then` region and an optional `else` region,
    similar to standard if-else control flow.

    Arguments:
    - `condition`: A value of type !firrtl.uint<1> that determines which branch executes.

    Results:
    - Optional results if both branches return values of the same types.

    Regions:
    - `thenRegion`: The region executed when the condition is true.
    - `elseRegion`: Optional region executed when the condition is false.

    Assembly format:
    ```mlir
    cmt2.if %condition : !firrtl.uint<1> {
      // then region
    } else {
      // else region (optional)
    }
    ```

    For operations with results:
    ```mlir
    %result = cmt2.if %condition : !firrtl.uint<1> -> !firrtl.uint<32> {
      %val = ...
      cmt2.yield %val : !firrtl.uint<32>
    } else {
      %val = ...
      cmt2.yield %val : !firrtl.uint<32>
    }
    ```
  }];
  let arguments = (ins
    FIRRTLBaseType:$condition
  );
  let results = (outs Variadic<FIRRTLBaseType>:$results);
  let regions = (region
    SizedRegion<1>:$thenRegion,
    AnyRegion:$elseRegion
  );

  let assemblyFormat = [{
    $condition `:` qualified(type($condition))
    (`->` type($results)^)?
    $thenRegion
    (`else` $elseRegion^)?
    attr-dict
  }];

  let hasVerifier = 1;
}

def YieldOp : Cmt2Op<
  "yield", [
    Terminator,
    ParentOneOf<["IfOp"]>,
    ReturnLike
  ]> {
  let summary = "Yield values from an if region.";
  let description = [{
    The `cmt2.yield` operation is used to yield values from the regions of
    a `cmt2.if` operation. It marks the end of a region and provides the
    values to be returned.

    Arguments:
    - `results`: The values to yield from the region.

    Assembly format:
    ```mlir
    cmt2.yield %result1, %result2, ... : <type>
    ```
  }];
  let arguments = (ins
    Variadic<FIRRTLBaseType>:$results
  );

  let assemblyFormat = [{
    attr-dict ($results^ `:` qualified(type($results)))?
  }];

  let builders = [OpBuilder<(ins), [{
    build($_builder, $_state, mlir::ValueRange());
  }]>];
}

#endif // CIRCT_DIALECT_Cmt2_Cmt2OPS_TD