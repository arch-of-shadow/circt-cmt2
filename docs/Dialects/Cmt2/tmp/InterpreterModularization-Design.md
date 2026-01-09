# CMT2 Interpreter Modularization Design

Design plan for refactoring the monolithic cmt2-dbg interpreter into a modular, extensible architecture.

**Date:** 2026-01-06
**Status:** Proposed

---

## Problem Statement

The current interpreter (`tools/cmt2-dbg/Interpreter.cpp`, ~1,286 lines) is monolithic with several critical issues:

### Current Architecture Issues

1. **Single-class design**: All functionality in one `Cmt2Interpreter` class (360 lines header, 1,286 lines impl)

2. **Hardcoded operation dispatch**: The `executeOp()` method (lines 789-1088) is a linear if-else chain handling 30+ operations with no extension mechanism

3. **No handler registration**: Cannot add new operation handlers without modifying `executeOp()` directly

4. **Fixed procedural support**: Incomplete static step support - `proc.static_step` doesn't track latency cycles properly (lines 1212-1234: "TODO: Track static step latency properly")

5. **No dialect abstraction**: hw::, comb::, and firrtl:: ops are all handled inline with no separation

6. **Monolithic state management**: All state (registers, FSMs, values) in one class with no abstraction

7. **Fixed conflict resolution**: Single hardcoded `resolveConflicts()` method (lines 427-503)

### Missing Static Control Support

The interpreter doesn't properly handle:
- `proc.static_step` latency tracking (just marks done after one cycle)
- `proc.static_if` conditional timing
- `proc.static_repeat` loop timing
- FSM cycle counters for static control
- Timing attribute validation at runtime

### External Module Interpretation Problem

**This is a fundamental issue.** The current interpreter only recognizes registers by name pattern matching (lines 116-120):

```cpp
// For now, treat all external modules with reg/Reg/Register in name as registers
StringRef modName = refModule.moduleName();
if (modName.contains_insensitive("reg") || modName.contains_insensitive("register")) {
  hasRead = true;
  hasWrite = true;
}
```

**Problems:**
1. **Fragile pattern matching**: Only recognizes "reg" or "register" in name
2. **No Memory support**: Cannot interpret Memory modules (read latency, multi-port)
3. **No FIFO support**: Cannot interpret FIFO behavior (enq/deq, capacity)
4. **No custom modules**: External multipliers, dividers, etc. have no interpretation
5. **No state beyond registers**: Each module type needs its own state model
6. **Hardcoded in Interpreter.cpp**: No extension mechanism

---

## Proposed Architecture

### High-Level Design

```
┌─────────────────────────────────────────────────────────────────┐
│                         Cmt2Interpreter                          │
│  (Facade providing existing API, delegates to components)        │
└─────────────────────────────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        ▼                       ▼                       ▼
┌───────────────┐     ┌──────────────────┐     ┌──────────────────┐
│ ExecutionCore │     │  StateManager    │     │  CommandDispatch │
│ - step()      │     │  - registers     │     │  - REPL commands │
│ - evaluateOp  │     │  - FSM states    │     │  - breakpoints   │
│ - control flow│     │  - pending writes│     │  - tracing       │
└───────────────┘     └──────────────────┘     └──────────────────┘
        │
        ├─────────────────────────────────────────────────────────┐
        ▼                                                         ▼
┌─────────────────────────────────────────┐     ┌─────────────────────────────────────────┐
│          OpHandler Registry             │     │     Module Interpreter Registry          │
│  - registerHandler<OpType>(handler_fn)  │     │  - resolveInterpreter(ExtModuleFirrtlOp) │
│  - dispatch(Operation*) → InterpValue   │     │  - loadFromJSON(path)                    │
└─────────────────────────────────────────┘     └─────────────────────────────────────────┘
        │                                                         │
        ├──────────┬──────────┬──────────┐          ┌─────────────┼─────────────┐
        ▼          ▼          ▼          ▼          ▼             ▼             ▼
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│HW Hndlrs │ │Comb Hndlr│ │FIRRTL    │ │CMT2      │ │Reg       │ │FIFO      │ │Memory    │
│ConstOp  │ │Add,Sub.. │ │AddPrimOp │ │CallOp    │ │Interp    │ │Interp    │ │Interp    │
└──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘
                                                          │
                                              ┌───────────┴───────────┐
                                              ▼                       ▼
                                        ┌──────────┐           ┌──────────┐
                                        │JSON-based│           │Custom    │
                                        │Interp    │           │(via .so) │
                                        └──────────┘           └──────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    Control Flow Plugins                          │
│  - DynamicControlPlugin (proc.step, proc.while, etc.)           │
│  - StaticControlPlugin (proc.static_step, timing FSMs)          │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    Scheduler Interface                           │
│  - ORAATScheduler (current default)                             │
│  - ConflictAwareScheduler (uses scheduling attributes)          │
└─────────────────────────────────────────────────────────────────┘
```

---

## Component Specifications

### 1. OpHandler Registry

**Purpose:** Extensible operation dispatch mechanism

**File:** `include/circt/Dialect/Cmt2/Interpreter/OpHandlerRegistry.h`

```cpp
namespace circt::cmt2::interp {

/// Context passed to operation handlers
struct OpContext {
  StateManager &state;
  llvm::DenseMap<mlir::Value, InterpValue> &valueMap;
  llvm::raw_ostream &os;

  InterpValue getValue(mlir::Value v);
  void setValue(mlir::Value v, const InterpValue &val);
};

/// Handler function type
using OpHandler = std::function<std::optional<InterpValue>(
    mlir::Operation *, OpContext &)>;

/// Registry for operation handlers
class OpHandlerRegistry {
public:
  /// Register a handler for a specific operation type
  template<typename OpT>
  void registerHandler(OpHandler handler) {
    handlers_[mlir::TypeID::get<OpT>()] = std::move(handler);
  }

  /// Register handlers for a dialect
  void registerDialect(StringRef dialectName,
                       std::function<void(OpHandlerRegistry &)> registrar);

  /// Execute an operation using registered handlers
  std::optional<InterpValue> execute(mlir::Operation *op, OpContext &ctx);

  /// Check if a handler exists for an operation type
  bool hasHandler(mlir::Operation *op) const;

private:
  llvm::DenseMap<mlir::TypeID, OpHandler> handlers_;
  llvm::StringMap<std::function<void(OpHandlerRegistry &)>> dialectRegistrars_;
};

/// Default handler registrations
void registerHWHandlers(OpHandlerRegistry &registry);
void registerCombHandlers(OpHandlerRegistry &registry);
void registerFIRRTLHandlers(OpHandlerRegistry &registry);
void registerCMT2Handlers(OpHandlerRegistry &registry);

} // namespace circt::cmt2::interp
```

### 2. StateManager

**Purpose:** Centralized state management with observer support

**File:** `include/circt/Dialect/Cmt2/Interpreter/StateManager.h`

