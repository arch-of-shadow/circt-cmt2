// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s

// Test TDCC pass on sequential control

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

        // CHECK-LABEL: cmt2.module @TestTDCCSeq
        cmt2.module @TestTDCCSeq(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @reg_a = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @reg_b = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Groups that will be enabled
            cmt2.proc.step @load {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1 : !firrtl.uint<1>
            }

            cmt2.proc.step @store {
                %c42 = firrtl.constant 42 : !firrtl.uint<32>
                cmt2.call @reg_b @write(%c42) : (!firrtl.uint<32>) -> ()
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1 : !firrtl.uint<1>
            }

            // Sequential control: load then store
            // TDCC assigns: load=state0, store=state1, done=state2
            // CHECK: cmt2.proc.rule @seq_test
            // CHECK-SAME: tdcc.done_state = 2
            // CHECK-SAME: tdcc.fsm_width = 2
            // CHECK-SAME: tdcc.num_states = 3
            cmt2.proc.rule @seq_test() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @load
                    cmt2.proc.enable @store
                }
            }
        }
    }
}
