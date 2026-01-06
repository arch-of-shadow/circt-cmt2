// RUN: circt-opt %s -cmt2-compile-static | FileCheck %s

// Test CompileStatic pass that transforms static steps into wrappers
// with internal FSM, tick rule, done value, and start rule.

builtin.module {
    firrtl.circuit "Reg_width4_init0" {
        // Register module used for FSM
        firrtl.module @Reg_width4_init0(in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                                        in %write_enable: !firrtl.uint<1>, in %write_data: !firrtl.uint<4>,
                                        out %read_ready: !firrtl.uint<1>, out %read_data: !firrtl.uint<4>,
                                        out %write_ready: !firrtl.uint<1>) {
            %c0_ui4 = firrtl.constant 0 : !firrtl.uint<4>
            %reg = firrtl.regreset %clock, %reset, %c0_ui4 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<4>, !firrtl.uint<4>
            %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_data, %reg : !firrtl.uint<4>
            firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>
            firrtl.when %write_enable : !firrtl.uint<1> {
                firrtl.matchingconnect %reg, %write_data : !firrtl.uint<4>
            }
        }

        // Simple adder module
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
        // Register module for FSM
        cmt2.module.extern.firrtl @Reg_width4_init0 : @Reg_width4_init0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : () -> !firrtl.uint<4> [ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
            cmt2.bind.method @write : (!firrtl.uint<4>) -> () [enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
        }

        // Adder module
        cmt2.module.extern.firrtl @adder : @Adder(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.method @add : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> (!firrtl.uint<1>, !firrtl.uint<32>) [
                enable = "go", ready = "done", arguments = ["a", "b"], results = ["result"]
            ]
        }

        // CHECK-LABEL: cmt2.module @StaticWrapperTest
        cmt2.module @StaticWrapperTest(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @add_unit = @adder (%clk) : !firrtl.clock

            // Static step with 4 cycle latency
            // After CompileStatic, should have wrapper attributes and generated rules
            // CHECK: cmt2.proc.static_step @compute<4>
            // CHECK: wrapper_generated
            cmt2.proc.static_step @compute <4> {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %c2 = firrtl.constant 2 : !firrtl.uint<32>
                %result = cmt2.call @add_unit @add(%c1, %c2) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            }

            // CHECK: cmt2.instance @__fsm_compute = @Reg_width4_init0
            // CHECK: cmt2.rule @compute__tick
            // CHECK: cmt2.value @compute__done
            // CHECK: cmt2.rule @compute__start
        }
    }
}
