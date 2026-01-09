// Behavioral model for Wire module (combinational, no latency)
// Used by cmt2-dbg interpreter for simulation
//
// Wire is a combinational element with immediate read/write:
//   - Read latency = 0 (combinational)
//   - Write latency = 0 (combinational, immediately visible)
//
// Methods:
//   - read() -> i32: Read current value
//   - write(data: i32): Write value (immediately visible to subsequent reads)

// State: current value
memref.global "private" @data : memref<i32> = dense<0>

//===----------------------------------------------------------------------===//
// Value: read() -> i32
//===----------------------------------------------------------------------===//

func.func @read__guard() -> i1 {
  // Read is always ready
  %true = arith.constant true
  return %true : i1
}

func.func @read__body() -> i32 {
  %data_mem = memref.get_global @data : memref<i32>
  %val = memref.load %data_mem[] : memref<i32>
  return %val : i32
}

//===----------------------------------------------------------------------===//
// Method: write(data: i32) -> ()
//===----------------------------------------------------------------------===//

func.func @write__guard(%wdata: i32) -> i1 {
  // Write is always ready
  %true = arith.constant true
  return %true : i1
}

func.func @write__body(%wdata: i32) {
  %data_mem = memref.get_global @data : memref<i32>
  memref.store %wdata, %data_mem[] : memref<i32>
  return
}

//===----------------------------------------------------------------------===//
// Special lifecycle functions
//===----------------------------------------------------------------------===//

// Called at end of cycle: nothing to do for wire (no pending state)
func.func @__commit__() {
  return
}

func.func @__reset__() {
  // Reset value to 0
  %data_mem = memref.get_global @data : memref<i32>
  %zero = arith.constant 0 : i32
  memref.store %zero, %data_mem[] : memref<i32>
  return
}
