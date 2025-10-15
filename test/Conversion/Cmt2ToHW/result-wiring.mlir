// RUN: circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-hw %s | FileCheck %s

// Test that method/value call results are properly wired to body results
// This tests the newly implemented result wiring feature

builtin.module {
  cmt2.circuit {
    // CHECK-LABEL: hw.module @testResultWiring
    // CHECK-SAME: in %clk : i1, in %rst : i1
    // CHECK-SAME: in %getValue_enable : i1
    // CHECK-SAME: in %compute_enable : i1
    // CHECK-SAME: out getValue_ready : i1, out getValue_data_0 : i32
    // CHECK-SAME: out compute_ready : i1, out compute_data_0 : i32
    cmt2.module @testResultWiring(%clk: i1, %rst: i1) {
      // A value that returns a constant
      cmt2.value @getValue() -> (i32) {
        %true = hw.constant 1 : i1
        cmt2.return %true : i1
      } {
        %c100 = hw.constant 100 : i32
        cmt2.return %c100 : i32
      }

      // A method that calls the value and adds 1
      cmt2.method @compute() -> (i32) {
        %true = hw.constant 1 : i1
        cmt2.return %true : i1
      } {
        // This call should be wired to getValue's body result
        %val = cmt2.call @this @getValue () : () -> (i32)
        %c1 = hw.constant 1 : i32
        %result = comb.add %val, %c1 : i32
        cmt2.return %result : i32
      }

      // A rule that uses both
      cmt2.rule @useResults() -> i1 {
        %val1 = cmt2.call @this @getValue () : () -> (i32)
        %val2 = cmt2.call @this @compute () : () -> (i32)
        %sum = comb.add %val1, %val2 : i32
        %c200 = hw.constant 200 : i32
        %cond = comb.icmp bin sgt %sum, %c200 : i32
        cmt2.return %cond : i1
      } {
        cmt2.return
      }
    }
  }
}
