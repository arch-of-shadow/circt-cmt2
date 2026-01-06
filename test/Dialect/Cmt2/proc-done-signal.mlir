// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s --check-prefix=TDCC
// RUN: circt-opt %s -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=STMT

// Test done signal integration for dynamic steps:
// - Dynamic steps should have done_step attribute in transitions
// - ProcStmtToAction should generate done-guarded transitions

builtin.module {
    firrtl.circuit "TestReg" {
        firrtl.module @TestReg(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
                             in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                             out %writeReady: !firrtl.uint<1>, out %readReady: !firrtl.uint<1>,
                             out %read: !firrtl.uint<32>) {
            %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
            %r = firrtl.regreset %clock, %reset, %c0_ui32 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>
            %next = firrtl.mux(%writeEnable, %write, %r) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            firrtl.connect %r, %next : !firrtl.uint<32>, !firrtl.uint<32>
            %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.connect %writeReady, %c1_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
            firrtl.connect %readReady, %c1_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
            firrtl.connect %read, %r : !firrtl.uint<32>, !firrtl.uint<32>
        }
    }

    cmt2.circuit {
        cmt2.module.extern.firrtl @reg : @TestReg(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ ready = "readReady", arguments = [], results = ["read"]]
            cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
                enable = "writeEnable", ready = "writeReady", arguments = ["write"], results = []
            ]
        } {
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        // Test: Dynamic step with done signal should have done_step in transition
        // TDCC-LABEL: cmt2.module @TestDynamicDone
        // TDCC: cmt2.proc.rule @dynamic_rule
        // TDCC-SAME: tdcc.transitions
        // Check that dynamic step exit has done_step attribute
        // TDCC-SAME: done_step = "write_step"

        // STMT-LABEL: cmt2.module @TestDynamicDone
        // STMT: cmt2.rule @dynamic_rule_state
        // STMT: firrtl.mux
        cmt2.module @TestDynamicDone(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Dynamic step with computed done signal
            cmt2.proc.step @write_step {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %new_val = firrtl.add %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_val 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%trunc) : (!firrtl.uint<32>) -> ()
                // Done signal: constant 1 (always done in 1 cycle)
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            // Rule with dynamic step
            cmt2.proc.rule @dynamic_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @write_step
                cmt2.proc.control_end
            }
        }

        // Test: Sequence of dynamic steps
        // TDCC-LABEL: cmt2.module @TestSeqDynamic
        cmt2.module @TestSeqDynamic(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.step @step_a {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %new_val = firrtl.add %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_val 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%trunc) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            cmt2.proc.step @step_b {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %new_val = firrtl.add %val, %c2 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_val 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%trunc) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            // Sequential: step_a then step_b
            // Both should have done_step guards on their exit transitions
            cmt2.proc.rule @seq_dynamic_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @step_a
                    cmt2.proc.enable @step_b
                }
                cmt2.proc.control_end
            }
        }

        // Test: Mixed static and dynamic steps
        // TDCC-LABEL: cmt2.module @TestMixedStaticDynamic
        cmt2.module @TestMixedStaticDynamic(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Static step (2 cycles) - no done_step needed
            cmt2.proc.static_step @static_step <2> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                cmt2.call @counter @write(%c10) : (!firrtl.uint<32>) -> ()
            }

            // Dynamic step - needs done_step
            cmt2.proc.step @dynamic_step {
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                cmt2.call @counter @write(%c20) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            // Sequence: static then dynamic
            cmt2.proc.rule @mixed_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @static_step
                    cmt2.proc.enable @dynamic_step
                }
                cmt2.proc.control_end
            }
        }
    }
}
