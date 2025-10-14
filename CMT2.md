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

- [x] Add InstanceGraph interfaces to Cmt2 operations (ModuleOp, ExtModuleHwOp, InstanceOp)
- [x] Create Cmt2InstanceGraph class and implementation
- [x] Add PrintInstanceGraph pass for testing
- [x] Create hierarchical test case and verify functionality

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

- [x] Design CallInfo data structure (CallType enum, CallInfo struct, ModuleCallInfo, CallInfoView)
- [x] Create Cmt2CallInfo.h and Cmt2CallInfo.cpp with analysis implementation
- [x] Add PrintCallInfo pass to print call information
- [x] Test with gcd.mlir and verify output

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

**Phase 1: Module-like Operations (ModuleOp, ExtModuleHwOp)**
- [x] Study hw.module parse/print implementation in HWOps.cpp
- [x] Add argNames attribute to ModuleOp/ExtModuleHwOp
- [x] Remove assemblyFormat, add hasCustomAssemblyFormat = 1
- [x] Implement custom parse() for ModuleOp (parse port list, attributes, body)
- [x] Implement custom print() for ModuleOp (print in hw.module style)
- [x] Implement custom parse() for ExtModuleHwOp
- [x] Implement custom print() for ExtModuleHwOp
- [x] Add getAsmBlockArgumentNames() for proper naming
- [x] Fix parseArgumentList() for comma-separated argument parsing
- [x] Support trailing attribute dict (without 'attributes' keyword)
- [x] Build successful
- [x] Test ModuleOp/ExtModuleHwOp parsing with updated gcd.mlir

**Phase 2: Function-like Operations (RuleOp, MethodOp, ValueOp, BindMethodOp, BindValueOp)**
- [x] Study handshake.func parse/print implementation
- [x] Add function_type attribute to RuleOp (MethodOp and ValueOp already had it)
- [x] Verify BindMethodOp and BindValueOp use assemblyFormat effectively
- [x] Update gcd.mlir to include function_type for all RuleOp instances
- [x] Test function-like operations parsing
- [x] Implement custom parse/print with shared argument lists for two-region operations
- [x] Add Cmt2FunctionLike interface with FunctionOpInterface-compatible features
- [x] Add arg_attrs and res_attrs attributes to all function-like operations
- [x] Implement comprehensive interface methods for type manipulation, body handling, and attribute access

Note: Function-like operations (RuleOp, MethodOp, ValueOp) now support handshake.func-style syntax with shared arguments for both guard and body regions: `cmt2.method @start(%a: i32, %b: i32) -> () { guard } { body }`. The Cmt2FunctionLike interface provides full FunctionOpInterface-compatible features while supporting two-region operations, which FunctionOpInterface cannot handle.

**Phase 3: Integration and Testing**
- [x] Update gcd.mlir to use new syntax (ModuleOp, ExtModuleHwOp, RuleOp)
- [x] Test complete file with circt-opt
- [x] Verify all operations work correctly

Phase 1, 2, and 3 are complete! The "Better Parse/Print Support" task has been successfully implemented with:
- Custom parse/print for ModuleOp and ExtModuleHwOp (hw.module-style with argument lists)
- Custom parse/print for function-like operations with handshake.func-style shared argument syntax
- Cmt2FunctionLike interface providing comprehensive FunctionOpInterface-compatible features:
  - Symbol name handling, function kind identification
  - Type queries and manipulation methods
  - Body/region handling (isExternal, getFunctionBody)
  - Argument/result counts and attribute access
  - Support for both two-region operations (RuleOp, MethodOp, ValueOp) and no-region operations (BindMethodOp, BindValueOp)
- arg_attrs and res_attrs attributes for all function-like operations
- Consistent syntax across all operations
- Successful parsing and printing with gcd.mlir test case

Key achievement: Unlike FunctionOpInterface which requires single-region operations, Cmt2FunctionLike supports two-region operations (guard and body) while maintaining full compatibility with FunctionOpInterface patterns.

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