```cpp
namespace circt::cmt2::interp {

/// Observer for state changes
class StateObserver {
public:
  virtual ~StateObserver() = default;
  virtual void onRegisterRead(StringRef name, const InterpValue &value) {}
  virtual void onRegisterWrite(StringRef name, const InterpValue &oldVal,
                                const InterpValue &newVal) {}
  virtual void onFSMTransition(StringRef fsmName, unsigned oldState,
                                unsigned newState) {}
  virtual void onCycleStart(uint64_t cycle) {}
  virtual void onCycleEnd(uint64_t cycle) {}
};

/// Manages interpreter state: registers, FSMs, pending writes
class StateManager {
public:
  //===------------------------------------------------------------------===//
  // Register Access
  //===------------------------------------------------------------------===//

  void addRegister(StringRef name, unsigned width,
                   std::optional<InterpValue> resetValue = std::nullopt);
  std::optional<InterpValue> readRegister(StringRef name);
  void writeRegister(StringRef name, const InterpValue &value); // Queued
  void commitWrites(); // Apply pending writes
  void discardWrites();

  std::vector<std::string> getRegisterNames() const;

  //===------------------------------------------------------------------===//
  // FSM State (for procedural control)
  //===------------------------------------------------------------------===//

  void addFSM(StringRef name, unsigned numStates);
  unsigned getFSMState(StringRef name) const;
  void setFSMState(StringRef name, unsigned state);
  bool isFSMIdle(StringRef name) const;
  bool isFSMRunning(StringRef name) const;

  //===------------------------------------------------------------------===//
  // Static Step Cycle Tracking (NEW)
  //===------------------------------------------------------------------===//

  /// Register a static step with its latency
  void addStaticStep(StringRef stepName, unsigned latency,
                     std::optional<unsigned> interval = std::nullopt);

  /// Start a static step (sets cycle counter to 0)
  void startStaticStep(StringRef stepName);

  /// Advance static step cycle counter, returns true if done
  bool tickStaticStep(StringRef stepName);

  /// Get current cycle within a static step
  unsigned getStaticStepCycle(StringRef stepName) const;

  /// Check if static step is done
  bool isStaticStepDone(StringRef stepName) const;

  /// Check if static step can accept new initiation (for pipelining)
  bool canInitiate(StringRef stepName) const;

  //===------------------------------------------------------------------===//
  // Observers
  //===------------------------------------------------------------------===//

  void addObserver(std::shared_ptr<StateObserver> observer);
  void removeObserver(StateObserver *observer);

  //===------------------------------------------------------------------===//
  // Cycle Management
  //===------------------------------------------------------------------===//

  uint64_t getCycle() const { return cycle_; }
  void incrementCycle();
  void reset();

private:
  uint64_t cycle_ = 0;
  llvm::StringMap<RegisterState> registers_;
  llvm::StringMap<InterpValue> pendingWrites_;
  llvm::StringMap<FSMState> fsmStates_;
  llvm::StringMap<StaticStepState> staticSteps_; // NEW
  std::vector<std::shared_ptr<StateObserver>> observers_;
};

/// State for static step tracking
struct StaticStepState {
  unsigned latency;           // Total cycles
  std::optional<unsigned> interval; // Initiation interval (for pipelining)
  unsigned currentCycle = 0;  // Current cycle within step
  bool active = false;        // Is step currently running?
  uint64_t lastInitiation = 0; // Cycle of last initiation (for II tracking)
};

} // namespace circt::cmt2::interp
```

### 3. Control Flow Plugins

**Purpose:** Modular control flow interpretation with static timing support

**File:** `include/circt/Dialect/Cmt2/Interpreter/ControlFlowPlugin.h`

```cpp
namespace circt::cmt2::interp {

/// Abstract interface for control flow plugins
class ControlFlowPlugin {
public:
  virtual ~ControlFlowPlugin() = default;

  /// Initialize plugin with module
  virtual void initialize(cmt2::ModuleOp module, StateManager &state) = 0;

  /// Check if plugin handles this operation
  virtual bool handles(mlir::Operation *op) const = 0;

  /// Execute control flow operation, returns done status
  virtual bool execute(mlir::Operation *op, OpContext &ctx) = 0;

  /// Get plugin name (for debugging)
  virtual StringRef getName() const = 0;
};

/// Plugin for dynamic control (proc.step, proc.while, proc.if)
class DynamicControlPlugin : public ControlFlowPlugin {
public:
  void initialize(cmt2::ModuleOp module, StateManager &state) override;
  bool handles(mlir::Operation *op) const override;
  bool execute(mlir::Operation *op, OpContext &ctx) override;
  StringRef getName() const override { return "DynamicControl"; }

private:
  llvm::StringMap<ProcStepOp> steps_;
  StateManager *state_ = nullptr;
};

/// Plugin for static control (proc.static_step, proc.static_if, proc.static_repeat)
class StaticControlPlugin : public ControlFlowPlugin {
public:
  void initialize(cmt2::ModuleOp module, StateManager &state) override;
  bool handles(mlir::Operation *op) const override;
  bool execute(mlir::Operation *op, OpContext &ctx) override;
  StringRef getName() const override { return "StaticControl"; }

  //===------------------------------------------------------------------===//
  // Static Step Execution
  //===------------------------------------------------------------------===//

  /// Execute a static step, tracking cycle-by-cycle progress
  bool executeStaticStep(ProcStaticStepOp step, OpContext &ctx);

  /// Execute static if, selecting branch based on condition
  bool executeStaticIf(ProcStaticIfOp ifOp, OpContext &ctx);

  /// Execute static repeat, counting iterations
  bool executeStaticRepeat(ProcStaticRepeatOp repeatOp, OpContext &ctx);

  //===------------------------------------------------------------------===//
  // Timing Validation
  //===------------------------------------------------------------------===//

  /// Validate call timing attributes at runtime
  bool validateCallTiming(CallOp call, unsigned currentCycle);

private:
  llvm::StringMap<ProcStaticStepOp> staticSteps_;
  llvm::StringMap<StaticStepState> stepStates_;
  StateManager *state_ = nullptr;
};

} // namespace circt::cmt2::interp
```

### 4. Scheduler Interface

**Purpose:** Pluggable conflict resolution strategies

**File:** `include/circt/Dialect/Cmt2/Interpreter/Scheduler.h`

```cpp
namespace circt::cmt2::interp {

/// Abstract scheduler interface
class Scheduler {
public:
  virtual ~Scheduler() = default;

  /// Initialize scheduler with module (extracts scheduling constraints)
  virtual void initialize(cmt2::ModuleOp module) = 0;

  /// Select rules to fire from enabled rules
  virtual std::vector<std::string> selectRules(
      const std::vector<std::string> &enabledRules) = 0;

  /// Get scheduler name
  virtual StringRef getName() const = 0;
};

/// Default ORAAT scheduler (one rule at a time)
class ORAATScheduler : public Scheduler {
public:
  void initialize(cmt2::ModuleOp module) override;
  std::vector<std::string> selectRules(
      const std::vector<std::string> &enabledRules) override;
  StringRef getName() const override { return "ORAAT"; }

private:
  llvm::StringMap<unsigned> priorities_;
};

/// Scheduler using scheduling annotations (sequenceBefore, conflictFree)
class AnnotationScheduler : public Scheduler {
public:
  void initialize(cmt2::ModuleOp module) override;
  std::vector<std::string> selectRules(
      const std::vector<std::string> &enabledRules) override;
  StringRef getName() const override { return "Annotation"; }

private:
  // Conflict matrix from scheduling attributes
  llvm::StringMap<llvm::StringSet<>> conflicts_;
  llvm::StringMap<llvm::StringSet<>> sequenceBefore_;
};

} // namespace circt::cmt2::interp
```

