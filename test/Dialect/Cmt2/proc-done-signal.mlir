// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s --check-prefix=TDCC
// RUN: circt-opt %s -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=STMT

// Test dynamic proc.step semantics:
// - No explicit step-local done op / transition guards
// - Steps advance to NEXT when the state rule fires (ready==1)

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

        // Test: Dynamic step does not use done_step guards in TDCC transitions
        // TDCC-LABEL: cmt2.module @TestDynamicDone
        // TDCC: cmt2.proc.rule @dynamic_rule
        // TDCC-SAME: tdcc.transitions
        // TDCC-NOT: done_step

        // STMT-LABEL: cmt2.module @TestDynamicDone
        // STMT: cmt2.rule @dynamic_rule_state
        cmt2.module @TestDynamicDone(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Dynamic step (single-fire)
            cmt2.proc.step @write_step {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %new_val = firrtl.add %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_val 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%trunc) : (!firrtl.uint<32>) -> ()
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
            }

            cmt2.proc.step @step_b {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %new_val = firrtl.add %val, %c2 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_val 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%trunc) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
            }

            // Sequential: step_a then step_b
            // Both steps advance on fire (no done guards)
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

            // Static step (2 cycles)
            cmt2.proc.static_step @static_step <2> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                cmt2.call @counter @write(%c10) : (!firrtl.uint<32>) -> ()
            }

            // Dynamic step
            cmt2.proc.step @dynamic_step {
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                cmt2.call @counter @write(%c20) : (!firrtl.uint<32>) -> ()
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
