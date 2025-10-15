// RUN: circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-hw %s | FileCheck %s

// Test conversion of module with methods and values
// Note: Private functions must be inlined before conversion
// Note: myValue is a private function (only called via @this), so it will be inlined
// and won't have module-level ports. Methods don't expose result ports at module level.

builtin.module {
  cmt2.circuit {
    // CHECK-LABEL: hw.module @testModule
    // CHECK-SAME: in %clk : i1, in %rst : i1
    // CHECK-SAME: in %myMethod_enable : i1
    // CHECK-SAME: out myMethod_ready : i1
    // Note: myMethod results are not exposed as module outputs since they depend on call arguments
    // Note: myValue is private and will be inlined, so no ports for it
    cmt2.module @testModule(%clk: i1, %rst: i1) {
      // A simple value that always returns 42
      cmt2.value @myValue() -> (i32) {
        %true = hw.constant 1 : i1
        cmt2.return %true : i1
      } {
        %c42 = hw.constant 42 : i32
        cmt2.return %c42 : i32
      }

      // A simple method that takes an input and returns it
      cmt2.method @myMethod(%input: i32) -> (i32) {
        %true = hw.constant 1 : i1
        cmt2.return %true : i1
      } {
        cmt2.return %input : i32
      }

      // A rule that uses the value
      cmt2.rule @testRule() -> i1 {
        %val = cmt2.call @this @myValue () : () -> (i32)
        %c10 = hw.constant 10 : i32
        %cond = comb.icmp bin sgt %val, %c10 : i32
        cmt2.return %cond : i1
      } {
        cmt2.return
      }
    }
  }
}
