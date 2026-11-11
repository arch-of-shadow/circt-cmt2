// RUN: circt-opt %s -cmt2-static-inference | FileCheck %s

// Tests for the StaticInference pass - infers latencies and marks promotable control

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

        // CHECK-LABEL: cmt2.module @TestStaticInference
        cmt2.module @TestStaticInference(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @reg_a = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Test 1: Static step with known latency (3 cycles)
            // CHECK: cmt2.proc.static_step @step_3<3>
            cmt2.proc.static_step @step_3 <3> {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
            }

            // Test 2: Static step with latency 2
            // CHECK: cmt2.proc.static_step @step_2<2>
            cmt2.proc.static_step @step_2 <2> {
                %c42 = firrtl.constant 42 : !firrtl.uint<32>
                cmt2.call @reg_a @write(%c42) : (!firrtl.uint<32>) -> ()
            }

            // Test 3: Dynamic step (not promotable)
            cmt2.proc.step @dynamic_step {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
            }

            // Test 4: Rule with static_repeat - should be marked promotable
            // Static repeat with 4 iterations of a 3-cycle step = 12 cycles
            // CHECK: cmt2.proc.rule @test_static_repeat () -> () attributes {promotable, total_latency = 12 : i64}
            cmt2.proc.rule @test_static_repeat() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.static_repeat 4 {
                // CHECK:   cmt2.proc.enable @step_3
                // CHECK: } {inferred_latency = 12 : i64, promotable}
                cmt2.proc.static_repeat 4 {
                    cmt2.proc.enable @step_3
                }
            }

            // Test 5: Rule with seq of static steps
            // Sequential: sum of latencies = 3 + 2 = 5 cycles
            // CHECK: cmt2.proc.rule @test_seq () -> () attributes {promotable, total_latency = 5 : i64}
            cmt2.proc.rule @test_seq() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.seq {
                // CHECK:   cmt2.proc.enable @step_3
                // CHECK:   cmt2.proc.enable @step_2
                // CHECK: } {inferred_latency = 5 : i64, promotable}
                cmt2.proc.seq {
                    cmt2.proc.enable @step_3
                    cmt2.proc.enable @step_2
                }
            }

            // Test 6: Rule with par of static steps
            // Parallel: max of latencies = max(3, 2) = 3 cycles
            // CHECK: cmt2.proc.rule @test_par () -> () attributes {promotable, total_latency = 3 : i64}
            cmt2.proc.rule @test_par() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.par {
                // CHECK:   cmt2.proc.enable @step_3
                // CHECK:   cmt2.proc.enable @step_2
                // CHECK: } {inferred_latency = 3 : i64, promotable}
                cmt2.proc.par {
                    cmt2.proc.enable @step_3
                    cmt2.proc.enable @step_2
                }
            }

            // Test 7: Rule with static_if with explicit latencies
            // Static if: max(3, 2) = 3 cycles (control region has cmt2.call which is unknown)
            // CHECK: cmt2.proc.rule @test_static_if () -> ()
            cmt2.proc.rule @test_static_if() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %cond = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
                %c0 = firrtl.constant 0 : !firrtl.uint<32>
                %is_zero = firrtl.eq %cond, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                // CHECK: cmt2.proc.static_if
                // CHECK: } {inferred_latency = 3 : i64, promotable}
                cmt2.proc.static_if %is_zero : !firrtl.uint<1> <3, 2> {
                    cmt2.proc.enable @step_3
                } else {
                    cmt2.proc.enable @step_2
                }
            }

            // Test 8: Rule with nested static constructs
            // seq { step_3(3), static_repeat 2 { step_2(2) }(4), step_3(3) } = 10 cycles
            // CHECK: cmt2.proc.rule @test_nested () -> () attributes {promotable, total_latency = 10 : i64}
            cmt2.proc.rule @test_nested() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.seq {
                // CHECK: } {inferred_latency = 10 : i64, promotable}
                cmt2.proc.seq {
                    cmt2.proc.enable @step_3
                    cmt2.proc.static_repeat 2 {
                        cmt2.proc.enable @step_2
                    }
                    cmt2.proc.enable @step_3
                }
            }

            // Test 9: Rule with dynamic step - NOT promotable
            // CHECK: cmt2.proc.rule @test_not_promotable () -> () {
            // CHECK-NOT: promotable
            // CHECK: cmt2.proc.enable @dynamic_step
            cmt2.proc.rule @test_not_promotable() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // Dynamic step doesn't have known latency
                cmt2.proc.enable @dynamic_step
            }

            // Test 10: Rule with explicit body_latency in static_repeat
            // CHECK: cmt2.proc.rule @test_explicit_body_latency () -> () attributes {promotable, total_latency = 15 : i64}
            cmt2.proc.rule @test_explicit_body_latency() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // 3 iterations * 5 cycles per iteration = 15 cycles
                // CHECK: cmt2.proc.static_repeat 3<5> {
                // CHECK: } {inferred_latency = 15 : i64, promotable}
                cmt2.proc.static_repeat 3 <5> {
                    cmt2.proc.enable @step_3
                }
            }
        }
    }
}
