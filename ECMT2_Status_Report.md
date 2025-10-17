# ECMT2 Embedded DSL - Status Report

**Date:** 2025-10-18
**Task:** Compare FIRRTL output from hello_example.cpp (ECMT2) vs hello.mlir (hand-written)

---

## Executive Summary

✅ **ECMT2 Successfully Demonstrates Core Concepts:**
- Zero-serialization C++ API that directly constructs MLIR operations
- Complete interface mechanism (circuit-level definitions, module-level declarations/definitions)
- Hierarchical module composition with interface bindings
- Fluent API design with method chaining
- Proper FIRRTL type integration

❌ **Critical Issues Preventing Production Use:**
1. Clock/Reset modeled as constants instead of module arguments
2. Missing BindBareOp for external module clock/reset binding
3. Precedence enforcement not working in conversion output (conversion bug)

---

## Detailed Findings

### 1. What Works Perfectly ✅

#### Interface Mechanism
The ECMT2 interface implementation is **fully functional** and generates correct Cmt2 MLIR:

```cpp
// Circuit-level interface definition
auto *readerInterface = circuit.addInterface("Reader");
readerInterface->addValue("getData", {},
  {mlir::TypeAttr::get(circt::firrtl::UIntType::get(&context, 32))});

// Module-level interface declaration (inward)
auto *childReaderDecl = childMod->defineInterface("reader", "Reader");

// Module-level interface definition (outward binding)
auto *readXDef = helloMod->defineInterfaceDef("ReadX", "Reader");
readXDef->bind("x", "read", "getData");
readXDef->finalize();

// Instance with interface binding
auto *childInst = helloMod->addInstance("c", childMod,
    {helloClk.getValue(), helloRst.getValue()},
    {{"ReadX", "reader"}});
```

**Generates:**
```mlir
cmt2.interface @Reader {
  cmt2.value @getData () -> (!firrtl.uint<32>) { ... }
}

cmt2.module @child {
  cmt2.interface.decl @reader : @Reader
  ...
}

cmt2.module @hello {
  cmt2.interface.def @ReadX : @Reader [[@x, @read, @getData]]
  cmt2.instance @c = @child(...) with [[@ReadX, @reader]]
  ...
}
```

**Converts to FIRRTL with correct ports:**
```mlir
firrtl.module @child(
  in %reader_getData_ready: !firrtl.uint<1>,
  in %reader_getData_res0: !firrtl.uint<32>, ...)
```

✅ Interface calls are properly resolved and connected in FIRRTL.

#### Module Hierarchy and Instance Management
```cpp
auto *childMod = circuit.addModule("child");
auto *childReg = childMod->addInstance("r", regMod, {clk, rst});

// Method calls work correctly
auto results = childReg->callValue("read", builder);
```

✅ Instance creation, method calls, and value access all work correctly.

#### Function-Like Operations
```cpp
auto *setMethod = childMod->addMethod("set",
    {{"v", firrtl::UIntType::get(&context, 32)}},
    {firrtl::UIntType::get(&context, 32)});

setMethod->guard([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
  // Guard logic
});

setMethod->body([&](mlir::OpBuilder &builder, llvm::ArrayRef<mlir::BlockArgument> args) {
  // Body logic with args[0] for parameter access
});

setMethod->finalize();
```

✅ Method, Value, and Rule implementations work correctly with lambda-based guard/body builders.

#### Signal Operations
```cpp
Signal readerSig(readerData[0], &builder, childMod->getLoc());
Signal sum1 = readerSig + vSignal;  // Creates firrtl.add
Signal sum1_trunc = sum1.bits(31, 0);  // Creates firrtl.bits
```

✅ Operator overloading creates correct FIRRTL operations.

---

### 2. Critical Issues ❌

#### Issue 1: Clock/Reset as Constants (HIGH PRIORITY)

**Problem:**
```cpp
// Current API creates constants
Clock childClk(childMod->getBuilder(), childMod->getLoc());
// Generates: %0 = firrtl.constant 0 : !firrtl.uint<1>
//           %clk = firrtl.asClock %0
```

**Expected (hello.mlir):**
```mlir
cmt2.module @child(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
  cmt2.instance @r = @reg(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
}
```