### 5. External Module Interpreter (NEW - Critical)

**Purpose:** Provide interpretation logic for external modules (Reg, FIFO, Memory, custom)

**Problem:** External modules (defined in `cmt2.module.extern.firrtl`) have no execution semantics in the interpreter. The current code only pattern-matches "reg" in module names.

**Solution:** Use MLIR software dialects to describe external module behavior. Each external module can have an associated MLIR file that defines its state and method implementations using standard MLIR operations.

#### Design Rationale

Using MLIR for behavioral models has significant advantages:
1. **Reuses existing infrastructure** - MLIR parsing, verification, types
2. **Well-defined semantics** - Standard MLIR operations have clear meanings
3. **Extensible** - Can support more dialects as needed
4. **Debuggable** - MLIR has good tooling for inspection
5. **Familiar syntax** - No need to learn a new DSL

#### MLIR-Based Behavioral Model Format

External module behavior is described using standard MLIR dialects:
- `memref.global` - Define module state (registers, arrays)
- `func.func` - Define method implementations
- `arith`, `scf`, `math` - Computation operations

#### Guard and Body Convention

Each method has **two functions**:
- `@<method>__guard(args...) -> i1` - Returns true if method is ready
- `@<method>__body(args...) -> results` - Executes the method action

This matches CMT2's guard/body semantics for rules and methods.

**Example: Register Module (reg_model.mlir)**

```mlir
// State: a single value stored in the register
memref.global "private" @value : memref<i32> = dense<0>
memref.global "private" @pending_write : memref<i32> = dense<0>
memref.global "private" @has_pending : memref<i1> = dense<false>

//===----------------------------------------------------------------------===//
// Method: read() -> i32
//===----------------------------------------------------------------------===//

// Guard: read is always ready (no pending state affects readability)
func.func @read__guard() -> i1 {
  %true = arith.constant true
  return %true : i1
}

// Body: return current value
func.func @read__body() -> i32 {
  %mem = memref.get_global @value : memref<i32>
  %val = memref.load %mem[] : memref<i32>
  return %val : i32
}

//===----------------------------------------------------------------------===//
// Method: write(data: i32) -> ()
//===----------------------------------------------------------------------===//

// Guard: write is ready when no pending write exists
// (enforces sequenceBefore(read, write) - only one write per cycle)
func.func @write__guard(%data: i32) -> i1 {
  %flag_mem = memref.get_global @has_pending : memref<i1>
  %has_pending = memref.load %flag_mem[] : memref<i1>
  %true = arith.constant true
  %ready = arith.xori %has_pending, %true : i1  // ready = !has_pending
  return %ready : i1
}

// Body: queue write for end of cycle
func.func @write__body(%data: i32) {
  %pending = memref.get_global @pending_write : memref<i32>
  %flag = memref.get_global @has_pending : memref<i1>
  memref.store %data, %pending[] : memref<i32>
  %true = arith.constant true
  memref.store %true, %flag[] : memref<i1>
  return
}

//===----------------------------------------------------------------------===//
// Special lifecycle functions
//===----------------------------------------------------------------------===//

// Called at end of cycle: apply pending writes
func.func @__commit__() {
  %flag_mem = memref.get_global @has_pending : memref<i1>
  %has_pending = memref.load %flag_mem[] : memref<i1>
  scf.if %has_pending {
    %pending = memref.get_global @pending_write : memref<i32>
    %value = memref.get_global @value : memref<i32>
    %data = memref.load %pending[] : memref<i32>
    memref.store %data, %value[] : memref<i32>
    %false = arith.constant false
    memref.store %false, %flag_mem[] : memref<i1>
  }
  return
}

// Called on reset
func.func @__reset__() {
  %value = memref.get_global @value : memref<i32>
  %zero = arith.constant 0 : i32
  memref.store %zero, %value[] : memref<i32>
  %flag = memref.get_global @has_pending : memref<i1>
  %false = arith.constant false
  memref.store %false, %flag[] : memref<i1>
  return
}
```

**Example: FIFO Module (fifo_model.mlir)**

