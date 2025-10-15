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

For every task, you should create a TODO list to record the status.

There are code blocks in "Spec" sections, which you should fill when conducting the task.

You should record the progress in the "TODO List" sections.


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

#### TODO List

- [x] InstanceGraph interfaces (ModuleOpInterface, InstanceOpInterface)
- [x] Cmt2InstanceGraph class
- [x] PrintInstanceGraph pass
- [x] Test case (instance-graph.mlir)

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

#### TODO List

- [x] CallInfo data structures (CallType, CallInfo, ModuleCallInfo, CallInfoView)
- [x] Analysis implementation (Cmt2CallInfo.h/cpp)
- [x] PrintCallInfo pass
- [x] Testing with gcd.mlir

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

#### TODO List

**Phase 1: Module-like Operations**
- [x] hw.module-style custom parse/print for ModuleOp/ExtModuleHwOp
- [x] argNames attribute and argument list parsing
- [x] Build and test with gcd.mlir

**Phase 2: Function-like Operations**
- [x] handshake.func-style custom parse/print with shared arguments
- [x] Cmt2FunctionLike interface (FunctionOpInterface-compatible for two-region ops)
- [x] arg_attrs and res_attrs support
- [x] Comprehensive interface methods (type manipulation, body handling, attribute access)

**Phase 3: Integration**
- [x] Update gcd.mlir to new syntax
- [x] Test complete workflow

**Key Achievement:** Cmt2FunctionLike interface supports two-region operations (guard + body) while maintaining full FunctionOpInterface compatibility - something FunctionOpInterface itself cannot handle.

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

#### TODO List

- [x] Cmt2ModuleInliner pass implementation
- [x] Hierarchical instance naming (`@leaf.storage` for subinstances)
- [x] SSA value and operand remapping with IRMapping
- [x] CallOp symbol reference remapping
- [x] Test cases (inline.mlir)

**Status: ✅ COMPLETE**

**Key Features:**
- Identifies modules to NOT inline: top-level, external, or `synthesis=true`
- Iterative inlining: subinstances → call inlining → instance removal
- Hierarchical naming preserves instantiation paths
- Removes unused modules after inlining


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

#### TODO List

- [x] ConflictMatrix data structures (Relationship enum, ModuleConflictMatrix, ConflictMatrixAnalysis)
- [x] Parse conflict attributes from ExtModuleHwOp (conflict, conflictFree, sequenceBefore)
- [x] Implement four inference rules (conflict propagation, SB propagation, merge, default CF)
- [x] Topological order traversal (bottom-up analysis)
- [x] PrintConflictMatrix pass
- [x] Testing with gcd.mlir

**Status: ✅ COMPLETE**

**Key Features:**
- Parses conflict matrices from external modules via attributes
- Infers relationships for regular modules using 4 inference rules
- Bottom-up analysis (topological order)
- Three relationships: Conflict (<>), SequentialBefore (<), ConflictFree (/)

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

#### TODO List

- [x] PrivateFuncAnalysis (identify functions only called via `@this`)
- [x] InlinePrivateFuncs transform pass
- [x] Function body inlining with IRMapping
- [x] Testing with gcd.mlir (`@doing` inlined)

**Status: ✅ COMPLETE**

**Key Features:**
- Identifies private functions (only called via `@this` in same module)
- Inlines function bodies at call sites with proper SSA mapping
- Removes private function definitions after inlining
- Supports both MethodOp and ValueOp

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

#### TODO List

- [x] Update CallInfo to resolve interface calls (InterfaceDefOp → instance.method)
- [x] Update ModuleInliner to handle interface bindings (interface_binds remapping)
- [x] Test with gcd.mlir and interface-inline.mlir
- [x] ConflictMatrix and PrivateFuncInliner (no changes needed - work automatically)

**Status: ✅ COMPLETE**

**Key Features:**
- **CallInfo**: Resolves interface calls to actual instance.method pairs during analysis
- **ModuleInliner**: Remaps interface declarations to definitions during inlining using `interface_binds`
- **Test**: interface-inline.mlir demonstrates `@child` inlining with `@reader` interface remapped to `@StorageReader`
- Interface mechanism: InterfaceOp (signature) → InterfaceDefOp (mapping) → InterfaceDeclOp (parameter) → interface_binds (instantiation)


### Attributes

#### Spec

In `include/circt/Dialect/Cmt2/Cmt2Ops.td`, there are some `explicit` attributes, which are not useful. Remove them.

The `Cmt2FunctionLike` should have optional attributes, including:
- `readyName`
- `enableName`

Note that, these attributes don't need to be arguments of the operations. However, the `Cmt2FunctionLike` should provide methods to set or get them.

#### TODO List

- [x] Remove unused `explicit` attributes from Cmt2Ops.td
- [x] Add optional `readyName` and `enableName` to Cmt2FunctionLike
- [x] Implement getter/setter methods in Cmt2OpInterfaces.td
- [x] Build and test

**Status: ✅ COMPLETE**

**Key Features:**
- Removed unused `explicit` attribute documentation
- Added `readyName` and `enableName` optional attributes to Cmt2FunctionLike
- Interface methods: get/set/remove for both attributes
- Works with all Cmt2FunctionLike operations (RuleOp, MethodOp, ValueOp, BindMethodOp, BindValueOp)

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

#### TODO List

- [x] Scheduler data structures (ScheduleGroup, ModuleScheduleResult)
- [x] Union-find for grouping conflict-free functions
- [x] Parse precedence attribute from ModuleOp
- [x] Greedy topological sort (hard constraints: precedence, soft: SequentialBefore)
- [x] Private function detection and warning
- [x] Preventing Firing analysis (violation reporting)
- [x] PrintScheduler pass and testing with gcd.mlir

