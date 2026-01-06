// RUN: circt-opt %s -cmt2-inline-modules | FileCheck %s

// Test module inlining with synthesis attribute

cmt2.circuit {
  // Module with synthesis=true should NOT be inlined
  // CHECK-LABEL: cmt2.module @SynthModule
  cmt2.module @SynthModule(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>) attributes {synthesis = true} {
    cmt2.method @store(%data: !firrtl.uint<32>) -> () {
      %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1_i1 : !firrtl.uint<1>
    } {
      cmt2.return
    }

    cmt2.value @load() -> (!firrtl.uint<32>) {
      %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1_i1 : !firrtl.uint<1>
    } {
      %c0_i32 = firrtl.constant 0 : !firrtl.uint<32>
      cmt2.return %c0_i32 : !firrtl.uint<32>
    }
  }

  // Simple leaf module - SHOULD be inlined
  cmt2.module @LeafModule(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>) {
    cmt2.instance @storage = @SynthModule (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>

    cmt2.method @increment(%data: !firrtl.uint<32>) -> () {
      %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1_i1 : !firrtl.uint<1>
    } {
      // Add one to the data
      %c1_i32 = firrtl.constant 1 : !firrtl.uint<32>
      %sum = firrtl.add %data, %c1_i32 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
      %sum32 = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
      cmt2.call @storage @store(%sum32) : (!firrtl.uint<32>) -> ()
      cmt2.return
    }

    cmt2.value @get() -> (!firrtl.uint<32>) {
      %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1_i1 : !firrtl.uint<1>
    } {
      %0 = cmt2.call @storage @load() : () -> (!firrtl.uint<32>)
      cmt2.return %0 : !firrtl.uint<32>
    }
  }

  // Middle module that uses LeafModule - SHOULD be inlined
  cmt2.module @MiddleModule(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>) {
    cmt2.instance @leaf = @LeafModule (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>

    cmt2.method @doWork(%val: !firrtl.uint<32>) -> () {
      %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1_i1 : !firrtl.uint<1>
    } {
      cmt2.call @leaf @increment(%val) : (!firrtl.uint<32>) -> ()
      cmt2.return
    }

    cmt2.value @result() -> (!firrtl.uint<32>) {
      %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1_i1 : !firrtl.uint<1>
    } {
      %0 = cmt2.call @leaf @get() : () -> (!firrtl.uint<32>)
      cmt2.return %0 : !firrtl.uint<32>
    }
  }

  // Top module - should NOT be inlined (top-level/no uses)
  // CHECK-LABEL: cmt2.module @TopModule
  cmt2.module @TopModule(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>) {
    // CHECK: cmt2.instance @synth
    cmt2.instance @synth = @SynthModule (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>
    // CHECK: cmt2.instance @mid.leaf.storage
    // CHECK-NOT: cmt2.instance @mid =
    cmt2.instance @mid = @MiddleModule (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>

    cmt2.rule @runRule () -> () {
      %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1_i1 : !firrtl.uint<1>
    } {
      %c42 = firrtl.constant 42 : !firrtl.uint<32>
      // Calls to inlined modules should be replaced
      cmt2.call @mid @doWork(%c42) : (!firrtl.uint<32>) -> ()
      %res = cmt2.call @mid @result() : () -> (!firrtl.uint<32>)
      cmt2.return
    }
  }
}

// CHECK-NOT: cmt2.module @LeafModule
// CHECK-NOT: cmt2.module @MiddleModule
