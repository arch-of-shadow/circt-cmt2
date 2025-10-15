// RUN: circt-opt %s -cmt2-print-instance-graph | FileCheck %s

// This test checks the InstanceGraph functionality with a hierarchical module structure

builtin.module {
    "hw.module"() ({
        ^bb0(%data: !firrtl.uint<32>, %clock: !seq.clock):
        %init = seq.initial () {
            %c0_i32 = "hw.constant"() {value = 0 : i32} : () -> i32
            seq.yield %c0_i32 : !firrtl.uint<32>
        } : ()  -> !seq.immutable<i32>
        %out = seq.compreg %data, %clock initial %init : !firrtl.uint<32>
        %ready = "hw.constant"() {value = 1 : i1} : () -> i1
        "hw.output"(%ready, %out) : (!firrtl.uint<1>, !firrtl.uint<32>) -> ()
        }) {
        argNames = ["data", "clock"],
        comment = "",
        parameters = [],
        resultNames = ["ready", "out"],
        sym_name = "Register",
        module_type = !hw.modty<input data : !firrtl.uint<32>, input clock : !seq.clock, output ready : !firrtl.uint<1>, output out : i32>
    } : () -> ()

    cmt2.circuit {
        // Base register module
        cmt2.module.extern.firrtl @reg : @Register {
            ^bb0(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>):
            cmt2.bind.bare %clk, @clock : !firrtl.uint<1>
            cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ ready = @ready, data = [@out]]
            cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
                enable = @writeEnable,
                ready = @ready,
                inputs = [@data],
                outputs = []
            ]
        } {
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        // Counter module - uses a register
        cmt2.module @counter {
            ^bb0(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>):
            cmt2.instance @cnt = @reg (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>

            cmt2.method @increment : () -> () {
                cmt2.return
            } {
                %val = cmt2.call @cnt @read () : () -> (!firrtl.uint<32>)
                %c1 = "hw.constant"() {value = 1 : i32} : () -> i32
                %next = "comb.add"(%val, %c1) : (i32, !firrtl.uint<32>) -> i32
                cmt2.call @cnt @write (%next) : (!firrtl.uint<32>) -> ()
            }

            cmt2.value @getValue : () -> () {
                cmt2.return
            } {
                %val = cmt2.call @cnt @read () : () -> (!firrtl.uint<32>)
                cmt2.return %val : !firrtl.uint<32>
            }
        }

        // Pair module - uses two counters
        cmt2.module @pair {
            ^bb0(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>):
            cmt2.instance @left = @counter (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>
            cmt2.instance @right = @counter (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>

            cmt2.method @incrementLeft : () -> () {
                cmt2.return
            } {
                cmt2.call @left @increment () : () -> ()
            }

            cmt2.method @incrementRight : () -> () {
                cmt2.return
            } {
                cmt2.call @right @increment () : () -> ()
            }

            cmt2.value @getSum : () -> () {
                cmt2.return
            } {
                %l = cmt2.call @left @getValue () : () -> (!firrtl.uint<32>)
                %r = cmt2.call @right @getValue () : () -> (!firrtl.uint<32>)
                %sum = "comb.add"(%l, %r) : (i32, !firrtl.uint<32>) -> i32
                cmt2.return %sum : !firrtl.uint<32>
            }
        }

        // Top module - uses a pair and a counter
        cmt2.module @top {
            ^bb0(%clk: !firrtl.uint<1>, %rst: !firrtl.uint<1>):
            cmt2.instance @p = @pair (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>
            cmt2.instance @c = @counter (%clk, %rst) : !firrtl.uint<1>,  !firrtl.uint<1>

            cmt2.rule @incrementAll {
                cmt2.return
            } {
                cmt2.call @p @incrementLeft () : () -> ()
                cmt2.call @p @incrementRight () : () -> ()
                cmt2.call @c @increment () : () -> ()
            }
        }
    }
}

// CHECK: digraph
// CHECK-DAG: reg
// CHECK-DAG: counter
// CHECK-DAG: pair
// CHECK-DAG: top
