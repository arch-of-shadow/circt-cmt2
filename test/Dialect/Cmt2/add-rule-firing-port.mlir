// RUN: circt-opt %s --cmt2-add-rule-firing-port | FileCheck %s

// Test that the AddRuleFiringPort pass adds debug.firing_port attributes to rules
// and debug.firing_ports to modules.

// CHECK-LABEL: cmt2.circuit
builtin.module {
  cmt2.circuit {
    // CHECK: cmt2.module @Top
    // CHECK: cmt2.rule @rule1
    // CHECK-SAME: {debug.firing_port = "dbg_rule1_firing"}
    // CHECK: cmt2.rule @rule2
    // CHECK-SAME: {debug.firing_port = "dbg_rule2_firing"}
    // CHECK: {debug.firing_ports = ["dbg_rule1_firing", "dbg_rule2_firing"]}
    cmt2.module @Top(%clock: !firrtl.clock, %reset: !firrtl.reset) {
      cmt2.rule @rule1() -> () {
        %true = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %true : !firrtl.uint<1>
      } {
        cmt2.return
      }

      cmt2.rule @rule2() -> () {
        %true = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %true : !firrtl.uint<1>
      } {
        cmt2.return
      }
    }
  }
}
