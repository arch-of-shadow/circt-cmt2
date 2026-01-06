// RUN: circt-opt %s -cmt2-timing-validation -verify-diagnostics

// Tests for TimingValidation pass error detection.
// These tests verify that the pass correctly rejects invalid timing.
// Note: Basic bounds checking (start >= 0, end <= step latency) is done
// by the CallOp verifier. This file tests pass-level semantic validation.

builtin.module {
    firrtl.circuit "PipelinedMult" {
        firrtl.module @PipelinedMult(
            in %clk: !firrtl.clock,
            in %go: !firrtl.uint<1>,
            in %left: !firrtl.uint<32>,
            in %right: !firrtl.uint<32>,
            out %out: !firrtl.uint<32>,
            out %done: !firrtl.uint<1>
        ) {
            %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
            %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
            firrtl.connect %out, %c0_ui32 : !firrtl.uint<32>, !firrtl.uint<32>
            firrtl.connect %done, %c0_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
        }
    }

    cmt2.circuit {
        // External module with 4-cycle latency, II=3
        cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @multiply static<4> : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]
            ] {interval = #cmt2.interval<3>}
        } {
            conflict = [[@multiply, @multiply]],
            conflictFree = []
        }

        // Test 1: Result timing before method completes
        // The method has latency 4, but we try to capture result at cycle 2
        cmt2.module @ResultTooEarly(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            cmt2.proc.static_step @early_result_step <6> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                // expected-error @+1 {{result 0 timing [2, 3) starts before method latency (4)}}
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) {arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>], result_timing = [#cmt2.timing<[2, 3]>]} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @early_result_step
            }
        }

        // Test 2: Pipelined calls with spacing < II
        // The method has II=3, but we try to call again after only 2 cycles
        cmt2.module @PipelineSpacingError(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            cmt2.proc.static_step @bad_pipeline_step <10> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %c3 = firrtl.constant 3 : !firrtl.uint<32>
                %c4 = firrtl.constant 4 : !firrtl.uint<32>

                // First call at cycle 0
                // expected-remark @+1 {{previous call to same method}}
                %r1 = cmt2.call @mult_unit @multiply(%c1, %c2) {arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>], result_timing = [#cmt2.timing<[4, 5]>]} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

                // Second call at cycle 2 (spacing = 2 < II = 3, ERROR)
                // expected-error @+1 {{pipelined call spacing (2 cycles) is less than initiation interval (3 cycles)}}
                %r2 = cmt2.call @mult_unit @multiply(%c3, %c4) {arg_timing = [#cmt2.timing<[2, 3]>, #cmt2.timing<[2, 3]>], result_timing = [#cmt2.timing<[6, 7]>]} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run_pipeline() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @bad_pipeline_step
            }
        }
    }
}