**Actual (ECMT2):**
```mlir
cmt2.module @child {
  %c0 = firrtl.constant 0 : !firrtl.uint<1>
  %clk = firrtl.asClock %c0 : (!firrtl.uint<1>) -> !firrtl.clock
  cmt2.instance @r = @reg(%clk, %c0) : !firrtl.clock, !firrtl.uint<1>
} attributes {has_clock_reset = true}
```

**Impact:**
- Generated FIRRTL modules lack clock/reset ports
- Clock signal is always constant 0 (non-functional)
- Cannot connect clock from top-level down through hierarchy

**Root Cause:**
1. `Clock(builder, loc)` constructor creates constant instead of using BlockArgument
2. `Module::setClockReset()` only sets attribute, doesn't add module arguments
3. API doesn't model clock/reset as "passed down" from parent

**Required Fix:**
Redesign API to make clock/reset explicit module parameters:

```cpp
// Option 1: Constructor with arguments
auto *childMod = circuit.addModule("child",
    {{"clk", firrtl::ClockType::get(ctx)},
     {"rst", firrtl::UIntType::get(ctx, 1)}});

// Option 2: Explicit argument addition
auto *childMod = circuit.addModule("child");
auto clkArg = childMod->addArgument("clk", firrtl::ClockType::get(ctx));
auto rstArg = childMod->addArgument("rst", firrtl::UIntType::get(ctx, 1));

// Then pass to instances
childMod->addInstance("r", regMod, {clkArg, rstArg});
```

---

#### Issue 2: Missing BindBareOp for External Modules

**Problem:**
```cpp
regMod->bindClock("clock")
      .bindReset("reset")
```
Only sets attributes, doesn't create `cmt2.bind.bare` operations.

**Expected (hello.mlir):**
```mlir
cmt2.module.extern.firrtl @reg : @Reg32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
  cmt2.bind.bare %clk, @clock : !firrtl.clock
  cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
  cmt2.bind.value @read : ...
  cmt2.bind.method @write : ...
}
```

**Actual (ECMT2):**
```mlir
cmt2.module.extern.firrtl @reg : @FIRRTLReg {
  cmt2.bind.value @read : ...
  cmt2.bind.method @write : ...
} attributes {clock_port = "clock", reset_port = "reset"}
```

**Impact:**
- Conversion pass cannot determine how to connect clock/reset to external module instances
- External modules receive incorrect clock connections

**Required Fix:**
Modify `ExternalModule::bindClock()` and `bindReset()` to:
1. Add clock/reset as ExtModuleFirrtlOp arguments (block arguments)
2. Create `cmt2.bind.bare` operations in the module body
3. Update fluent API to return Clock/Reset wrappers around BlockArgument

---

#### Issue 3: Precedence Not Enforced in FIRRTL Output

**Problem:**
Both hello.mlir and hello_example.cpp have:
```mlir
cmt2.module @hello(...) {
  cmt2.method @write ...
  cmt2.rule @incr ...
} attributes {precedence = [[@write, @incr]]}
```

But only hello.mlir generates correct FIRRTL with precedence enforcement:
```mlir
// hello.mlir FIRRTL (CORRECT)
%write_fire = ...
%incr_ready = ...
%not_write_fire = firrtl.xor %write_fire, %c1
%incr_fire = firrtl.and %incr_ready, %not_write_fire  // Prevents firing when write fires

// hello_example.cpp FIRRTL (INCORRECT)
%write_fire = ...
%incr_fire = ...  // No check for write_fire
```

**Impact:**
`@incr` rule can fire simultaneously with `@write` method, violating the precedence constraint.

**Root Cause:**
This appears to be a **bug in the Cmt2ToFIRRTL conversion pass** that manifests differently depending on module structure. Possibly related to:
- Missing clock/reset arguments affecting scheduling analysis
- Differences in how ConflictMatrix or Scheduler analyze the ECMT2-generated modules
- Need to investigate `LowerCmt2ToFIRRTL.cpp` to understand why precedence is ignored

**Investigation Needed:**
1. Check if Scheduler analysis behaves differently without clock/reset arguments
2. Verify that ConflictMatrix correctly reads precedence attribute
3. Test with a simpler case (two rules with precedence, no interfaces)

---

## Comparison Summary Table