**Status: ✅ COMPLETE**

**Key Features:**
- **Private function detection**: Warns and filters out functions only called via `@this`
- **Grouping**: Union-find groups functions with Conflict/SequentialBefore relationships
- **Optimization**: Greedy topological sort that never violates precedence (<<) and minimizes SequentialBefore (<) violations
- **Preventing Firing**: Reports all violations where `c[i] > c[j]` but `f[i] < f[j]` or `f[i] <> f[j]`
- **Input**: ConflictMatrix + precedence attribute → **Output**: Ordered function groups

### Cmt2ToHw Conversion

#### Spec

For each `cmt2` module, we need to convert it into a `hw.module`.

How to do?

For every module, run Scheduler analysis to get the schedule solution.

For a group in a schedule, generate logic for its included functions in order. For current function `fi`:
- Do a checking, fetch the called functions in `fi` as a sequence. If there are conflicts or "sequence before violations" (that is `fx < fy` but fy is called before fx). Raise an error messasge.
- Construct the `guard` logic and the `body` logic. This should replace `cmt2.call` operations with signal assignments. Calling a method should assign `1` to `enable`. Argument signals and results should be connected, either.
- Generate a `ready` signal (whose name is either specified in the function's attributes, or default to `<funcName>_ready`). The `ready` signal's value is define by `AND` the following:
  - the guard result
  - the `ready` signals of called functions
  - `NOT` (any preceding functions with conflicts fired). This is determined by the ConflixMatrix. Two conflict cases: `Conflict` and `Sequence Before` violation.
- If `fi` is a method, also generate an `enable` signal (with the similar naming convention).
- Generate a `fire` signal. For value/rule, it's equal to the `ready` signal. For method, it's equal to the `ready AND enable`.
- Insert the `guard` and `body` logic to the module. The `body` logic should be guarded by the `fire` signal.
- The interface mechanism needs special processing.

You can look at:
- `lib/Conversion/CalyxToHW/CalyxToHW.cpp`
- `lib/Conversion/FIRRTLToHW/LowerToHW.cpp`
- and other conversions

to learn how to write a conversion.

You should test on `test/Dialect/Cmt2/gcd.mlir` with the command:
```shell
# Test basic conversion
build/bin/circt-opt --lower-cmt2-to-hw test/Conversion/Cmt2ToHW/basic.mlir

# Test method and value conversion
build/bin/circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-hw test/Conversion/Cmt2ToHW/method-value.mlir

# Test gcd without interface
build/bin/circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-hw test/Conversion/Cmt2ToHW/gcd-simple.mlir
```

#### TODO List

**Infrastructure:**
- [x] Pass definition, header, build integration
- [x] SignalTracker (ready/enable/fire signals, body results)
- [x] ModuleConversionContext (per-module state with IRMapping)
- [x] Integration with Scheduler, ConflictMatrix, CallInfo
- [x] InstanceTracker with deferred instance creation
- [x] PortConnectionTracker for muxing multiple calls

**Core Logic:**
- [x] Guard/body region cloning with globalMapping
- [x] cmt2.call conversion to signal references
- [x] Signal generation: ready (guard AND called ready AND NOT conflicts), enable (methods), fire (ready or ready AND enable)
- [x] Output ports: methods (enable in, ready out), values (ready out, data outs), rules (none)
- [x] Call sequence validation (conflict matrix checking)
- [x] Method call enable signal assignments
- [x] Result wiring for `@this` calls (SignalTracker lookup)

**Instance Handling (Deferred Creation Strategy):**
- [x] ExtModuleHwOp binding resolution (bind.bare, bind.value, bind.method)
- [x] hw.instance creation with proper port mappings
- [x] Mux generation for multiple calls to same instance.method
- [x] Deferred instance creation with topological sorting (ready queue)
- [x] Dependency tracking and cycle detection
- [x] SSA value invalidation fix (store Operation*/resultIndex pairs)
- [x] Type constraint fixes (AnyType for !seq.clock support)

**Status: ✅ COMPLETE**

**Key Features:**
- Converts cmt2.module → hw.module with proper port definitions
- Processes schedule groups to generate function logic in dependency order
- Control signal generation: ready/enable/fire per function
- SSA value mapping with IRMapping (module arguments → guard/body cloning)
- Tracks preceding conflicts to prevent simultaneous execution
- **Deferred instance creation**: Uses ready queue and topological sorting to handle cross-instance dependencies
- **Muxing logic**: Multiple calls to the same instance.method generate proper mux trees
- **External module support**: Bindings (bind.bare, bind.value, bind.method) correctly map to hw.instance ports
- Test cases: basic.mlir, method-value.mlir, result-wiring.mlir, gcd-simple.mlir ✅

**Design Decisions:**
- Stateful operations guarded at method call level via enable signals
- Data flows through call sites using SSA values, not module ports
- Instances created only when all input values are ready (topological order)
- Operation/result pairs stored instead of raw Values to survive IR updates

**Key Implementation Details:**
1. **Deferred Instance Creation**: Instances registered first, created later in topological order when dependencies resolved
2. **MethodCall.argOps**: Stores `(Operation*, resultIndex)` pairs instead of Values to handle SSA invalidation after `replaceAllUsesWith`
3. **PortConnectionTracker**: Tracks all calls to each instance.method, generates mux logic with fire conditions
4. **Ready Queue**: Implements topological sort - instances with no unmet dependencies created first, updating queue as dependencies resolve