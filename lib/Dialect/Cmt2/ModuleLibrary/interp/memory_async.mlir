// Behavioral model for Memory module (async read, write latency = 1)
// Used by cmt2-dbg interpreter for simulation
//
// This model is for Mem1r1w0c - asynchronous memory with:
//   - Read latency = 0 (combinational)
//   - Write latency = 1 (registered)
//
// Methods:
//   - read(addr: i32) -> i32: Read from address (combinational)
//   - write(addr: i32, data: i32): Write to address (applied at cycle end)

// State: memory array (1024 words x 32 bits)
memref.global "private" @data : memref<1024xi32> = dense<0>

// Pending write
memref.global "private" @pending_write_addr : memref<index> = dense<0>
memref.global "private" @pending_write_data : memref<i32> = dense<0>
memref.global "private" @pending_write : memref<i1> = dense<false>

//===----------------------------------------------------------------------===//
// Method: read(addr: i32) -> i32 (async - no latency)
//===----------------------------------------------------------------------===//

func.func @read__guard(%addr: i32) -> i1 {
  // Async read is always ready
  %true = arith.constant true
  return %true : i1
}

func.func @read__body(%addr: i32) -> i32 {
  %data = memref.get_global @data : memref<1024xi32>
  %addr_idx = arith.index_cast %addr : i32 to index
  %val = memref.load %data[%addr_idx] : memref<1024xi32>
  return %val : i32
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
  // Clear pending write flag (memory contents preserved)
  %flag_mem = memref.get_global @pending_write : memref<i1>
  %false = arith.constant false
  memref.store %false, %flag_mem[] : memref<i1>
  return
}
