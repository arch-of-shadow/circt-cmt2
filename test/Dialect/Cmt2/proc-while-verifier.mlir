// RUN: circt-opt %s -split-input-file -verify-diagnostics

// Test that cmt2.call in while body region is rejected
// cmt2.call is only allowed in the condition region of proc.while

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
            cmt2.bind.value @read : () -> (!firrtl.uint<32>) [ ready = "readReady", arguments = [], results = ["read"]]
            cmt2.bind.method @write : (!firrtl.uint<32>) -> () [
                enable = "writeEnable", ready = "writeReady", arguments = ["write"], results = []
            ]
        } {
            sequenceBefore = [[@read, @write]]
        }

        cmt2.module @TestCallInWhileBody(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.step @inc {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1 : !firrtl.uint<1>
            }

            // This should fail: cmt2.call is in the body region of proc.while
            cmt2.proc.rule @bad_while() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.while {
                    %c1 = firrtl.constant 1 : !firrtl.uint<1>
                    cmt2.proc.while_cond %c1 : !firrtl.uint<1>
                } do {
                    // expected-error @+1 {{cmt2.call is not allowed in the body region of cmt2.proc.while; use the condition region instead}}
                    %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                    cmt2.proc.yield
                }
                cmt2.proc.control_end
            }
        }
    }
}

// -----

// Test valid usage: cmt2.call in condition region is allowed

builtin.module {
    firrtl.circuit "TestReg2" {
        firrtl.module @TestReg2(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
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
        cmt2.module.extern.firrtl @reg : @TestReg2(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : () -> (!firrtl.uint<32>) [ ready = "readReady", arguments = [], results = ["read"]]
            cmt2.bind.method @write : (!firrtl.uint<32>) -> () [
                enable = "writeEnable", ready = "writeReady", arguments = ["write"], results = []
            ]
        } {
            sequenceBefore = [[@read, @write]]
        }

        // This is valid: cmt2.call is in the condition region
        cmt2.module @TestCallInCondRegion(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @counter = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.step @inc {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.step_done %c1 : !firrtl.uint<1>
            }

            cmt2.proc.rule @good_while() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.while {
                    // cmt2.call in condition region is valid
                    %val = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                    %c0 = firrtl.constant 0 : !firrtl.uint<32>
                    %cond = firrtl.neq %val, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                    cmt2.proc.while_cond %cond : !firrtl.uint<1>
                } do {
                    cmt2.proc.enable @inc
                    cmt2.proc.yield
                }
                cmt2.proc.control_end
            }
        }
    }
}
