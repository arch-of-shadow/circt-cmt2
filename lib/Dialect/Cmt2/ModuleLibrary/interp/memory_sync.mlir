// Behavioral model for Memory module (sync read/write, latency = 1)
// Used by cmt2-dbg interpreter for simulation
//
// This model is for Mem1r1w1c - synchronous memory with:
//   - Read latency = 1 (registered)
//   - Write latency = 1 (registered)
//
// Split-phase read:
//   - rd0(addr: i32): Initiate read at address
//   - rd1() -> i32: Get read result (available next cycle)
//
// Methods:
//   - rd0(addr: i32): Start read from address
//   - rd1() -> i32: Get previous read result
//   - write(addr: i32, data: i32): Write to address (applied at cycle end)

// State: memory array (1024 words x 32 bits)
memref.global "private" @data : memref<1024xi32> = dense<0>

// Read pipeline (latency = 1)
memref.global "private" @read_result : memref<i32> = dense<0>
memref.global "private" @read_valid : memref<i1> = dense<false>
memref.global "private" @pending_read_addr : memref<index> = dense<0>
memref.global "private" @pending_read : memref<i1> = dense<false>

// Pending write
memref.global "private" @pending_write_addr : memref<index> = dense<0>
memref.global "private" @pending_write_data : memref<i32> = dense<0>
memref.global "private" @pending_write : memref<i1> = dense<false>

//===----------------------------------------------------------------------===//
// Method: rd0(addr: i32) -> () - initiate read
//===----------------------------------------------------------------------===//

func.func @rd0__guard(%addr: i32) -> i1 {
  // Only one read per cycle
  %pending_mem = memref.get_global @pending_read : memref<i1>
  %pending = memref.load %pending_mem[] : memref<i1>
  %true = arith.constant true
  %ready = arith.xori %pending, %true : i1
  return %ready : i1
}

func.func @rd0__body(%addr: i32) {
  %addr_mem = memref.get_global @pending_read_addr : memref<index>
  %flag_mem = memref.get_global @pending_read : memref<i1>

  %addr_idx = arith.index_cast %addr : i32 to index
  memref.store %addr_idx, %addr_mem[] : memref<index>
  %true = arith.constant true
  memref.store %true, %flag_mem[] : memref<i1>
  return
}

//===----------------------------------------------------------------------===//
// Value: rd1() -> i32 - get read result
//===----------------------------------------------------------------------===//

func.func @rd1__guard() -> i1 {
  // Result is available when read_valid is true
  %valid_mem = memref.get_global @read_valid : memref<i1>
  %valid = memref.load %valid_mem[] : memref<i1>
  return %valid : i1
}

func.func @rd1__body() -> i32 {
  %result_mem = memref.get_global @read_result : memref<i32>
  %result = memref.load %result_mem[] : memref<i32>
  return %result : i32
}

//===----------------------------------------------------------------------===//
// Method: write(addr: i32, data: i32) -> ()
//===----------------------------------------------------------------------===//

func.func @write__guard(%addr: i32, %wdata: i32) -> i1 {
  // Only one write per cycle
  %pending_mem = memref.get_global @pending_write : memref<i1>
  %pending = memref.load %pending_mem[] : memref<i1>
  %true = arith.constant true
  %ready = arith.xori %pending, %true : i1
  return %ready : i1
}

func.func @write__body(%addr: i32, %wdata: i32) {
  %addr_mem = memref.get_global @pending_write_addr : memref<index>
  %data_mem = memref.get_global @pending_write_data : memref<i32>
  %flag_mem = memref.get_global @pending_write : memref<i1>

  %addr_idx = arith.index_cast %addr : i32 to index
  memref.store %addr_idx, %addr_mem[] : memref<index>
  memref.store %wdata, %data_mem[] : memref<i32>
  %true = arith.constant true
  memref.store %true, %flag_mem[] : memref<i1>
  return
}

//===----------------------------------------------------------------------===//
// Special lifecycle functions
//===----------------------------------------------------------------------===//

// Called at start of cycle: advance read pipeline
func.func @__tick__() {
  // Process pending read -> make result available
  %pending_mem = memref.get_global @pending_read : memref<i1>
  %has_pending = memref.load %pending_mem[] : memref<i1>
  scf.if %has_pending {
    %data_arr = memref.get_global @data : memref<1024xi32>
    %addr_mem = memref.get_global @pending_read_addr : memref<index>
    %result_mem = memref.get_global @read_result : memref<i32>
    %valid_mem = memref.get_global @read_valid : memref<i1>

    %addr = memref.load %addr_mem[] : memref<index>
    %val = memref.load %data_arr[%addr] : memref<1024xi32>
    memref.store %val, %result_mem[] : memref<i32>
    %true = arith.constant true
    memref.store %true, %valid_mem[] : memref<i1>

    // Clear pending read flag
    %false = arith.constant false
    memref.store %false, %pending_mem[] : memref<i1>
  } else {
    // No pending read -> invalidate result
    %valid_mem = memref.get_global @read_valid : memref<i1>
    %false = arith.constant false
    memref.store %false, %valid_mem[] : memref<i1>
  }
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
  %false = arith.constant false

  // Clear all pending flags
  %pending_read = memref.get_global @pending_read : memref<i1>
  %pending_write = memref.get_global @pending_write : memref<i1>
  %read_valid = memref.get_global @read_valid : memref<i1>
  memref.store %false, %pending_read[] : memref<i1>
  memref.store %false, %pending_write[] : memref<i1>
  memref.store %false, %read_valid[] : memref<i1>
  return
}
