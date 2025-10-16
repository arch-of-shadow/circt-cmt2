// RUN: circt-opt --lower-cmt2-to-hw %s | FileCheck %s

// CHECK-LABEL: firrtl.circuit @SimpleModule
cmt2.circuit {
  cmt2.module @SimpleModule() {
    // CHECK: firrtl.module @SimpleModule

    // A simple rule with a guard and body
    cmt2.rule @myRule() -> !firrtl.uint<1> {
      %true = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %true : !firrtl.uint<1>
    } {
      // Body - empty for now
      cmt2.return
    }
  }
}
