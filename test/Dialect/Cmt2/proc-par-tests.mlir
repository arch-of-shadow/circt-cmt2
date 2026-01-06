// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s --check-prefix=TDCC
// RUN: circt-opt %s -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=STMT

// Test parallel (par) control FSM generation in TDCC pass:
// - Fork state where all branches are enabled simultaneously
// - Each branch executes concurrently
// - Join when all branches complete

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

        // Test simple parallel with two branches
        // TDCC-LABEL: cmt2.module @TestSimplePar
        // TDCC: cmt2.proc.rule @par_rule
        // TDCC-SAME: tdcc.transitions
        // Check that transitions go through fork state

        // STMT-LABEL: cmt2.module @TestSimplePar
        // STMT: cmt2.instance @__fsm_par_rule
        // STMT: cmt2.rule @par_rule_state
        cmt2.module @TestSimplePar(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @regA = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @regB = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Step to write to regA
            cmt2.proc.step @write_a {
                %c100 = firrtl.constant 100 : !firrtl.uint<32>
                cmt2.call @regA @write(%c100) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            // Step to write to regB
            cmt2.proc.step @write_b {
                %c200 = firrtl.constant 200 : !firrtl.uint<32>
                cmt2.call @regB @write(%c200) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            // Parallel rule: execute both writes concurrently
            cmt2.proc.rule @par_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.par {
                    cmt2.proc.enable @write_a
                    cmt2.proc.enable @write_b
                }
                cmt2.proc.control_end
            }
        }

        // Test parallel with different latency branches (static steps)
        // TDCC-LABEL: cmt2.module @TestParDiffLatency
        cmt2.module @TestParDiffLatency(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @regA = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @regB = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // 2-cycle static step
            cmt2.proc.static_step @fast_step <2> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                cmt2.call @regA @write(%c10) : (!firrtl.uint<32>) -> ()
            }

            // 4-cycle static step
            cmt2.proc.static_step @slow_step <4> {
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                cmt2.call @regB @write(%c20) : (!firrtl.uint<32>) -> ()
            }

            // Parallel: fast (2 cycles) and slow (4 cycles) run concurrently
            // Total time should be max(2, 4) = 4 cycles
            cmt2.proc.rule @par_diff_latency() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.par {
                    cmt2.proc.enable @fast_step
                    cmt2.proc.enable @slow_step
                }
                cmt2.proc.control_end
            }
        }

        // Test parallel followed by sequential step
        // TDCC-LABEL: cmt2.module @TestParThenSeq
        cmt2.module @TestParThenSeq(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @regA = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @regB = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @regC = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.step @step_a {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                cmt2.call @regA @write(%c1) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            cmt2.proc.step @step_b {
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                cmt2.call @regB @write(%c2) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            cmt2.proc.step @step_c {
                %c3 = firrtl.constant 3 : !firrtl.uint<32>
                cmt2.call @regC @write(%c3) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            // Par (a, b) then seq c
            cmt2.proc.rule @par_then_seq() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.par {
                        cmt2.proc.enable @step_a
                        cmt2.proc.enable @step_b
                    }
                    cmt2.proc.enable @step_c
                }
                cmt2.proc.control_end
            }
        }
    }
}
