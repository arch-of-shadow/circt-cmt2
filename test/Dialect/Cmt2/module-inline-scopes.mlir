// RUN: circt-opt %s --cmt2-inline-modules | FileCheck %s
// Shared children and identical names in separate circuit symbol tables.

// CHECK-NOT: cmt2.module @Leaf
// CHECK-NOT: cmt2.module @Wrapper
// CHECK-LABEL: cmt2.module @Left
// CHECK-NOT: cmt2.instance
// CHECK: firrtl.constant 7 : !firrtl.uint<8>
// CHECK: firrtl.constant 7 : !firrtl.uint<8>
// CHECK: firrtl.xor
// CHECK-NOT: cmt2.call
cmt2.circuit {
  cmt2.module @Leaf(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.value @read() -> (!firrtl.uint<8>) {
      cmt2.return
    } {
      %c = firrtl.constant 7 : !firrtl.uint<8>
      cmt2.return %c : !firrtl.uint<8>
    }
  }
  cmt2.module @Wrapper(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.instance @leaf = @Leaf(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.value @read() -> (!firrtl.uint<8>) {
      cmt2.return
    } {
      %v = cmt2.call @leaf @read() : () -> (!firrtl.uint<8>)
      cmt2.return %v : !firrtl.uint<8>
    }
  }
  cmt2.module @Left(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.instance @a = @Wrapper(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.instance @b = @Wrapper(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.value @read() -> (!firrtl.uint<8>) {
      cmt2.return
    } {
      %a = cmt2.call @a @read() : () -> (!firrtl.uint<8>)
      %b = cmt2.call @b @read() : () -> (!firrtl.uint<8>)
      %v = firrtl.xor %a, %b : (!firrtl.uint<8>, !firrtl.uint<8>) -> !firrtl.uint<8>
      cmt2.return %v : !firrtl.uint<8>
    }
  }
}

// CHECK-NOT: cmt2.module @Leaf
// CHECK-NOT: cmt2.module @Wrapper
// CHECK-LABEL: cmt2.module @Right
// CHECK-NOT: cmt2.instance
// CHECK: firrtl.constant 9 : !firrtl.uint<16>
// CHECK: firrtl.constant 9 : !firrtl.uint<16>
// CHECK: firrtl.xor
// CHECK-NOT: cmt2.call
cmt2.circuit {
  cmt2.module @Leaf(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.value @read() -> (!firrtl.uint<16>) {
      cmt2.return
    } {
      %c = firrtl.constant 9 : !firrtl.uint<16>
      cmt2.return %c : !firrtl.uint<16>
    }
  }
  cmt2.module @Wrapper(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.instance @leaf = @Leaf(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.value @read() -> (!firrtl.uint<16>) {
      cmt2.return
    } {
      %v = cmt2.call @leaf @read() : () -> (!firrtl.uint<16>)
      cmt2.return %v : !firrtl.uint<16>
    }
  }
  cmt2.module @Right(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.instance @a = @Wrapper(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.instance @b = @Wrapper(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.value @read() -> (!firrtl.uint<16>) {
      cmt2.return
    } {
      %a = cmt2.call @a @read() : () -> (!firrtl.uint<16>)
      %b = cmt2.call @b @read() : () -> (!firrtl.uint<16>)
      %v = firrtl.xor %a, %b : (!firrtl.uint<16>, !firrtl.uint<16>) -> !firrtl.uint<16>
      cmt2.return %v : !firrtl.uint<16>
    }
  }
}
