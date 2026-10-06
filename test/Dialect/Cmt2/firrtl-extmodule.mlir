// RUN: circt-opt %s --lower-cmt2-to-firrtl | FileCheck %s
// CMT2 externs can bind Verilog-backed FExtModuleOp, not just FModuleOp.
// CHECK: firrtl.extmodule @TransactionalFP
// CHECK: firrtl.module @top
// CHECK: firrtl.instance fp @TransactionalFP

module {
  firrtl.circuit "TransactionalFP" {
    firrtl.extmodule @TransactionalFP(
      in clock: !firrtl.clock,
      in reset: !firrtl.uint<1>,
      in issue_enable: !firrtl.uint<1>,
      out issue_ready: !firrtl.uint<1>,
      in operand0: !firrtl.uint<32>,
      in operand1: !firrtl.uint<32>,
      in collect_enable: !firrtl.uint<1>,
      out collect_ready: !firrtl.uint<1>,
      out collect_data: !firrtl.uint<32>,
      out collect_flags: !firrtl.uint<5>
    ) attributes {defname = "TransactionalFP"}
  }
  cmt2.circuit {
    cmt2.module.extern.firrtl @FP : @TransactionalFP(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.method @issue : (!firrtl.uint<32>, !firrtl.uint<32>) -> () [enable = "issue_enable", ready = "issue_ready", arguments = ["operand0", "operand1"], results = []]
      cmt2.bind.method @collect : () -> (!firrtl.uint<32>, !firrtl.uint<5>) [enable = "collect_enable", ready = "collect_ready", arguments = [], results = ["collect_data", "collect_flags"]]
    } {conflict = [[@issue, @issue], [@collect, @collect]], conflictFree = [[@issue, @collect]]}
    cmt2.module @top(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @fp = @FP(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
      cmt2.method @issue(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> () {
        cmt2.return
      } {
        cmt2.call @fp @issue(%a, %b) : (!firrtl.uint<32>, !firrtl.uint<32>) -> ()
        cmt2.return
      }
      cmt2.method @collect() -> (!firrtl.uint<32>, !firrtl.uint<5>) {
        cmt2.return
      } {
        %response:2 = cmt2.call @fp @collect() : () -> (!firrtl.uint<32>, !firrtl.uint<5>)
        cmt2.return %response#0, %response#1 : !firrtl.uint<32>, !firrtl.uint<5>
      }
    }
  }
}
