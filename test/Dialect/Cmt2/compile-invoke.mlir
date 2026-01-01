// RUN: circt-opt %s -cmt2-compile-invoke | FileCheck %s

// Test CompileInvoke pass: converts proc.invoke to proc.enable + generated groups

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

        // CHECK-LABEL: cmt2.module @TestInvokeCompile
        cmt2.module @TestInvokeCompile(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @reg_a = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Check that a step was generated for the invoke (it gets placed before existing groups)
            // CHECK: cmt2.proc.step @__invoke_group_0
            // CHECK: cmt2.call @this @add
            // CHECK: cmt2.proc.step_done

            // Existing steps should be preserved
            // CHECK: cmt2.proc.step @existing_group
            cmt2.proc.step @existing_group {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1 : !firrtl.uint<1>
            }

            // CHECK: cmt2.proc.method @add
            cmt2.proc.method @add(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @existing_group
            }

            // CHECK: cmt2.proc.rule @invoke_test
            cmt2.proc.rule @invoke_test() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                // The invoke should be converted to an enable with a generated group
                // CHECK-NOT: cmt2.proc.invoke
                // CHECK: cmt2.proc.enable @__invoke_group_0
                %result = cmt2.proc.invoke @this @add(%c10, %c20) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }
        }
    }
}
