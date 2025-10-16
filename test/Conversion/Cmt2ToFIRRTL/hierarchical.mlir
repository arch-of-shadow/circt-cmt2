// RUN: circt-opt %s -cmt2-inline-private-funcs --lower-cmt2-to-firrtl | FileCheck %s

// Test hierarchical module instantiation where cmt2 modules instantiate other cmt2 modules

// CHECK-LABEL: firrtl.circuit "Parent"
builtin.module {
  cmt2.circuit {
    // Leaf module with a method that adds two numbers
    // CHECK: firrtl.module @Adder(
    // CHECK-SAME: in %add_enable: !firrtl.uint<1>
    // CHECK-SAME: out %add_ready: !firrtl.uint<1>
    // CHECK-SAME: in %add_a: !firrtl.uint<32>
    // CHECK-SAME: in %add_b: !firrtl.uint<32>
    // CHECK-SAME: out %add_res0: !firrtl.uint<32>
    cmt2.module @Adder() {
      // Method: add two numbers
      cmt2.method @add(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
        cmt2.return
      } {
        %sum_value = firrtl.add %a, %b : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
        %result = firrtl.bits %sum_value 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
        cmt2.return %result : !firrtl.uint<32>
      }
    }

    // Intermediate module that uses Adder
    // CHECK: firrtl.module @Calculator(
    // CHECK-SAME: in %compute_enable: !firrtl.uint<1>
    // CHECK-SAME: out %compute_ready: !firrtl.uint<1>
    // CHECK-SAME: in %compute_x: !firrtl.uint<32>
    // CHECK-SAME: in %compute_y: !firrtl.uint<32>
    // CHECK-SAME: out %compute_res0: !firrtl.uint<32>
    cmt2.module @Calculator() {
      // Instance of Adder
      cmt2.instance @adder = @Adder

      // Method that uses the adder
      cmt2.method @compute(%x: !firrtl.uint<32>, %y: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
        cmt2.return
      } {
        %result = cmt2.call @adder @add (%x, %y) : (!firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<32>)
        cmt2.return %result : !firrtl.uint<32>
      }
    }

    // Parent module that uses Calculator
    // CHECK: firrtl.module @Parent(
    // CHECK-SAME: in %run_enable: !firrtl.uint<1>
    // CHECK-SAME: out %run_ready: !firrtl.uint<1>
    // CHECK-SAME: in %run_a: !firrtl.uint<32>
    // CHECK-SAME: in %run_b: !firrtl.uint<32>
    // CHECK-SAME: out %run_res0: !firrtl.uint<32>
    cmt2.module @Parent() {
      // Instance of Calculator
      cmt2.instance @calc = @Calculator

      // Method that uses the calculator
      cmt2.method @run(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
        cmt2.return
      } {
        %res = cmt2.call @calc @compute (%a, %b) : (!firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<32>)
        cmt2.return %res : !firrtl.uint<32>
      }
    }
  }
}

// CHECK: firrtl.instance adder @Adder
// CHECK: firrtl.instance calc @Calculator
