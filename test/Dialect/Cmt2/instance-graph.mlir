// RUN: circt-opt %s -cmt2-print-instance-graph | FileCheck %s

// This test checks the InstanceGraph functionality with a hierarchical module structure

builtin.module {
    "hw.module"() ({
        ^bb0(%data: i32, %clock: !seq.clock):
        %init = seq.initial () {
            %c0_i32 = "hw.constant"() {value = 0 : i32} : () -> i32
            seq.yield %c0_i32 : i32
        } : ()  -> !seq.immutable<i32>
        %out = seq.compreg %data, %clock initial %init : i32
        %ready = "hw.constant"() {value = 1 : i1} : () -> i1
        "hw.output"(%ready, %out) : (i1, i32) -> ()
        }) {
        argNames = ["data", "clock"],
        comment = "",
        parameters = [],
        resultNames = ["ready", "out"],
        sym_name = "Register",
        module_type = !hw.modty<input data : i32, input clock : !seq.clock, output ready : i1, output out : i32>
    } : () -> ()

    cmt2.circuit {
        // Base register module
        cmt2.module.extern.hw @reg : @Register {
            ^bb0(%clk: i1, %rst: i1):
            cmt2.bind.bare %clk, @clock : i1
            cmt2.bind.value @read : (i1) -> (i32) [ ready = @ready, data = [@out]]
            cmt2.bind.method @write : (i1, i32) -> (i1) [
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
            ^bb0(%clk: i1, %rst: i1):
            cmt2.instance @cnt = @reg (%clk, %rst) : i1, i1

            cmt2.method @increment : () -> () {
                cmt2.return
            } {
                %val = cmt2.call @cnt @read () : () -> (i32)
                %c1 = "hw.constant"() {value = 1 : i32} : () -> i32
                %next = "comb.add"(%val, %c1) : (i32, i32) -> i32
                cmt2.call @cnt @write (%next) : (i32) -> ()
            }

            cmt2.value @getValue : () -> () {
                cmt2.return
            } {
                %val = cmt2.call @cnt @read () : () -> (i32)
                cmt2.return %val : i32
            }
        }

        // Pair module - uses two counters
        cmt2.module @pair {
            ^bb0(%clk: i1, %rst: i1):
            cmt2.instance @left = @counter (%clk, %rst) : i1, i1
            cmt2.instance @right = @counter (%clk, %rst) : i1, i1

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
                %l = cmt2.call @left @getValue () : () -> (i32)
                %r = cmt2.call @right @getValue () : () -> (i32)
                %sum = "comb.add"(%l, %r) : (i32, i32) -> i32
                cmt2.return %sum : i32
            }
        }

        // Top module - uses a pair and a counter
        cmt2.module @top {
            ^bb0(%clk: i1, %rst: i1):
            cmt2.instance @p = @pair (%clk, %rst) : i1, i1
            cmt2.instance @c = @counter (%clk, %rst) : i1, i1

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
