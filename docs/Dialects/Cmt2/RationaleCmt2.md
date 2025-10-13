# `cmt2` Dialect Rationale

This document describes various design points of the Cmt2 dialect. This follows in the spirit of
other [MLIR Rationale docs](https://mlir.llvm.org/docs/Rationale/).

## Introduction

This dialect is the MLIR implementation of cmt2:
```
cmt2: Rule-Based Hardware Description in Rust with Temporal Semantics
Youwei Xiao, Zizhang Luo, and Yun Liang
In 5th Workshop on Languages, Tools, and Techniques for Accelerator Design (LATTE’25), 2025
```

## Operations

- `cmt2.module`: Define a Cmt2 module. It includes signals for clock/reset, instances, rules, methods, and etc.
- `cmt2.module.extern.hw`: Bind a `hw.module`
<!-- - `cmt2.module.extern.firrtl`: Bind a `firrtl.module` -->
- `cmt2.method`: define an action method
- `cmt2.value`: define a value method
- `cmt2.rule`: define a rule
- `cmt2.instance`: instantiate a module
- `cmt2.call`: call a method
- `cmt2.return`: return a value to caller
- `cmt2.interface`: declare an interface comprised of methods.
- `cmt2.interface.def`: define an interface instance
- `cmt2.interface.decl`: declare an interface instance

## Interfaces

### Cmt2ModuleLike Interface

The `Cmt2ModuleLike` interface provides a common abstraction for module-like operations in the Cmt2 dialect (`ModuleOp` and `ExtModuleHwOp`). This interface enables uniform access to methods and values defined within modules, which is essential for analysis passes like CallInfo.

**Required Methods:**
- `lookupFunctionLike(StringAttr name)`: Look up a Cmt2FunctionLike by name

**Utility Functions:**
The Cmt2 dialect provides utility functions in `Cmt2OpInterfaces.h`:
- `isMethodOp(Operation*)`: Check if an operation is a MethodOp
- `isValueOp(Operation*)`: Check if an operation is a ValueOp
- `getCalleeType(Operation*)`: Determine the type of a callee (Method or Value)

These utilities simplify code that needs to distinguish between methods and values, avoiding verbose `isa_and_nonnull` checks throughout the codebase.

### Cmt2FunctionLike Interface

The `Cmt2FunctionLike` interface provides a common abstraction for function-like operations in the Cmt2 dialect that can be called. This includes:
- `RuleOp`: Rules that define behavior
- `MethodOp`: Action methods that can modify state
- `ValueOp`: Value methods that read state
- `BindMethodOp`: Bound methods from external hardware modules
- `BindValueOp`: Bound values from external hardware modules

This interface enables uniform handling of all callable entities in the Cmt2 dialect, which is essential for analysis passes and transformations.

**Required Methods:**
- `getFunctionKind()`: Returns a `FunctionKind` enum indicating whether this is a Rule, Method, or Value
- `functionName()`: Get the function name as StringRef
- `functionNameAttr()`: Get the function name as StringAttr

**FunctionKind Enum:**
The `FunctionKind` enum distinguishes between different types of callable entities:
- `FunctionKind::Rule`: Represents a RuleOp
- `FunctionKind::Method`: Represents a MethodOp or BindMethodOp
- `FunctionKind::Value`: Represents a ValueOp or BindValueOp

**Utility Functions:**
The Cmt2 dialect provides utility functions in `Cmt2OpInterfaces.h`:
- `isRuleOp(Operation*)`: Check if an operation is a RuleOp
- `getFunctionKind(Operation*)`: Get the FunctionKind of an operation

These utilities simplify code that needs to distinguish between different types of callable entities, providing a uniform interface for working with rules, methods, and values.

## Appendix

Quotes from [`sequence/gaa`](https://github.com/sequencer/circt/blob/gaa/docs/RationalGAA.md):

```markdown
## Introduction

[Guarded Atomic Action](https://ieeexplore.ieee.org/document/1560170/) provides
a state transition based RTL design paradigm. Like any other RTL, it is defined
by modules, and instantiates modules to construct the hierarchy of circuit. 

### One-Rule-At-A-Time(ORAAT)

The one-rule-at-a-time (ORAAT) semantics of a collection of rules is to pick a
rule nondeterministically, execute it, and commit its results.
The process is repeated endlessly, as if exactly one rule executed in each 
clock cycle: if one rule writes to a register, the next rule observes the newly
written value. The ORAAT semantics need not produce a deterministic answer 
because the rules are not required to be confluent.

This also implies: A state is valid, if and only if, the state can be achieved
via executing a list of rules in sequence.

The ORAAT is the essential semantic of GAA. While FIRRTL using the last-connect 
semantic, GAA Dialect fundamentally eliminates the multi-write issue in an 
elegant way: each rule can write to a same port, but won't be enabled together. 

### Hardware Contract

To achieve ORAAT semantics, the contract of GAA is the essence: for all
hardware ports, `ready` signal should always exist(even it's always asserted).
The semantic of `ready` is: port can be operated under this state.
Accesses to different ports will `AND` together those `ready` signals together,
serving as the implicit condition.
Besides `ready` signals for each port, `enable` is needed for ports which might
permute system states. The semantic of `enable` is, after analysing the system
states(via `ready` signal), assert this signal to trigger permutation on 
corresponding ports. 

GAA Dialect can and only can provide one contract: `ready`-`enable` contract,
which means, it's impossible to maintain `ready`-`valid` and `token` contract
under this Dialect, thus it needs a seamless integration to other Dialect, like
HW or FIRRTL Dialect.

### Methods

GAA Dialect borrows ideas from function-call in software, using function-like
call-return to perform the connect operation. Basically, this idea was derived
from Bluespec, which can was firstly introduced in "Kernel of Bluespec"(KBS1)
presented in [Modular compilation of guarded atomic actions](https://ieeexplore.ieee.org/document/6670957).

For each sequential components(register, FIFO, RAM), rather than directly using
connectable ports, e,g, D/Q for register, enqueue/dequeue for FIFO, read/write
port to RAM. GAA Dialect regards those components as instances, and call 
methods on those instance, those call to instance will be passed to the MLIR 
compiler to analyse the conflict matrix between rules.

### Scheduling
MLIR Compiler will schedule as much as possibles rules in a single cycle. The
scheduling result is opaque to users, which protects user from dealing with 
complex control circuits.

Scheduler will analyse from the bottom of instance hierarchy(always being 
registers, FIFOs and RAMs), based on the call map to those methods, scheduler 
can construct a conflict matrix among rules. There will be 4 relationships:
- `r0 /  r1`, Conflict(C): `r0` and `r1` cannot be executed in a same cycle.
- `r0 <> r1`, Conflict Free(CF): `r0` and `r1` can be executed in a same cycle 
  in any order.
- `r0 >  r1`, sequential after(SA): `r0` and `r1` can be executed in a same 
  cycle, but `r0` should be executed after `r1`.
- `r0 <  r1`, sequential before(SB): `r0` and `r1` can be executed in a same
  cycle, but `r0` should be executed before `r1`.

After construction to conflict matrix, `enable` signal should be automatically
constructed by scheduler hardware generator via procedure below:
1. From circuit bottom to top, gather the information about in the method
   definition body, this method calls which method on which instance. Gather
   conflict matrix information to this method.
2. In the Rule definition, use conflict matrix for each method, generate the 
   scheduling table, then construct a PLA.
3. For each method definition, reduce-`AND` together all method `ready` and 
   explicit `guard` signal as the `ready` signal of this method.
4. pull all rule `ready` signal to scheduler PLA.

## Operators
- `gaa.module`: Defining a GAA Module.
  It has a function-like region, inputs are scheduler unrelated signals, which
  can pass down though the instance hierarchy, this is really useful to give
  different instance clock/reset domains, and help user embedded their own
  blackbox under other dialect into GAA Dialect.  
  `body` of which is used to define the module body, which contains three 
  different parts: 
  - module instantiation via `gaa.insatnce`
  - rule definition via `gaa.rule`
  - method definition via `gaa.method`
  attributes:
  `moduleName`: the global name of this module.

- `gaa.module.extern`: Binding a `hw.module` to GAA ExtModule.
  Like `gaa.module`, this is also a function-like region. However, this is 
  always used for defining primitive and user operations.
  `body` of which is used to define the module body, which only contains 
  `gaa.method` to make a method definition, and `gaa.method` should contain a
  `gaa.bind`, which is used to bind port of `hw.module` to `gaa.module`, making
  scheduler being able to infer the conflict matrix.

- `gaa.method`: Define a method, can be used in both GAA ExtModule and Module.
  It defines the interface to interact with this module, in lowering to HW 
  Dialect flow, this method is used for port generation. Beside pure hardware
  generation, method is also used for conflict matrix detection: scheduler will
  collect calling relationship for each method calling, from bottom to up to 
  collect the calling relationship, and use this to schedule rules to construct
  the hardware.  
  For each method, the first output is the `guard` in i1, second return is 
  optional in any hardware type, representing the return value to user call.
  the argument of `gaa.method` can is a list, can be empty or multiple values,
  which represent the input signals to the internal `gaa.method`.

- `gaa.rule`: Define a rule region, only can be used in the GAA Module.
  Rule is used to define behaviors of a module. Inside rule body, user can use
  `gaa.call` methods to each instance of this module. and use Comb Dialect to 
  express the operation to data.

- `gaa.insatnce`: Instantiate a GAA Module or GAA ExtModule. 
  It instantiates modules to construct the module hierarchy, this module can 
  invoke instance module with `gaa.call`.

- `gaa.call`: call a method from an instance.
  It is used to call a `gaa.method` of an instance in this module, it can be
  used from the body of `gaa.rule` and `gaa.method`.

- `gaa.return`: return guard and value to caller.
- `gaa.bind`: Binding signal to method, can only be used in GAA ExtModule.
  - if `gaa.bind` is in the `gaa.module.extern` region, the binding should be a
    pure blackbox binding.
  - if `gaa.bind` is in the `gaa.method` region, the binding should be a 
    scheduling binding, scheduler generated IO should be bind to blackbox IO.

```