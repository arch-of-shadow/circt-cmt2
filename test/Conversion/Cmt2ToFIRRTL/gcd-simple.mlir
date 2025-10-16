// RUN: circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-hw %s | FileCheck %s

// Simplified GCD example without interfaces and external modules
// CHECK-LABEL: firrtl.circuit @gcd
cmt2.circuit {
  cmt2.module @gcd(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    // CHECK: firrtl.module @gcd

    // Simple rule that fires when a condition is met
    cmt2.rule @swap() -> !firrtl.uint<1> {
      // Guard: check if x > y (simplified - using constant)
      %true = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %true : !firrtl.uint<1>
    } {
      // Body: would swap x and y (simplified for now)
      cmt2.return
    }

    cmt2.rule @sub() -> !firrtl.uint<1> {
      // Guard: check if x <= y (simplified - using constant)
      %false = firrtl.constant 0 : !firrtl.uint<1>
      cmt2.return %false : !firrtl.uint<1>
    } {
      // Body: would subtract y from x (simplified for now)
      cmt2.return
    }
  }
}
