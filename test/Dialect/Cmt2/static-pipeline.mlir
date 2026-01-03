// RUN: circt-opt %s -cmt2-static-inference | FileCheck %s --check-prefix=INFER
// RUN: circt-opt %s -cmt2-static-inference -cmt2-control-collapsing | FileCheck %s --check-prefix=PIPELINE

// End-to-end test for the static pass pipeline
// This test demonstrates the full static compilation flow inspired by Calyx.
//
// Pass pipeline:
//   1. StaticInference: Infer latencies and mark promotable control structures
//   2. ControlCollapsing: Flatten nested seq/par (currently stub)
//   3. StaticPromotion: Promote dynamic control to static (when applicable)
//   4. CompileStatic: Compile static control to FSM (future)

builtin.module {
    firrtl.circuit "Adder" {
        // Simple adder module with 1-cycle latency
        firrtl.module @Adder(in %a: !firrtl.uint<32>, in %b: !firrtl.uint<32>,
                            in %go: !firrtl.uint<1>, in %clock: !firrtl.clock,
                            out %result: !firrtl.uint<32>, out %done: !firrtl.uint<1>) {
            %sum = firrtl.add %a, %b : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
            %truncated = firrtl.tail %sum, 1 : (!firrtl.uint<33>) -> !firrtl.uint<32>
            firrtl.connect %result, %truncated : !firrtl.uint<32>, !firrtl.uint<32>
            firrtl.connect %done, %go : !firrtl.uint<1>, !firrtl.uint<1>
        }
    }

    cmt2.circuit {
        cmt2.module.extern.firrtl @adder : @Adder(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.method @add : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["a", "b"], results = ["result"]
            ]
        } {
            conflict = [[@add, @add]],
            conflictFree = []
        }

        // INFER-LABEL: cmt2.module @StaticComputeExample
        // PIPELINE-LABEL: cmt2.module @StaticComputeExample
        cmt2.module @StaticComputeExample(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @add_unit = @adder (%clk) : !firrtl.clock

            // Static step for addition with 1 cycle latency
            // INFER: cmt2.proc.static_step @do_add<1>
            cmt2.proc.static_step @do_add <1> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %result = cmt2.call @add_unit @add(%c1, %c2) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            // Static step for another operation with 2 cycles
            // INFER: cmt2.proc.static_step @do_mul<2>
            cmt2.proc.static_step @do_mul <2> {
                %c3 = firrtl.constant 3 : !firrtl.uint<32>
                %c4 = firrtl.constant 4 : !firrtl.uint<32>
                %result = cmt2.call @add_unit @add(%c3, %c4) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            // Compute pipeline: seq of static steps
            // Total latency = 1 + 2 = 3 cycles
            // INFER: cmt2.proc.rule @compute_seq () -> () attributes {promotable, total_latency = 3 : i64}
            // PIPELINE: cmt2.proc.rule @compute_seq () -> () attributes {promotable, total_latency = 3 : i64}
            cmt2.proc.rule @compute_seq() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // INFER: cmt2.proc.seq {
                // INFER:   cmt2.proc.enable @do_add
                // INFER:   cmt2.proc.enable @do_mul
                // INFER: } {inferred_latency = 3 : i64, promotable}
                cmt2.proc.seq {
                    cmt2.proc.enable @do_add
                    cmt2.proc.enable @do_mul
                }
            }

            // Parallel computation: par of static steps
            // Total latency = max(1, 2) = 2 cycles
            // INFER: cmt2.proc.rule @compute_par () -> () attributes {promotable, total_latency = 2 : i64}
            // PIPELINE: cmt2.proc.rule @compute_par () -> () attributes {promotable, total_latency = 2 : i64}
            cmt2.proc.rule @compute_par() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // INFER: cmt2.proc.par {
                // INFER:   cmt2.proc.enable @do_add
                // INFER:   cmt2.proc.enable @do_mul
                // INFER: } {inferred_latency = 2 : i64, promotable}
                cmt2.proc.par {
                    cmt2.proc.enable @do_add
                    cmt2.proc.enable @do_mul
                }
            }

            // Repeated computation: static_repeat with static steps
            // Total latency = 4 * 1 = 4 cycles
            // INFER: cmt2.proc.rule @compute_repeat () -> () attributes {promotable, total_latency = 4 : i64}
            // PIPELINE: cmt2.proc.rule @compute_repeat () -> () attributes {promotable, total_latency = 4 : i64}
            cmt2.proc.rule @compute_repeat() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // INFER: cmt2.proc.static_repeat 4 {
                // INFER:   cmt2.proc.enable @do_add
                // INFER: } {inferred_latency = 4 : i64, promotable}
                cmt2.proc.static_repeat 4 {
                    cmt2.proc.enable @do_add
                }
            }

            // Complex pipeline: seq { par { a, b }, repeat 3 { a } }
            // Total = max(1,2) + 3*1 = 2 + 3 = 5 cycles
            // INFER: cmt2.proc.rule @compute_complex () -> () attributes {promotable, total_latency = 5 : i64}
            // PIPELINE: cmt2.proc.rule @compute_complex () -> () attributes {promotable, total_latency = 5 : i64}
            cmt2.proc.rule @compute_complex() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // INFER: cmt2.proc.seq {
                // INFER: } {inferred_latency = 5 : i64, promotable}
                cmt2.proc.seq {
                    cmt2.proc.par {
                        cmt2.proc.enable @do_add
                        cmt2.proc.enable @do_mul
                    }
                    cmt2.proc.static_repeat 3 {
                        cmt2.proc.enable @do_add
                    }
                }
            }
        }
    }
}