- [x] Study FIRRTL ModuleInliner implementation
- [x] Design simplified Cmt2 module inlining strategy
- [x] Implement Cmt2ModuleInliner pass (lib/Dialect/Cmt2/Transforms/ModuleInliner.cpp)
- [x] Create test case with synthesis attribute (test/Dialect/Cmt2/inline.mlir, inline-simple.mlir)
- [x] Debug and fix SSA value use-def issues in the inline transform
- [x] Fix operand remapping when cloning instances
- [x] Fix symbol reference remapping in cloned CallOps
- [x] Test and verify successful inlining
- [x] Subinstances' name resolution: When inlining a module `Leaf`, which is instantiated as `@leaf` in module `Parent`, its instance `@x` is cloned to `Parent` with the updated instance name `@leaf.x`. The hierarchical name uses `.` as a separator and is implemented as a StringAttr, preserving the hierarchy path for debugging and analysis purposes.

**Status: ✅ COMPLETE**

The module inliner pass has been successfully implemented and tested with the following features:

**Features:**
- Identifies modules that should NOT be inlined: top-level modules (modules with no uses), external modules (ExtModuleHwOp), and modules with `synthesis=true` attribute
- Inlines modules iteratively until no more inlining is possible by:
  1. Mapping module block arguments to instance operands (crucial for correct SSA value mapping)
  2. Cloning subinstances from target module to parent with proper operand mapping and **hierarchical naming**
  3. Tracking instance name mappings for updating CallOp references
  4. Inlining calls to the instance's methods/values by cloning method/value bodies
  5. Remapping CallOp callee references to point to cloned instances with hierarchical names
  6. Removing the inlined instance
- Removes unused modules after all inlining is complete
- **Hierarchical Instance Naming**: When inlining module `@Leaf` instantiated as `@leaf`, its subinstance `@storage` is renamed to `@leaf.storage`, preserving the full instantiation path for clarity and avoiding name conflicts

**Key Implementation Details:**
- Uses `IRMapping` to map SSA values during cloning
- Uses `DenseMap<StringAttr, StringAttr>` to map instance names for CallOp remapping
- Properly handles module block arguments by mapping them to instance operands before cloning
- Updates CallOp callee symbols to reference cloned instances

**Test Results:**
Both test cases pass successfully:
- `test/Dialect/Cmt2/inline.mlir` - Multi-level hierarchical inlining

**Files Modified:**
- `include/circt/Dialect/Cmt2/Cmt2Passes.td` - Added ModuleInliner pass definition
- `lib/Dialect/Cmt2/Transforms/ModuleInliner.cpp` - Main implementation (~320 lines)
- `lib/Dialect/Cmt2/Transforms/CMakeLists.txt` - Added ModuleInliner.cpp to build
- `test/Dialect/Cmt2/inline.mlir` - Test case with multi-level module hierarchy


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

- [x] Design ConflictMatrix data structure (Relationship enum, ModuleConflictMatrix, ConflictMatrixAnalysis)
- [x] Create Cmt2ConflictMatrix.h and Cmt2ConflictMatrix.cpp with analysis implementation
- [x] Parse conflict matrix attributes from ExtModuleHwOp (conflict, conflictFree, sequenceBefore)
- [x] Implement topological order traversal for bottom-up analysis
- [x] Implement inference rules for conflict relationships
- [x] Add PrintConflictMatrix pass to print conflict matrices
- [x] Test with gcd.mlir and verify conflict matrix output

**Status: ✅ COMPLETE**

The conflict matrix analysis has been successfully implemented and tested with the following features:

**Features:**
- Parses conflict matrices from external modules via attributes:
  - `conflict`: [[@f1, @f2], ...] - functions that cannot execute in the same cycle
  - `conflictFree`: [[@f1, @f2], ...] - functions that can execute in any order
  - `sequenceBefore`: [[@f1, @f2], ...] - f1 must execute before f2
- Infers conflict matrices for regular modules using four inference rules:
  1. If `i.m0 <> i.m1` in instance's module, then `fx <> fy` (Conflict propagation)
  2. If `i.m0 < i.m1` in instance's module, then `fx < fy` (Sequential Before propagation)
  3. If `fx < fy` AND `fy < fx`, then `fx <> fy` (Merge to Conflict)
  4. Default to ConflictFree if no relationships inferred
- Analyzes modules in topological order (bottom-up) to ensure submodules are analyzed first
- Collects calls from both guard and body regions of functions
- Prints conflict matrices in human-readable format grouped by relationship type

**Key Implementation Details:**
- Uses `ModuleConflictMatrix` to store relationships for each module
- Uses normalized function pairs (sorted order) for consistent lookup
- Implements topological sort using dependency tracking
- Handles external modules (ExtModuleHwOp) and regular modules (ModuleOp)

