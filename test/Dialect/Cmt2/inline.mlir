// RUN: circt-opt %s -cmt2-inline-modules | FileCheck %s

// Test module inlining with synthesis attribute

cmt2.circuit {
  // Module with synthesis=true should NOT be inlined
  // CHECK-LABEL: cmt2.module @SynthModule
  cmt2.module @SynthModule(%clk: i1, %rst: i1) attributes {synthesis = true} {
    cmt2.method @store(%data: i32) -> () {
      %c1_i1 = hw.constant 1 : i1
      cmt2.return %c1_i1 : i1
    } {
      cmt2.return
    }

    cmt2.value @load() -> (i32) {
      %c1_i1 = hw.constant 1 : i1
      cmt2.return %c1_i1 : i1
    } {
      %c0_i32 = hw.constant 0 : i32
      cmt2.return %c0_i32 : i32
    }
  }

  // Simple leaf module - SHOULD be inlined
  cmt2.module @LeafModule(%clk: i1, %rst: i1) {
    cmt2.instance @storage = @SynthModule (%clk, %rst) : i1, i1

    cmt2.method @increment(%data: i32) -> () {
      %c1_i1 = hw.constant 1 : i1
      cmt2.return %c1_i1 : i1
    } {
      // Add one to the data
      %c1_i32 = hw.constant 1 : i32
      %sum = comb.add %data, %c1_i32 : i32
      cmt2.call @storage @store(%sum) : (i32) -> ()
      cmt2.return
    }

    cmt2.value @get() -> (i32) {
      %c1_i1 = hw.constant 1 : i1
      cmt2.return %c1_i1 : i1
    } {
      %0 = cmt2.call @storage @load() : () -> (i32)
      cmt2.return %0 : i32
    }
  }

  // Middle module that uses LeafModule - SHOULD be inlined
  cmt2.module @MiddleModule(%clk: i1, %rst: i1) {
    cmt2.instance @leaf = @LeafModule (%clk, %rst) : i1, i1

    cmt2.method @doWork(%val: i32) -> () {
      %c1_i1 = hw.constant 1 : i1
      cmt2.return %c1_i1 : i1
    } {
      cmt2.call @leaf @increment(%val) : (i32) -> ()
      cmt2.return
    }

    cmt2.value @result() -> (i32) {
      %c1_i1 = hw.constant 1 : i1
      cmt2.return %c1_i1 : i1
    } {
      %0 = cmt2.call @leaf @get() : () -> (i32)
      cmt2.return %0 : i32
    }
  }

  // Top module - should NOT be inlined (top-level/no uses)
  // CHECK-LABEL: cmt2.module @TopModule
  cmt2.module @TopModule(%clk: i1, %rst: i1) {
    // CHECK: cmt2.instance @synth
    cmt2.instance @synth = @SynthModule (%clk, %rst) : i1, i1
    // CHECK: cmt2.instance @mid.leaf.storage
    // CHECK-NOT: cmt2.instance @mid =
    cmt2.instance @mid = @MiddleModule (%clk, %rst) : i1, i1

    cmt2.rule @runRule() -> i1 {
      %c1_i1 = hw.constant 1 : i1
      cmt2.return %c1_i1 : i1
    } {
      %c42 = hw.constant 42 : i32
      // Calls to inlined modules should be replaced
      cmt2.call @mid @doWork(%c42) : (i32) -> ()
      %res = cmt2.call @mid @result() : () -> (i32)
      cmt2.return
    }
  }
}

// CHECK-NOT: cmt2.module @LeafModule
// CHECK-NOT: cmt2.module @MiddleModule