```mlir
// State: circular buffer
memref.global "private" @buffer : memref<8xi32> = dense<0>  // capacity = 8
memref.global "private" @head : memref<index> = dense<0>
memref.global "private" @tail : memref<index> = dense<0>
memref.global "private" @count : memref<index> = dense<0>

// Pending operations
memref.global "private" @pending_enq_data : memref<i32> = dense<0>
memref.global "private" @pending_enq : memref<i1> = dense<false>
memref.global "private" @pending_deq : memref<i1> = dense<false>

//===----------------------------------------------------------------------===//
// Value: notEmpty() -> i1
//===----------------------------------------------------------------------===//

func.func @notEmpty__guard() -> i1 {
  %true = arith.constant true
  return %true : i1
}

func.func @notEmpty__body() -> i1 {
  %count_mem = memref.get_global @count : memref<index>
  %count = memref.load %count_mem[] : memref<index>
  %zero = arith.constant 0 : index
  %result = arith.cmpi ugt, %count, %zero : index
  return %result : i1
}

//===----------------------------------------------------------------------===//
// Value: notFull() -> i1
//===----------------------------------------------------------------------===//

func.func @notFull__guard() -> i1 {
  %true = arith.constant true
  return %true : i1
}

func.func @notFull__body() -> i1 {
  %count_mem = memref.get_global @count : memref<index>
  %count = memref.load %count_mem[] : memref<index>
  %capacity = arith.constant 8 : index
  %result = arith.cmpi ult, %count, %capacity : index
  return %result : i1
}

//===----------------------------------------------------------------------===//
// Value: first() -> i32 (only valid when notEmpty)
//===----------------------------------------------------------------------===//

func.func @first__guard() -> i1 {
  // Guard: FIFO must not be empty
  %count_mem = memref.get_global @count : memref<index>
  %count = memref.load %count_mem[] : memref<index>
  %zero = arith.constant 0 : index
  %not_empty = arith.cmpi ugt, %count, %zero : index
  return %not_empty : i1
}

func.func @first__body() -> i32 {
  %buffer = memref.get_global @buffer : memref<8xi32>
  %head_mem = memref.get_global @head : memref<index>
  %head = memref.load %head_mem[] : memref<index>
  %val = memref.load %buffer[%head] : memref<8xi32>
  return %val : i32
}

//===----------------------------------------------------------------------===//
// Method: enq(data: i32) -> ()
//===----------------------------------------------------------------------===//

func.func @enq__guard(%data: i32) -> i1 {
  // Guard: FIFO must not be full, and no pending enqueue
  %count_mem = memref.get_global @count : memref<index>
  %count = memref.load %count_mem[] : memref<index>
  %capacity = arith.constant 8 : index
  %not_full = arith.cmpi ult, %count, %capacity : index

  %pending_mem = memref.get_global @pending_enq : memref<i1>
  %pending = memref.load %pending_mem[] : memref<i1>
  %true = arith.constant true
  %no_pending = arith.xori %pending, %true : i1

  %ready = arith.andi %not_full, %no_pending : i1
  return %ready : i1
}

func.func @enq__body(%data: i32) {
  %pending_data = memref.get_global @pending_enq_data : memref<i32>
  %pending_flag = memref.get_global @pending_enq : memref<i1>
  memref.store %data, %pending_data[] : memref<i32>
  %true = arith.constant true
  memref.store %true, %pending_flag[] : memref<i1>
  return
}

//===----------------------------------------------------------------------===//
// Method: deq() -> i32
//===----------------------------------------------------------------------===//

func.func @deq__guard() -> i1 {
  // Guard: FIFO must not be empty, and no pending dequeue
  %count_mem = memref.get_global @count : memref<index>
  %count = memref.load %count_mem[] : memref<index>
  %zero = arith.constant 0 : index
  %not_empty = arith.cmpi ugt, %count, %zero : index

  %pending_mem = memref.get_global @pending_deq : memref<i1>
  %pending = memref.load %pending_mem[] : memref<i1>
  %true = arith.constant true
  %no_pending = arith.xori %pending, %true : i1

  %ready = arith.andi %not_empty, %no_pending : i1
  return %ready : i1
}

func.func @deq__body() -> i32 {
  // Read current first value
  %buffer = memref.get_global @buffer : memref<8xi32>
  %head_mem = memref.get_global @head : memref<index>
  %head = memref.load %head_mem[] : memref<index>
  %val = memref.load %buffer[%head] : memref<8xi32>

  // Mark pending dequeue
  %pending_flag = memref.get_global @pending_deq : memref<i1>
  %true = arith.constant true
  memref.store %true, %pending_flag[] : memref<i1>

  return %val : i32
}

//===----------------------------------------------------------------------===//
// Special lifecycle functions
//===----------------------------------------------------------------------===//

func.func @__commit__() {
  // Handle pending enqueue
  %enq_flag_mem = memref.get_global @pending_enq : memref<i1>
  %has_enq = memref.load %enq_flag_mem[] : memref<i1>
  scf.if %has_enq {
    %buffer = memref.get_global @buffer : memref<8xi32>
    %tail_mem = memref.get_global @tail : memref<index>
    %count_mem = memref.get_global @count : memref<index>
    %pending_data = memref.get_global @pending_enq_data : memref<i32>

    %tail = memref.load %tail_mem[] : memref<index>
    %data = memref.load %pending_data[] : memref<i32>
    memref.store %data, %buffer[%tail] : memref<8xi32>

    // Advance tail (circular)
    %one = arith.constant 1 : index
    %capacity = arith.constant 8 : index
    %new_tail = arith.addi %tail, %one : index
    %wrapped_tail = arith.remui %new_tail, %capacity : index
    memref.store %wrapped_tail, %tail_mem[] : memref<index>

    // Increment count
    %count = memref.load %count_mem[] : memref<index>
    %new_count = arith.addi %count, %one : index
    memref.store %new_count, %count_mem[] : memref<index>

    // Clear flag
    %false = arith.constant false
    memref.store %false, %enq_flag_mem[] : memref<i1>
  }

  // Handle pending dequeue
  %deq_flag_mem = memref.get_global @pending_deq : memref<i1>
  %has_deq = memref.load %deq_flag_mem[] : memref<i1>
  scf.if %has_deq {
    %head_mem = memref.get_global @head : memref<index>
    %count_mem = memref.get_global @count : memref<index>

    // Advance head (circular)
    %head = memref.load %head_mem[] : memref<index>
    %one = arith.constant 1 : index
    %capacity = arith.constant 8 : index
    %new_head = arith.addi %head, %one : index
    %wrapped_head = arith.remui %new_head, %capacity : index
    memref.store %wrapped_head, %head_mem[] : memref<index>

    // Decrement count
    %count = memref.load %count_mem[] : memref<index>
    %new_count = arith.subi %count, %one : index
    memref.store %new_count, %count_mem[] : memref<index>

    // Clear flag
    %false = arith.constant false
    memref.store %false, %deq_flag_mem[] : memref<i1>
  }
  return
}

func.func @__reset__() {
  %zero = arith.constant 0 : index
  %false = arith.constant false

  %head = memref.get_global @head : memref<index>
  %tail = memref.get_global @tail : memref<index>
  %count = memref.get_global @count : memref<index>
  memref.store %zero, %head[] : memref<index>
  memref.store %zero, %tail[] : memref<index>
  memref.store %zero, %count[] : memref<index>

  %enq_flag = memref.get_global @pending_enq : memref<i1>
  %deq_flag = memref.get_global @pending_deq : memref<i1>
  memref.store %false, %enq_flag[] : memref<i1>
  memref.store %false, %deq_flag[] : memref<i1>
  return
}
```

**Example: Memory with Read Latency (memory_model.mlir)**

