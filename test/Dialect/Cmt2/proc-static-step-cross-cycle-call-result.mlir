// RUN: not circt-opt %s -cmt2-timing-inference -cmt2-static-fsm-allocation -cmt2-compile-static -cmt2-tdcc -cmt2-proc-stmt-to-action 2>&1 | FileCheck %s

// This test checks that ProcStmtToAction rejects cross-cycle SSA dependencies
// inside proc.static_step bodies.
//
// The result of the @read call is an SSA value produced in an earlier cycle,
// but it is consumed by a @write call scheduled at local cycle 3 via arg_timing.
// This must be carried across cycles via explicit state (e.g. a Reg), not SSA.

builtin.module {
  firrtl.circuit "TestReg" {
    firrtl.module @TestReg(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
                           in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                           out %writeReady: !firrtl.uint<1>, out %readReady: !firrtl.uint<1>,
                           out %read: !firrtl.uint<32>) {
      %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
      %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %writeReady, %c0_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
      firrtl.connect %readReady, %c0_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
      firrtl.connect %read, %c0_ui32 : !firrtl.uint<32>, !firrtl.uint<32>
    }
  }

  cmt2.circuit {
    cmt2.module.extern.firrtl @reg : @TestReg(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ ready = "readReady", arguments = [], results = ["read"]]
      cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
        enable = "writeEnable", ready = "writeReady", arguments = ["write"], results = []
      ]
    } {
      conflict = [[@write, @write]],
      conflictFree = [[@read, @read]],
      sequenceBefore = [[@read, @write]]
    }

    cmt2.module @Top(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @reg_a = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
      cmt2.instance @reg_b = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

      // Producer result is available only in cycle 0, but consumed at cycle 3.
      cmt2.proc.static_step @bad_step <6> {
        %x = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
        cmt2.call @reg_b @write(%x) {arg_timing = [#cmt2.timing<[3, 4]>]} : (!firrtl.uint<32>) -> ()
      }

      cmt2.proc.rule @run() -> () {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } control {
        cmt2.proc.enable @bad_step
      }
    }
  }
}

// CHECK: error: cross-cycle dependency on call result
// CHECK: hint:
