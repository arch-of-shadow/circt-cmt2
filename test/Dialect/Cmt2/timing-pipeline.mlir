// RUN: circt-opt %s \
// RUN:   -cmt2-timing-inference \
// RUN:   -cmt2-timing-validation \
// RUN:   -cmt2-static-fsm-allocation \
// RUN:   -cmt2-compile-static \
// RUN:   | FileCheck %s

// End-to-end test for the timing pass pipeline.
// Tests a pipelined multiplier example going through all timing passes.

builtin.module {
    firrtl.circuit "PipelinedMult" {
        // Stub FIRRTL module for the pipelined multiplier
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
        // External pipelined multiplier: 4-cycle latency, II=3 for pipelining
        // CHECK-LABEL: cmt2.module.extern.firrtl @mult
        cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @multiply static<4> : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]
            ] {interval = #cmt2.interval<3>}
        } {
            conflict = [[@multiply, @multiply]],
            conflictFree = []
        }

        // Test 1: Simple single-call pipeline
        // The timing passes should:
        // 1. Infer timing for the call
        // 2. Validate timing constraints
        // 3. Allocate FSM states (6 states, one-hot encoding)
        // 4. Compile to FSM register info
        // CHECK-LABEL: cmt2.module @SingleCall
        cmt2.module @SingleCall(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // CHECK: cmt2.proc.static_step @compute<6>
            // CHECK: cmt2.call @mult_unit @multiply
            // CHECK-SAME: fsm_guard_expr
            // CHECK-SAME: fsm_start_state
            // CHECK: fsm_bitwidth = 6
            // CHECK-SAME: fsm_encoding = "one_hot"
            // CHECK-SAME: fsm_states = 6
            // CHECK-SAME: static_compiled
            cmt2.proc.static_step @compute <6> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) {
                    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[4, 5]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @compute
            }
        }

        // Test 2: Pipelined calls with correct spacing (II=3)
        // CHECK-LABEL: cmt2.module @PipelinedCalls
        cmt2.module @PipelinedCalls(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // Two pipelined multiplies with 3-cycle spacing (respects II=3)
            // Total latency: first call at 0, result at 4; second at 3, result at 7
            // CHECK: cmt2.proc.static_step @pipeline_step<10>
            // First call should have fsm_start_state = 0
            // CHECK: cmt2.call @mult_unit @multiply
            // CHECK-SAME: fsm_start_state = 0
            // Second call should have fsm_start_state = 3
            // CHECK: cmt2.call @mult_unit @multiply
            // CHECK-SAME: fsm_start_state = 3
            // CHECK: fsm_states = 10
            // CHECK-SAME: static_compiled
            cmt2.proc.static_step @pipeline_step <10> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %c3 = firrtl.constant 3 : !firrtl.uint<32>
                %c4 = firrtl.constant 4 : !firrtl.uint<32>

                // First call at cycle 0
                %r1 = cmt2.call @mult_unit @multiply(%c1, %c2) {
                    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[4, 5]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

                // Second call at cycle 3 (spacing = 3 = II, valid!)
                %r2 = cmt2.call @mult_unit @multiply(%c3, %c4) {
                    arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>],
                    result_timing = [#cmt2.timing<[7, 8]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run_pipeline() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @pipeline_step
            }
        }

        // Test 3: Large FSM with binary encoding (> 8 states)
        // CHECK-LABEL: cmt2.module @LargeFSM
        cmt2.module @LargeFSM(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // 12-state FSM should use binary encoding (4 bits)
            // CHECK: cmt2.proc.static_step @large_step<12>
            // CHECK: fsm_bitwidth = 4
            // CHECK-SAME: fsm_encoding = "binary"
            // CHECK-SAME: fsm_states = 12
            // CHECK-SAME: static_compiled
            cmt2.proc.static_step @large_step <12> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) {
                    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[4, 5]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @large_step
            }
        }
    }
}
