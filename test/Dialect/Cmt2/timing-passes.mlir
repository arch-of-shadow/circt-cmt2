// RUN: circt-opt %s -cmt2-timing-inference | FileCheck %s --check-prefix=INFER
// RUN: circt-opt %s -cmt2-timing-validation | FileCheck %s --check-prefix=VALID
// RUN: circt-opt %s -cmt2-timing-inference -cmt2-timing-validation | FileCheck %s --check-prefix=BOTH

// Tests for cycle-precise timing passes: TimingInference and TimingValidation.

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
        // External module with known static timing (4 cycles, II=3)
        cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @multiply static<4> : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]
            ] {interval = #cmt2.interval<3>}
        } {
            conflict = [[@multiply, @multiply]],
            conflictFree = []
        }

        // Test 1: TimingInference should add timing to calls without explicit timing
        // INFER-LABEL: cmt2.module @InferCallTiming
        // VALID-LABEL: cmt2.module @InferCallTiming
        // BOTH-LABEL: cmt2.module @InferCallTiming
        cmt2.module @InferCallTiming(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // Static step with call that has no timing - inference should add it
            // INFER: cmt2.proc.static_step @infer_step<6>
            // INFER: cmt2.call @mult_unit @multiply
            // The call should get inferred timing after the pass
            cmt2.proc.static_step @infer_step <6> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                // Call without explicit timing - TimingInference should add it
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            // Static step with partial timing: arg_timing present but
            // result_timing omitted. Inference should fill in result_timing.
            // INFER: cmt2.proc.static_step @infer_partial_step<10>
            // INFER: %{{.*}} = cmt2.call @mult_unit @multiply
            // INFER-SAME: arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>]
            // INFER-SAME: call_timing = #cmt2.timing<[3, 4]>
            // INFER-SAME: result_timing = [#cmt2.timing<[7, 8]>]
            cmt2.proc.static_step @infer_partial_step <10> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %result = cmt2.call @mult_unit @multiply(%c1, %c2) {
                    call_timing = #cmt2.timing<[3, 4]>,
                    arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @compute() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @infer_step
            }
        }

        // Test 2: TimingValidation should pass for correct timing
        // VALID-LABEL: cmt2.module @ValidTiming
        cmt2.module @ValidTiming(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // Correct timing: latency >= 4 (method latency), results read at cycle 4
            // VALID: cmt2.proc.static_step @valid_step<6>
            cmt2.proc.static_step @valid_step <6> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                // Correct: args at cycle 0, results at cycle 4 (method latency is 4)
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) {arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>], result_timing = [#cmt2.timing<[4, 5]>]} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @valid_step
            }
        }

        // Test 3: Pipelined calls with correct spacing (II=3)
        // VALID-LABEL: cmt2.module @ValidPipelined
        cmt2.module @ValidPipelined(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // Two pipelined calls with spacing >= II (3)
            // VALID: cmt2.proc.static_step @pipelined_step<10>
            cmt2.proc.static_step @pipelined_step <10> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %c3 = firrtl.constant 3 : !firrtl.uint<32>
                %c4 = firrtl.constant 4 : !firrtl.uint<32>

                // First call at cycle 0
                %r1 = cmt2.call @mult_unit @multiply(%c1, %c2) {arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>], result_timing = [#cmt2.timing<[4, 5]>]} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

                // Second call at cycle 3 (spacing = 3 >= II = 3, OK)
                %r2 = cmt2.call @mult_unit @multiply(%c3, %c4) {
                    call_timing = #cmt2.timing<[3, 4]>,
                    arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>],
                    result_timing = [#cmt2.timing<[7, 8]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run_pipeline() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @pipelined_step
            }
        }

        // Test 4: Combined inference and validation pipeline
        // BOTH-LABEL: cmt2.module @CombinedPass
        cmt2.module @CombinedPass(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // BOTH: cmt2.proc.static_step @combined_step<8>
            cmt2.proc.static_step @combined_step <8> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                // Timing will be inferred, then validated
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @combined_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @combined_step
            }
        }
    }
}
