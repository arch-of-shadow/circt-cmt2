// RUN: circt-opt %s \
// RUN:   -cmt2-timing-inference \
// RUN:   -cmt2-timing-validation \
// RUN:   -cmt2-static-fsm-allocation \
// RUN:   -cmt2-compile-static \
// RUN:   | FileCheck %s

// End-to-end test for multi-call scheduling scenarios.
// Tests complex scheduling patterns with multiple method calls.

builtin.module {
    firrtl.circuit "Components" {
        // Top-level module (required by FIRRTL)
        firrtl.module @Components() {}

        // Combinational adder (0 latency)
        firrtl.module @Adder(
            in %a: !firrtl.uint<32>,
            in %b: !firrtl.uint<32>,
            out %out: !firrtl.uint<32>
        ) {
            %sum = firrtl.add %a, %b : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
            %result = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
            firrtl.connect %out, %result : !firrtl.uint<32>, !firrtl.uint<32>
        }

        // Pipelined multiplier (3-cycle latency)
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

        // Memory with 1-cycle read latency
        firrtl.module @Memory(
            in %clk: !firrtl.clock,
            in %ren: !firrtl.uint<1>,
            in %addr: !firrtl.uint<8>,
            out %data: !firrtl.uint<32>,
            out %rvalid: !firrtl.uint<1>
        ) {
            %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
            %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
            firrtl.connect %data, %c0_ui32 : !firrtl.uint<32>, !firrtl.uint<32>
            firrtl.connect %rvalid, %c0_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
        }
    }

    cmt2.circuit {
        // Combinational adder: no latency
        // CHECK-LABEL: cmt2.module.extern.firrtl @adder
        cmt2.module.extern.firrtl @adder : @Adder() {
            cmt2.bind.method @add static<1> : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32> [
                arguments = ["a", "b"], results = ["out"]
            ]
        } {
            conflict = [],
            conflictFree = []
        }

        // Pipelined multiplier: 3-cycle latency
        // CHECK-LABEL: cmt2.module.extern.firrtl @mult
        cmt2.module.extern.firrtl @mult : @PipelinedMult(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @multiply static<3> : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["left", "right"], results = ["out"]
            ]
        } {
            conflict = [[@multiply, @multiply]],
            conflictFree = []
        }

        // Memory: 1-cycle read latency (static<1>)
        // CHECK-LABEL: cmt2.module.extern.firrtl @mem
        cmt2.module.extern.firrtl @mem : @Memory(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clk : !firrtl.clock
            cmt2.bind.method @read static<1> : (!firrtl.uint<1>, !firrtl.uint<8>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "ren", ready = "rvalid", arguments = ["addr"], results = ["data"]
            ]
        } {
            conflict = [],
            conflictFree = []
        }

        // Test 1: Multiple independent calls to different units
        // Should execute in parallel, FSM covers all
        // CHECK-LABEL: cmt2.module @ParallelCalls
        cmt2.module @ParallelCalls(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock
            cmt2.instance @mem_unit = @mem (%clk) : !firrtl.clock

            // Both mult (3 cycles) and mem read (2 cycles) start at cycle 0
            // Total latency is max(3, 2) = 3 cycles for step, but need +1 for state = 4
            // CHECK: cmt2.proc.static_step @parallel_step<4>
            // CHECK: cmt2.call @mult_unit @multiply
            // CHECK-SAME: fsm_start_state = 0
            // CHECK: cmt2.call @mem_unit @read
            // CHECK-SAME: fsm_start_state = 0
            // CHECK: fsm_states = 4
            // CHECK-SAME: static_compiled
            cmt2.proc.static_step @parallel_step <4> {
                %c10 = firrtl.constant 10 : !firrtl.uint<32>
                %c20 = firrtl.constant 20 : !firrtl.uint<32>
                %addr = firrtl.constant 5 : !firrtl.uint<8>

                // Multiply at cycle 0, result at cycle 3
                %mult_r = cmt2.call @mult_unit @multiply(%c10, %c20) {
                    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[3, 4]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

                // Read at cycle 0, result at cycle 1
                %mem_r = cmt2.call @mem_unit @read(%addr) {
                    arg_timing = [#cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[1, 2]>]
                } : (!firrtl.uint<8>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @parallel_step
            }
        }

        // Test 2: Sequential data dependency
        // Read from memory, then multiply result by constant
        // CHECK-LABEL: cmt2.module @SequentialDataDep
        cmt2.module @SequentialDataDep(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock
            cmt2.instance @mem_unit = @mem (%clk) : !firrtl.clock

            // Read takes 2 cycles (result at 1), then mult starts at cycle 2
            // Mult result at cycle 2+3=5, so need 6 states
            // CHECK: cmt2.proc.static_step @sequential_step<6>
            // Memory read first
            // CHECK: cmt2.call @mem_unit @read
            // CHECK-SAME: fsm_start_state = 0
            // Then multiply (starts after read completes)
            // CHECK: cmt2.call @mult_unit @multiply
            // CHECK-SAME: fsm_start_state = 2
            // CHECK: fsm_states = 6
            // CHECK-SAME: static_compiled
            cmt2.proc.static_step @sequential_step <6> {
                %addr = firrtl.constant 10 : !firrtl.uint<8>
                %const = firrtl.constant 2 : !firrtl.uint<32>

                // Read at cycle 0, result at cycle 1-2
                %mem_data = cmt2.call @mem_unit @read(%addr) {
                    arg_timing = [#cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[1, 2]>]
                } : (!firrtl.uint<8>) -> !firrtl.uint<32>

                // Multiply starts at cycle 2 (after read result is ready)
                %mult_r = cmt2.call @mult_unit @multiply(%mem_data, %const) {
                    call_timing = #cmt2.timing<[2, 3]>,
                    arg_timing = [#cmt2.timing<[2, 3]>, #cmt2.timing<[2, 3]>],
                    result_timing = [#cmt2.timing<[5, 6]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @sequential_step
            }
        }

        // Test 3: Three calls with staggered timing
        // CHECK-LABEL: cmt2.module @StaggeredCalls
        cmt2.module @StaggeredCalls(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @mult_unit = @mult (%clk) : !firrtl.clock
            cmt2.instance @mem_unit = @mem (%clk) : !firrtl.clock
            cmt2.instance @adder_unit = @adder

            // Three calls:
            // - Read at cycle 0, done at 1
            // - Mult at cycle 0, done at 3
            // - Add at cycle 3 (uses mult result), done at 3 (combinational, 1 cycle)
            // Total: 4 states + 1 = 5 states
            // CHECK: cmt2.proc.static_step @staggered_step<5>
            // CHECK: cmt2.call @mem_unit @read
            // CHECK: cmt2.call @mult_unit @multiply
            // CHECK: cmt2.call @adder_unit @add
            // CHECK: fsm_states = 5
            // CHECK-SAME: static_compiled
            cmt2.proc.static_step @staggered_step <5> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %addr = firrtl.constant 0 : !firrtl.uint<8>

                // Read at cycle 0
                %mem_data = cmt2.call @mem_unit @read(%addr) {
                    arg_timing = [#cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[1, 2]>]
                } : (!firrtl.uint<8>) -> !firrtl.uint<32>

                // Mult at cycle 0 (parallel with read)
                %mult_r = cmt2.call @mult_unit @multiply(%c1, %c2) {
                    arg_timing = [#cmt2.timing<[0, 1]>, #cmt2.timing<[0, 1]>],
                    result_timing = [#cmt2.timing<[3, 4]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>

                // Add at cycle 3 (after mult completes, uses mult result + mem result)
                %add_r = cmt2.call @adder_unit @add(%mult_r, %mem_data) {
                    call_timing = #cmt2.timing<[3, 4]>,
                    arg_timing = [#cmt2.timing<[3, 4]>, #cmt2.timing<[3, 4]>],
                    result_timing = [#cmt2.timing<[4, 5]>]
                } : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            cmt2.proc.rule @run() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.enable @staggered_step
            }
        }
    }
}
