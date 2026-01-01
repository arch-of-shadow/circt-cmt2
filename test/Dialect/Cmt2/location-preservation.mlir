// RUN: circt-opt %s -cmt2-proc-stmt-to-action 2>&1 | FileCheck %s --check-prefix=PROC
// RUN: circt-opt %s -cmt2-compile-invoke -cmt2-tdcc -cmt2-proc-stmt-to-action -cmt2-proc-to-gaa 2>&1 | FileCheck %s --check-prefix=GAA
// RUN: circt-opt %s -cmt2-compile-invoke -cmt2-tdcc -cmt2-proc-stmt-to-action -cmt2-proc-to-gaa -lower-cmt2-to-firrtl 2>&1 | FileCheck %s --check-prefix=FIRRTL

// Test that operations with source locations pass through the CMT2 pipeline correctly.
// Locations are embedded in operations and preserved through transformations.

module {
  cmt2.circuit {
    cmt2.module.extern.firrtl @Reg32 : @Reg32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clk : !firrtl.clock
      cmt2.bind.bare %rst, @rst : !firrtl.uint<1>
      cmt2.bind.value @read : () -> !firrtl.uint<32>[ arguments = [], results = ["res0"]]
      cmt2.bind.method @write : (!firrtl.uint<32>) -> ()[ arguments = ["data"], results = []]
    } {conflict = [["write", "write"]], conflictFree = [["read", "read"]], sequenceBefore = [["read", "write"]]}

    // Test location on module - should propagate to FIRRTL module
    cmt2.module @LocationTest(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @counter = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

      // Test location on step - should propagate to generated rules
      cmt2.proc.step @incr_step {
        %0 = cmt2.call @counter @read() : () -> !firrtl.uint<32> loc("test.py":10:5)
        %c1_ui32 = firrtl.constant 1 : !firrtl.uint<32> loc("test.py":11:5)
        %1 = firrtl.add %0, %c1_ui32 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33> loc("test.py":12:5)
        %2 = firrtl.bits %1 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32> loc("test.py":13:5)
        cmt2.call @counter @write(%2) : (!firrtl.uint<32>) -> () loc("test.py":14:5)
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.proc.step_done %c1_ui1 : !firrtl.uint<1>
      } loc("test.py":9:3)

      // Test location on proc.rule - should propagate to generated GAA rule
      cmt2.proc.rule @incr_loop () -> () {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } control {
        cmt2.proc.enable @incr_step
        cmt2.proc.control_end
      } loc("test.py":20:3)

      // Test location on regular rule - should propagate to FIRRTL
      cmt2.rule @reset_rule () -> () {
        %0 = cmt2.call @counter @read() : () -> !firrtl.uint<32> loc("test.py":25:7)
        %c10_ui32 = firrtl.constant 10 : !firrtl.uint<32>
        %1 = firrtl.eq %0, %c10_ui32 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1> loc("test.py":26:7)
        cmt2.return %1 : !firrtl.uint<1>
      } {
        %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32> loc("test.py":28:7)
        cmt2.call @counter @write(%c0_ui32) : (!firrtl.uint<32>) -> () loc("test.py":29:7)
        cmt2.return
      } loc("test.py":24:3)

    } loc("test.py":5:1)
  }
}

// PROC: cmt2.proc.step @incr_step
// PROC: cmt2.rule @reset_rule

// GAA: cmt2.rule

// FIRRTL: firrtl.circuit "LocationTest"
// FIRRTL: firrtl.module @LocationTest
