# CMT2 Operations Reference

Complete reference for all CMT2 MLIR operations.

---

## Top-Level Operations

### cmt2.circuit

Top-level container for modules.

```mlir
cmt2.circuit {
    cmt2.module @MyModule(...) { ... }
    cmt2.module.extern.firrtl @ExtMod { ... }
}
```

### cmt2.module

CMT2 module definition.

```mlir
cmt2.module @Counter(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.instance @reg = @FIRRTLReg_32(%clk, %rst) : ...
    cmt2.rule @increment () -> () { ... }
    cmt2.value @read () -> (!firrtl.uint<32>) { ... }
}
```

### cmt2.module.extern.firrtl

External FIRRTL module binding.

```mlir
cmt2.module.extern.firrtl @Reg32 : @Reg32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.bind.bare %clk, @clk : !firrtl.clock
    cmt2.bind.bare %rst, @rst : !firrtl.uint<1>
    cmt2.bind.value @read : () -> !firrtl.uint<32>[...]
    cmt2.bind.method @write : (!firrtl.uint<32>) -> ()[...]
} {sequenceBefore = [["read", "write"]]}
```

---

## Function-Like Operations

### cmt2.rule

Rule with guard and body regions.

```mlir
cmt2.rule @increment () -> () {
    // Guard region - returns boolean
    %guard = firrtl.constant 1 : !firrtl.uint<1>
    cmt2.return %guard : !firrtl.uint<1>
} {
    // Body region - atomic actions
    %val = cmt2.call @reg @read() : () -> !firrtl.uint<32>
    %one = firrtl.constant 1 : !firrtl.uint<32>
    %new = firrtl.add %val, %one : ...
    cmt2.call @reg @write(%new) : (!firrtl.uint<32>) -> ()
    cmt2.return
}
```

### cmt2.method

Action method with ready-enable protocol.

```mlir
cmt2.method @store (%data: !firrtl.uint<32>) -> () {
    // Guard region
    %ready = ...
    cmt2.return %ready : !firrtl.uint<1>
} {
    // Body region
    cmt2.call @reg @write(%data) : (!firrtl.uint<32>) -> ()
    cmt2.return
}
```

**Attributes:**
- `static_latency`: Fixed latency in cycles
- `interval`: Initiation interval for pipelining

### cmt2.value

Read-only value method with ready-data protocol.

```mlir
cmt2.value @peek () -> (!firrtl.uint<32>) {
    // Guard region
    %ready = ...
    cmt2.return %ready : !firrtl.uint<1>
} {
    // Body region
    %data = cmt2.call @reg @read() : () -> !firrtl.uint<32>
    cmt2.return %data : !firrtl.uint<32>
}
```

---

## Instance Operations

### cmt2.instance

Module instantiation.

```mlir
cmt2.instance @counter = @FIRRTLReg_32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
```

### cmt2.call

Method or value invocation.

```mlir
// Value call
%val = cmt2.call @counter @read() : () -> !firrtl.uint<32>

// Method call
cmt2.call @counter @write(%new_val) : (!firrtl.uint<32>) -> ()

// With timing attributes
cmt2.call @mem @read(%addr) {
    arg_timing = [#cmt2.timing<[0, 1]>],
    result_timing = [#cmt2.timing<[2, 3]>]
} : (!firrtl.uint<8>) -> !firrtl.uint<32>
```

---

## Control Flow Operations

### cmt2.if

Conditional execution.

```mlir
%result = cmt2.if %cond -> !firrtl.uint<32> {
    cmt2.yield %then_val : !firrtl.uint<32>
} else {
    cmt2.yield %else_val : !firrtl.uint<32>
}
```

### cmt2.yield

Yields values from if regions.

```mlir
cmt2.yield %value : !firrtl.uint<32>
```

### cmt2.return

Returns from rules, methods, values.

```mlir
cmt2.return                         // No return value
cmt2.return %guard : !firrtl.uint<1>  // Guard return
cmt2.return %val : !firrtl.uint<32>   // Value return
```

---

## External Module Binding Operations

### cmt2.bind.bare

Bind signal to port (clock, reset).

```mlir
cmt2.bind.bare %clk, @clk : !firrtl.clock
cmt2.bind.bare %rst, @rst : !firrtl.uint<1>
```

### cmt2.bind.value

Bind value method to FIRRTL logic.

```mlir
cmt2.bind.value @read : () -> !firrtl.uint<32>[
    ready = "read_ready",
    arguments = [],
    results = ["read_data"]
] {static_latency = 2 : i64}
```

### cmt2.bind.method

Bind action method with ready-enable protocol.

```mlir
cmt2.bind.method @write static<4> : (!firrtl.uint<32>) -> ()[
    enable = "write_enable",
    ready = "write_ready",
    arguments = ["write_data"],
    results = []
] {interval = #cmt2.interval<2>}
```

