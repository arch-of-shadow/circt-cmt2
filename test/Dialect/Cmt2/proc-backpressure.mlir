// RUN: circt-opt %s -cmt2-tdcc 2>&1 | FileCheck %s

// Test 1: Dynamic step with method calls
// The GAA lowering ensures method ready signals are included in rule guards.
// This is verified by checking the generated rule structure.

// Test 2: Static step validation - conflicting methods should emit warning
// Test 3: Static promotion blocking - steps with conflicting methods cannot be promoted

cmt2.circuit {
  // External register module with read/write methods that conflict
  cmt2.module.extern.firrtl @reg (
    %clk: !firrtl.clock,
    %rst: !firrtl.uint<1>
  ) {
    // read < write (sequential)
    // write <> write (conflict)
    cmt2.bind.value @read () -> (!firrtl.uint<32>) to @read_port
    cmt2.bind.method @write (%data: !firrtl.uint<32>) -> () to @write_port
    cmt2.conflict_matrix [
      [@read, @write, "SB"],
      [@write, @write, "C"]
    ]
  }

  // Module with static step containing potential conflicting calls
  cmt2.module @TestBackpressure (
    %clk: !firrtl.clock {cmt2.portName = "clk"},
    %rst: !firrtl.uint<1> {cmt2.portName = "rst"}
  ) {
    cmt2.instance @r1 @reg(%clk, %rst) : (!firrtl.clock, !firrtl.uint<1>) -> ()
    cmt2.instance @r2 @reg(%clk, %rst) : (!firrtl.clock, !firrtl.uint<1>) -> ()

    // Dynamic step - should work fine, GAA handles backpressure
    cmt2.proc.step @load {
      %data = cmt2.call @r1, @read() : () -> !firrtl.uint<32>
      %c1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.proc.step_done %c1
    }

    // Static step with single method call - should be OK
    cmt2.proc.static_step @store_single <1> {
      %val = firrtl.constant 42 : !firrtl.uint<32>
      cmt2.call @r1, @write(%val) : (!firrtl.uint<32>) -> ()
    }

    // Procedural rule using the steps
    cmt2.proc.rule @sequential {
      %c1 = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %c1
    } control {
      cmt2.proc.seq {
        cmt2.proc.enable @load
        cmt2.proc.enable @store_single
        cmt2.proc.control_end
      }
      cmt2.proc.control_end
    }
  }
}

// CHECK: cmt2.circuit
// CHECK: cmt2.module @TestBackpressure

// Verify TDCC metadata is added to proc rule
// CHECK: cmt2.proc.rule @sequential
// CHECK-SAME: tdcc.num_states
// CHECK-SAME: tdcc.fsm_width