```mlir
// State: memory array
memref.global "private" @data : memref<1024xi32> = dense<0>

// Read pipeline: latency = 2 cycles
memref.global "private" @read_pipeline : memref<2xi32> = dense<0>
memref.global "private" @read_valid : memref<2xi1> = dense<false>

// Pending write
memref.global "private" @pending_write_addr : memref<index> = dense<0>
memref.global "private" @pending_write_data : memref<i32> = dense<0>
memref.global "private" @pending_write : memref<i1> = dense<false>

//===----------------------------------------------------------------------===//
// Method: read(addr: i32) -> i32 {latency = 2}
//===----------------------------------------------------------------------===//

func.func @read__guard(%addr: i32) -> i1 {
  // Read is always ready (pipelined)
  %true = arith.constant true
  return %true : i1
}

func.func @read__body(%addr: i32) -> i32 {
  // Return from end of pipeline (2 cycles ago)
  %pipeline = memref.get_global @read_pipeline : memref<2xi32>
  %last_idx = arith.constant 1 : index
  %result = memref.load %pipeline[%last_idx] : memref<2xi32>

  // Start new read (will be available in 2 cycles)
  %data = memref.get_global @data : memref<1024xi32>
  %addr_idx = arith.index_cast %addr : i32 to index
  %val = memref.load %data[%addr_idx] : memref<1024xi32>

  // Insert into pipeline head
  %zero_idx = arith.constant 0 : index
  memref.store %val, %pipeline[%zero_idx] : memref<2xi32>

  return %result : i32
}

//===----------------------------------------------------------------------===//
// Method: write(addr: i32, data: i32) -> ()
//===----------------------------------------------------------------------===//

func.func @write__guard(%addr: i32, %data: i32) -> i1 {
  // Only one write per cycle
  %pending_mem = memref.get_global @pending_write : memref<i1>
  %pending = memref.load %pending_mem[] : memref<i1>
  %true = arith.constant true
  %ready = arith.xori %pending, %true : i1
  return %ready : i1
}

func.func @write__body(%addr: i32, %data: i32) {
  %addr_mem = memref.get_global @pending_write_addr : memref<index>
  %data_mem = memref.get_global @pending_write_data : memref<i32>
  %flag_mem = memref.get_global @pending_write : memref<i1>

  %addr_idx = arith.index_cast %addr : i32 to index
  memref.store %addr_idx, %addr_mem[] : memref<index>
  memref.store %data, %data_mem[] : memref<i32>
  %true = arith.constant true
  memref.store %true, %flag_mem[] : memref<i1>
  return
}

//===----------------------------------------------------------------------===//
// Special lifecycle functions
//===----------------------------------------------------------------------===//

// Called at start of each cycle: advance read pipeline
func.func @__tick__() {
  %pipeline = memref.get_global @read_pipeline : memref<2xi32>
  // Shift: pipeline[1] = pipeline[0]
  %zero_idx = arith.constant 0 : index
  %one_idx = arith.constant 1 : index
  %val = memref.load %pipeline[%zero_idx] : memref<2xi32>
  memref.store %val, %pipeline[%one_idx] : memref<2xi32>
  return
}

// Called at end of cycle: apply pending write
func.func @__commit__() {
  %flag_mem = memref.get_global @pending_write : memref<i1>
  %has_write = memref.load %flag_mem[] : memref<i1>
  scf.if %has_write {
    %data_arr = memref.get_global @data : memref<1024xi32>
    %addr_mem = memref.get_global @pending_write_addr : memref<index>
    %data_mem = memref.get_global @pending_write_data : memref<i32>

    %addr = memref.load %addr_mem[] : memref<index>
    %data = memref.load %data_mem[] : memref<i32>
    memref.store %data, %data_arr[%addr] : memref<1024xi32>

    %false = arith.constant false
    memref.store %false, %flag_mem[] : memref<i1>
  }
  return
}

func.func @__reset__() {
  // Reset pipeline
  %pipeline = memref.get_global @read_pipeline : memref<2xi32>
  %zero = arith.constant 0 : i32
  %zero_idx = arith.constant 0 : index
  %one_idx = arith.constant 1 : index
  memref.store %zero, %pipeline[%zero_idx] : memref<2xi32>
  memref.store %zero, %pipeline[%one_idx] : memref<2xi32>

  // Clear pending write
  %flag_mem = memref.get_global @pending_write : memref<i1>
  %false = arith.constant false
  memref.store %false, %flag_mem[] : memref<i1>
  return
}
```

#### Module Interpreter Interface

**File:** `include/circt/Dialect/Cmt2/Interpreter/ModuleInterpreter.h`

```cpp
namespace circt::cmt2::interp {

/// Abstract base class for external module interpretation
class ModuleInterpreter {
public:
  virtual ~ModuleInterpreter() = default;

  /// Get the module type this interpreter handles
  virtual StringRef getModuleType() const = 0;

  /// Initialize state for an instance of this module
  virtual void initializeInstance(StringRef instanceName,
                                  ArrayRef<NamedAttribute> params) = 0;

  /// Reset instance to initial state (calls __reset__ if defined)
  virtual void resetInstance(StringRef instanceName) = 0;

  /// Call a method on this module instance
  virtual std::optional<std::vector<InterpValue>> callMethod(
      StringRef instanceName,
      StringRef methodName,
      ArrayRef<InterpValue> args) = 0;

  /// Called each cycle (calls __tick__ if defined)
  virtual void tick(StringRef instanceName) = 0;

  /// Commit pending state changes (calls __commit__ if defined)
  virtual void commitCycle(StringRef instanceName) = 0;

  /// Get state for debugging/inspection
  virtual llvm::json::Value getInstanceState(StringRef instanceName) const = 0;
};

/// MLIR-based module interpreter
/// Loads behavioral model from MLIR file and executes using MLIR interpreter
class MLIRModuleInterpreter : public ModuleInterpreter {
public:
  /// Load behavioral model from MLIR file
  static std::unique_ptr<MLIRModuleInterpreter>
  loadFromFile(StringRef mlirPath, MLIRContext &ctx);

  /// Load behavioral model from MLIR module
  static std::unique_ptr<MLIRModuleInterpreter>
  loadFromModule(mlir::ModuleOp module);

  StringRef getModuleType() const override { return moduleType_; }

  void initializeInstance(StringRef instanceName,
                          ArrayRef<NamedAttribute> params) override;
  void resetInstance(StringRef instanceName) override;
  std::optional<std::vector<InterpValue>> callMethod(
      StringRef instanceName, StringRef methodName,
      ArrayRef<InterpValue> args) override;
  void tick(StringRef instanceName) override;
  void commitCycle(StringRef instanceName) override;
  llvm::json::Value getInstanceState(StringRef instanceName) const override;

private:
  std::string moduleType_;
  mlir::ModuleOp behaviorModel_;

  /// Per-instance state: cloned memref globals
  struct InstanceState {
    llvm::StringMap<std::vector<InterpValue>> memrefs;
  };
  llvm::StringMap<InstanceState> instances_;

  /// MLIR execution engine for running functions
  std::unique_ptr<mlir::ExecutionEngine> engine_;

  /// Helper: Execute a func.func with given arguments
  std::vector<InterpValue> executeFunction(
      StringRef funcName,
      ArrayRef<InterpValue> args,
      InstanceState &state);
};

/// Built-in interpreters (hardcoded for efficiency)
class RegInterpreter : public ModuleInterpreter { /* ... */ };
class FIFOInterpreter : public ModuleInterpreter { /* ... */ };
class MemoryInterpreter : public ModuleInterpreter { /* ... */ };

} // namespace circt::cmt2::interp
```

#### Module Interpreter Registry

