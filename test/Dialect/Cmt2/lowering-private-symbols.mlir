// RUN: circt-opt %s --cmt2-inline-private-funcs --cmt2-verify-private-funcs-inlined --lower-cmt2-to-firrtl | FileCheck %s
// Lowering builds its source index only after private @this functions have
// been inlined/erased; it must not retain analysis-time symbol-table entries.
cmt2.circuit {
  cmt2.module @Top(%clk: !firrtl.clock) {
    cmt2.value @helper() -> (!firrtl.uint<8>) {
      cmt2.return
    } {
      %v = firrtl.constant 42 : !firrtl.uint<8>
      cmt2.return %v : !firrtl.uint<8>
    }
    cmt2.value @read() -> (!firrtl.uint<8>) {
      cmt2.return
    } {
      %v = cmt2.call @this @helper () : () -> (!firrtl.uint<8>)
      cmt2.return %v : !firrtl.uint<8>
    }
  }
}
// CHECK: firrtl.module @Top
// CHECK-NOT: helper
// CHECK: firrtl.constant 42 : !firrtl.uint<8>
// CHECK-NOT: cmt2.call
