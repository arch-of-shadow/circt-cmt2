// RUN: circt-opt %s -cmt2-compile-invoke -cmt2-tdcc -cmt2-proc-stmt-to-action -cmt2-proc-to-gaa -cmt2-inline-modules --lower-cmt2-to-firrtl | FileCheck %s
// RUN: circt-opt %s -cmt2-compile-invoke -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=CHECK-ACTION

// End-to-end test for procedural layer with modular passes:
// 1. CompileInvoke: proc.invoke -> proc.enable + group
// 2. TDCC: compute FSM states and transitions
// 3. ProcStmtToAction: generate FSM registers, state rules, and status values
// 4. ProcToGAA: mark proc ops as converted
// 5. LowerCmt2ToFIRRTL: final FIRRTL output

builtin.module {
    firrtl.circuit "Reg32" {
        firrtl.module @Reg32(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
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
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        // CHECK-LABEL: firrtl.module @SimpleProc
        // CHECK-NOT: ResultOutOfBound
        // CHECK-ACTION-LABEL: cmt2.module @SimpleProc
        cmt2.module @SimpleProc(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @r = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Groups for reading and writing
            cmt2.proc.step @read_group {
                %val = cmt2.call @r @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
            }

            cmt2.proc.step @write_group {
                %c42 = firrtl.constant 42 : !firrtl.uint<32>
                cmt2.call @r @write(%c42) : (!firrtl.uint<32>) -> ()
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
            }

            // Procedural rule with sequential control
            // CHECK-ACTION: cmt2.instance @__fsm_seq_test = @__FSMReg_
            // CHECK-ACTION: cmt2.rule @seq_test_state0
            // CHECK-ACTION: cmt2.rule @seq_test_state1
            // CHECK-ACTION: cmt2.value @seq_test__idle
            // CHECK-ACTION: cmt2.value @seq_test__running
            cmt2.proc.rule @seq_test() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @read_group
                    cmt2.proc.enable @write_group
                }
                cmt2.proc.control_end
            }

            // Simple non-procedural rule
            cmt2.rule @always_ready() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                // Empty body
            }
        }
    }
}