```cpp
/// Registry for module interpreters with resolution strategies
class ModuleInterpreterRegistry {
public:
  /// Register default built-in interpreters (Reg, FIFO, Memory)
  void registerBuiltins();

  /// Register a custom interpreter
  void registerInterpreter(std::unique_ptr<ModuleInterpreter> interp);

  //===------------------------------------------------------------------===//
  // Resolution Strategies (Priority Order)
  //===------------------------------------------------------------------===//

  /// Strategy 1: Check for interpreter_model attribute (MLIR file path)
  ///
  /// ```mlir
  /// cmt2.module.extern.firrtl @CustomMult {
  ///   ...
  /// } attributes { interpreter_model = "custom_mult.mlir" }
  /// ```

  /// Strategy 2: Check for interpreter_class attribute (built-in type)
  ///
  /// ```mlir
  /// cmt2.module.extern.firrtl @MyReg {
  ///   ...
  /// } attributes { interpreter_class = "Reg", width = 32 }
  /// ```

  /// Strategy 3: Pattern matching on module name (legacy compatibility)
  /// - "Reg", "register" → RegInterpreter
  /// - "FIFO", "fifo" → FIFOInterpreter
  /// - "Mem", "memory" → MemoryInterpreter

  /// Strategy 4: Fallback configuration file (--module-config)
  /// JSON file mapping module names to MLIR model files

  /// Resolve interpreter for an external module
  ModuleInterpreter* resolveInterpreter(ExtModuleFirrtlOp extModule);

  /// Get list of external modules without interpreters
  std::vector<ExtModuleFirrtlOp> getUnresolvedModules() const;

  /// Load module mappings from JSON configuration
  mlir::LogicalResult loadConfig(StringRef jsonPath);

  /// Generate template MLIR for an unresolved module
  std::string generateTemplateMlir(ExtModuleFirrtlOp extModule) const;

private:
  llvm::StringMap<std::unique_ptr<ModuleInterpreter>> interpreters_;
  std::vector<ExtModuleFirrtlOp> unresolvedModules_;
  MLIRContext *ctx_;
};
```

#### Supported MLIR Operations

The interpreter should support these MLIR operations for behavioral models:

| Dialect | Operations |
|---------|-----------|
| `func` | `func.func`, `func.return`, `func.call` |
| `memref` | `memref.global`, `memref.get_global`, `memref.load`, `memref.store`, `memref.alloc`, `memref.dealloc` |
| `arith` | All arithmetic ops: `addi`, `subi`, `muli`, `divui`, `remui`, `andi`, `ori`, `xori`, `cmpi`, `select`, `constant`, `index_cast`, etc. |
| `scf` | `scf.if`, `scf.for`, `scf.while`, `scf.yield` |
| `math` | Basic math ops if needed |

#### Special Functions

| Function | Called When | Purpose |
|----------|-------------|---------|
| `@__reset__()` | On interpreter reset | Initialize state to reset values |
| `@__tick__()` | Each cycle start | Advance pipelines, timers |
| `@__commit__()` | Each cycle end | Apply pending writes atomically |
| `@__init__(%params...)` | Instance creation | Initialize with parameters |

### 6. ExecutionCore

**Purpose:** Main execution engine using components

**File:** `include/circt/Dialect/Cmt2/Interpreter/ExecutionCore.h`

```cpp
namespace circt::cmt2::interp {

class ExecutionCore {
public:
  ExecutionCore(StateManager &state, OpHandlerRegistry &handlers);

  //===------------------------------------------------------------------===//
  // Plugin Management
  //===------------------------------------------------------------------===//

  void addControlPlugin(std::unique_ptr<ControlFlowPlugin> plugin);
  void setScheduler(std::unique_ptr<Scheduler> scheduler);

  //===------------------------------------------------------------------===//
  // Initialization
  //===------------------------------------------------------------------===//

  mlir::LogicalResult initialize(cmt2::ModuleOp module);

  //===------------------------------------------------------------------===//
  // Execution
  //===------------------------------------------------------------------===//

  /// Execute one cycle with ORAAT semantics
  std::vector<RuleResult> step();

  /// Evaluate a guard region
  bool evaluateGuard(mlir::Region &guardRegion);

  /// Execute a body region
  void executeBody(mlir::Region &bodyRegion);

  /// Execute a single operation
  std::optional<InterpValue> executeOp(mlir::Operation *op);

  //===------------------------------------------------------------------===//
  // Rule Management
  //===------------------------------------------------------------------===//

  std::vector<std::string> evaluateGuards();
  std::vector<std::string> getRuleNames() const;

private:
  cmt2::ModuleOp topModule_;
  StateManager &state_;
  OpHandlerRegistry &handlers_;
  std::vector<std::unique_ptr<ControlFlowPlugin>> controlPlugins_;
  std::unique_ptr<Scheduler> scheduler_;
  llvm::DenseMap<mlir::Value, InterpValue> valueMap_;
};

} // namespace circt::cmt2::interp
```

### 6. Refactored Cmt2Interpreter (Facade)

**Purpose:** Backward-compatible API delegating to components

```cpp
class Cmt2Interpreter {
public:
  explicit Cmt2Interpreter(mlir::ModuleOp module, llvm::raw_ostream &os);

  // Existing public API (unchanged)
  mlir::LogicalResult initialize(llvm::StringRef circuitName);
  void reset();
  std::vector<RuleResult> step();
  // ... all existing methods ...

private:
  // Components (replaces monolithic implementation)
  std::unique_ptr<StateManager> state_;
  std::unique_ptr<OpHandlerRegistry> handlers_;
  std::unique_ptr<ExecutionCore> core_;
  std::unique_ptr<BreakpointManager> breakpoints_;
  std::unique_ptr<TraceManager> traces_;

  mlir::ModuleOp module_;
  llvm::raw_ostream &os_;
};
```

---

## External Module Resolution Flow

When the interpreter encounters a `cmt2.call` to an external module instance, it needs to find the appropriate interpreter. The resolution follows this priority order:

### Resolution Algorithm

```
resolveInterpreter(ExtModuleFirrtlOp extModule):
  1. Check for `interpreter_file` attribute on extModule
     → If found, load JSONModuleInterpreter from file
     → Cache and return

  2. Check for `interpreter_class` attribute on extModule
     → If found, look up built-in interpreter by class name
     → Return built-in interpreter (Reg, FIFO, Memory, etc.)

  3. Pattern match on module name (legacy compatibility)
     → "Reg", "register" → RegInterpreter
     → "FIFO", "fifo" → FIFOInterpreter
     → "Mem", "memory" → MemoryInterpreter

  4. Check fallback JSON config (--module-config option)
     → If module name found in config, use specified interpreter

  5. No interpreter found
     → Add to unresolvedModules_ list
     → At initialization end, report unresolved modules
     → Offer to generate template JSON
```

### Attribute-Based Resolution

```mlir
// Strategy 1: Interpreter file attribute
cmt2.module.extern.firrtl @CustomMultiplier {
  cmt2.bind.method @multiply : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
} attributes {
  interpreter_file = "custom_mult.json"
}

// Strategy 2: Built-in class attribute
cmt2.module.extern.firrtl @MyReg32 {
  cmt2.bind.method @read : () -> !firrtl.uint<32>
  cmt2.bind.method @write : (!firrtl.uint<32>) -> ()
} attributes {
  interpreter_class = "Reg",
  width = 32 : i64
}