**Test Results:**
Successfully tested with `test/Dialect/Cmt2/gcd.mlir`:
- External module `@reg`: correctly parsed conflict, sequenceBefore, and conflictFree relationships
- Module `@gcd`: correctly inferred conflicts between rules/methods/values based on their calls to instance methods

Example inferred relationships:
- `@start <> @swap`: both call `@write` on instances, and `@write <> @write` in `@reg`
- `@result / @sub`: conflict-free because they call different methods/instances
- `@doing / @result`: conflict-free because both only call `@read`, which is conflict-free with itself

**Files Created:**
- `include/circt/Dialect/Cmt2/Cmt2ConflictMatrix.h` - Header file with data structures
- `lib/Dialect/Cmt2/Cmt2ConflictMatrix.cpp` - Main analysis implementation (~400 lines)
- `lib/Dialect/Cmt2/Transforms/PrintConflictMatrix.cpp` - Print pass implementation

**Files Modified:**
- `include/circt/Dialect/Cmt2/Cmt2Passes.td` - Added PrintConflictMatrix pass definition
- `lib/Dialect/Cmt2/CMakeLists.txt` - Added Cmt2ConflictMatrix.cpp to build
- `lib/Dialect/Cmt2/Transforms/CMakeLists.txt` - Added PrintConflictMatrix.cpp to build

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

- [x] Design PrivateFuncAnalysis data structure
- [x] Create PrivateFuncAnalysis.h and PrivateFuncAnalysis.cpp with analysis implementation
- [x] Implement private function identification using CallInfo
- [x] Add InlinePrivateFuncs pass definition to Cmt2Passes.td
- [x] Create InlinePrivateFuncs transform pass implementation
- [x] Update CMakeLists.txt files
- [x] Test with gcd.mlir and verify private function inlining

**Status: ✅ COMPLETE**

The private function analysis and transform has been successfully implemented and tested with the following features:

**Features:**
- Identifies private functions in each module:
  - A private function is a method or value that is ONLY called via `@this` instance
  - All calls must be from functions in the same module
  - If a function is called from outside the module or from a different instance, it's not private
- Inlines private functions by:
  1. Identifying all private functions using the PrivateFuncAnalysis
  2. For each private function, finding all call sites (CallOps with `@this` callee)
  3. Cloning the function body at each call site with proper SSA value mapping
  4. Replacing the call results with the inlined results
  5. Removing the private function definition after all calls are inlined

**Key Implementation Details:**
- Uses `PrivateFuncAnalysis` class to identify private functions
- Analyzes CallInfo to determine which functions are called and from where
- Supports both MethodOp and ValueOp private functions
- Uses `IRMapping` to map function arguments to call inputs during inlining
- Properly handles SSA values and maintains correctness

**Test Results:**
Successfully tested with `test/Dialect/Cmt2/gcd.mlir`:
- Function `@doing` was correctly identified as a private function (only called via `@this`)
- All calls to `@doing` in rules `@swap`, `@sub`, and methods `@start`, `@result` were inlined
- The inlined code correctly reads from `@y @read` and checks if it's not equal to 0
- The `@doing` function definition was removed after inlining

Example transformation:
```mlir
// Before (original @doing function):
cmt2.value @doing() -> (i1) {} {
  %y = cmt2.call @y @read () : () -> (i32)
  %0 = hw.constant 0: i32
  %1 = comb.icmp ne %y, %0 : i32
  cmt2.return %1 : i1
}

// Before (call site in @swap rule):
%3 = cmt2.call @this @doing () : () -> (i1)

// After (inlined at call site):
%3 = cmt2.call @y @read() : () -> i32
%c0_i32 = hw.constant 0 : i32
%4 = comb.icmp ne %3, %c0_i32 : i32
// %4 is used instead of %3
```

**Files Created:**
- `include/circt/Dialect/Cmt2/Transforms/PrivateFuncAnalysis.h` - Header file for private function analysis
- `lib/Dialect/Cmt2/Transforms/PrivateFuncAnalysis.cpp` - Analysis implementation (~150 lines)
- `lib/Dialect/Cmt2/Transforms/InlinePrivateFuncs.cpp` - Transform pass implementation (~160 lines)

**Files Modified:**
- `include/circt/Dialect/Cmt2/Cmt2Passes.td` - Added InlinePrivateFuncs pass definition
- `lib/Dialect/Cmt2/Transforms/CMakeLists.txt` - Added PrivateFuncAnalysis.cpp and InlinePrivateFuncs.cpp to build