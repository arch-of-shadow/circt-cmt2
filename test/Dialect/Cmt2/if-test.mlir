// RUN: circt-opt %s | FileCheck %s --check-prefix=PARSE
// RUN: circt-opt %s --lower-cmt2-to-firrtl | FileCheck %s --check-prefix=FIRRTL

// This test demonstrates the cmt2.if operation with conditional execution

builtin.module {
    cmt2.circuit {
        cmt2.module @test_if(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            // Test if without else and without results
            cmt2.rule @test_simple_if () -> (!firrtl.uint<1>) {
                cmt2.return
            } {
                %cond = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.if %cond : !firrtl.uint<1> {
                    %c1 = firrtl.constant 42 : !firrtl.uint<32>
                    cmt2.yield
                }
                cmt2.return
            }

            // Test if with else but without results
            cmt2.rule @test_if_else_no_result () -> (!firrtl.uint<1>) {
                cmt2.return
            } {
                %cond = firrtl.constant 0 : !firrtl.uint<1>
                cmt2.if %cond : !firrtl.uint<1> {
                    %c1 = firrtl.constant 1 : !firrtl.uint<32>
                    cmt2.yield
                } else {
                    %c2 = firrtl.constant 2 : !firrtl.uint<32>
                    cmt2.yield
                }
                cmt2.return
            }

            // Test if with else and with results
            cmt2.value @test_if_with_result() -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %cond = firrtl.constant 1 : !firrtl.uint<1>
                %result = cmt2.if %cond : !firrtl.uint<1> -> !firrtl.uint<32> {
                    %c10 = firrtl.constant 10 : !firrtl.uint<32>
                    cmt2.yield %c10 : !firrtl.uint<32>
                } else {
                    %c20 = firrtl.constant 20 : !firrtl.uint<32>
                    cmt2.yield %c20 : !firrtl.uint<32>
                }
                cmt2.return %result : !firrtl.uint<32>
            }

            // Test nested if
            cmt2.method @test_nested_if(%val: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %c5 = firrtl.constant 5 : !firrtl.uint<32>
                %c10 = firrtl.constant 10 : !firrtl.uint<32>

                %gt5 = firrtl.gt %val, %c5 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>

                %result = cmt2.if %gt5 : !firrtl.uint<1> -> !firrtl.uint<32> {
                    %gt10 = firrtl.gt %val, %c10 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                    %inner = cmt2.if %gt10 : !firrtl.uint<1> -> !firrtl.uint<32> {
                        %c100 = firrtl.constant 100 : !firrtl.uint<32>
                        cmt2.yield %c100 : !firrtl.uint<32>
                    } else {
                        %c50 = firrtl.constant 50 : !firrtl.uint<32>
                        cmt2.yield %c50 : !firrtl.uint<32>
                    }
                    cmt2.yield %inner : !firrtl.uint<32>
                } else {
                    %c0 = firrtl.constant 0 : !firrtl.uint<32>
                    cmt2.yield %c0 : !firrtl.uint<32>
                }

                cmt2.return %result : !firrtl.uint<32>
            }
        }
    }
}

// PARSE-LABEL: cmt2.circuit
// PARSE: cmt2.module @test_if
// PARSE: cmt2.rule @test_simple_if
// PARSE: cmt2.if %{{.*}} : !firrtl.uint<1>
// PARSE-NEXT: cmt2.yield
// PARSE: cmt2.rule @test_if_else_no_result
// PARSE: cmt2.if %{{.*}} : !firrtl.uint<1>
// PARSE: } else {
// PARSE: cmt2.value @test_if_with_result
// PARSE: cmt2.if %{{.*}} : !firrtl.uint<1> -> !firrtl.uint<32>
// PARSE: cmt2.yield %{{.*}} : !firrtl.uint<32>
// PARSE: } else {
// PARSE: cmt2.yield %{{.*}} : !firrtl.uint<32>
// PARSE: cmt2.method @test_nested_if

// FIRRTL-LABEL: firrtl.circuit "test_if"
// FIRRTL: firrtl.module @test_if
// FIRRTL: firrtl.when %{{.*}}
// FIRRTL: firrtl.when %{{.*}}, true