// Strategy 3: Name-based (no attribute needed for standard names)
cmt2.module.extern.firrtl @Reg32 { ... }  // Matches "Reg"
cmt2.module.extern.firrtl @FIFO_8x32 { ... }  // Matches "FIFO"
```

### Fallback JSON Configuration

When the interpreter cannot resolve modules automatically, users can provide a JSON configuration file that maps module names to MLIR behavioral model files:

```bash
cmt2-dbg circuit.mlir --module-config=interpreters.json
```

**interpreters.json:**
```json
{
  "modules": {
    "CustomMultiplier": {
      "model": "models/custom_mult.mlir"
    },
    "ExternalDivider": {
      "model": "models/divider.mlir"
    },
    "BlackBoxALU": {
      "class": "Reg",
      "width": 32
    }
  }
}
```

The JSON configuration only contains:
- `model`: Path to MLIR behavioral model file
- `class`: Built-in interpreter class name (Reg, FIFO, Memory)
- Type-specific parameters (width, depth, etc.)

### Template Generation

When unresolved modules are detected, the interpreter can generate template MLIR files:

```
$ cmt2-dbg circuit.mlir
error: 2 external modules have no interpreter:
  - CustomMultiplier (methods: multiply(i32, i32) -> i64)
  - ExternalDivider (methods: divide(i32, i32) -> i32, remainder(i32, i32) -> i32)

hint: Run with --generate-model-templates to create template MLIR files
hint: Or add interpreter_model attribute to the modules in MLIR

$ cmt2-dbg circuit.mlir --generate-model-templates
Generated: CustomMultiplier_model.mlir
Generated: ExternalDivider_model.mlir
```

**Generated CustomMultiplier_model.mlir:**
```mlir
// Behavioral model for CustomMultiplier
// TODO: Implement state and method logic

// State variables (add as needed)
memref.global "private" @state : memref<i64> = dense<0>

// Method: multiply(arg0: i32, arg1: i32) -> i64
func.func @multiply(%arg0: i32, %arg1: i32) -> i64 {
  // TODO: Implement multiplication logic
  %zero = arith.constant 0 : i64
  return %zero : i64
}

// Called at end of each cycle
func.func @__commit__() {
  return
}

// Called on reset
func.func @__reset__() {
  return
}
```

---

## Implementation Plan

### Phase 0: Module Interpreter Infrastructure (NEW - Priority)

**Goal:** Extensible external module interpretation

1. **Create ModuleInterpreter interface:**
   - Abstract base class with initializeInstance, callMethod, commitCycle
   - Test: Interface compiles

2. **Implement built-in interpreters:**
   - RegInterpreter (extract from current code)
   - FIFOInterpreter (enq/deq/first/notEmpty/notFull)
   - MemoryInterpreter (read/write with latency)
   - Test: Each interpreter in isolation

3. **Implement ModuleInterpreterRegistry:**
   - Resolution priority chain
   - Attribute checking (interpreter_file, interpreter_class)
   - Legacy pattern matching
   - Test: Resolution priority tests

4. **Add JSON configuration support:**
   - Load from --module-config
   - Generate template for unresolved modules
   - Test: JSON loading tests

5. **Integrate with ExecutionCore:**
   - Replace hardcoded register detection
   - Route cmt2.call through ModuleInterpreterRegistry
   - Test: Existing tests pass, new FIFO/Memory tests

### Phase 1: Core Infrastructure (No Behavior Change)

**Goal:** Extract components without changing behavior

1. **Create header files:**
   - `include/circt/Dialect/Cmt2/Interpreter/OpHandlerRegistry.h`
   - `include/circt/Dialect/Cmt2/Interpreter/StateManager.h`
   - `include/circt/Dialect/Cmt2/Interpreter/Types.h` (InterpValue, etc.)

2. **Implement StateManager:**
   - Extract register/FSM state from Cmt2Interpreter
   - Add observer pattern (but don't require observers yet)
   - Test: Existing tests should pass

3. **Implement OpHandlerRegistry:**
   - Create registration mechanism
   - Migrate existing ops from executeOp() to handlers
   - Test: Existing tests should pass

4. **Update Cmt2Interpreter:**
   - Use StateManager and OpHandlerRegistry internally
   - Maintain exact same public API
   - Test: All existing tests must pass

### Phase 2: Control Flow Plugins

**Goal:** Modular control flow with static timing support

1. **Create ControlFlowPlugin interface:**
   - DynamicControlPlugin (current proc.step, proc.while behavior)
   - Test: Existing dynamic control tests pass

2. **Implement StaticControlPlugin:**
   - Proper latency tracking for proc.static_step
   - Cycle-accurate FSM for static steps
   - Test: New tests for static step latency

3. **Add StaticStepState to StateManager:**
   - Track latency, current cycle, interval
   - Support pipelined initiation tracking
   - Test: Multi-cycle static step tests

4. **Implement static control ops:**
   - `proc.static_if`: Branch selection with latency matching
   - `proc.static_repeat`: Iteration counting with latency
   - Test: Static control flow tests

### Phase 3: Enhanced Features

**Goal:** New capabilities enabled by modular design

1. **Timing validation at runtime:**
   - Validate `arg_timing`, `result_timing` attributes
   - Report timing violations during interpretation
   - Test: Timing validation tests

2. **Scheduler plugins:**
   - Extract ORAATScheduler
   - Add AnnotationScheduler (uses scheduling attributes)
   - Test: Scheduler selection tests

3. **REPL command registration:**
   - Create CommandRegistry
   - Allow plugins to register new commands
   - Test: Custom command tests

---

## File Structure

```
include/circt/Dialect/Cmt2/Interpreter/
├── Types.h                  # InterpValue, RegisterState, etc.
├── StateManager.h           # State management with observers
├── OpHandlerRegistry.h      # Operation handler registration
├── ModuleInterpreter.h      # External module interpreter interface (NEW)
├── MLIRModuleInterpreter.h  # MLIR-based interpreter (NEW)
├── ControlFlowPlugin.h      # Control flow plugin interface
├── DynamicControlPlugin.h   # Dynamic control implementation
├── StaticControlPlugin.h    # Static control implementation
├── Scheduler.h              # Scheduler interface
├── ExecutionCore.h          # Main execution engine
└── Interpreter.h            # Facade (backward compat)

lib/Dialect/Cmt2/Interpreter/
├── StateManager.cpp
├── OpHandlerRegistry.cpp
├── Handlers/
│   ├── HWHandlers.cpp       # hw:: op handlers
│   ├── CombHandlers.cpp     # comb:: op handlers
│   ├── FIRRTLHandlers.cpp   # firrtl:: op handlers
│   └── CMT2Handlers.cpp     # cmt2:: op handlers
├── ModuleInterpreters/      # External module interpreters (NEW)
│   ├── ModuleInterpreterRegistry.cpp
│   ├── MLIRModuleInterpreter.cpp  # MLIR-based interpreter
│   ├── RegInterpreter.cpp         # Built-in register
│   ├── FIFOInterpreter.cpp        # Built-in FIFO
│   └── MemoryInterpreter.cpp      # Built-in memory
├── models/                  # Built-in MLIR behavioral models (NEW)
│   ├── reg.mlir
│   ├── fifo.mlir
│   └── memory.mlir
├── DynamicControlPlugin.cpp
├── StaticControlPlugin.cpp
├── Schedulers/
│   ├── ORAATScheduler.cpp
│   └── AnnotationScheduler.cpp
├── ExecutionCore.cpp
└── Interpreter.cpp          # Facade implementation

