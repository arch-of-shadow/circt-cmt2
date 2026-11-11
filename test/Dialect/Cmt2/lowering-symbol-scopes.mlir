// RUN: circt-opt %s --lower-cmt2-to-firrtl | FileCheck %s
// RUN: circt-opt %s --lower-cmt2-to-firrtl --lower-cmt2-to-firrtl | FileCheck %s
// Identical function and instance names must be resolved in their source scope,
// including repeated calls in guards and bodies. A second lowering must not use
// symbol-table entries from the erased CMT2 circuit.

cmt2.circuit {
  cmt2.module @Leaf8(%clk: !firrtl.clock) {
    cmt2.value @read() -> (!firrtl.uint<8>) {
      cmt2.return
    } {
      %v = firrtl.constant 8 : !firrtl.uint<8>
      cmt2.return %v : !firrtl.uint<8>
    }
  }
  cmt2.module @Leaf16(%clk: !firrtl.clock) {
    cmt2.value @read() -> (!firrtl.uint<16>) {
      cmt2.return
    } {
      %v = firrtl.constant 16 : !firrtl.uint<16>
      cmt2.return %v : !firrtl.uint<16>
    }
  }
  cmt2.module @Wrap8(%clk: !firrtl.clock) {
    cmt2.instance @cell = @Leaf8 (%clk) : !firrtl.clock
    cmt2.value @read() -> (!firrtl.uint<8>) {
      %v = cmt2.call @cell @read () : () -> (!firrtl.uint<8>)
      %ok = firrtl.bits %v 0 to 0 : (!firrtl.uint<8>) -> !firrtl.uint<1>
      cmt2.return %ok : !firrtl.uint<1>
    } {
      %v = cmt2.call @cell @read () : () -> (!firrtl.uint<8>)
      cmt2.return %v : !firrtl.uint<8>
    }
  }
  cmt2.module @Wrap16(%clk: !firrtl.clock) {
    cmt2.instance @cell = @Leaf16 (%clk) : !firrtl.clock
    cmt2.value @read() -> (!firrtl.uint<16>) {
      cmt2.return
    } {
      %v = cmt2.call @cell @read () : () -> (!firrtl.uint<16>)
      cmt2.return %v : !firrtl.uint<16>
    }
  }
  cmt2.module @Top(%clk: !firrtl.clock) {
    cmt2.instance @a = @Wrap8 (%clk) : !firrtl.clock
    cmt2.instance @b = @Wrap16 (%clk) : !firrtl.clock
    cmt2.value @read() -> (!firrtl.uint<8>, !firrtl.uint<16>) {
      cmt2.return
    } {
      %a = cmt2.call @a @read () : () -> (!firrtl.uint<8>)
      %b = cmt2.call @b @read () : () -> (!firrtl.uint<16>)
      cmt2.return %a, %b : !firrtl.uint<8>, !firrtl.uint<16>
    }
  }
}

// CHECK: firrtl.circuit "Top"
// CHECK: firrtl.module @Leaf8
// CHECK: firrtl.module @Wrap8
// CHECK: firrtl.instance cell @Leaf8({{.*}}out read_res0: !firrtl.uint<8>)
// CHECK: firrtl.bits %cell_read_res0
// CHECK: firrtl.and {{.*}}%cell_read_ready
// CHECK: firrtl.and {{.*}}%cell_read_ready
// CHECK: firrtl.connect {{.*}}, %cell_read_res0 : !firrtl.uint<8>
// CHECK: firrtl.module @Leaf16
// CHECK: firrtl.module @Wrap16
// CHECK: firrtl.instance cell @Leaf16({{.*}}out read_res0: !firrtl.uint<16>)
// CHECK: firrtl.connect {{.*}}, %cell_read_res0 : !firrtl.uint<16>
// CHECK: firrtl.module @Top
// CHECK: firrtl.instance a @Wrap8
// CHECK: firrtl.instance b @Wrap16
// CHECK: firrtl.and
// CHECK: firrtl.when
// CHECK-NOT: cmt2.call
