// RUN: circt-opt %s -split-input-file -verify-diagnostics

// Tests for CallOp timing verifier error detection.
// These tests verify that the verifier correctly rejects invalid timing at parse time.

// -----
// Test: result_timing end exceeds step latency

builtin.module {
    firrtl.circuit "TestMod" {
        firrtl.module @TestMod(in %clk: !firrtl.clock) {}
    }
    cmt2.circuit {
        cmt2.module.extern.firrtl @ext : @TestMod(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @op static<4> : (!firrtl.uint<32>) -> !firrtl.uint<32> [
                enable = "clk", arguments = ["clk"], results = ["clk"]
            ]
        } {
            conflict = [],
            conflictFree = []
        }

        cmt2.module @TestResultExceedsStep(%clk: !firrtl.clock) {
            cmt2.instance @ext_inst = @ext (%clk) : !firrtl.clock

            // Step latency is 4, but result_timing ends at 5
            cmt2.proc.static_step @step <4> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                // expected-error @+1 {{result_timing[0] end (5) exceeds step latency (4)}}
                %r = cmt2.call @ext_inst @op(%c10) {
                    arg_timing = [#cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[4, 5]>]
                } : (!firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @step
            }
        }
    }
}

// -----
// Test: arg_timing end exceeds step latency

builtin.module {
    firrtl.circuit "TestMod" {
        firrtl.module @TestMod(in %clk: !firrtl.clock) {}
    }
    cmt2.circuit {
        cmt2.module.extern.firrtl @ext : @TestMod(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @op static<4> : (!firrtl.uint<32>) -> !firrtl.uint<32> [
                enable = "clk", arguments = ["clk"], results = ["clk"]
            ]
        } {
            conflict = [],
            conflictFree = []
        }

        cmt2.module @TestArgExceedsStep(%clk: !firrtl.clock) {
            cmt2.instance @ext_inst = @ext (%clk) : !firrtl.clock

            // Step latency is 4, but arg_timing ends at 6
            cmt2.proc.static_step @step <4> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                // expected-error @+1 {{arg_timing[0] end (6) exceeds step latency (4)}}
                %r = cmt2.call @ext_inst @op(%c10) {
                    arg_timing = [#cmt2.timing<[5, 6]>],
                    result_timing = [#cmt2.timing<[3, 4]>]
                } : (!firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @step
            }
        }
    }
}
