// RUN: circt-opt %s -cmt2-print-instance-graph 2>&1 | FileCheck %s

// This test checks the InstanceGraph functionality with a hierarchical module structure

builtin.module {
    firrtl.circuit "Register" {
        firrtl.module @Register(in %data: !firrtl.uint<32>, in %clock: !firrtl.clock,
                                out %ready: !firrtl.uint<1>, out %out: !firrtl.uint<32>) {
            %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
            %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.connect %ready, %c1_ui1 : !firrtl.uint<1>
            firrtl.connect %out, %data : !firrtl.uint<32>
        }
    }

    cmt2.circuit {
        // Base register module
        cmt2.module.extern.firrtl @reg : @Register(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ ready = "ready", arguments = [], results = ["out"]]
            cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
                enable = "writeEnable",
                ready = "ready",
                arguments = ["data"],
                results = []
            ]
        } {
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        // Counter module - uses a register
        cmt2.module @counter(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @cnt = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.method @increment() -> () {
                cmt2.return
            } {
                %val = cmt2.call @cnt @read () : () -> (!firrtl.uint<32>)
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %next = firrtl.add %val, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %next32 = firrtl.bits %next 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @cnt @write (%next32) : (!firrtl.uint<32>) -> ()
                cmt2.return
            }

            cmt2.value @getValue() -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %val = cmt2.call @cnt @read () : () -> (!firrtl.uint<32>)
                cmt2.return %val : !firrtl.uint<32>
            }
        }

        // Pair module - uses two counters
        cmt2.module @pair(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @left = @counter (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @right = @counter (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.method @incrementLeft() -> () {
                cmt2.return
            } {
                cmt2.call @left @increment () : () -> ()
                cmt2.return
            }

            cmt2.method @incrementRight() -> () {
                cmt2.return
            } {
                cmt2.call @right @increment () : () -> ()
                cmt2.return
            }

            cmt2.value @getSum() -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %l = cmt2.call @left @getValue () : () -> (!firrtl.uint<32>)
                %r = cmt2.call @right @getValue () : () -> (!firrtl.uint<32>)
                %sum = firrtl.add %l, %r : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %sum32 = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.return %sum32 : !firrtl.uint<32>
            }
        }

        // Top module - uses a pair and a counter
        cmt2.module @top(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @p = @pair (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @c = @counter (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.rule @incrementAll () -> () {
                cmt2.return
            } {
                cmt2.call @p @incrementLeft () : () -> ()
                cmt2.call @p @incrementRight () : () -> ()
                cmt2.call @c @increment () : () -> ()
                cmt2.return
            }
        }
    }
}

// CHECK: digraph
// CHECK-DAG: reg
// CHECK-DAG: counter
// CHECK-DAG: pair
// CHECK-DAG: top
