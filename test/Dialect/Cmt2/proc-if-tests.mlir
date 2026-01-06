// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s --check-prefix=TDCC
// RUN: circt-opt %s -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=STMT

// Test if/else guard generation in TDCC pass:
// - Then branch gets positive guard (cond = true)
// - Else branch gets negative guard (cond = false / inverted)
// - Transitions include guard_op_id and guard_inverted attributes

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

        // Test simple if/else - TDCC should generate guarded transitions
        // TDCC-LABEL: cmt2.module @TestIfElse
        // TDCC: cmt2.proc.rule @if_else_rule
        // TDCC-SAME: tdcc.cond_ops = [{id = 0 : i64, type = "if"}]
        // TDCC-SAME: tdcc.transitions = [
        // TDCC-SAME: {from = 0 : i64, guard_inverted = false, guard_op_id = 0 : i64, to = 1 : i64}
        // TDCC-SAME: {from = 0 : i64, guard_inverted = true, guard_op_id = 0 : i64, to = 2 : i64}

        // STMT-LABEL: cmt2.module @TestIfElse
        // STMT: cmt2.rule @if_else_rule_state0
        // STMT: firrtl.mux
        // STMT: cmt2.call @__fsm_if_else_rule @write
        cmt2.module @TestIfElse(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @result = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @selector = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Step for then branch: write 100
            cmt2.proc.step @set_100 {
                %c100 = firrtl.constant 100 : !firrtl.uint<32>
                cmt2.call @result @write(%c100) : (!firrtl.uint<32>) -> ()
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1 : !firrtl.uint<1>
            }

            // Step for else branch: write 200
            cmt2.proc.step @set_200 {
                %c200 = firrtl.constant 200 : !firrtl.uint<32>
                cmt2.call @result @write(%c200) : (!firrtl.uint<32>) -> ()
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1 : !firrtl.uint<1>
            }

            // If/else rule: if selector != 0, set 100; else set 200
            cmt2.proc.rule @if_else_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // Read selector to get condition
                %sel_raw = cmt2.call @selector @read() : () -> !firrtl.uint<32>
                %c0 = firrtl.constant 0 : !firrtl.uint<32>
                %cond = firrtl.neq %sel_raw, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>

                cmt2.proc.if %cond : !firrtl.uint<1> {
                    cmt2.proc.enable @set_100
                } else {
                    cmt2.proc.enable @set_200
                }
                cmt2.proc.control_end
            }
        }

        // Test static if/else with known latencies
        // TDCC-LABEL: cmt2.module @TestStaticIfElse
        // TDCC: cmt2.proc.rule @static_if_rule
        // TDCC-SAME: tdcc.cond_ops = [{id = 0 : i64, type = "static_if"}]
        cmt2.module @TestStaticIfElse(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @result = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.static_step @static_step_a <2> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                cmt2.call @result @write(%c10) : (!firrtl.uint<32>) -> ()
            }

            cmt2.proc.static_step @static_step_b <3> {
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                cmt2.call @result @write(%c20) : (!firrtl.uint<32>) -> ()
            }

            cmt2.proc.rule @static_if_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %cond = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.static_if %cond : !firrtl.uint<1> <2, 3> {
                    cmt2.proc.enable @static_step_a
                } else {
                    cmt2.proc.enable @static_step_b
                }
                cmt2.proc.control_end
            }
        }

        // Test if without else (only then branch)
        // TDCC-LABEL: cmt2.module @TestIfOnly
        cmt2.module @TestIfOnly(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @result = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.step @increment {
                %val = cmt2.call @result @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %new_val = firrtl.add %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new_val 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @result @write(%trunc) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %done : !firrtl.uint<1>
            }

            // If only: increment if condition is true
            cmt2.proc.rule @if_only_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %cond = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.if %cond : !firrtl.uint<1> {
                    cmt2.proc.enable @increment
                }
                cmt2.proc.control_end
            }
        }
    }
}
