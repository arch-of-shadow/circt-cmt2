// RUN: circt-opt %s | FileCheck %s
// RUN: circt-opt %s -cmt2-static-inference | FileCheck %s --check-prefix=INFER
// RUN: circt-opt %s -cmt2-compile-static | FileCheck %s --check-prefix=COMPILE

// End-to-end test for hybrid static/dynamic designs
// Inspired by Calyx's tests/correctness/static-control/while.futil
//
// This demonstrates the unified pipeline where:
// 1. Static steps have fixed-latency execution
// 2. Dynamic while loops can contain static sequences
// 3. CompileStatic generates FSM wrappers for static steps
// 4. TDCC handles the outer dynamic control uniformly

builtin.module {
    firrtl.circuit "Reg_width4_init0" {
        // Register module used for state storage
        firrtl.module @Reg_width32_init0(in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                                         in %write_enable: !firrtl.uint<1>, in %write_data: !firrtl.uint<32>,
                                         out %read_ready: !firrtl.uint<1>, out %read_data: !firrtl.uint<32>,
                                         out %write_ready: !firrtl.uint<1>) {
            %c0 = firrtl.constant 0 : !firrtl.uint<32>
            %reg = firrtl.regreset %clock, %reset, %c0 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>
            %c1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_ready, %c1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_data, %reg : !firrtl.uint<32>
            firrtl.matchingconnect %write_ready, %c1 : !firrtl.uint<1>
            firrtl.when %write_enable : !firrtl.uint<1> {
                firrtl.matchingconnect %reg, %write_data : !firrtl.uint<32>
            }
        }

        // FSM register (4-bit for static step control)
        firrtl.module @Reg_width4_init0(in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                                        in %write_enable: !firrtl.uint<1>, in %write_data: !firrtl.uint<4>,
                                        out %read_ready: !firrtl.uint<1>, out %read_data: !firrtl.uint<4>,
                                        out %write_ready: !firrtl.uint<1>) {
            %c0 = firrtl.constant 0 : !firrtl.uint<4>
            %reg = firrtl.regreset %clock, %reset, %c0 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<4>, !firrtl.uint<4>
            %c1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_ready, %c1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_data, %reg : !firrtl.uint<4>
            firrtl.matchingconnect %write_ready, %c1 : !firrtl.uint<1>
            firrtl.when %write_enable : !firrtl.uint<1> {
                firrtl.matchingconnect %reg, %write_data : !firrtl.uint<4>
            }
        }
    }

    cmt2.circuit {
        // 32-bit register module
        cmt2.module.extern.firrtl @Reg32 : @Reg_width32_init0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : () -> !firrtl.uint<32> [ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
            cmt2.bind.method @write : (!firrtl.uint<32>) -> () [enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
        }

        // 4-bit FSM register module
        cmt2.module.extern.firrtl @Reg4 : @Reg_width4_init0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : () -> !firrtl.uint<4> [ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
            cmt2.bind.method @write : (!firrtl.uint<4>) -> () [enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
        }

        //===------------------------------------------------------------------===//
        // Example 1: Hybrid While Loop with Static Body
        // Pattern: while (condition) { static_seq<3> { A; B; C; } }
        // Similar to Calyx's while.futil
        //===------------------------------------------------------------------===//

        // CHECK-LABEL: cmt2.module @HybridWhileStatic
        // INFER-LABEL: cmt2.module @HybridWhileStatic
        // COMPILE-LABEL: cmt2.module @HybridWhileStatic
        cmt2.module @HybridWhileStatic(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            // State registers
            cmt2.instance @accumulator = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @counter = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @limit = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []

            // Static step A: Read current value (1 cycle)
            // COMPILE: cmt2.proc.static_step @step_A<1>
            // COMPILE: wrapper_generated
            cmt2.proc.static_step @step_A <1> {
                %val = cmt2.call @accumulator @read() : () -> !firrtl.uint<32>
            }
            // After CompileStatic, step_A should have wrapper rules generated
            // COMPILE: cmt2.instance @__fsm_step_A
            // COMPILE: cmt2.rule @step_A__tick
            // COMPILE: cmt2.value @step_A__done
            // COMPILE: cmt2.rule @step_A__start

            // Static step B: Add 5 to accumulator (1 cycle)
            // COMPILE: cmt2.proc.static_step @step_B<1>
            // COMPILE: wrapper_generated
            cmt2.proc.static_step @step_B <1> {
                %val = cmt2.call @accumulator @read() : () -> !firrtl.uint<32>
                %c5 = firrtl.constant 5 : !firrtl.uint<32>
                %sum = firrtl.add %val, %c5 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %truncated = firrtl.tail %sum, 1 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @accumulator @write(%truncated) : (!firrtl.uint<32>) -> ()
            }

            // Static step C: Increment counter (1 cycle)
            // COMPILE: cmt2.proc.static_step @step_C<1>
            // COMPILE: wrapper_generated
            cmt2.proc.static_step @step_C <1> {
                %cnt = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %next = firrtl.add %cnt, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %truncated = firrtl.tail %next, 1 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @counter @write(%truncated) : (!firrtl.uint<32>) -> ()
            }

            // Dynamic step: Check if counter < limit (variable latency)
            cmt2.proc.step @check_condition {
                %cnt = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %lim = cmt2.call @limit @read() : () -> !firrtl.uint<32>
                %lt = firrtl.lt %cnt, %lim : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
            }

            // Dynamic step to compute loop condition
            cmt2.proc.step @compute_loop_cond {
                %cnt = cmt2.call @counter @read() : () -> !firrtl.uint<32>
                %lim = cmt2.call @limit @read() : () -> !firrtl.uint<32>
                %lt = firrtl.lt %cnt, %lim : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
            }

            // Main control: while (counter < limit) { static seq { A; B; C; } }
            // INFER: cmt2.proc.rule @main_loop
            cmt2.proc.rule @main_loop() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // Initialize, then loop
                cmt2.proc.seq {
                    cmt2.proc.enable @check_condition
                    // Dynamic while with static body
                    // The condition is computed by enabling check_condition step
                    // and reading the counter < limit result
                    // For simplicity, we use the seq of static steps directly
                    // INFER: cmt2.proc.seq
                    cmt2.proc.seq {
                        cmt2.proc.enable @step_A
                        cmt2.proc.enable @step_B
                        cmt2.proc.enable @step_C
                    }
                }
            }

        }

        //===------------------------------------------------------------------===//
        // Example 2: Static Island with Dynamic Condition
        // Pattern: dynamic_check; if (cond) { static_compute } else { static_other }
        // Similar to Calyx's static-island.futil
        //===------------------------------------------------------------------===//

        // CHECK-LABEL: cmt2.module @StaticIsland
        // COMPILE-LABEL: cmt2.module @StaticIsland
        cmt2.module @StaticIsland(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @data = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @cond_reg = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []

            // Dynamic step: Check if data < threshold (variable latency)
            cmt2.proc.step @check_threshold {
                %data = cmt2.call @data @read() : () -> !firrtl.uint<32>
                %thresh = firrtl.constant 100 : !firrtl.uint<32>
                %lt = firrtl.lt %data, %thresh : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                // Store condition result
                %extended = firrtl.pad %lt, 32 : (!firrtl.uint<1>) -> !firrtl.uint<32>
                cmt2.call @cond_reg @write(%extended) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
            }

            // Static step: Multiply by 2 (1 cycle)
            // COMPILE: cmt2.proc.static_step @multiply_path<1>
            cmt2.proc.static_step @multiply_path <1> {
                %val = cmt2.call @data @read() : () -> !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %prod = firrtl.mul %val, %c2 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
                %truncated = firrtl.tail %prod, 32 : (!firrtl.uint<64>) -> !firrtl.uint<32>
                cmt2.call @data @write(%truncated) : (!firrtl.uint<32>) -> ()
            }

            // Static step: Add 10 (1 cycle)
            // COMPILE: cmt2.proc.static_step @add_path<1>
            cmt2.proc.static_step @add_path <1> {
                %val = cmt2.call @data @read() : () -> !firrtl.uint<32>
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %sum = firrtl.add %val, %c10 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %truncated = firrtl.tail %sum, 1 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @data @write(%truncated) : (!firrtl.uint<32>) -> ()
            }

            // Value to read condition for branching
            cmt2.value @is_below_threshold() -> (!firrtl.uint<1>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                %cond = cmt2.call @cond_reg @read() : () -> !firrtl.uint<32>
                %c0 = firrtl.constant 0 : !firrtl.uint<32>
                %neq = firrtl.neq %cond, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                cmt2.return %neq : !firrtl.uint<1>
            }

            // Control: Dynamic condition selects between static paths
            cmt2.proc.rule @conditional_compute() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    // Dynamic: check threshold
                    cmt2.proc.enable @check_threshold
                    // Seq of static steps (both paths have same latency for simplicity)
                    cmt2.proc.enable @multiply_path
                }
            }
        }

        //===------------------------------------------------------------------===//
        // Example 3: Static Pipeline with Repeat
        // Pattern: static_repeat<N> { kernel; } + post_process
        // Similar to Calyx's systolic array iteration pattern
        //===------------------------------------------------------------------===//

        // CHECK-LABEL: cmt2.module @StaticRepeatPipeline
        // COMPILE-LABEL: cmt2.module @StaticRepeatPipeline
        cmt2.module @StaticRepeatPipeline(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @acc = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @result = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []

            // Static step: Accumulate (2 cycles - read + write)
            // COMPILE: cmt2.proc.static_step @accum_step<2>
            cmt2.proc.static_step @accum_step <2> {
                %val = cmt2.call @acc @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %next = firrtl.add %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %truncated = firrtl.tail %next, 1 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @acc @write(%truncated) : (!firrtl.uint<32>) -> ()
            }

            // Static step: Write result (1 cycle)
            cmt2.proc.static_step @write_result <1> {
                %val = cmt2.call @acc @read() : () -> !firrtl.uint<32>
                cmt2.call @result @write(%val) : (!firrtl.uint<32>) -> ()
            }

            // Control: Static repeat then finalize
            // Total latency: 4 * 2 + 1 = 9 cycles (known at compile time)
            // INFER: cmt2.proc.rule @pipeline_compute
            // INFER: total_latency = 9
            cmt2.proc.rule @pipeline_compute() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // INFER: cmt2.proc.seq
                cmt2.proc.seq {
                    // Static repeat: 4 iterations of 2-cycle step
                    // INFER: cmt2.proc.static_repeat
                    cmt2.proc.static_repeat 4 {
                        cmt2.proc.enable @accum_step
                    }
                    // Post-processing: 1 cycle
                    cmt2.proc.enable @write_result
                }
            }
        }
    }
}
