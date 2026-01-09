// Behavioral model for Register module
// Used by cmt2-dbg interpreter for simulation
//
// State: single value stored in register
// Methods:
//   - read() -> i32: Returns current value (always ready)
//   - write(data: i32): Queues value for update at cycle end

// State: register value
memref.global "private" @value : memref<i32> = dense<0>

// Pending write staging
memref.global "private" @pending_write : memref<i32> = dense<0>
memref.global "private" @has_pending : memref<i1> = dense<false>

//===----------------------------------------------------------------------===//
// Method: read() -> i32
//===----------------------------------------------------------------------===//

// Guard: read is always ready
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

// Guard: write is ready when no pending write (one write per cycle)
func.func @write__guard(%data: i32) -> i1 {
  %flag_mem = memref.get_global @has_pending : memref<i1>
  %has_pending = memref.load %flag_mem[] : memref<i1>
  %true = arith.constant true
  %ready = arith.xori %has_pending, %true : i1
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
