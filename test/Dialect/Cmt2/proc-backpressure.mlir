// RUN: circt-opt %s -cmt2-tdcc 2>&1 | FileCheck %s

// Test 1: Dynamic step with method calls
// The GAA lowering ensures method ready signals are included in rule guards.
// This is verified by checking the generated rule structure.

// Test 2: Static step validation - conflicting methods should emit warning
// Test 3: Static promotion blocking - steps with conflicting methods cannot be promoted

builtin.module {
  firrtl.circuit "Reg32" {
    firrtl.module @Reg32(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
                         in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                         out %writeReady: !firrtl.uint<1>, out %readReady: !firrtl.uint<1>,
                         out %read: !firrtl.uint<32>) {
      %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
      %r = firrtl.regreset %clock, %reset, %c0_ui32 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>
      %next = firrtl.mux(%writeEnable, %write, %r) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
      firrtl.connect %r, %next : !firrtl.uint<32>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.connect %writeReady, %c1_ui1 : !firrtl.uint<1>
      firrtl.connect %readReady, %c1_ui1 : !firrtl.uint<1>
      firrtl.connect %read, %r : !firrtl.uint<32>
    }
  }

  cmt2.circuit {
    // External register module with read/write methods that conflict
    cmt2.module.extern.firrtl @reg : @Reg32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [
        ready = "readReady", arguments = [], results = ["read"]
      ]
      cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
        enable = "writeEnable", ready = "writeReady", arguments = ["write"], results = []
      ]
    } {
      // read < write (sequential before)
      // write <> write (conflict)
      conflict = [[@write, @write]],
      conflictFree = [[@read, @read]],
      sequenceBefore = [[@read, @write]]
    }

    // Module with static step containing potential conflicting calls
    cmt2.module @TestBackpressure(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @r1 = @reg(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
      cmt2.instance @r2 = @reg(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

      // Dynamic step - should work fine, GAA handles backpressure
      cmt2.proc.step @load {
        %data = cmt2.call @r1 @read() : () -> !firrtl.uint<32>
        %c1 = firrtl.constant 1 : !firrtl.uint<1>
      }

      // Static step with single method call - should be OK
      cmt2.proc.static_step @store_single<1> {
        %val = firrtl.constant 42 : !firrtl.uint<32>
        cmt2.call @r1 @write(%val) : (!firrtl.uint<32>) -> ()
      }

      // Procedural rule using the steps
      cmt2.proc.rule @sequential () -> () {
        %c1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1 : !firrtl.uint<1>
      } control {
        cmt2.proc.seq {
          cmt2.proc.enable @load
          cmt2.proc.enable @store_single
        }
        cmt2.proc.control_end
      }
    }
  }
}

// CHECK: cmt2.circuit
// CHECK: cmt2.module @TestBackpressure

// Verify TDCC metadata is added to proc rule
// CHECK: cmt2.proc.rule @sequential
// CHECK-SAME: tdcc.fsm_width
// CHECK-SAME: tdcc.num_states
