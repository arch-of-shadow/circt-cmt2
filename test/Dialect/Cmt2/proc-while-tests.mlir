// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s --check-prefix=TDCC
// RUN: circt-opt %s -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=STMT

// Test while loop FSM generation in TDCC pass:
// - Header state for condition checking
// - Body entry guarded by cond=true
// - Back-edge from body exit to header
// - Exit transition guarded by cond=false (inverted)

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

        // Test simple while loop with back-edge and exit transitions
        // TDCC-LABEL: cmt2.module @TestWhileLoop
        // TDCC: cmt2.proc.rule @countdown_rule
        // TDCC-SAME: tdcc.cond_ops
        // TDCC-SAME: tdcc.transitions
        // Check for back-edge to header state and proper guards

        // STMT-LABEL: cmt2.module @TestWhileLoop
        // STMT: cmt2.instance @__fsm_countdown_rule
        // STMT: cmt2.rule @countdown_rule_state
        cmt2.module @TestWhileLoop(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Step to decrement counter
            cmt2.proc.step @decrement {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %new_val = firrtl.sub %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_val 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%trunc) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
            }

            // While loop: decrement while counter > 0
            // The condition is computed in a dedicated region that supports cmt2.call
            cmt2.proc.rule @countdown_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.while {
                    // Condition region: read counter and check if > 0
                    %count = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                    %c0 = firrtl.constant 0 : !firrtl.uint<32>
                    %running = firrtl.neq %count, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                    cmt2.proc.while_cond %running : !firrtl.uint<1>
                } do {
                    cmt2.proc.enable @decrement
                    cmt2.proc.yield
                }
                cmt2.proc.control_end
            }
        }

        // Test while loop with sequence inside body
        // TDCC-LABEL: cmt2.module @TestWhileWithSeq
        cmt2.module @TestWhileWithSeq(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @accum = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.step @read_step {
                %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %done = firrtl.constant 1 : !firrtl.uint<1>
            }

            cmt2.proc.step @update_step {
                %acc = cmt2.call @accum @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %new_acc = firrtl.add %acc, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_acc 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @accum @write(%trunc) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
            }

            // While with sequential body
            cmt2.proc.rule @while_seq_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.while {
                    %count = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                    %c0 = firrtl.constant 0 : !firrtl.uint<32>
                    %running = firrtl.neq %count, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                    cmt2.proc.while_cond %running : !firrtl.uint<1>
                } do {
                    cmt2.proc.seq {
                        cmt2.proc.enable @read_step
                        cmt2.proc.enable @update_step
                    }
                    cmt2.proc.yield
                }
                cmt2.proc.control_end
            }
        }
    }
}
