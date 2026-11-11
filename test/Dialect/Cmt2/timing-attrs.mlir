// RUN: circt-opt %s | circt-opt | FileCheck %s

// Tests for cycle-precise timing attributes.
// This tests the parsing and printing of timing attributes on method
// signatures and call sites.

builtin.module {
    firrtl.circuit "PipelinedMult" {
        // Pipelined multiplier with 4-cycle latency
        firrtl.module @PipelinedMult(
            in %clk: !firrtl.clock,
            in %go: !firrtl.uint<1>,
            in %left: !firrtl.uint<32>,
            in %right: !firrtl.uint<32>,
            out %out: !firrtl.uint<32>,
            out %done: !firrtl.uint<1>
        ) {
            // Implementation omitted for test
            %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
            %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
            firrtl.connect %out, %c0_ui32 : !firrtl.uint<32>, !firrtl.uint<32>
            firrtl.connect %done, %c0_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
        }

        // Simple 1-cycle adder
        firrtl.module @Adder(
            in %clk: !firrtl.clock,
            in %go: !firrtl.uint<1>,
            in %a: !firrtl.uint<32>,
            in %b: !firrtl.uint<32>,
            out %result: !firrtl.uint<32>,
            out %done: !firrtl.uint<1>
        ) {
            %sum = firrtl.add %a, %b : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
            %trunc = firrtl.tail %sum, 1 : (!firrtl.uint<33>) -> !firrtl.uint<32>
            firrtl.connect %result, %trunc : !firrtl.uint<32>, !firrtl.uint<32>
            firrtl.connect %done, %go : !firrtl.uint<1>, !firrtl.uint<1>
        }
    }

    cmt2.circuit {
        // Test 1: External module with static method timing
        // CHECK-LABEL: cmt2.module.extern.firrtl @mult : @PipelinedMult
        // CHECK: cmt2.bind.method @multiply static<4>
        cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @multiply static<4> : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]
            ]
        } {
            conflict = [[@multiply, @multiply]],
            conflictFree = []
        }

        // Test 2: External module with static method and initiation interval
        // CHECK-LABEL: cmt2.module.extern.firrtl @mult_pipelined : @PipelinedMult
        // CHECK: cmt2.bind.method @multiply static<4> : {{.*}} {interval = #cmt2.interval<3>}
        cmt2.module.extern.firrtl @mult_pipelined : @PipelinedMult(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @multiply static<4> : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]
            ] {interval = #cmt2.interval<3>}
        } {
            conflict = [[@multiply, @multiply]],
            conflictFree = []
        }

        // Test 3: External module without static timing (dynamic)
        // CHECK-LABEL: cmt2.module.extern.firrtl @adder : @Adder
        // CHECK: cmt2.bind.method @add :
        // CHECK-NOT: static<
        cmt2.module.extern.firrtl @adder : @Adder(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @add : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["a", "b"], results = ["result"]
            ]
        } {
            conflict = [[@add, @add]],
            conflictFree = []
        }

        // Test 4: Module with static step and call timing
        // CHECK-LABEL: cmt2.module @StaticPipeline
        cmt2.module @StaticPipeline(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            // Static step with 5-cycle latency (result available at cycle 4)
            // CHECK: cmt2.proc.static_step @do_multiply<5>
            cmt2.proc.static_step @do_multiply <5> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                // Call with timing guards
                // CHECK: cmt2.call @mult_unit @multiply
                // CHECK-SAME: arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>]
                // CHECK-SAME: result_timing = [#cmt2.timing<[4, 5]>]
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) {arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>], result_timing = [#cmt2.timing<[4, 5]>]} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            // Rule using the static step
            cmt2.proc.rule @compute() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @do_multiply
            }
        }

        // Test 5: Module with pipelined static step (multiple calls with staggered timing)
        // CHECK-LABEL: cmt2.module @PipelinedCompute
        cmt2.module @PipelinedCompute(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult_pipelined (%clk) : !firrtl.clock

            // Pipelined static step with overlapping multiplies
            // CHECK: cmt2.proc.static_step @pipelined_multiply<10>
            cmt2.proc.static_step @pipelined_multiply <10> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %c3 = firrtl.constant 3 : !firrtl.uint<32>
                %c4 = firrtl.constant 4 : !firrtl.uint<32>

                // First multiply: cycles 0-3
                // CHECK: arg_timing = [#cmt2.timing<[0, 1]>
                %r1 = cmt2.call @mult_unit @multiply(%c1, %c2) {arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>], result_timing = [#cmt2.timing<[4, 5]>]} : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

                // Second multiply: cycles 3-6 (pipelined, II=3)
                // CHECK: arg_timing = [#cmt2.timing<[3, 4]>
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
                cmt2.proc.enable @pipelined_multiply
            }
        }
    }
}