| Feature | hello.mlir | hello_example.cpp | Status |
|---------|------------|-------------------|--------|
| Interface definitions | ✅ Working | ✅ Working | Match |
| Interface declarations | ✅ Working | ✅ Working | Match |
| Interface definitions | ✅ Working | ✅ Working | Match |
| Interface port generation | ✅ Correct | ✅ Correct | Match |
| Module hierarchy | ✅ Working | ✅ Working | Match |
| Instance creation | ✅ Working | ✅ Working | Match |
| Method/Value/Rule logic | ✅ Working | ✅ Working | Match |
| FIRRTL arithmetic ops | ✅ Correct | ✅ Correct | Match |
| Ready signal generation | ✅ Correct | ✅ Correct | Match |
| Fire signal generation | ✅ Correct | ✅ Correct | Match |
| **Clock/Reset as arguments** | ✅ Yes | ❌ Constants | **BROKEN** |
| **BindBareOp for clk/rst** | ✅ Yes | ❌ Missing | **BROKEN** |
| **Precedence enforcement** | ✅ Working | ❌ Not enforced | **BUG** |

---

## Testing Evidence

### ECMT2 Generated Cmt2 MLIR:
```mlir
cmt2.module @hello {
  %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
  %0 = firrtl.asClock %c0_ui1 : (!firrtl.uint<1>) -> !firrtl.clock
  ...
} attributes {precedence = [[@write, @incr]]}
```

### Hand-Written Cmt2 MLIR:
```mlir
cmt2.module @hello(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
  cmt2.instance @x = @reg(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
  ...
} attributes {precedence = [[@write, @incr]]}
```

### FIRRTL Output Comparison:
**hello.mlir → FIRRTL:**
```mlir
firrtl.module @hello(in %clk: !firrtl.clock, in %rst: !firrtl.uint<1>, ...) {
  %x_... = firrtl.instance x @Reg32(...)
  firrtl.connect %x_clock, %clk : !firrtl.clock
  ...
  %not_write = firrtl.xor %write_fire, %c1 : ...  // Precedence check
  %incr_fire = firrtl.and %incr_ready, %not_write : ...
}
```

**hello_example.cpp → FIRRTL:**
```mlir
firrtl.module @hello(in %write_enable: !firrtl.uint<1>, ...) {
  // No clock/reset ports
  %x_... = firrtl.instance x @FIRRTLReg(...)
  // No precedence check
  %incr_fire = firrtl.and %incr_ready, %c1 : ...
}
```

---

## Recommendations

### Short-term (Block Production Use)
1. **Document the clock/reset limitation** in examples and API docs
2. **Add validation** to detect and warn when clock/reset are used incorrectly
3. **Fix precedence bug** in Cmt2ToFIRRTL conversion pass

### Medium-term (Required for Production)
1. **Redesign Clock/Reset API**:
   - Make module arguments explicit
   - Remove Clock/Reset constructors that create constants
   - Add builder methods like `Module::addClockArgument()`
2. **Implement BindBareOp generation**:
   - Update `ExternalModule::bindClock()/bindReset()`
   - Add clock/reset to ExtModuleFirrtlOp signature
3. **Add comprehensive tests**:
   - Test clock/reset propagation through hierarchy
   - Test precedence enforcement with various configurations
   - Test interface mechanism edge cases

### Long-term (Nice to Have)
1. **Higher-level API layer** for common patterns
2. **Better error messages** with source location tracking
3. **Debug utilities** for visualizing generated MLIR
4. **Documentation website** with interactive examples

---

## Conclusion

The ECMT2 embedded DSL successfully demonstrates the **zero-serialization architecture** and **interface mechanism**, which are the core innovations. The implementation validates that:

✅ C++ code can directly construct MLIR operations without text generation
✅ Interface-based modular composition works correctly
✅ Fluent API design provides good developer experience
✅ FIRRTL type integration is seamless

However, **critical issues with clock/reset handling** prevent production use. These are architectural issues requiring API redesign, not simple bug fixes. The precedence enforcement bug also needs investigation.

**Estimated Effort:**
- Clock/Reset API redesign: 2-3 days
- BindBareOp implementation: 1 day
- Precedence bug fix: 1-2 days
- Testing and validation: 1-2 days

**Total:** ~1 week to make ECMT2 production-ready

**Status:** ✅ Proof of concept successful, ❌ Not ready for production use
