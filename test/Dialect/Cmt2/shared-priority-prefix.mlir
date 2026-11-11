// RUN: circt-opt %s --lower-cmt2-to-firrtl | FileCheck %s
// Different guards must not prevent sharing the common arbitration prefix.
// Otherwise N competing methods produce quadratically many AND nodes.
cmt2.circuit {
  cmt2.module.extern.firrtl @Sink : @Sink {
    cmt2.bind.method @write : (!firrtl.uint<1>) -> () [
      enable = "write_enable", ready = "write_ready",
      arguments = ["data"], results = []
    ]
  } {conflict = [[@write, @write]]}
  cmt2.module @Top(%clk: !firrtl.clock) {
    cmt2.instance @sink = @Sink
    cmt2.method @a(%ga: !firrtl.uint<1>) {
      cmt2.return %ga : !firrtl.uint<1>
    } {
      cmt2.call @sink @write(%ga) : (!firrtl.uint<1>) -> ()
      cmt2.return
    }
    cmt2.method @b(%gb: !firrtl.uint<1>) {
      cmt2.return %gb : !firrtl.uint<1>
    } {
      cmt2.call @sink @write(%gb) : (!firrtl.uint<1>) -> ()
      cmt2.return
    }
    cmt2.method @c(%gc: !firrtl.uint<1>) {
      cmt2.return %gc : !firrtl.uint<1>
    } {
      cmt2.call @sink @write(%gc) : (!firrtl.uint<1>) -> ()
      cmt2.return
    }
  }
}

// CHECK: firrtl.module @Top
// CHECK: %[[A_PRIVATE:.*]] = firrtl.and %a_ga, %sink_write_ready
// CHECK: %[[A_FIRE:.*]] = firrtl.and %[[A_PRIVATE]], %a_enable
// CHECK: %[[B_PRIVATE:.*]] = firrtl.and %b_gb, %sink_write_ready
// CHECK: %[[ONE:.*]] = firrtl.constant 1 : !firrtl.uint<1>
// CHECK: %[[A_NOT:.*]] = firrtl.xor %[[A_FIRE]], %[[ONE]]
// CHECK: %[[B_READY:.*]] = firrtl.and %[[B_PRIVATE]], %[[A_NOT]]
// CHECK: %[[B_FIRE:.*]] = firrtl.and %[[B_READY]], %b_enable
// CHECK: %[[C_PRIVATE:.*]] = firrtl.and %c_gc, %sink_write_ready
// CHECK: %[[B_NOT:.*]] = firrtl.xor %[[B_FIRE]], %{{.*}}
// CHECK: %[[AB_PREFIX:.*]] = firrtl.and %[[A_NOT]], %[[B_NOT]]
// CHECK: firrtl.and %[[C_PRIVATE]], %[[AB_PREFIX]]
