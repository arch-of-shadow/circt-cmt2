# Develop Cmt2

We (human and Claude Code) are working together to complete the Cmt2 dialect. Read (`Status`)[#Status] for basic information. Read and record in (`Tasks`)(#Tasks) for development.

## Status

Base workspace: 
- `/home/uvxiao/circt-cmt2`

For building, cd to `build/` and run `ninja`

The `cmt2` dialect has been added:
- `include/circt/Dialect/Cmt2`
- `lib/Dialect/Cmt2`

Now, only basic operation definitions work. The `circt-opt` (`build/bin/circt-opt`) can parse a `gcd.mlir` (`test/Dialect/Cmt2/gcd.mlir`) example successfully:
```shell
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir 
```

Docs are maintained at `docs/Dialects/Cmt2/RationaleCmt2.md`

## Tasks

For every task, you should create a Progress to record the status.

There are code blocks in "Spec" sections, which you should fill when conducting the task.

You should record the progress in the "Progress" sections.


### InstanceGraph Analysis

#### Spec

You should read:
- `include/circt/Support/InstanceGraph.h`
- `include/circt/Support/InstanceGraphInterface.td`
- `include/circt/Support/InstanceGraphInterface.h`

To understand what an InstanceGraph can do. Summarize below:
```text
InstanceGraph Overview:
- A generic instance graph that tracks modules and their instantiation relationships
- Similar to a call graph but for hardware modules
- Core classes:
  1. InstanceRecord: Represents an edge/instantiation in the graph
     - Tracks the instance operation (InstanceOpInterface)
     - Knows its parent module and target module
  2. InstanceGraphNode: Represents a node/module in the graph
     - Tracks the module operation (ModuleOpInterface)
     - Contains list of instances within this module
     - Maintains use list of instances that instantiate this module
     - Provides iterators for both directions
  3. InstanceGraph: The main graph container
     - Built from a parent operation containing modules
     - Maps module names to nodes
     - Supports graph traversal (top-down and bottom-up)
     - Can be used as a cached analysis

Required interfaces (from InstanceGraphInterface.td):
- ModuleOpInterface: Operations that represent modules must implement:
  - getModuleName() and getModuleNameAttr()
- InstanceOpInterface: Operations that represent instances must implement:
  - getInstanceName() and getInstanceNameAttr()
  - getReferencedModuleNames() and getReferencedModuleNamesAttr()
```

We need to add the InstanceGraph support for `cmt2`. 

You should first read:
- `include/circt/Dialect/FIRRTL/FIRRTLInstanceGraph.h`
- `lib/Dialect/FIRRTL/FIRRTLInstanceGraph.cpp`
- `include/circt/Dialect/HW/HWInstanceGraph.h`
- `lib/Dialect/HW/HWInstanceGraph.cpp`

To understand how to add the support. Summarize below:
```text
FIRRTL InstanceGraph Implementation:
- Extends igraph::InstanceGraph base class
- Constructor takes CircuitOp or ModuleOp, finds the CircuitOp inside
- Identifies top-level module from CircuitOp's name attribute
- Very simple: just wraps the generic InstanceGraph with FIRRTL-specific types

HW InstanceGraph Implementation:
- Also extends igraph::InstanceGraph
- Creates a virtual "entry" node that links to all public modules
- Constructor iterates all modules and adds public ones to entry node
- Provides addHWModule() to handle adding new modules
- Overrides erase() to clean up links to the entry node
- More complex: handles the concept of public/private module visibility

Steps to add InstanceGraph support:
1. Ops must implement required interfaces:
   - ModuleOp and ExtModuleHwOp need ModuleOpInterface
   - InstanceOp needs InstanceOpInterface
2. Add these interfaces in the TD file
3. Implement the interface methods in Ops.cpp
4. Create a Cmt2InstanceGraph class (similar to FIRRTL or HW)
5. Decide on top-level module strategy (named like FIRRTL, or virtual entry like HW)
```

You also need to modify:
- `include/circt/Dialect/Cmt2/Cmt2Ops.td`
- `include/circt/Dialect/Cmt2/Cmt2Ops.h`
- `lib/Dialect/Cmt2/Cmt2Ops.cpp`

To give necessary interfaces or methods for Cmt2 operations (`ModuleOp`, `InstanceOp`, and more) according to InstanceGraph's need.

You should add a test pass under `cmt2`. You can look at 
- `include/circt/Dialect/HW/HWPasses.h`
- `include/circt/Dialect/HW/Passes.td`
- `lib/Dialect/HW/Transforms`
to understand how to add a pass.

You should create a test case (similar to `gcd.mlir` but have more module hierarchy), and fill the shell command below to test:
```shell
# Test the InstanceGraph with the hierarchical test case
build/bin/circt-opt test/Dialect/Cmt2/instance-graph.mlir -cmt2-print-instance-graph

# Alternative: Just parse and verify the test case
build/bin/circt-opt test/Dialect/Cmt2/instance-graph.mlir
```

#### Progress

✅ **COMPLETE** - InstanceGraph support with interfaces for ModuleOp/ExtModuleHwOp/InstanceOp, Cmt2InstanceGraph class, and PrintInstanceGraph pass.

### CallInfo Analysis

#### Spec

We want an analysis result to show: for every `cmt2` module, what does each of its rule/method/value call? 

The data structure should look like:
```text
CallInfoView:
  Dict:
    Key: ModuleName (SymbolRefAttr)
    Value:
      Dict:
        Key: EntityName (SymbolRefAttr)  # rule/method/value name
        Value:
          List:
            Each entry is a CallInfo:
              - calleeInstance: SymbolRefAttr
              - calleeEntity: SymbolRefAttr
              - callType: Enum (MethodCall, ValueCall)
                # callType represents the TYPE of the callee (not the caller)
                # i.e., whether we're calling a Method or a Value

```

You should read `include/circt/Dialect/Cmt2/Cmt2Ops.td` to understand how `cmt2` operations are defined, especially `CallOp`, `RuleOp`, `MethodOp`, and `ValueOp`, which are involved in this analysis.

Then, similar to `include/circt/Dialect/Cmt2/Cmt2InstanceGraph.h` and `lib/Dialect/Cmt2/Cmt2InstanceGraph.cpp`, you should create:
- `include/circt/Dialect/Cmt2/Cmt2CallInfo.h`
- `lib/Dialect/Cmt2/Cmt2CallInfo.cpp`

You should add a test pass under `cmt2`, similar to `PrintInstanceGraph`, to print the CallInfoView result.

You should use the `gcd.mlir` (`test/Dialect/Cmt2/gcd.mlir`) to test the CallInfo analysis. You can fill the shell command below to test:
```shell
# Test the CallInfo analysis with gcd.mlir
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-print-call-info
```

#### Progress

✅ **COMPLETE** - CallInfo analysis tracking rule/method/value calls with CallInfoView data structure and PrintCallInfo pass.

### Better Parse/Print Support

#### Spec

Currently, `cmt2` operations use `assemblyFormat` in TD files for parse/print, which is simple but restricted. We need better Parse/Print for several operations:
- `ModuleOp` and `ExtModuleHwOp`, we need `hw.module`-style parse/print:
    ```mlir
    hw.module @adder(in %in1: i2, in %in2: i2, out out: i2) 
    ```
- `RuleOp`, `BindMethodOp`, `BindValueOp`, `MethodOp`, `ValueOp` should have `handshake.func`-style parse/print:
    ```mlir
    handshake.func @pack_unpack(%arg0 : i32, %arg1 : i1) -> (i32, i1)
    ```

To do so, you need to read:
- `include/circt/Dialect/HW/HWStructure.td`
- `include/circt/Dialect/Handshake/HandshakeOps.td`
- `lib/Dialect/HW/HWOps.cpp`
- `lib/Dialect/Handshake/HandshakeOps.cpp`

You need to edit `include/circt/Dialect/Cmt2/Cmt2Ops.td` to update the operations' arguments and interfaces. You need to implement the parsers and printers. Keep in mind to reuse code as much as you can (there will be many reuse opportunities).

In addition to parse/print, you also need to add `HWModuleOpBase`'s similar interfaces and `Handshake` `FuncOp`'s interfaces for the `cmt2` operations above for easy access and manipulation. You also need to add similar builders for the involved operations.

You should update the `test/Dialect/Cmt2/gcd.mlir` to use the new syntax. You should run the below command to test the successful parse/print.
```shell
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir
```

#### Progress

✅ **COMPLETE** - Custom parse/print for ModuleOp/ExtModuleHwOp (hw.module-style) and function-like operations (handshake.func-style). Cmt2FunctionLike interface supports two-region operations with FunctionOpInterface-compatible features.

### Inline Transform

#### Spec

We need a module inline transform. It comprises the following steps:
1. Identify which modules need to be inlined. There are cases that DON'T need inlining:
    1. top module in a circuit
    2. extern module
    3. module with attribute "synthesis" is true
    Any other modules need to be inlined
2. Inline modules from bottom up. In the instance graph, when an instance's module should be inlined, do the following:
    1. add subinstances to the parent module;
    2. inline parent module's call to the instance's methods/values (use CallInfo analysis).

You may refer to FIRRTL's inline as a lesson:
- `lib/Dialect/FIRRTL/Transforms/ModuleInliner.cpp`

Add a test that have modules (synthesis = true / false) and extern modules to test the inline transform. Run the following command to test:
```shell
# Test the module inliner pass
build/bin/circt-opt test/Dialect/Cmt2/inline.mlir -cmt2-inline-modules

# Or run with FileCheck
build/bin/circt-opt test/Dialect/Cmt2/inline.mlir -cmt2-inline-modules | build/bin/FileCheck test/Dialect/Cmt2/inline.mlir
```

#### Progress

✅ **COMPLETE** - Module inlining pass that identifies non-inlinable modules (top-level, external, synthesis=true), inlines modules bottom-up with hierarchical instance naming (e.g., `@leaf.storage`), and properly remaps SSA values and CallOp references.


### ConflictMatrix Analysis

#### Spec

We now need an analysis to infer relationships among rule/method/value of a module, as a conflict matrix, to prepare the "scheduling".

```markdown
MLIR Compiler will schedule as much as possibles rules in a single cycle. The
scheduling result is opaque to users, which protects user from dealing with 
complex control circuits.

Scheduler will analyse the included instances, based on the call map to those methods, scheduler can construct a conflict matrix among rule/action/value. There will be 3 relationships:
- `r0 <>  r1`, Conflict(C): `r0` and `r1` cannot be executed in a same cycle.
- `r0 / r1`, Conflict Free(CF): `r0` and `r1` can be executed in a same cycle 
  in any order.
- `r0 <  r1`, Sequential Before(SB): `r0` and `r1` can be executed in a same
  cycle, but `r0` should be executed before `r1`.
```

How to infer the conflict matrix? Given two function (either rule/method/value) `fx` and `fy`, first collect calls of each, denoted as `calls(fx)` and `calls(fy)`, from two regions (guard and body) of each. Each call can be denoted as `<instance>.<method>`. Then, there are some inference rules:
1. If there exists a call `i.m0` in `calls(fx)` and a call `i.m1` in `calls(fy)`, and `m0 <> m1` in `i`'s module, then `fx <> fy`. (Conflict)
2. If there exists a call `i.m0` in `calls(fx)` and a call `i.m1` in `calls(fy)`, and `m0 < m1` in `i`'s module, then `fx < fy`. (Sequential Before) 
3. If `fx < fy` and `fy < fx`, `fx <> fy`. (Merge)
4. If attributes specify `fx / fy` or there are no infered relationships between `fx` and `fy`, `fx / fy` (Conflict Free)

You should do this analysis with the help of existing InstanceGraph and CallInfo analysis. The analysis should be conducted in a topological order (from instance's modules to parent's module).

The starting point should be extmodules like `@reg` in `test/Dialect/Cmt2/gcd.mlir` with specified conflict matrix. You should test your analysis with the `gcd.mlir` case by the command:
```shell
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-print-conflict-matrix
```

#### Progress

✅ **COMPLETE** - ConflictMatrix analysis inferring relationships (Conflict, SequentialBefore, ConflictFree) between functions using four inference rules, parsing external module attributes, and bottom-up topological analysis.

### PrivateFunc Analysis and Transform


#### Spec

There is a special class of functions (method / value) called "private function". A private function is only called by functions in the same module by the `@this` instance. 

We need an analysis to identify private functions in every module.

Then, you need to create a transform to inline private functions.

You should use the `gcd.mlir` (`test/Dialect/Cmt2/gcd.mlir`) to test the analysis and transform. You can fill the shell command below to test:
```shell
# Test the private function inliner pass with gcd.mlir
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-inline-private-funcs
```

#### Progress

✅ **COMPLETE** - PrivateFunc analysis identifying functions only called via `@this`, and InlinePrivateFuncs transform pass that clones function bodies at call sites with proper SSA mapping.

### Interface Support

#### Spec

Currently, the analysis and transforms we have done only support direct instance calls. However, `cmt2` have support for the interface mechanism. You should read `include/circt/Dialect/Cmt2/Cmt2Ops.td` to understand how interface is defined and used, as exemplified in `test/Dialect/Cmt2/gcd.mlir`. 

In `cmt2` modules, a function can call the method/value of an interface (defined by the `InterfaceDefOp` operation) instead of a direct instance. So, we need to update our analysis and transforms to support interface calls. We need to update:
- CallInfo analysis: to include interface calls
- ConflictMatrix analysis: to include interface calls (this may affect the topological order, since interface comes from modules outside the current module)
- ModuleInliner transform: to inline interface calls
- More if you find necessary

You should use the `gcd.mlir` (`test/Dialect/Cmt2/gcd.mlir`) to test the analysis and transform. You can fill the shell command below to test:
```shell
# Test CallInfo analysis (should resolve interface calls to actual instances)
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-print-call-info

# Test ConflictMatrix analysis (should work with interface-resolved calls)
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-print-conflict-matrix

# Test ModuleInliner (should handle modules with interfaces)
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-inline-modules

# Test PrivateFuncInliner (should work with interface-using functions)
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-inline-private-funcs
```

`gcd.mlir`'s interface mechanism is too simple (cannot fully test the transforms, especially the ModuleInliner). You should create a more complex test case, which must have a module with functions and interfaces to be inlined. You can create `test/Dialect/Cmt2/interface-inline.mlir` for this purpose. You can fill the shell command below to test:
```shell
# Test CallInfo analysis with interface-inline.mlir (shows @reader calls before module inlining)
build/bin/circt-opt test/Dialect/Cmt2/interface-inline.mlir -cmt2-print-call-info

# Test ConflictMatrix analysis with interface-inline.mlir
build/bin/circt-opt test/Dialect/Cmt2/interface-inline.mlir -cmt2-print-conflict-matrix

# Test ModuleInliner with interface-inline.mlir (inlines @child into @parent, remaps interface calls)
build/bin/circt-opt test/Dialect/Cmt2/interface-inline.mlir -cmt2-inline-modules
``` 

#### Progress

✅ **COMPLETE** - Interface support (InterfaceOp/InterfaceDefOp/InterfaceDeclOp/interface_binds) with CallInfo resolving interface calls to actual instances, and ModuleInliner handling interface binding remapping during inlining.


### Attributes

#### Spec

In `include/circt/Dialect/Cmt2/Cmt2Ops.td`, there are some `explicit` attributes, which are not useful. Remove them.

The `Cmt2FunctionLike` should have optional attributes, including:
- `readyName`
- `enableName`

Note that, these attributes don't need to be arguments of the operations. However, the `Cmt2FunctionLike` should provide methods to set or get them.

#### Progress

✅ **COMPLETE** - Removed unused `explicit` attributes; added optional `readyName` and `enableName` attributes to Cmt2FunctionLike with getter/setter methods.

### Scheduler Analysis

#### Spec

Now we need to add a `Scheduler` analysis that work on `ModuleOp`.

It has the following steps:

- It collects the functions (rule/value/method) in the current module to be scheduled.
- Run ConflictMatrix analysis to get relationships between the functions.
- Get `precedence` attribute from `ModuleOp`, which should be in form
    ```
    [[@a, @b, @c], ...] // @a << @b << @c, where << means scheduled before
    ```
- Divide the functions into groups. Functions from different group are "Conflict Free". That is, rules with "Conflict" or "Sequence Before" relationships must be in one group. This needs a union-find data structure.
- For every group, solve an optimization problem:
    ```
    Given a list of functions [f1, ..., fn], find a permutation p: p[i] = {1, ..., n}, to minimize the number of cases: fi < fj and c[i] > c[j], and avoid any case: fi << fj and c[i] > c[j].

    This should be solved by a solver.
    ```
- Report the solution in the form:
    ```
    [
        [@a, @b], // a group
        [@c, @d], // another group
        ... // the remaining groups
    ]
    ```

Note that:
- Private functions should not be scheduled. The schedule must happen after a private function inlining. Should give warning and trigger the missing inlining automatically.
- The scheduler analysis also need to give a summary of "preventing firing": `c[i] > c[j] and (f[i] < f[j] or f[i] <> f[j])`.

You should test on the `test/Dialect/Cmt2/gcd.mlir` test with the command:
```shell
build/bin/circt-opt test/Dialect/Cmt2/gcd.mlir -cmt2-print-scheduler
```

#### Progress

✅ **COMPLETE** - Scheduler analysis with private function detection/warning, union-find grouping for conflict-free functions, precedence parsing, greedy topological sort optimization (never violate hard precedence constraints, minimize SequentialBefore violations), and preventing-firing violation reports.

### Migrate to FIRRTL

#### Spec

Currently, `cmt2` works with `hw`. However, lowering from `cmt2` to `hw` is hard, since `hw` is SSA, requiring elaborate positioning of instantiation and logic. Instead, `firrtl` is more easier to generate. So, we should use `cmt2` with `firrtl` and lower `cmt2` to `firrtl` modules.

You should update `include/circt/Dialect/Cmt2/Cmt2Ops.td` to replace `hw` things with `firrtl` things.
- For example, `ExtModuleHwOp` should be replaced by `ExtModuleFirrtlOp`

You should update any existing analysis and transforms to be compatible with `firrtl`.

You should update the tests under `test/Dialect/Cmt2` to use `firrtl` instead of `hw`

#### Progress

✅ **COMPLETE** - Migration from hw to firrtl dialect:
- ✅ Renamed `ExtModuleHwOp` to `ExtModuleFirrtlOp` with operation mnemonic `"module.extern.firrtl"`
- ✅ Replaced all `HWIntegerType` with `FIRRTLBaseType` in operation definitions (BindBareOp, InstanceOp, ReturnOp, CallOp)
- ✅ Updated operation descriptions to reference `firrtl.module` instead of `hw.module`
- ✅ Updated all C++ implementation files (Cmt2Ops.cpp, ModuleInliner.cpp, CallInfo.cpp, ConflictMatrix.cpp)
- ✅ Updated test files under `test/Dialect/Cmt2` to use FIRRTL types (`!firrtl.uint<32>` instead of `i32`, `!firrtl.uint<1>` instead of `i1`)
- ✅ Added FIRRTL type constraint definition in Cmt2Ops.td using C++ predicate
- ✅ Added FIRRTL type header include to Cmt2Ops.h
- ✅ Build succeeds with all 85 targets compiled successfully

**Type mapping used**:
- `i1` → `!firrtl.uint<1>`
- `i32` → `!firrtl.uint<32>`
- Clock signals remain as `!seq.clock` (not migrated to FIRRTL clock type)


### Cmt2ToFIRRTL Conversion

#### Spec

For each `cmt2` module, we need to convert it into a `firrtl.module`.

How to do?

For every module, run Scheduler analysis to get the schedule solution.

For a group in a schedule, generate logic for its included functions in order. For current function `fi`:
- Do a checking, fetch the called functions in `fi` as a sequence. If there are conflicts or "sequence before violations" (that is `fx < fy` but fy is called before fx). Raise an error messasge.
- Any @this call is not allowed. Emit error to suggest run `-cmt2-inline-private-funcs` before conversion.
- Construct the `guard` logic and the `body` logic. This should replace `cmt2.call` operations with signal assignments. Calling a method should assign `1` to `enable`. Argument signals and results should be connected, either.
- Generate a `ready` signal (whose name is either specified in the function's attributes, or default to `<funcName>_ready`). The `ready` signal's value is define by `AND` the following:
  - the guard result
  - the `ready` signals of called functions
  - `NOT` (any preceding functions with conflicts fired). This is determined by the ConflixMatrix. Two conflict cases: `Conflict` and `Sequence Before` violation.
- If `fi` is a method, also generate an `enable` signal (with the similar naming convention).
- Generate a `fire` signal. For value/rule, it's equal to the `ready` signal. For method, it's equal to the `ready AND enable`.
- Insert the `guard` and `body` logic to the module. The `body` logic should be guarded (`firrtl.when`) by the `fire` signal.
- The interface mechanism needs special processing: add ports on modules and do signal assignments.

You can look at:
- `lib/Conversion/CalyxToHW/CalyxToHW.cpp`
- and other conversions

to learn how to write a conversion.

During the conversion, you'd better create meaningful names for variables.

You should test on `test/Dialect/Cmt2/gcd.mlir` with the command:
```shell
# Test basic conversion
build/bin/circt-opt --lower-cmt2-to-firrtl test/Conversion/Cmt2ToFIRRTL/basic.mlir

# Test method and value conversion
build/bin/circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-firrtl test/Conversion/Cmt2ToFIRRTL/method-value.mlir

# Test gcd without interface
build/bin/circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-firrtl test/Dialect/gcd.mlir
```

We should also generate SystemVerilog from a `test/Dialect/gcd.mlir` design by the command:
```shell
# Generate SystemVerilog from Cmt2 design
build/bin/circt-opt test/Dialect/gcd.mlir -cmt2-inline-private-funcs --lower-cmt2-to-firrtl | build/bin/firtool --format=mlir

# Or save to a file
build/bin/circt-opt test/Dialect/gcd.mlir -cmt2-inline-private-funcs --lower-cmt2-to-firrtl | build/bin/firtool --format=mlir --disable-reg-randomization -o /tmp/gcd.sv
```

You should provide interface mechanism support. Look at `test/Dialect/Cmt2/hello.mlir` for an example. The module `@child` includes `cmt2.interface.decl @reader : @Reader`, which means the corresponding `firrtl.module` should have ports to call `@Reader`'s methods/values. When encountering a call to `@reader @getData`, the conversion should connect the ports (`enable` for method, arguments, results) properly. Also, the `@reader` also opens a `ready` signal, which should affect the `ready` and `fire` signal of the caller function. For the parent module `@hello`, it has
```
cmt2.interface.def @ReadX : @Read [
    [@x, @read, @getData]
]
```
Then, when 
```
cmt2.instance @c = @child(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with [
    [@ReadX, @reader]
]
```
bind @ReadX to @child @c's @reader, the conversion should connect the wires to the ports of `@child` properly. Note that we can pass a `cmt2.interface.decl` to deeper instances, and the ports should be connected properly.

The implementation should be robust and test on `test/Dialect/Cmt2/hello.mlir`:
```shell
# Test interface mechanism with hello.mlir
build/bin/circt-opt test/Dialect/Cmt2/hello.mlir --lower-cmt2-to-firrtl

# Generate SystemVerilog from hello.mlir to verify end-to-end
build/bin/circt-opt test/Dialect/Cmt2/hello.mlir --lower-cmt2-to-firrtl | build/bin/firtool --format=mlir --verilog
```

Note: `test/Dialect/Cmt2/hello.mlir` demonstrates both interface usage patterns:
1. **Interface definitions for child modules** (`@child` has `cmt2.interface.decl @reader` for inward interface ports)
2. **Interface declarations in top modules** (`@hello` has `cmt2.interface.decl @writer` for outward method call ports)


#### Progress

✅ **COMPLETE** - Cmt2ToFIRRTL conversion pass successfully converts Cmt2 to FIRRTL and generates valid SystemVerilog.

**Features:**
- Converts cmt2.circuit → firrtl.circuit with correct top module name
- Converts cmt2.module → firrtl.module with proper ports (enable, ready, args, results)
- Creates and initializes firrtl.instance for external modules with proper port connections
- Clones guard regions to compute ready conditions
- Generates ready signals based on guards, called functions, and conflict matrix
- Generates fire signals (ready for rules/values, ready AND enable for methods)
- Clones body regions inside firrtl.when blocks guarded by fire signals
- Handles SSA value remapping during region cloning with IRMapping
- Converts cmt2.call to FIRRTL signal accesses and connections
- Validates call sequences for sequential ordering violations
- Integrates with Scheduler, ConflictMatrix, and CallInfo analyses
- Properly connects module arguments (clock, reset) for both external and regular cmt2 modules
- Initializes all instance input ports to satisfy FIRRTL full initialization requirements
- Successfully generates valid, synthesizable SystemVerilog via firtool

**Interface Mechanism Support:**
- ✅ Creates interface ports on modules for InterfaceDeclOp operations (enable, ready, args, results)
- ✅ Detects and handles interface calls in convertCallOp()
- ✅ Connects interface calls to module ports in connectInterfaceCall()
- ✅ Connects child interface ports to parent instance ports based on InterfaceDefOp bindings
- ✅ Properly handles interface bindings during instance creation (interface_binds attribute)
- ✅ Includes interface call ready signals in generateReadySignal()
- ✅ Successfully tested with hello.mlir showing correct interface port generation and connections
- ✅ **Top module interface declarations**: Top-level modules can declare interfaces for outward method calls (creates output ports: `enable`, `data`; input ports: `ready`)
- ✅ Full pipeline working: Cmt2 with interfaces → FIRRTL → SystemVerilog


### Cycle Detection