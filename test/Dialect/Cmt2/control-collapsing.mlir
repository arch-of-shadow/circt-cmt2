// RUN: circt-opt %s -cmt2-control-collapsing | FileCheck %s

// Tests for the ControlCollapsing pass - simplifies procedural control structures
// This pass is currently a stub - these tests verify the pass runs without crashing
// and document the expected transformations once implemented.
//
// Key transformations (TODO):
// 1. Flatten nested seq: seq { seq { A; B }; C } -> seq { A; B; C }
// 2. Flatten nested par: par { par { A; B }; C } -> par { A; B; C }
// 3. Remove empty seq/par
// 4. Remove single-child seq/par wrappers

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

        // CHECK: cmt2.module @TestControlCollapsing
        cmt2.module @TestControlCollapsing(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @reg_a = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.proc.static_step @step_a <1> {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
            }

            cmt2.proc.static_step @step_b <1> {
                %c42 = firrtl.constant 42 : !firrtl.uint<32>
                cmt2.call @reg_a @write(%c42) : (!firrtl.uint<32>) -> ()
            }

            cmt2.proc.static_step @step_c <1> {
                %a = cmt2.call @reg_a @read() : () -> !firrtl.uint<32>
            }

            // Test 1: Nested seq should be flattened
            // seq { seq { A; B }; C } -> seq { A; B; C }
            // (Currently stub - pass runs without transformation)
            // CHECK: cmt2.proc.rule @test_nested_seq
            cmt2.proc.rule @test_nested_seq() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.seq
                cmt2.proc.seq {
                    cmt2.proc.seq {
                        cmt2.proc.enable @step_a
                        cmt2.proc.enable @step_b
                    }
                    cmt2.proc.enable @step_c
                }
            }

            // Test 2: Nested par should be flattened
            // par { par { A; B }; C } -> par { A; B; C }
            // (Currently stub - pass runs without transformation)
            // CHECK: cmt2.proc.rule @test_nested_par
            cmt2.proc.rule @test_nested_par() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.par
                cmt2.proc.par {
                    cmt2.proc.par {
                        cmt2.proc.enable @step_a
                        cmt2.proc.enable @step_b
                    }
                    cmt2.proc.enable @step_c
                }
            }

            // Test 3: Single-child seq should be unwrapped
            // seq { A } -> A
            // (Currently stub - pass runs without transformation)
            // CHECK: cmt2.proc.rule @test_single_seq
            cmt2.proc.rule @test_single_seq() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @step_a
                }
            }

            // Test 4: Mixed nested control
            // seq { par { A; par { B; C } }; D }
            // (Currently stub - pass runs without transformation)
            // CHECK: cmt2.proc.rule @test_mixed_nested
            cmt2.proc.rule @test_mixed_nested() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                // CHECK: cmt2.proc.seq
                cmt2.proc.seq {
                    cmt2.proc.par {
                        cmt2.proc.enable @step_a
                        cmt2.proc.par {
                            cmt2.proc.enable @step_b
                            cmt2.proc.enable @step_c
                        }
                    }
                    cmt2.proc.enable @step_a
                }
            }
        }
    }
}
