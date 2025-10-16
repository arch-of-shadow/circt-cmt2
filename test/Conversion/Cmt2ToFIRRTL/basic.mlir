// RUN: circt-opt --lower-cmt2-to-hw %s | FileCheck %s

// CHECK-LABEL: firrtl.circuit @test
cmt2.circuit {
  cmt2.module @test() {
    // Simple rule that always fires
    cmt2.rule @always_on() -> !firrtl.uint<1> {
      %true = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %true : !firrtl.uint<1>
    } {
      cmt2.return
    }
  }
}
