// RUN: circt-opt %s -cmt2-timing-inference -cmt2-static-fsm-allocation | FileCheck %s --check-prefix=ALLOC
// RUN: circt-opt %s -cmt2-timing-inference -cmt2-static-fsm-allocation -cmt2-compile-static | FileCheck %s --check-prefix=COMPILE
// RUN: circt-opt %s -cmt2-timing-inference -cmt2-static-fsm-allocation -cmt2-compile-static -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=STMT

// Tests for StaticFSMAllocation and CompileStatic passes.

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
            firrtl.connect %out, %c0_ui32 : !firrtl.uint<32>
            firrtl.connect %done, %c0_ui1 : !firrtl.uint<1>
        }
    }

    cmt2.circuit {
        // External module with known timing
        cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @multiply static<4> : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]
            ] {interval = #cmt2.interval<3>}
        } {
            conflict = [[@multiply, @multiply]],
            conflictFree = []
        }

        // Test 1: Small FSM should use one-hot encoding (6 states <= 8)
        // ALLOC-LABEL: cmt2.module @SmallFSM
        // ALLOC: cmt2.proc.static_step @small_step<6>
        // ALLOC: fsm_bitwidth = 6
        // ALLOC-SAME: fsm_encoding = "one_hot"
        // ALLOC-SAME: fsm_states = 6
        // COMPILE-LABEL: cmt2.module @SmallFSM
        // COMPILE: cmt2.proc.static_step @small_step<6>
        // COMPILE: fsm_done_expr = "fsm[5]"
        // COMPILE-SAME: fsm_init_expr = "6'b1"
        // COMPILE-SAME: fsm_next_expr = "{fsm[4:0], 1'b0}"
        cmt2.module @SmallFSM(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            cmt2.proc.static_step @small_step <6> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @small_step
            }
        }

        // Test 2: Large FSM should use binary encoding (12 states > 8)
        // ALLOC-LABEL: cmt2.module @LargeFSM
        // ALLOC: cmt2.proc.static_step @large_step<12>
        // ALLOC: fsm_bitwidth = 4
        // ALLOC-SAME: fsm_encoding = "binary"
        // ALLOC-SAME: fsm_states = 12
        // COMPILE-LABEL: cmt2.module @LargeFSM
        // COMPILE: cmt2.proc.static_step @large_step<12>
        // COMPILE: fsm_done_expr = "fsm == 11"
        // COMPILE-SAME: fsm_init_expr = "4'd0"
        // COMPILE-SAME: fsm_next_expr = "fsm + 1"
        cmt2.module @LargeFSM(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            cmt2.proc.static_step @large_step <12> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                %result = cmt2.call @mult_unit @multiply(%c10, %c20) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @large_step
            }
        }

        // Test 3: Pipelined calls with overlapping state ranges
        // ALLOC-LABEL: cmt2.module @PipelinedFSM
        // ALLOC: cmt2.proc.static_step @pipe_step<10>
        // ALLOC: state_assignments = {{.*}}[0, [0, "Enable"]]{{.*}}[3, [1, "Enable"]]{{.*}}[4, [0, "GetRes"]]{{.*}}[7, [1, "GetRes"]]
        // COMPILE-LABEL: cmt2.module @PipelinedFSM
        // COMPILE: cmt2.proc.static_step @pipe_step<10>
        // COMPILE: cmt2.call @mult_unit @multiply
        // COMPILE-SAME: fsm_guard_expr = "fsm == 0"
        // COMPILE: cmt2.call @mult_unit @multiply
        // COMPILE-SAME: fsm_guard_expr = "fsm == 3"
        // STMT-LABEL: cmt2.module @PipelinedFSM
        // STMT: cmt2.rule @run_state0() -> ()
        // STMT: cmt2.call @mult_unit @multiply
        // STMT-SAME: call_ty = "Enable"
        // STMT: cmt2.rule @run_state4() -> ()
        // STMT: cmt2.call @mult_unit @multiply
        // STMT-SAME: call_ty = "GetRes"
        // STMT: cmt2.rule @run_state3() -> ()
        // STMT: cmt2.call @mult_unit @multiply
        // STMT-SAME: call_ty = "Enable"
        // STMT: cmt2.rule @run_state7() -> ()
        // STMT: cmt2.call @mult_unit @multiply
        // STMT-SAME: call_ty = "GetRes"
        cmt2.module @PipelinedFSM(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock

            cmt2.proc.static_step @pipe_step <10> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %c3 = firrtl.constant 3 : !firrtl.uint<32>
                %c4 = firrtl.constant 4 : !firrtl.uint<32>
                // First call at cycle 0
                %r1 = cmt2.call @mult_unit @multiply(%c1, %c2) {
                    call_timing = #cmt2.timing<[0, 1]>,
                    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[4, 5]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
                // Second call at cycle 3
                %r2 = cmt2.call @mult_unit @multiply(%c3, %c4) {
                    call_timing = #cmt2.timing<[3, 4]>,
                    arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>],
                    result_timing = [#cmt2.timing<[7, 8]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @pipe_step
            }
        }
    }
}