tools/cmt2-dbg/
├── cmt2-dbg.cpp             # CLI (with --module-config option)
├── CommandRegistry.h        # REPL command registration
└── CommandRegistry.cpp
```

---

## Static Step Implementation Detail

The key missing feature is proper static step latency tracking. Here's the detailed design:

### StaticStepState Structure

```cpp
struct StaticStepState {
  StringRef stepName;
  unsigned latency;              // Total latency in cycles
  std::optional<unsigned> interval; // Initiation interval (II)

  // Execution state
  bool active = false;           // Currently running?
  unsigned currentCycle = 0;     // Cycle within step (0 to latency-1)

  // For pipelining (when interval < latency)
  std::vector<unsigned> activeInstances; // Cycle counters for each instance
  uint64_t lastInitiation = 0;   // Global cycle of last initiation

  // Done when currentCycle >= latency
  bool isDone() const { return currentCycle >= latency; }

  // Can initiate new instance when enough cycles since last initiation
  bool canInitiate(uint64_t currentGlobalCycle) const {
    if (!interval) return !active; // Non-pipelined: only when idle
    return currentGlobalCycle - lastInitiation >= *interval;
  }
};
```

### Execution Algorithm

```cpp
bool StaticControlPlugin::executeStaticStep(ProcStaticStepOp step,
                                             OpContext &ctx) {
  StringRef name = step.getSymName();
  auto &state = stepStates_[name];

  // First call: start the step
  if (!state.active) {
    state.active = true;
    state.currentCycle = 0;
    state.lastInitiation = ctx.state.getCycle();
  }

  // Execute operations for current cycle
  // Operations with arg_timing/result_timing are only executed
  // when currentCycle is within their timing window
  executeStepBody(step.getBody(), state.currentCycle, ctx);

  // Advance cycle
  state.currentCycle++;

  // Check completion
  if (state.isDone()) {
    state.active = false;
    state.currentCycle = 0;
    return true; // Done
  }

  return false; // Not done
}

void StaticControlPlugin::executeStepBody(Region &body,
                                          unsigned cycle,
                                          OpContext &ctx) {
  for (Operation &op : body.front()) {
    if (auto call = dyn_cast<CallOp>(op)) {
      // Check if this call should execute at this cycle
      if (auto argTiming = call->getAttrOfType<ArrayAttr>("arg_timing")) {
        // Extract timing windows and check if cycle is in window
        if (!isInTimingWindow(argTiming, cycle))
          continue; // Skip this cycle
      }
    }
    ctx.handlers.execute(&op, ctx);
  }
}
```

### Example: 4-Cycle Multiplier

```mlir
cmt2.proc.static_step @multiply<4> {
  // Cycle 0: load inputs
  %a = cmt2.call @reg_a @read() {result_timing = [#cmt2.timing<[0, 1]>]}
  %b = cmt2.call @reg_b @read() {result_timing = [#cmt2.timing<[0, 1]>]}

  // Cycle 0-3: multiplication happens in external unit
  %result = cmt2.call @mult_unit @multiply(%a, %b) {
    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
    result_timing = [#cmt2.timing<[3, 4]>]  // Result at cycle 3
  }

  // Cycle 3: store result
  cmt2.call @reg_result @write(%result) {arg_timing = [#cmt2.timing<[3, 4]>]}
}
```

**Interpreter behavior:**
- Cycle 0: Execute read ops, start multiply (set args)
- Cycle 1-2: Tick state, multiply unit is working
- Cycle 3: Read multiply result, write to reg_result
- Cycle 4: Step done, FSM returns to idle

---

## Success Criteria

### Functional Requirements

1. All existing tests pass without modification
2. Static steps properly track latency (multi-cycle execution)
3. Static control flow (if, repeat) works correctly
4. Timing attributes are validated at runtime
5. New operations can be added via handler registration

### Non-Functional Requirements

1. No performance regression for simple circuits
2. Clear extension points documented
3. Each component testable in isolation
4. Backward-compatible public API

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Breaking existing tests | High | Phase 1 maintains exact behavior |
| Performance overhead from indirection | Medium | Profile after Phase 1, optimize hot paths |
| Incomplete static timing semantics | Medium | Start with simple cases, iterate |
| Scope creep | Medium | Strict phase boundaries |

---

## Dependencies

- Existing interpreter implementation (`tools/cmt2-dbg/`)
- CMT2 dialect definitions
- LLVM/MLIR infrastructure

---

## References

- Current implementation: `tools/cmt2-dbg/Interpreter.{h,cpp}` (1,646 lines total)
- CMT2 ops: `include/circt/Dialect/Cmt2/Cmt2Ops.td`
- Timing attributes: `include/circt/Dialect/Cmt2/Cmt2Attributes.td`
- Calyx comparison: `docs/Dialects/Cmt2/Cmt2ProcVsCalyx.md`

---

## Appendix: Current Code Analysis

### Operation Coverage (executeOp, lines 789-1088)

| Dialect | Operations | Count |
|---------|-----------|-------|
| cmt2 | CallOp | 1 |
| hw | ConstantOp | 1 |
| comb | AddOp, SubOp, AndOp, OrOp, XorOp, ICmpOp, MuxOp | 7 |
| firrtl | ConstantOp, AddPrimOp, SubPrimOp, BitsPrimOp, DShrPrimOp, ShrPrimOp, ShlPrimOp, PadPrimOp, AndPrimOp, OrPrimOp, XorPrimOp, EQPrimOp, NEQPrimOp, LTPrimOp, LEQPrimOp, GTPrimOp, GEQPrimOp, MuxPrimOp | 18 |
| **Total** | | **27** |

### Missing Operations

- `cmt2.if`, `cmt2.yield` - conditional within rules
- `proc.seq`, `proc.par` - control composition
- `proc.static_if`, `proc.static_repeat` - static control
- Interface ops (handled by preprocessing)

### Data Members (lines 308-355)

| Member | Type | Purpose |
|--------|------|---------|
| `module_` | ModuleOp | MLIR module |
| `circuit_` | CircuitOp | CMT2 circuit |
| `topModule_` | ModuleOp | Top-level module |
| `cycle_` | uint64_t | Current cycle |
| `registers_` | StringMap | Register states |
| `pendingWrites_` | StringMap | Queued writes |
| `valueMap_` | DenseMap | SSA values |
| `breakpoints_` | vector | Breakpoints |
| `traces_` | vector | Trace history |
| `rulePriorities_` | DenseMap | Scheduler cache |
| `procFSMStates_` | StringMap | Procedural FSMs |
| `procSteps_` | StringMap | Step definitions |
| `procStaticSteps_` | StringMap | Static step defs |
| `stepsDoneThisCycle_` | DenseSet | Done signals |
