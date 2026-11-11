// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s --check-prefix=TDCC
// RUN: circt-opt %s -cmt2-tdcc -cmt2-proc-stmt-to-action | FileCheck %s --check-prefix=STMT

//===----------------------------------------------------------------------===//
// Complex Par Testing
// Test parallel control with nested seq, if, and par structures
//===----------------------------------------------------------------------===//

builtin.module {
    cmt2.circuit {
        //===--------------------------------------------------------------===//
        // Test 1: Par with seq branches
        // par { seq { @a; @b }, seq { @c; @d } }
        //===--------------------------------------------------------------===//

        // TDCC-LABEL: cmt2.module @TestParWithSeqBranches
        // TDCC: cmt2.proc.rule @par_seq_rule
        // TDCC-SAME: tdcc.transitions

        // STMT-LABEL: cmt2.module @TestParWithSeqBranches
        // STMT: cmt2.instance @__fsm_par_seq_rule
        cmt2.module @TestParWithSeqBranches(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.proc.static_step @step_a<1> {}
            cmt2.proc.static_step @step_b<1> {}
            cmt2.proc.static_step @step_c<1> {}
            cmt2.proc.static_step @step_d<1> {}

            // Par with sequential branches:
            // Branch 1: seq { @a (1 cycle), @b (1 cycle) } = 2 cycles
            // Branch 2: seq { @c (1 cycle), @d (1 cycle) } = 2 cycles
            // Total: max(2, 2) = 2 cycles
            cmt2.proc.rule @par_seq_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.par {
                    cmt2.proc.seq {
                        cmt2.proc.enable @step_a
                        cmt2.proc.enable @step_b
                    }
                    cmt2.proc.seq {
                        cmt2.proc.enable @step_c
                        cmt2.proc.enable @step_d
                    }
                }
                cmt2.proc.control_end
            }
        }

        //===--------------------------------------------------------------===//
        // Test 2: Par with asymmetric seq branches
        // par { seq { @a; @b; @c }, enable @d }
        //===--------------------------------------------------------------===//

        // TDCC-LABEL: cmt2.module @TestParAsymmetric
        // TDCC: cmt2.proc.rule @par_asym_rule
        // TDCC-SAME: tdcc.complex_pars

        // STMT-LABEL: cmt2.module @TestParAsymmetric
        // STMT: cmt2.instance @__fsm_par_asym_rule
        cmt2.module @TestParAsymmetric(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.proc.static_step @step_a<2> {}
            cmt2.proc.static_step @step_b<2> {}
            cmt2.proc.static_step @step_c<2> {}
            cmt2.proc.static_step @step_d<1> {}

            // Par with asymmetric branches:
            // Branch 1: seq { @a (2), @b (2), @c (2) } = 6 cycles
            // Branch 2: @d (1 cycle)
            // Total: max(6, 1) = 6 cycles
            cmt2.proc.rule @par_asym_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.par {
                    cmt2.proc.seq {
                        cmt2.proc.enable @step_a
                        cmt2.proc.enable @step_b
                        cmt2.proc.enable @step_c
                    }
                    cmt2.proc.enable @step_d
                }
                cmt2.proc.control_end
            }
        }

        //===--------------------------------------------------------------===//
        // Test 3: Par with static_repeat in branches
        // par { static_repeat 3 { @a }, enable @b }
        //===--------------------------------------------------------------===//

        // TDCC-LABEL: cmt2.module @TestParWithRepeat
        // TDCC: cmt2.proc.rule @par_repeat_rule
        // TDCC-SAME: tdcc.transitions

        // STMT-LABEL: cmt2.module @TestParWithRepeat
        cmt2.module @TestParWithRepeat(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.proc.static_step @step_a<2> {}
            cmt2.proc.static_step @step_b<1> {}

            // Par with repeat:
            // Branch 1: static_repeat 3 { @a (2) } = 6 cycles
            // Branch 2: @b (1 cycle)
            // Total: max(6, 1) = 6 cycles
            cmt2.proc.rule @par_repeat_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.par {
                    cmt2.proc.static_repeat 3 {
                        cmt2.proc.enable @step_a
                    }
                    cmt2.proc.enable @step_b
                }
                cmt2.proc.control_end
            }
        }

        //===--------------------------------------------------------------===//
        // Test 4: Nested par
        // par { par { @a, @b }, par { @c, @d } }
        //===--------------------------------------------------------------===//

        // TDCC-LABEL: cmt2.module @TestNestedPar
        // TDCC: cmt2.proc.rule @nested_par_rule
        // TDCC-SAME: tdcc.complex_pars

        // STMT-LABEL: cmt2.module @TestNestedPar
        cmt2.module @TestNestedPar(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.proc.static_step @step_a<1> {}
            cmt2.proc.static_step @step_b<2> {}
            cmt2.proc.static_step @step_c<1> {}
            cmt2.proc.static_step @step_d<3> {}

            // Nested par:
            // Branch 1: par { @a (1), @b (2) } = max(1, 2) = 2 cycles
            // Branch 2: par { @c (1), @d (3) } = max(1, 3) = 3 cycles
            // Total: max(2, 3) = 3 cycles
            cmt2.proc.rule @nested_par_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.par {
                    cmt2.proc.par {
                        cmt2.proc.enable @step_a
                        cmt2.proc.enable @step_b
                    }
                    cmt2.proc.par {
                        cmt2.proc.enable @step_c
                        cmt2.proc.enable @step_d
                    }
                }
                cmt2.proc.control_end
            }
        }

        //===--------------------------------------------------------------===//
        // Test 5: Par followed by seq
        // seq { par { @a, @b }, @c }
        //===--------------------------------------------------------------===//

        // TDCC-LABEL: cmt2.module @TestSeqParSeq
        // STMT-LABEL: cmt2.module @TestSeqParSeq
        cmt2.module @TestSeqParSeq(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.proc.static_step @step_a<2> {}
            cmt2.proc.static_step @step_b<3> {}
            cmt2.proc.static_step @step_c<2> {}

            // seq { par { @a (2), @b (3) }, @c (2) }
            // = max(2, 3) + 2 = 5 cycles
            cmt2.proc.rule @seq_par_seq_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.par {
                        cmt2.proc.enable @step_a
                        cmt2.proc.enable @step_b
                    }
                    cmt2.proc.enable @step_c
                }
                cmt2.proc.control_end
            }
        }

        //===--------------------------------------------------------------===//
        // Test 6: Par with if in branch
        // par { if (cond) { @a } else { @b }, @c }
        //===--------------------------------------------------------------===//

        // TDCC-LABEL: cmt2.module @TestParWithIf
        // TDCC: cmt2.proc.rule @par_if_rule
        // TDCC-SAME: tdcc.complex_pars

        // STMT-LABEL: cmt2.module @TestParWithIf
        cmt2.module @TestParWithIf(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.proc.static_step @step_a<2> {}
            cmt2.proc.static_step @step_b<3> {}
            cmt2.proc.static_step @step_c<1> {}

            // Par with conditional branch:
            // Branch 1: if (cond) { @a (2) } else { @b (3) } = max(2, 3) = 3 cycles
            // Branch 2: @c (1 cycle)
            // Total: max(3, 1) = 3 cycles
            cmt2.proc.rule @par_if_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %cond = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.par {
                    cmt2.proc.if %cond : !firrtl.uint<1> {
                        cmt2.proc.enable @step_a
                    } else {
                        cmt2.proc.enable @step_b
                    }
                    cmt2.proc.enable @step_c
                }
                cmt2.proc.control_end
            }
        }

        //===--------------------------------------------------------------===//
        // Test 7: Par with nested if-seq
        // par { seq { @a, if (cond) { @b } }, @c }
        //===--------------------------------------------------------------===//

        // TDCC-LABEL: cmt2.module @TestParWithNestedIfSeq
        // STMT-LABEL: cmt2.module @TestParWithNestedIfSeq
        cmt2.module @TestParWithNestedIfSeq(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.proc.static_step @step_a<1> {}
            cmt2.proc.static_step @step_b<2> {}
            cmt2.proc.static_step @step_c<4> {}

            // Par with nested if in seq:
            // Branch 1: seq { @a (1), if(cond) { @b (2) } } = 1 + 2 = 3 cycles
            // Branch 2: @c (4 cycles)
            // Total: max(3, 4) = 4 cycles
            cmt2.proc.rule @par_nested_if_rule() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                %cond = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.proc.par {
                    cmt2.proc.seq {
                        cmt2.proc.enable @step_a
                        cmt2.proc.if %cond : !firrtl.uint<1> {
                            cmt2.proc.enable @step_b
                        }
                    }
                    cmt2.proc.enable @step_c
                }
                cmt2.proc.control_end
            }
        }
    }
}
