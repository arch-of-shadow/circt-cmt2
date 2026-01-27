# PyCMT2 API Reference

**Auto-generated from docstrings**

---

## Table of Contents

- [Circuit](#circuit)
- [Module Builder](#module-builder)
- [Types](#types)
- [STL Components](#stl-components)
- [Dataflow Builders](#dataflow-builders)
- [Pipeline Builders](#pipeline-builders)
- [Timing](#timing)
- [Simulation](#simulation)
- [Testbench](#testbench)
- [External Module](#external-module)
- [Diagnostics](#diagnostics)

---

## Circuit

Circuit builder for PyCMT2 EDSL.

### `Circuit`

Top-level circuit container.

Circuit is the entry point for building CMT2 designs. It manages
modules, external modules, and interfaces.

Example:
    circuit = Circuit("MyDesign")

    with circuit.module("Counter") as mod:
        clk = mod.clock()
        rst = mod.reset()
        # ... define rules, methods, etc.

    print(circuit.emit_mlir())

#### Methods

**`__enter__(self)`**

> Enter context manager.


**`__exit__(self, exc_type, exc_val, exc_tb)`**

> Exit context manager.


**`__init__(self, name: 'str | None' = None)`**

> Create a new circuit.


**`debug_pipeline(self, output_dir: 'str', passes: 'list[str] | None' = None, stop_on_error: 'bool' = True, debug_ports: 'bool' = False) -> 'dict[str, str]'`**

> Run passes and dump IR after each pass for debugging.


**`emit_firrtl(self) -> 'str'`**

> Run CMT2-to-FIRRTL conversion and emit FIRRTL MLIR.


**`emit_mlir(self) -> 'str'`**

> Emit the circuit as MLIR text.


**`emit_verilog(self, output_dir: 'str | None' = None, debug_ports: 'bool' = False) -> 'str'`**

> Run full compilation pipeline and emit Verilog.


**`external_module(self, name: 'str') -> "Iterator['ExternalModuleBuilder']"`**

> Create an external module binding.


**`include_library_module(self, library_name: 'str', params: 'dict[str, int] | None' = None) -> 'str | None'`**

> Include a FIRRTL module from the ModuleLibrary.


**`interface(self, name: 'str | None' = None) -> 'Iterator[object]'`**

> Define an interface.


**`interpreter(self, output: "'Callable[[str], None] | None'" = None) -> "'Interpreter'"`**

> Create a Python interpreter for this circuit.


**`module(self, name: 'str | None' = None) -> 'Iterator[ModuleBuilder]'`**

> Create a module within this circuit.


**`to_verilog(self, debug_ports: 'bool' = False) -> 'str'`**

> Run the compilation pipeline and emit Verilog.


### `Context`

MLIR context wrapper for PyCMT2.

#### Methods

**`__init__(self)`**


---

## Module Builder

Module builder for PyCMT2 EDSL.

### `ModuleBuilder`

Builder for CMT2 modules.

ModuleBuilder provides methods for defining ports, instances, rules,
methods, values, and procedural constructs within a module.

Example:
    with circuit.module("Counter") as mod:
        clk = mod.clock()
        rst = mod.reset()

        count_reg = mod.instance(Reg(32, init=0))

        with mod.rule("increment") as rule:
            with rule.guard() as g:
                g.always()
            with rule.body() as b:
                val = b.call(count_reg, count_reg.read)
                b.call(count_reg, count_reg.write, val + b.const(1, 32))

#### Methods

**`__init__(self, circuit: 'Circuit', name: 'str | None' = None)`**


**`clock(self, name: 'str' = 'clk') -> 'Signal[ClockType]'`**

> Add a clock input port.


**`conflict(self, a: 'MethodRef | ValueRef', b: 'MethodRef | ValueRef') -> 'ModuleBuilder'`**

> Declare that 'a' and 'b' conflict.


**`conflict_free(self, a: 'MethodRef | ValueRef', b: 'MethodRef | ValueRef') -> 'ModuleBuilder'`**

> Declare that 'a' and 'b' are conflict-free.


**`dataflow(self, name: 'str | None' = None, args: 'list[tuple[str, Cmt2Type]] | None' = None, returns: 'list[Cmt2Type] | None' = None, interval: 'int | None' = None) -> 'Iterator'`**

> Define a dataflow pipeline with token-based synchronization.


**`input(self, name: 'str', ty: 'Cmt2Type') -> 'Signal'`**

> Add an input port.


**`instance(self, module, name: 'str | None' = None, interface_bindings: 'dict | None' = None, **port_connections) -> 'Instance'`**

> Create an instance of another module.


**`method(self, name: 'str | None' = None, args: 'list[tuple[str, Cmt2Type]] | None' = None, returns: 'list[Cmt2Type] | None' = None) -> 'Iterator[MethodBuilder]'`**

> Define an atomic action method.


**`precedence(self, *refs) -> 'ModuleBuilder'`**

> Declare scheduling precedence among rules, methods, and values.


**`proc_method(self, name: 'str | None' = None, args: 'list[tuple[str, Cmt2Type]] | None' = None, returns: 'list[Cmt2Type] | None' = None, static_latency: 'int | None' = None, interval: 'int | None' = None) -> 'Iterator[ProcMethodBuilder]'`**

> Define a procedural method with optional timing attributes.


**`proc_rule(self, name: 'str | None' = None) -> 'Iterator[ProcRuleBuilder]'`**

> Define a procedural rule with multi-cycle control.


**`reset(self, name: 'str' = 'rst') -> 'Signal[ResetType]'`**

> Add a reset input port.


**`rule(self, name: 'str | None' = None) -> 'Iterator[RuleBuilder]'`**

> Define a rule.


**`sequence_before(self, before: 'MethodRef | ValueRef', after: 'MethodRef | ValueRef') -> 'ModuleBuilder'`**

> Declare that 'before' must sequence before 'after'.


**`static_step(self, latency: 'int', name: 'str | None' = None, interval: 'int | None' = None) -> 'Iterator[StepBuilder]'`**

> Define a static latency step.


**`step(self, name: 'str | None' = None) -> 'Iterator[StepBuilder]'`**

> Define a procedural step (go-done interface).


**`value(self, name: 'str | None' = None, returns: 'list[Cmt2Type] | None' = None) -> 'Iterator[ValueBuilder]'`**

> Define a value method.


---

## Types

CMT2 type system for PyCMT2 EDSL.

### `AsyncResetType`

Asynchronous reset type.

#### Methods

**`__init__(self) -> None`**


**`bit_width(self) -> 'int'`**


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**


### `Bundle`

Bundle (struct) type with named fields.

Fields are specified as tuples of (name, type, is_flip).

#### Methods

**`__init__(self, fields: 'tuple[tuple[str, Cmt2Type, bool], ...]') -> None`**


**`bit_width(self) -> 'int'`**


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**


### `ClockType`

Clock signal type.

#### Methods

**`__init__(self) -> None`**


**`bit_width(self) -> 'int'`**


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**


### `Cmt2Type`

Base class for all CMT2 types.

#### Methods

**`__init__(self) -> None`**


**`bit_width(self) -> 'int'`**

> Return the bit width of this type.


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**

> Convert to FIRRTL MLIR type.


### `ResetType`

Synchronous reset type (1-bit unsigned).

#### Methods

**`__init__(self) -> None`**


**`bit_width(self) -> 'int'`**


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**


### `SInt`

Signed integer type with static width.

#### Methods

**`__init__(self, width: 'int') -> None`**


**`bit_width(self) -> 'int'`**


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**


### `SyncToken`

Synchronization token type for dataflow pipelines.

Tokens carry optional data payloads and synchronize pipeline stages.
They support two modes:
- LS (Latency Sensitive): Implemented as shift registers
- LI (Latency Insensitive): Implemented as FIFOs

Args:
    data_type: Optional data type carried by the token (default: None for void token)
    mode: Token mode - "ls" or "li" (default: "ls")

Example:
    # Token without data
    void_token = SyncToken()

    # Token carrying 32-bit data
    data_token = SyncToken(UInt(32))

    # Latency-insensitive token
    li_token = SyncToken(UInt(32), mode="li")

#### Methods

**`__init__(self, data_type: 'Cmt2Type | None' = None, mode: 'str' = 'ls') -> None`**


**`bit_width(self) -> 'int'`**

> Bit width of the data payload (1 for void token).


**`has_data(self) -> 'bool'`**

> Return True if this token carries data.


**`is_latency_insensitive(self) -> 'bool'`**

> Return True if this is a latency-insensitive token.


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**

> Convert to CMT2 SyncTokenType MLIR type.


### `UInt`

Unsigned integer type with static width.

#### Methods

**`__init__(self, width: 'int') -> None`**


**`bit_width(self) -> 'int'`**


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**


### `Vector`

Vector (array) type.

#### Methods

**`__init__(self, element: 'Cmt2Type', size: 'int') -> None`**


**`bit_width(self) -> 'int'`**


**`to_firrtl_type(self, ctx: 'MlirContext') -> 'MlirType'`**


### Functions

### `bundle(**fields: 'Cmt2Type | tuple[Cmt2Type, bool]') -> 'Bundle'`

Create a bundle type with named fields.

Args:
    **fields: Field names mapped to types or (type, is_flip) tuples.

Example:
    valid_ready = bundle(valid=Bool, ready=(Bool, True), data=UInt(32))

---

## STL Components

Standard Library (STL) module wrappers for PyCMT2.

This module provides Python wrappers for common hardware components
that can be instantiated in CMT2 designs. These match the C++ STLLibrary
bindings in lib/Dialect/Cmt2/ECMT2/STLLibrary.cpp.

Key differences from old implementation:
- Reg and Wire are external modules (bindings to ModuleLibrary)
- FIFOs are CMT2 modules built from Reg and Wire primitives
- Memory modules are external modules (bindings to ModuleLibrary)
- No inline RTL generation - RTL comes from ModuleLibrary

Example:
    from pycmt2 import Circuit
    from pycmt2.stl import Reg, FIFO1Push

    circuit = Circuit("Counter")

    with circuit.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()

        # Create a 32-bit register using STL
        reg_mod = Reg.create(circuit, 32)
        count = m.instance(reg_mod, "count", clk=clk, rst=rst)

        with m.rule("increment") as r:
            with r.guard() as g:
                g.always()
            with r.body() as body:
                val = body.call(count, "read")
                new_val = body.add(val, body.const(1, 32))
                body.call(count, "write", body.truncate(new_val, 32))

### `FIFO`

Factory for FIFO modules.

For specific FIFO variants, use:
- FIFO1Push: Depth-1 with active push semantics
- FIFO1Pull: Depth-1 with active pull semantics
- FIFO2I: Depth-2 with independent enq/deq

Example:
    fifo_mod = FIFO.create(circuit, 32)
    fifo = m.instance(fifo_mod, "data_fifo")

#### Methods

**`create(circuit: 'Circuit', width: 'int', depth: 'int' = 2) -> 'ModuleBuilder'`**

> Create a FIFO module.


### `FIFO1Pull`

Factory for depth-1 FIFO with active pull semantics as CMT2 module.

Matches ECMT2 STLLibrary::createFIFO1PullModule.

Built from: Reg (data), Reg (full), Wire (deqed), Wire (enqed)

Interface:
- value "full" -> bool: Returns true when FIFO is full
- method "enq" (data): Enqueue data (always ready)
- value "deq" -> data: Dequeue data (guard: full & enqed)

The "pull" semantics means:
- Producer (enq) can always enqueue
- Consumer (deq) is guarded by data availability

Example:
    fifo_mod = FIFO1Pull.create(circuit, 32)
    fifo = m.instance(fifo_mod, "output_fifo")

#### Methods

**`create(circuit: 'Circuit', width: 'int') -> 'ModuleBuilder'`**

> Create a depth-1 pull-style FIFO as CMT2 module.


### `FIFO1Push`

Factory for depth-1 FIFO with active push semantics as CMT2 module.

Matches ECMT2 STLLibrary::createFIFO1PushModule.

Built from: Reg (data), Reg (full), Wire (deqed), Wire (enqed)

Interface:
- value "full" -> bool: Returns true when FIFO is full
- method "deq" -> data: Dequeue data (guard: full)
- method "enq" (data): Enqueue data (guard: !full | deqed)

The "push" semantics means:
- Producer (enq) is guarded by FIFO state
- Consumer (deq) can always dequeue when data is available

Example:
    fifo_mod = FIFO1Push.create(circuit, 32)
    fifo = m.instance(fifo_mod, "input_fifo")

#### Methods

**`create(circuit: 'Circuit', width: 'int') -> 'ModuleBuilder'`**

> Create a depth-1 push-style FIFO as CMT2 module.


### `FIFO2I`

Factory for depth-2 FIFO with independent enq/deq as CMT2 module.

Matches ECMT2 STLLibrary::createFIFO2IModule.

Built from: Reg (reg0), Reg (reg1), Reg (state), WireDefault (deqed), WireDefault (enqed), Wire (enq_value)

Interface:
- value "full" -> bool: Returns true when state == 2 (FIFO at capacity)
- method "deq" -> data: Dequeue data (guard: state != 0)
- method "enq" (data): Enqueue data (guard: state != 2)

This FIFO allows independent enq and deq in the same cycle.

Example:
    fifo_mod = FIFO2I.create(circuit, 32)
    fifo = m.instance(fifo_mod, "buffer")

#### Methods

**`create(circuit: 'Circuit', width: 'int') -> 'ModuleBuilder'`**

> Create a depth-2 independent FIFO as CMT2 module.


### `Memory`

Factory for Memory external modules.

Provides bindings for memory modules in ModuleLibrary:
- Mem1r1w1c: 1R1W synchronous memory (1-cycle read latency)
- Mem1r1w0c: 1R1W asynchronous memory (0-cycle read latency)

#### Methods

**`create(circuit: 'Circuit', data_width: 'int', addr_width: 'int', depth: 'int', sync: 'bool' = True) -> 'ExternalModuleBuilder'`**

> Create a memory external module.


**`create_1r1w_async(circuit: 'Circuit', data_width: 'int', addr_width: 'int', depth: 'int') -> 'ExternalModuleBuilder'`**

> Create a 1R1W asynchronous memory (0-cycle read latency).


**`create_1r1w_sync(circuit: 'Circuit', data_width: 'int', addr_width: 'int', depth: 'int') -> 'ExternalModuleBuilder'`**

> Create a 1R1W synchronous memory (1-cycle read latency).


### `Reg`

Factory for register external modules.

Creates an external module matching C++ STLLibrary::createRegModule:
- clock port: "clk"
- reset port: "rst"
- value method: "read" -> returns data
- action method: "write" <- takes data
- sequence_before("read", "write")

Example:
    reg_mod = Reg.create(circuit, 32)
    count = m.instance(reg_mod, "count", clk=clk, rst=rst)

    # In a rule body:
    val = body.call(count, "read")
    body.call(count, "write", val + body.const(1, 32))

#### Methods

**`create(circuit: 'Circuit', width: 'int', init: 'int' = 0) -> 'ExternalModuleBuilder'`**

> Create a register external module.


### `ShiftReg`

Factory for shift register as CMT2 module.

A shift register is a chain of registers that delays data by a fixed
number of cycles. Data enters at one end (enq) and exits at the other (deq).

Built from: Reg instances for each stage

Interface:
- method "enq" (data): Write data to input stage
- value "deq" -> data: Read data from output stage
- value "valid" -> bool: Output is valid (always true after delay cycles)

The shift register delays data by `depth` cycles.

Example:
    shiftreg_mod = ShiftReg.create(circuit, 32, depth=4)
    sr = m.instance(shiftreg_mod, "delay_line", clk=clk, rst=rst)

#### Methods

**`create(circuit: 'Circuit', width: 'int', depth: 'int' = 2) -> 'ModuleBuilder'`**

> Create a shift register as CMT2 module.


### `Wire`

Factory for wire external modules.

Creates an external module matching C++ STLLibrary::createWireModule:
- value method: "read" -> returns data
- action method: "write" <- takes data
- sequence_before("write", "read")
- conflict("write", "write")

Note: Wire is a combinational module and does NOT have clock/reset ports.

Example:
    wire_mod = Wire.create(circuit, 32)
    temp = m.instance(wire_mod, "temp")

#### Methods

**`create(circuit: 'Circuit', width: 'int') -> 'ExternalModuleBuilder'`**

> Create a wire external module.


### `WireDefault`

Factory for wire with default value as CMT2 module.

Matches ECMT2 STLLibrary::createWireDefaultModule.
Wraps a Wire with a default value rule that always writes the init value.

Scheduling: write < default < read

Example:
    wire_default_mod = WireDefault.create(circuit, 1, init=0)
    flag = m.instance(wire_default_mod, "flag")

#### Methods

**`create(circuit: 'Circuit', width: 'int', init: 'int' = 0) -> 'ModuleBuilder'`**

> Create a wire with default value as CMT2 module.


### Functions

### `add_stl_rtl_to_workspace(workspace) -> 'None'`

Add all registered STL RTL files to a simulation workspace.

DEPRECATED: STL modules now use ModuleLibrary for RTL.
This function is a no-op for backward compatibility.

Args:
    workspace: SimulationWorkspace instance (ignored).

### `clear_stl_registry() -> 'None'`

Clear the STL RTL registry.

DEPRECATED: STL modules now use ModuleLibrary for RTL.
This function is a no-op for backward compatibility.

### `get_stl_rtl_files() -> 'dict[str, str]'`

Get all registered STL RTL implementations.

DEPRECATED: STL modules now use ModuleLibrary for RTL.
This function returns an empty dict for backward compatibility.

Returns:
    Empty dictionary (RTL comes from ModuleLibrary).

---

## Dataflow Builders

Dataflow builders for PyCMT2 EDSL.

This module provides builders for constructing dataflow pipelines using
token-based synchronization. Dataflow pipelines consist of tasks connected
by tokens that carry data and synchronization signals.

Example:
    with mod.dataflow("pipeline", args=[("input", UInt(32))],
                     returns=[UInt(32)]) as df:
        # Stage 0: Process input
        with df.task("stage0") as task:
            tok0 = task.create_token(df.input, UInt(32))
            task.yield_tokens(tok0)

        # Stage 1: Transform data
        with df.task("stage1", tokens_in=[tok0]) as task:
            data = task.token_data(tok0)
            result = task.add(data, task.const(1, 32))
            tok1 = task.create_token(result, UInt(32))
            task.yield_tokens(tok1)

        # Final stage: Output result
        with df.task("final", tokens_in=[tok1]) as task:
            output = task.token_data(tok1)
            task.return_values(output)

### `DataflowBuilder`

Builder for dataflow pipelines.

Dataflow pipelines describe concurrent computations connected
by token-based synchronization. Each task in the pipeline
executes when its input tokens are valid.

Example:
    with mod.dataflow("adder_pipe", args=[("a", UInt(32)), ("b", UInt(32))],
                     returns=[UInt(32)], interval=1) as df:
        # First stage
        with df.task("add") as task:
            sum_val = task.add(df.a, df.b)
            tok = task.create_token(sum_val, UInt(33))
            task.yield_tokens(tok)

        # Final stage
        with df.task("output", tokens_in=[tok]) as task:
            result = task.token_data(tok)
            truncated = task.bits(result, 31, 0)
            task.return_values(truncated)

#### Methods

**`__init__(self, module: 'ModuleBuilder', name: 'str | None', args: 'list[tuple[str, Cmt2Type]]', returns: 'list[Cmt2Type]', interval: 'int | None' = None)`**


**`task(self, name: 'str | None' = None, tokens_in: 'list[Token] | None' = None, tokens_out: 'list[SyncToken] | None' = None, timing: 'tuple[int, int] | None' = None) -> 'Iterator[TaskBuilder]'`**

> Create a dataflow task.


### `TaskBuilder`

Builder for dataflow tasks.

Tasks are the atomic units of computation in a dataflow pipeline.
They consume input tokens, perform computation, and produce output tokens.

Example:
    with df.task("multiply", tokens_in=[tok_a, tok_b]) as task:
        a = task.token_data(tok_a)
        b = task.token_data(tok_b)
        result = task.mul(a, b)
        tok_out = task.create_token(result, UInt(64))
        task.yield_tokens(tok_out)

#### Methods

**`__enter__(self) -> 'RegionBuilder'`**


**`__exit__(self, exc_type, exc_val, exc_tb)`**


**`__init__(self, dataflow: 'DataflowBuilder', name: 'str | None', tokens_in: 'list[Token]', timing: 'tuple[int, int] | None' = None)`**


**`add(self, a: 'Signal', b: 'Signal | int') -> 'Signal'`**

> Add two signals.


**`and_(self, a: 'Signal', b: 'Signal | int') -> 'Signal'`**

> Bitwise AND.


**`as_sint(self, a: 'Signal') -> 'Signal'`**

> Convert to signed.


**`as_uint(self, a: 'Signal') -> 'Signal[UInt]'`**

> Convert to unsigned.


**`bit(self, a: 'Signal', idx: 'int') -> 'Signal[UInt]'`**

> Extract a single bit.


**`bits(self, a: 'Signal', high: 'int', low: 'int') -> 'Signal[UInt]'`**

> Extract a range of bits (inclusive).


**`call(self, target, method_or_value, *args: 'Signal', arg_timing: 'list[tuple[int, int]] | None' = None, result_timing: 'list[tuple[int, int]] | None' = None) -> 'tuple[Signal, ...] | Signal | None'`**

> Call a method or value on an instance.


**`concat(self, *signals: 'Signal') -> 'Signal[UInt]'`**

> Concatenate signals (first is MSB).


**`const(self, value: 'int', width: 'int') -> 'Signal[UInt]'`**

> Create a constant unsigned integer.


**`convert_width(self, a: 'Signal', width: 'int') -> 'Signal'`**

> Convert signal to given width (truncate or pad as needed).


**`create_token(self, data: 'Signal | None' = None, data_type: 'Cmt2Type | None' = None, mode: 'str' = 'ls') -> 'Token'`**

> Create a new token, optionally with data.


**`enable(self, step_name: 'str') -> 'None'`**

> Enable a step by name.


**`eq(self, a: 'Signal', b: 'Signal | int') -> 'Signal[UInt]'`**

> Equality comparison.


**`ge(self, a: 'Signal', b: 'Signal | int') -> 'Signal[UInt]'`**

> Greater than or equal comparison.


**`gt(self, a: 'Signal', b: 'Signal | int') -> 'Signal[UInt]'`**

> Greater than comparison.


**`if_(self, condition: 'Signal') -> 'Iterator[tuple[TaskBuilder, TaskBuilder]]'`**

> Create a conditional control block.


**`join_tokens(self, *tokens: 'Token', mode: 'str' = 'ls') -> 'Token'`**

> Join multiple tokens into a single synchronization token.


**`le(self, a: 'Signal', b: 'Signal | int') -> 'Signal[UInt]'`**

> Less than or equal comparison.


**`lt(self, a: 'Signal', b: 'Signal | int') -> 'Signal[UInt]'`**

> Less than comparison.


**`mul(self, a: 'Signal', b: 'Signal | int') -> 'Signal'`**

> Multiply two signals.


**`mux(self, cond: 'Signal', t: 'Signal', f: 'Signal') -> 'Signal'`**

> Multiplexer: if cond then t else f.


**`neq(self, a: 'Signal', b: 'Signal | int') -> 'Signal[UInt]'`**

> Inequality comparison.


**`not_(self, a: 'Signal') -> 'Signal'`**

> Bitwise NOT.


**`or_(self, a: 'Signal', b: 'Signal | int') -> 'Signal'`**

> Bitwise OR.


**`pad(self, a: 'Signal', width: 'int') -> 'Signal'`**

> Zero-extend or sign-extend to given width.


**`par(self) -> 'Iterator[TaskBuilder]'`**

> Create a parallel control block.


**`reduce_and(self, a: 'Signal') -> 'Signal[UInt]'`**

> Reduce AND.


**`reduce_or(self, a: 'Signal') -> 'Signal[UInt]'`**

> Reduce OR.


**`reduce_xor(self, a: 'Signal') -> 'Signal[UInt]'`**

> Reduce XOR.


**`return_values(self, *values: 'Signal') -> 'None'`**

> Return values from the dataflow pipeline.


**`rsub(self, a: 'int', b: 'Signal') -> 'Signal'`**

> Subtract signal from integer (a - b).


**`seq(self) -> 'Iterator[TaskBuilder]'`**

> Create a sequential control block.


**`shl(self, a: 'Signal', amount: 'Signal | int') -> 'Signal'`**

> Left shift.


**`shr(self, a: 'Signal', amount: 'Signal | int') -> 'Signal'`**

> Right shift.


**`static_repeat(self, count: 'int', latency: 'int | None' = None) -> 'Iterator[TaskBuilder]'`**

> Create a compile-time unrolled loop.


**`sub(self, a: 'Signal', b: 'Signal | int') -> 'Signal'`**

> Subtract two signals.


**`token_data(self, token: 'Token') -> 'Signal'`**

> Extract the data payload from a token.


**`token_valid(self, token: 'Token') -> 'Signal'`**

> Extract the valid signal from a token.


**`truncate(self, a: 'Signal', width: 'int') -> 'Signal'`**

> Truncate signal to given width (extract low bits).


**`xor_(self, a: 'Signal', b: 'Signal | int') -> 'Signal'`**

> Bitwise XOR.


**`yield_tokens(self, *tokens: 'Token') -> 'None'`**

> Yield tokens from this task for downstream consumption.


### `Token`

Reference to a dataflow token.

Tokens are produced by tasks via create_token() and yield_tokens(),
and consumed by downstream tasks via their tokens_in parameter.

#### Methods

**`__init__(self, value: 'object', token_type: 'SyncToken', name: 'str') -> None`**


### Functions

### `pipeline_dataflow(module: 'ModuleBuilder', name: 'str', stages: 'int', data_type: 'Cmt2Type', interval: 'int' = 1) -> 'DataflowBuilder'`

Create a simple linear pipeline dataflow.

This is a convenience function for creating pipelines where
data flows linearly through N stages.

Args:
    module: The module to add the pipeline to.
    name: Pipeline name.
    stages: Number of pipeline stages.
    data_type: Type of data flowing through the pipeline.
    interval: Initiation interval (default 1 = fully pipelined).

Returns:
    A DataflowBuilder configured for linear pipeline.

Note:
    For more complex dataflow patterns (fork/join), use
    module.dataflow() directly.

---

## Pipeline Builders

Pipeline shorthand builders for PyCMT2 EDSL.

This module provides high-level builders for common pipeline patterns,
simplifying the creation of linear pipelines and other common dataflow
topologies.

Example - Simple linear pipeline:
    from pycmt2 import Circuit, UInt, Pipeline

    circuit = Circuit("Adder")
    with circuit.module("AdderPipe") as mod:
        pipe = Pipeline(mod, "add_pipe", UInt(32), stages=3)

        @pipe.stage(0)
        def s0(task, input_data):
            # Stage 0: Compute partial sum
            partial = task.add(input_data, task.const(1, 32))
            return partial

        @pipe.stage(1)
        def s1(task, data):
            # Stage 1: Add more
            partial = task.add(data, task.const(2, 32))
            return partial

        @pipe.stage(2)
        def s2(task, data):
            # Stage 2: Final output
            return task.bits(data, 31, 0)

        pipe.build()  # Creates the dataflow IR

### `ForkJoinPipeline`

High-level builder for fork-join dataflow patterns.

ForkJoinPipeline allows defining parallel branches that fork from
a common source and join at a sink.

Example:
    fjp = ForkJoinPipeline(mod, "parallel_add", UInt(32))

    @fjp.source()
    def source(task, input_data):
        return input_data

    @fjp.branch("add1")
    def add1(task, data):
        return task.add(data, task.const(1, 32))

    @fjp.branch("add2")
    def add2(task, data):
        return task.add(data, task.const(2, 32))

    @fjp.sink()
    def sink(task, results):
        a, b = results
        return task.add(a, b)

    fjp.build()

#### Methods

**`__init__(self, module: 'ModuleBuilder', name: 'str', data_type: 'Cmt2Type', output_type: 'Cmt2Type | None' = None, interval: 'int | None' = None)`**

> Create a fork-join pipeline.


**`branch(self, name: 'str') -> 'Callable'`**

> Decorator to define a parallel branch.


**`build(self) -> 'None'`**

> Build the fork-join dataflow IR.


**`sink(self, name: 'str' = 'sink') -> 'Callable'`**

> Decorator to define the sink (join) stage.


**`source(self, name: 'str' = 'source') -> 'Callable'`**

> Decorator to define the source stage.


### `Pipeline`

High-level builder for linear pipelines.

Pipeline provides a decorator-based API for defining pipeline stages
that process data in sequence. Each stage receives data from the
previous stage and passes transformed data to the next.

The Pipeline automatically generates the dataflow IR with proper
token connections between stages.

Example:
    pipe = Pipeline(mod, "adder", UInt(32), stages=3, interval=1)

    @pipe.stage(0)
    def stage0(task, data):
        return task.add(data, task.const(1, 32))

    @pipe.stage(1)
    def stage1(task, data):
        return task.add(data, task.const(2, 33))

    @pipe.stage(2)
    def stage2(task, data):
        return task.bits(data, 31, 0)

    pipe.build()

#### Methods

**`__init__(self, module: 'ModuleBuilder', name: 'str', data_type: 'Cmt2Type', stages: 'int', interval: 'int' = 1, output_type: 'Cmt2Type | None' = None)`**

> Create a linear pipeline.


**`build(self) -> 'None'`**

> Build the pipeline dataflow IR.


**`stage(self, index: 'int', name: 'str | None' = None, latency: 'int' = 1) -> 'Callable'`**

> Decorator to define a pipeline stage.


### `PipelineStage`

Definition of a pipeline stage.

#### Methods

**`__init__(self, index: 'int', name: 'str', transform_fn: 'Callable[[TaskBuilder, Signal], Signal]', latency: 'int' = 1) -> None`**


---

## Timing

Timing helpers for PyCMT2 EDSL.

This module provides utilities for working with timing in dataflow
pipelines and procedural constructs.

Example:
    from pycmt2.timing import timing_interval, pipeline_timing, validate_timing

    # Create timing interval
    t = timing_interval(0, 4)  # [0, 4) = cycles 0, 1, 2, 3

    # Generate timing for N-stage pipeline
    stages = pipeline_timing(stages=4, stage_latency=2)
    # Returns: [(0, 2), (2, 4), (4, 6), (6, 8)]

    # Validate timing constraints
    validate_timing(timing_list, interval=2)

### `TimingInterval`

Represents a half-open timing interval [start, end).

The interval includes cycles from `start` to `end - 1`.

#### Methods

**`__init__(self, start: 'int', end: 'int') -> None`**


**`contains(self, cycle: 'int') -> 'bool'`**

> Check if a cycle is within this interval.


**`overlaps(self, other: 'TimingInterval') -> 'bool'`**

> Check if this interval overlaps with another.


**`shift(self, offset: 'int') -> 'TimingInterval'`**

> Return a new interval shifted by offset cycles.


**`to_tuple(self) -> 'tuple[int, int]'`**

> Convert to (start, end) tuple.


### Functions

### `arg_timing(num_args: 'int', start: 'int' = 0, duration: 'int' = 1) -> 'list[tuple[int, int]]'`

Generate timing for call arguments.

All arguments are assumed to be provided at the same time.

Args:
    num_args: Number of arguments.
    start: Start cycle (default 0).
    duration: Duration in cycles (default 1).

Returns:
    List of (start, end) tuples, one per argument.

Example:
    # Two arguments provided at cycle 0
    arg_t = arg_timing(2)  # [(0, 1), (0, 1)]

    # Three arguments provided at cycle 2 for 2 cycles
    arg_t = arg_timing(3, start=2, duration=2)  # [(2, 4), (2, 4), (2, 4)]

### `interleaved_timing(tasks: 'int', latency: 'int', interval: 'int') -> 'list[TimingInterval]'`

Generate timing for interleaved (pipelined) execution.

When multiple operations are pipelined with initiation interval II,
each starts II cycles after the previous.

Args:
    tasks: Number of tasks.
    latency: Latency of each task.
    interval: Initiation interval (II).

Returns:
    List of TimingInterval for each task.

Example:
    # 4 pipelined tasks, latency=4, II=1
    timing = interleaved_timing(4, latency=4, interval=1)
    # Returns: [[0,4), [1,5), [2,6), [3,7)]

### `pipeline_timing(stages: 'int', stage_latency: 'int' = 1, start_cycle: 'int' = 0) -> 'list[TimingInterval]'`

Generate timing intervals for a linear pipeline.

Each stage occupies `stage_latency` cycles, starting after
the previous stage completes.

Args:
    stages: Number of pipeline stages.
    stage_latency: Latency of each stage in cycles (default 1).
    start_cycle: Starting cycle for the pipeline (default 0).

Returns:
    List of TimingInterval for each stage.

Example:
    # 4-stage pipeline, 1 cycle per stage
    timing = pipeline_timing(4)
    # Returns: [[0,1), [1,2), [2,3), [3,4)]

    # 3-stage pipeline, 2 cycles per stage
    timing = pipeline_timing(3, stage_latency=2)
    # Returns: [[0,2), [2,4), [4,6)]

### `result_timing(num_results: 'int', start: 'int', duration: 'int' = 1) -> 'list[tuple[int, int]]'`

Generate timing for call results.

All results are assumed to be available at the same time.

Args:
    num_results: Number of results.
    start: Start cycle when results are available.
    duration: Duration results are valid (default 1).

Returns:
    List of (start, end) tuples, one per result.

Example:
    # Single result available at cycle 4
    res_t = result_timing(1, start=4)  # [(4, 5)]

### `single_cycle(cycle: 'int') -> 'TimingInterval'`

Create a single-cycle timing interval.

Args:
    cycle: The cycle number.

Returns:
    A TimingInterval for one cycle.

Example:
    t = single_cycle(5)  # [5, 6)

### `timing_interval(start: 'int', end: 'int') -> 'TimingInterval'`

Create a timing interval.

Args:
    start: Start cycle (inclusive).
    end: End cycle (exclusive).

Returns:
    A TimingInterval representing [start, end).

Example:
    t = timing_interval(0, 4)  # Cycles 0, 1, 2, 3

### `timing_list_to_tuples(intervals: 'Sequence[TimingInterval]') -> 'list[tuple[int, int]]'`

Convert a list of TimingIntervals to tuples.

Args:
    intervals: Sequence of timing intervals.

Returns:
    List of (start, end) tuples.

### `timing_to_attr_tuple(interval: 'TimingInterval') -> 'tuple[int, int]'`

Convert TimingInterval to tuple format for MLIR attributes.

Args:
    interval: The timing interval.

Returns:
    (start, end) tuple for use with call() timing arguments.

### `total_latency(intervals: 'Sequence[TimingInterval]') -> 'int'`

Calculate total latency from a sequence of timing intervals.

Returns the end cycle of the last interval.

Args:
    intervals: Sequence of timing intervals.

Returns:
    Total latency in cycles.

Example:
    timing = pipeline_timing(4, stage_latency=2)
    total = total_latency(timing)  # Returns 8

### `validate_timing(intervals: 'Sequence[TimingInterval]', interval: 'int | None' = None, allow_overlap: 'bool' = False) -> 'list[str]'`

Validate timing constraints and return any violations.

Checks:
- Intervals are valid (end > start)
- Intervals don't overlap (unless allow_overlap=True)
- Pipeline interval constraints are respected

Args:
    intervals: Sequence of timing intervals to validate.
    interval: Optional initiation interval to check against.
    allow_overlap: Whether overlapping intervals are allowed.

Returns:
    List of error messages (empty if valid).

Example:
    errors = validate_timing(timing_list, interval=2)
    if errors:
        for e in errors:
            print(f"Timing error: {e}")

---

## Simulation

Simulation workspace generation for PyCMT2 designs.

This module provides utilities for generating RTL simulation workspaces
with Verilator or other simulators.

Example:
    from pycmt2 import Circuit
    from pycmt2.simulation import SimulationWorkspace

    circuit = Circuit("Counter")
    # ... build circuit ...

    ws = SimulationWorkspace(circuit, "./sim")
    ws.generate_placeholder()

    # Or with custom testbench:
    from pycmt2.testbench import Testbench

    tb = Testbench(circuit)
    with tb.sequence("basic") as seq:
        seq.reset(5)
        seq.wait(10)
        seq.expect("count", 10)

    ws.generate_with_testbench(tb)

### `SimulationWorkspace`

Generate simulation workspace for CMT2 designs.

SimulationWorkspace creates a directory structure suitable for
RTL simulation with Verilator or other simulators.

Directory structure:
    sim_workspace/
    ├── rtl/
    │   ├── <module>.sv       # Generated Verilog
    │   └── <module>_pkg.sv   # Package definitions (if any)
    ├── tb/
    │   ├── testbench.cpp     # C++ testbench (Verilator)
    │   └── testbench.py      # cocotb testbench (optional)
    ├── build/
    │   └── .gitkeep
    ├── waves/
    │   └── .gitkeep
    ├── Makefile              # Top-level Makefile
    └── README.md             # Instructions

#### Methods

**`__init__(self, circuit: 'Circuit', output_dir: 'str | Path', top_module: 'str | None' = None, debug_ports: 'bool' = False)`**

> Create a simulation workspace generator.


**`add_external_rtl(self, filename: 'str', content: 'str') -> "'SimulationWorkspace'"`**

> Add external RTL file to the workspace.


**`build(self) -> 'bool'`**

> Build the simulation executable.


**`build_and_run(self) -> 'tuple[bool, str]'`**

> Build and run the simulation.


**`generate_placeholder(self)`**

> Generate workspace with placeholder testbench.


**`generate_with_testbench(self, testbench: 'Testbench')`**

> Generate workspace with testbench from DSL.


**`run(self) -> 'tuple[bool, str]'`**

> Run the simulation.


---

## Testbench

Testbench DSL for PyCMT2 designs.

This module provides a Python DSL for describing testbenches that can be
compiled to C++ (Verilator) or Python (cocotb) testbenches.

Example:
    from pycmt2 import Circuit
    from pycmt2.testbench import Testbench
    from pycmt2.simulation import SimulationWorkspace

    circuit = Circuit("Counter")
    # ... build circuit ...

    tb = Testbench(circuit)

    with tb.sequence("basic_count") as seq:
        seq.reset(5)
        seq.wait(10)
        seq.expect("count", 10)

    with tb.sequence("stress_test") as seq:
        seq.reset(5)
        for i in range(100):
            seq.wait(1)
            seq.expect("count", i + 1)

    # Generate simulation workspace with testbench
    ws = SimulationWorkspace(circuit, "./sim")
    ws.generate_with_testbench(tb)

### `CallMethodOp`

Call a method on an instance (drive enable, check ready).

#### Methods

**`__init__(self, instance: 'str', method: 'str', args: 'tuple[Any, ...]') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `CommentOp`

Add a comment in the generated testbench.

#### Methods

**`__init__(self, text: 'str') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `DebugPortAllOp`

Print all debug firing ports.

#### Methods

**`__init__(self, rule_names: 'tuple[str, ...]') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `DebugPortCheckOp`

Check debug firing port for a rule.

#### Methods

**`__init__(self, rule_name: 'str', expected_fired: 'bool' = True) -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `DebugPortPrintOp`

Print debug firing port status for a rule.

#### Methods

**`__init__(self, rule_name: 'str') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `DriveOp`

Drive a value to an input port.

#### Methods

**`__init__(self, port: 'str', value: 'int | str') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `ExpectOp`

Assert expected value on output port.

#### Methods

**`__init__(self, port: 'str', value: 'int | str', message: 'str | None' = None) -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `PrintCycleDiffOp`

Print the difference between two recorded cycle counts.

#### Methods

**`__init__(self, start_label: 'str', end_label: 'str', message: 'str') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `PrintOp`

Print a message during simulation.

#### Methods

**`__init__(self, message: 'str', values: 'tuple[str, ...]' = ()) -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `RecordCycleOp`

Record the current cycle number for timing verification.

#### Methods

**`__init__(self, label: 'str') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `ResetOp`

Assert reset for N cycles.

#### Methods

**`__init__(self, cycles: 'int') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `TestOp`

Base class for testbench operations.

#### Methods

**`to_cpp(self) -> 'str'`**

> Generate C++ code for this operation.


**`to_python(self) -> 'str'`**

> Generate Python (cocotb) code for this operation.


### `TestSequence`

A sequence of test operations.

TestSequence provides a fluent API for building test sequences
that can be compiled to C++ or Python testbenches.

Example:
    with tb.sequence("basic") as seq:
        seq.reset(5)
        seq.drive("input_a", 42)
        seq.wait(10)
        seq.expect("output", 84)

#### Methods

**`__enter__(self) -> 'TestSequence'`**


**`__exit__(self, exc_type, exc_val, exc_tb)`**


**`__init__(self, name: 'str')`**


**`call_method(self, instance: 'str', method: 'str', *args: 'Any') -> 'TestSequence'`**

> Call a method on an instance.


**`comment(self, text: 'str') -> 'TestSequence'`**

> Add a comment in the generated testbench.


**`drive(self, port: 'str', value: 'int | str') -> 'TestSequence'`**

> Drive a value to an input port.


**`expect(self, port: 'str', value: 'int | str', message: 'str | None' = None) -> 'TestSequence'`**

> Assert expected value on output port.


**`expect_rule_fired(self, rule_name: 'str', fired: 'bool' = True) -> 'TestSequence'`**

> Assert that a rule fired (or did not fire) this cycle.


**`print(self, message: 'str', *values: 'str') -> 'TestSequence'`**

> Print a message during simulation.


**`print_all_rule_status(self, rule_names: 'list[str]') -> 'TestSequence'`**

> Print firing status of all specified rules.


**`print_cycle_diff(self, start_label: 'str', end_label: 'str', message: 'str') -> 'TestSequence'`**

> Print the difference between two recorded cycle counts.


**`print_rule_status(self, rule_name: 'str') -> 'TestSequence'`**

> Print whether a rule fired this cycle.


**`record_cycle(self, label: 'str') -> 'TestSequence'`**

> Record the current cycle number for timing verification.


**`reset(self, cycles: 'int' = 5) -> 'TestSequence'`**

> Assert reset for N cycles.


**`wait(self, cycles: 'int') -> 'TestSequence'`**

> Wait for N clock cycles.


**`wait_condition(self, condition: 'str', timeout: 'int' = 1000) -> 'TestSequence'`**

> Wait until a condition is true.


**`wait_ready(self, instance: 'str', method: 'str') -> 'TestSequence'`**

> Wait until a method is ready.


### `Testbench`

Testbench description for CMT2 designs.

Testbench provides a DSL for describing test sequences that can be
compiled to C++ (Verilator) or Python (cocotb) testbenches.

Example:
    tb = Testbench(circuit)

    with tb.sequence("basic") as seq:
        seq.reset(5)
        seq.wait(10)
        seq.expect("count", 10)

    # Generate with SimulationWorkspace
    ws = SimulationWorkspace(circuit, "./sim")
    ws.generate_with_testbench(tb)

Example with debug ports:
    tb = Testbench(circuit, auto_debug_ports=True)

    with tb.sequence("test_rule_firing") as seq:
        seq.reset(5)
        seq.wait(1)
        # Check specific rule fired
        seq.expect_rule_fired("increment")
        # Print all rule firing status
        seq.print_all_rule_status(tb.get_rule_names())

#### Methods

**`__init__(self, circuit: 'Circuit', auto_debug_ports: 'bool' = False)`**

> Create a testbench for a circuit.


**`add_debug_print_sequence(self, name: 'str' = 'debug_print_all', cycles: 'int' = 10) -> 'Testbench'`**

> Add a sequence that prints debug port status each cycle.


**`add_sequence(self, seq: 'TestSequence') -> 'Testbench'`**

> Add an existing sequence to the testbench.


**`generate_cocotb(self) -> 'str'`**

> Generate cocotb (Python) testbench code.


**`generate_cpp(self) -> 'str'`**

> Generate C++ testbench code.


**`get_rule_names(self, module_name: 'str | None' = None) -> 'list[str]'`**

> Get all rule names from the circuit.


**`sequence(self, name: 'str') -> 'Iterator[TestSequence]'`**

> Define a test sequence.


### `WaitConditionOp`

Wait until a condition is true.

#### Methods

**`__init__(self, condition: 'str', timeout: 'int' = 1000) -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `WaitOp`

Wait for N clock cycles.

#### Methods

**`__init__(self, cycles: 'int') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


### `WaitReadyOp`

Wait until a method is ready.

#### Methods

**`__init__(self, instance: 'str', method: 'str') -> None`**


**`to_cpp(self) -> 'str'`**


**`to_python(self) -> 'str'`**


---

## External Module

External module builder for PyCMT2 EDSL.

This module provides builders for creating CMT2 external modules that bind
to FIRRTL modules. External modules are used for standard library components
like registers, FIFOs, and memories.

Example:
    from pycmt2 import Circuit, UInt

    circuit = Circuit("MyDesign")

    # Define an external register module
    with circuit.external_module("Reg32") as reg:
        reg.clock("clk")
        reg.reset("rst")
        reg.value("read", returns=[UInt(32)])
        reg.method("write", args=[("data", UInt(32))])
        reg.sequence_before("read", "write")

    # Use it in a module
    with circuit.module("Counter") as m:
        clk = m.clock()
        rst = m.reset()
        count = m.instance(reg, "count", clk=clk, rst=rst)
        # ...

### `ExternalModuleBuilder`

Builder for CMT2 external modules.

External modules define the interface to FIRRTL modules, including:
- Clock and reset ports
- Value methods (read-only)
- Action methods (may have side effects)
- Scheduling constraints (sequence before, conflict, conflict-free)

#### Methods

**`__enter__(self)`**


**`__exit__(self, exc_type, exc_val, exc_tb)`**


**`__init__(self, circuit: 'Circuit', name: 'str')`**


**`clock(self, name: 'str' = 'clk') -> 'ExternalModuleBuilder'`**

> Declare a clock port.


**`conflict(self, method1: 'str', method2: 'str') -> 'ExternalModuleBuilder'`**

> Declare that two methods conflict (cannot fire together).


**`conflict_free(self, method1: 'str', method2: 'str') -> 'ExternalModuleBuilder'`**

> Declare that two methods are conflict-free.


**`get_method_arg_types(self, name: 'str') -> 'list[Cmt2Type]'`**

> Get the argument types for a method.


**`get_method_return_types(self, name: 'str') -> 'list[Cmt2Type]'`**

> Get the return types for a method.


**`get_value_arg_types(self, name: 'str') -> 'list[Cmt2Type]'`**

> Get the argument types for a value method.


**`get_value_return_types(self, name: 'str') -> 'list[Cmt2Type]'`**

> Get the return types for a value method.


**`method(self, name: 'str', enable_name: 'str | None' = None, ready_name: 'str | None' = None, args: 'list[tuple[str, Cmt2Type]] | None' = None, returns: 'list[tuple[str, Cmt2Type]] | None' = None, static_latency: 'int | None' = None, interval: 'int | None' = None) -> 'ExternalModuleBuilder'`**

> Declare an action method binding.


**`reset(self, name: 'str' = 'rst') -> 'ExternalModuleBuilder'`**

> Declare a reset port.


**`sequence_before(self, before: 'str', after: 'str') -> 'ExternalModuleBuilder'`**

> Declare that method 'before' must sequence before 'after'.


**`set_firrtl_module_name(self, name: 'str') -> 'ExternalModuleBuilder'`**

> Set the FIRRTL module name for this external module.


**`value(self, name: 'str', ready_name: 'str | None' = None, args: 'list[tuple[str, Cmt2Type]] | None' = None, returns: 'list[tuple[str, Cmt2Type]] | None' = None, static_latency: 'int | None' = None) -> 'ExternalModuleBuilder'`**

> Declare a value method binding.


### `ExternalModuleInstance`

Instance of an external module.

Provides access to the bound methods and values.

#### Methods

**`__init__(self, name: 'str', ext_module: 'ExternalModuleBuilder', inst_op)`**


**`method_ref(self, method_name: 'str') -> 'MethodRef'`**

> Get a reference to a method on this instance.


**`value_ref(self, value_name: 'str') -> 'ValueRef'`**

> Get a reference to a value on this instance.


---

## Diagnostics

CMT2 Diagnostic System for Python.

This module provides a multi-level diagnostic reporting system for PyCMT2
with complete source location tracking. Diagnostics emitted by CMT2 passes
can be captured, formatted, and displayed with Python source context.

Diagnostic Levels:
    - ERROR: Fatal errors that prevent compilation
    - WARNING: Non-fatal issues that may affect correctness
    - INFO: Informational messages about compilation progress
    - DEBUG: Detailed debugging information

Example:
    from pycmt2.diagnostics import DiagnosticHandler, DiagnosticLevel

    # Capture diagnostics during compilation
    with DiagnosticHandler() as handler:
        verilog = circuit.to_verilog()

        if handler.has_errors():
            print("Compilation failed!")
            for diag in handler.get_diagnostics():
                print(diag.format())

    # Or use the simple API
    from pycmt2.diagnostics import emit_error, emit_warning

    emit_error(location, "type mismatch", notes=["expected UInt<32>"])

### `Diagnostic`

A diagnostic message with location and notes.

Attributes:
    level: The severity level (ERROR, WARNING, INFO, DEBUG).
    message: The main diagnostic message.
    location: The Python source location where the issue was detected.
    notes: Additional notes providing context or hints.
    hint: A suggestion for how to fix the issue.
    mlir_location: The original MLIR location string (if available).

#### Methods

**`__init__(self, level: 'DiagnosticLevel', message: 'str', location: 'Optional[PythonLocation]' = None, notes: 'List[Note]' = <factory>, hint: 'Optional[str]' = None, mlir_location: 'Optional[str]' = None) -> None`**


**`add_note(self, message: 'str', location: 'Optional[PythonLocation]' = None) -> 'Diagnostic'`**

> Add a note to this diagnostic.


**`format(self, color: 'bool' = True, show_mlir_loc: 'bool' = False) -> 'str'`**

> Format the diagnostic for display.


**`set_hint(self, hint: 'str') -> 'Diagnostic'`**

> Set a hint for fixing the issue.


### `DiagnosticHandler`

A context manager for capturing diagnostics during compilation.

This handler can be used to capture diagnostics emitted by CMT2 passes
during compilation and format them with Python source context.

Example:
    with DiagnosticHandler() as handler:
        verilog = circuit.to_verilog()

    for diag in handler.get_diagnostics():
        print(diag.format())

    if handler.has_errors():
        sys.exit(1)

#### Methods

**`__enter__(self) -> 'DiagnosticHandler'`**


**`__exit__(self, exc_type, exc_val, exc_tb)`**


**`__init__(self, min_level: 'DiagnosticLevel' = <DiagnosticLevel.INFO: 2>, capture_debug: 'bool' = False)`**

> Create a diagnostic handler.


**`add_diagnostic(self, diag: 'Diagnostic') -> 'None'`**

> Add a diagnostic to the handler.


**`format_summary(self) -> 'str'`**

> Get a summary of diagnostics.


**`get_diagnostics(self, level: 'Optional[DiagnosticLevel]' = None) -> 'List[Diagnostic]'`**

> Get captured diagnostics, optionally filtered by level.


**`has_errors(self) -> 'bool'`**

> Check if any errors were captured.


**`has_warnings(self) -> 'bool'`**

> Check if any warnings were captured.


**`print_all(self, file=None, color: 'bool' = True) -> 'None'`**

> Print all diagnostics to a file (default: stderr).


### `DiagnosticLevel`

Severity levels for diagnostics.

### `Note`

A note attached to a diagnostic.

#### Methods

**`__init__(self, message: 'str', location: 'Optional[PythonLocation]' = None) -> None`**


**`format(self, color: 'bool' = False) -> 'str'`**

> Format the note for display.


### Functions

### `emit_debug(location: 'Optional[PythonLocation]', message: 'str') -> 'Diagnostic'`

Emit a debug diagnostic.

### `emit_error(location: 'Optional[PythonLocation]', message: 'str', notes: 'Optional[List[str]]' = None, hint: 'Optional[str]' = None) -> 'Diagnostic'`

Emit an error diagnostic.

Args:
    location: The Python source location.
    message: The error message.
    notes: Optional list of note messages.
    hint: Optional hint for fixing the issue.

Returns:
    The created Diagnostic.

### `emit_info(location: 'Optional[PythonLocation]', message: 'str', notes: 'Optional[List[str]]' = None) -> 'Diagnostic'`

Emit an info diagnostic.

### `emit_warning(location: 'Optional[PythonLocation]', message: 'str', notes: 'Optional[List[str]]' = None, hint: 'Optional[str]' = None) -> 'Diagnostic'`

Emit a warning diagnostic.

### `format_diagnostic_with_source(diag: 'Diagnostic', context_lines: 'int' = 2) -> 'str'`

Format a diagnostic with source code context.

Args:
    diag: The diagnostic to format.
    context_lines: Number of context lines before/after the error line.

Returns:
    Formatted string with source context.

### `parse_mlir_location(loc_str: 'str') -> 'Optional[PythonLocation]'`

Parse an MLIR FileLineColLoc string to extract Python location.

Args:
    loc_str: MLIR location string like 'loc("file.py":10:5)'

Returns:
    PythonLocation if the location is a Python file, None otherwise.

### `scheduling_conflict_warning(location: 'Optional[PythonLocation]', method1: 'str', method2: 'str', reason: 'str' = '') -> 'Diagnostic'`

Create a scheduling conflict warning diagnostic.

Args:
    location: Source location.
    method1: First conflicting method.
    method2: Second conflicting method.
    reason: Reason for the conflict.

Returns:
    Diagnostic for the scheduling conflict.

### `type_mismatch_error(location: 'Optional[PythonLocation]', expected: 'str', actual: 'str', context: 'str' = '') -> 'Diagnostic'`

Create a type mismatch error diagnostic.

Args:
    location: Source location.
    expected: Expected type string.
    actual: Actual type string.
    context: Additional context (e.g., "in method argument").

Returns:
    Diagnostic for the type mismatch.

### `undefined_reference_error(location: 'Optional[PythonLocation]', kind: 'str', name: 'str', container: 'str' = '') -> 'Diagnostic'`

Create an undefined reference error diagnostic.

Args:
    location: Source location.
    kind: Kind of reference (method, value, module, instance).
    name: Name of the undefined reference.
    container: Container where it was looked up.

Returns:
    Diagnostic for the undefined reference.

---
