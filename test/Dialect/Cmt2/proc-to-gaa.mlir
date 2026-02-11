// RUN: circt-opt %s -cmt2-compile-invoke -cmt2-tdcc -cmt2-proc-stmt-to-action -cmt2-proc-to-gaa | FileCheck %s

// Test full procedural layer pipeline:
// 1. CompileInvoke: proc.invoke -> proc.enable + group
// 2. TDCC: compute FSM states and transitions
// 3. ProcStmtToAction: generate FSM registers, state rules, status values
// 4. ProcToGAA: mark proc ops as converted

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

        // CHECK-LABEL: cmt2.module @TestProcPipeline
        cmt2.module @TestProcPipeline(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @reg_a = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @reg_b = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.step @load {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
            }

            cmt2.proc.step @store {
                %c42 = firrtl.constant 42 : !firrtl.uint<32>
                cmt2.call @reg_b @write(%c42) : (!firrtl.uint<32>) -> ()
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
            }

            // Test sequential control - ProcStmtToAction generates FSM and rules
            // CHECK: cmt2.proc.rule @seq_rule
            // CHECK-SAME: proc.converted
            // CHECK-SAME: proc.stmt_converted
            // CHECK: cmt2.instance @__fsm_seq_rule = @__FSMReg_
            // CHECK: cmt2.rule @seq_rule_state0
            // CHECK: cmt2.rule @seq_rule_state1
            // CHECK: cmt2.value @seq_rule__idle
            // CHECK: cmt2.value @seq_rule__running
            cmt2.proc.rule @seq_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @load
                    cmt2.proc.enable @store
                }
                cmt2.proc.control_end
            }

            // Test procedural method - also generates FSM infrastructure
            // CHECK: cmt2.proc.method @add_method
            // CHECK-SAME: proc.converted
            cmt2.proc.method @add_method(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @load
                cmt2.proc.control_end
            }

            // Test invoke compilation (CompileInvoke converts to enable + group)
            // CHECK: cmt2.proc.rule @invoke_rule
            // CHECK-SAME: proc.converted
            // CHECK-SAME: proc.stmt_converted
            // CHECK: cmt2.instance @__fsm_invoke_rule = @__FSMReg_
            cmt2.proc.rule @invoke_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                %result = cmt2.proc.invoke @this @add_method(%c10, %c20) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
                cmt2.proc.control_end
            }
        }
    }
}
