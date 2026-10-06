// RUN: circt-opt %s --lower-cmt2-to-firrtl | FileCheck %s
// Native ECMT2 construction uses empty result names. Preserve the same ABI as
// parsed CMT2 and give multiple unnamed results distinct ports.
// CHECK: out %read_res0: !firrtl.uint<16>
// CHECK-SAME: out %read_res1: !firrtl.uint<16>
"builtin.module"() ({
  "cmt2.circuit"() ({
    "cmt2.module"() <{argNames = ["clk", "rst"], sym_name = "top"}> ({
    ^bb0(%arg0: !firrtl.clock, %arg1: !firrtl.uint<1>):
      "cmt2.method"() <{argNames = [], bodyResNames = ["", ""], function_type = () -> (!firrtl.uint<16>, !firrtl.uint<16>), sym_name = "read"}> ({
        "cmt2.return"() : () -> ()
      }, {
        %0 = "firrtl.constant"() <{value = 1 : ui16}> : () -> !firrtl.uint<16>
        %1 = "firrtl.constant"() <{value = 2 : ui16}> : () -> !firrtl.uint<16>
        "cmt2.return"(%0, %1) : (!firrtl.uint<16>, !firrtl.uint<16>) -> ()
      }) : () -> ()
    }) : () -> ()
  }) : () -> ()
}) : () -> ()

