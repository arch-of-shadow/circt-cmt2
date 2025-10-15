// RUN: circt-opt --lower-cmt2-to-hw %s | FileCheck %s

// Simple test to verify the Cmt2ToHW conversion pass runs without errors
// on a basic Cmt2 module

builtin.module {
  cmt2.circuit {
    // CHECK-LABEL: hw.module @simple
    cmt2.module @simple(%clk: i1, %rst: i1) {
      // A simple rule that always fires
      cmt2.rule @alwaysFire() -> i1 {
        %true = hw.constant 1 : i1
        cmt2.return %true : i1
      } {
        // Empty body for now
        cmt2.return
      }
    }
  }
}
