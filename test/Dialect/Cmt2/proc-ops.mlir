// RUN: circt-opt %s | FileCheck %s

// Test procedural layer operations parsing and printing

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
        // CHECK: cmt2.module.extern.firrtl @reg
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

        // CHECK: cmt2.module @TestProcOps
        cmt2.module @TestProcOps(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @reg_a = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @reg_b = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // CHECK: cmt2.proc.group @load
            cmt2.proc.group @load {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.group_done %c1 : !firrtl.uint<1>
            }

            // CHECK: cmt2.proc.group @store
            cmt2.proc.group @store {
                %c42 = firrtl.constant 42 : !firrtl.uint<32>
                cmt2.call @reg_b @write(%c42) : (!firrtl.uint<32>) -> ()
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.group_done %c1 : !firrtl.uint<1>
            }

            // CHECK: cmt2.proc.static_group @multiply<4>
            cmt2.proc.static_group @multiply <4> {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
            }

            // CHECK: cmt2.proc.rule @compute
            cmt2.proc.rule @compute() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.seq
                cmt2.proc.seq {
                    // CHECK: cmt2.proc.enable @load
                    cmt2.proc.enable @load
                    // CHECK: cmt2.proc.enable @store
                    cmt2.proc.enable @store
                }
            }

            // CHECK: cmt2.proc.rule @conditional
            cmt2.proc.rule @conditional() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %cond = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c0 = firrtl.constant 0 : !firrtl.uint<32>
                %is_zero = firrtl.eq %cond, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                // CHECK: cmt2.proc.if
                cmt2.proc.if %is_zero : !firrtl.uint<1> {
                    cmt2.proc.enable @load
                } else {
                    cmt2.proc.enable @store
                }
            }

            // CHECK: cmt2.proc.rule @parallel
            cmt2.proc.rule @parallel() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.par
                cmt2.proc.par {
                    cmt2.proc.enable @load
                    cmt2.proc.enable @store
                }
            }

            // CHECK: cmt2.proc.rule @loop
            cmt2.proc.rule @loop() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %cond = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c0 = firrtl.constant 0 : !firrtl.uint<32>
                %not_zero = firrtl.neq %cond, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                // CHECK: cmt2.proc.while
                cmt2.proc.while %not_zero : !firrtl.uint<1> {
                    cmt2.proc.enable @load
                }
            }

            // CHECK: cmt2.proc.method @add
            cmt2.proc.method @add(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @load
                    cmt2.proc.enable @store
                }
            }

            // CHECK: cmt2.proc.rule @invoke_test
            cmt2.proc.rule @invoke_test() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                // CHECK: cmt2.proc.invoke @this @add
                %result = cmt2.proc.invoke @this @add(%c10, %c20) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }
        }
    }
}