---

## Procedural Layer Operations

### cmt2.proc.step

Dynamic step with go-done interface.

```mlir
cmt2.proc.step @wait_data {
    %ready = cmt2.call @fifo @has_data() : () -> !firrtl.uint<1>
    cmt2.proc.step_done %ready : !firrtl.uint<1>
    // Actions when enabled...
}
```

### cmt2.proc.static_step

Static step with fixed latency.

```mlir
cmt2.proc.static_step @compute<4> {
    // 4-cycle fixed latency
    %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
    %b = cmt2.call @reg_b @read() : () -> !firrtl.uint<32>
    %prod = firrtl.mul %a, %b : ...
    cmt2.call @result @write(%prod) : ...
} {interval = #cmt2.interval<2>}  // Optional pipelining
```

### cmt2.proc.step_done

Signal step completion.

```mlir
cmt2.proc.step_done %condition : !firrtl.uint<1>
```

### cmt2.proc.assign

Guarded assignment within procedural step.

```mlir
cmt2.proc.assign %target, %value when %guard : !firrtl.uint<32>
```

---

## Procedural Control Flow

### cmt2.proc.seq

Sequential composition.

```mlir
cmt2.proc.seq {
    cmt2.proc.enable @step_a
    cmt2.proc.enable @step_b
    cmt2.proc.enable @step_c
}
```

### cmt2.proc.par

Parallel composition.

```mlir
cmt2.proc.par {
    cmt2.proc.enable @step_a
    cmt2.proc.enable @step_b
}
```

### cmt2.proc.if

Conditional in procedural control.

```mlir
cmt2.proc.if %cond {
    cmt2.proc.enable @then_step
} else {
    cmt2.proc.enable @else_step
}
```

### cmt2.proc.while

Loop control.

```mlir
cmt2.proc.while %cond {
    cmt2.proc.enable @loop_body
}
```

### cmt2.proc.static_repeat

Fixed-iteration loop.

```mlir
cmt2.proc.static_repeat 4<16> {
    // 4 iterations, body latency 4 cycles each
    // Total: 16 cycles
    cmt2.proc.enable @loop_step
}
```

### cmt2.proc.static_if

Conditional with known latencies.

```mlir
cmt2.proc.static_if %cond<4, 4> {
    // then branch: 4 cycles
    cmt2.proc.enable @then_step
} else {
    // else branch: 4 cycles
    cmt2.proc.enable @else_step
}
```

### cmt2.proc.enable

Activate a step.

```mlir
cmt2.proc.enable @my_step
```

### cmt2.proc.invoke

Invoke a method within procedural control.

```mlir
cmt2.proc.invoke @instance @method(%arg) : (!firrtl.uint<32>) -> ()
```

### cmt2.proc.control_end

Terminate procedural control region.

```mlir
cmt2.proc.control_end
```

---

## Procedural Function Operations

### cmt2.proc.rule

Rule with procedural control.

```mlir
cmt2.proc.rule @compute () -> () {
    %guard = ...
    cmt2.return %guard : !firrtl.uint<1>
} control {
    cmt2.proc.seq {
        cmt2.proc.enable @step_a
        cmt2.proc.enable @step_b
    }
    cmt2.proc.control_end
}
```

### cmt2.proc.method

Multi-cycle method with procedural control.

```mlir
cmt2.proc.method @long_compute (%input: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
    %guard = ...
    cmt2.return %guard : !firrtl.uint<1>
} control {
    cmt2.proc.seq {
        cmt2.proc.enable @load
        cmt2.proc.enable @compute
        cmt2.proc.enable @store
    }
    cmt2.proc.control_end
}
```

---

## Interface Operations

### cmt2.interface

Container for methods.

```mlir
cmt2.interface @MyInterface {
    cmt2.method @foo(...) { ... }
    cmt2.value @bar(...) { ... }
}
```

### cmt2.interface.def

Define interface instance in module.

```mlir
cmt2.interface.def @my_iface : @MyInterface
```

### cmt2.interface.decl

Declare interface placeholder.

```mlir
cmt2.interface.decl @my_iface : @MyInterface
```

---

## Summary Table

| Category | Operations |
|----------|------------|
| **Top-Level** | circuit, module, module.extern.firrtl |
| **Functions** | rule, method, value |
| **Instances** | instance, call |
| **Control** | if, yield, return |
| **Binding** | bind.bare, bind.value, bind.method |
| **Proc Steps** | proc.step, proc.static_step, proc.step_done, proc.assign |
| **Proc Control** | proc.seq, proc.par, proc.if, proc.while, proc.static_repeat, proc.static_if, proc.enable, proc.invoke, proc.control_end |
| **Proc Functions** | proc.rule, proc.method |
| **Interfaces** | interface, interface.def, interface.decl |
