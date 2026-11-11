// RUN: circt-opt %s --lower-cmt2-to-firrtl | FileCheck %s

// Regression test: `cmt2.bind.value` argument ports must be materialized on the
// generated FIRRTL `firrtl.extmodule`, otherwise `cmt2.call` lowering fails
// with "Input port not found".

builtin.module {
  cmt2.circuit {
    cmt2.module.extern.firrtl @alu : @ALU {
      // A value binding with two data inputs and one data output.
      // CHECK: firrtl.extmodule @ALU
      // CHECK-SAME: in a:
      // CHECK-SAME: in b:
      // CHECK-SAME: out out:
      cmt2.bind.value @add : (!firrtl.uint<8>, !firrtl.uint<8>) -> !firrtl.uint<8> [
        arguments = ["a", "b"],
        results = ["out"]
      ]
    }

    cmt2.module @Top(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @alu_i = @alu

      cmt2.rule @do_add() -> () {
        %c1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1 : !firrtl.uint<1>
      } {
        %a = firrtl.constant 1 : !firrtl.uint<8>
        %b = firrtl.constant 2 : !firrtl.uint<8>
        %res = cmt2.call @alu_i @add(%a, %b) : (!firrtl.uint<8>, !firrtl.uint<8>) -> !firrtl.uint<8>
      }
    }
  }
}
