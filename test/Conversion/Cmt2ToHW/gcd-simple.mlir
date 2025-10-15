// RUN: circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-hw %s | FileCheck %s

// Simplified GCD test without interfaces
// Tests the full conversion pipeline on a realistic example

builtin.module {
    "hw.module"() ({
        ^bb0(%write: i32, %writeEnable: i1, %clock: !seq.clock):
        %init = seq.initial () {
            %c0_i32 = "hw.constant"() {value = 0 : i32} : () -> i32
            seq.yield %c0_i32 : i32
        } : ()  -> !seq.immutable<i32>
        %read = seq.compreg %next, %clock initial %init : i32
        %next = "comb.mux"(%writeEnable, %write, %read) : (i1, i32, i32) -> i32
        %writeReady = "hw.constant"() {value = 1 : i1} : () -> i1
        %readReady = "hw.constant"() {value = 1 : i1} : () -> i1
        "hw.output"(%writeReady, %readReady, %read) : (i1, i1, i32) -> ()
        }) {
        argNames = ["write", "writeEnable", "clock"],
        comment = "",
        parameters = [],
        resultNames = ["writeReady", "readReady", "ready"],
        sym_name = "Reg32",
        module_type = !hw.modty<input write : i32, input writeEnable : i1, input clock : !seq.clock, output writeReady : i1, output readReady : i1, output ready : i32>
    } : () -> ()
    cmt2.circuit {
        // External register module with conflict matrix
        cmt2.module.extern.hw @reg : @Reg32(%clk: !seq.clock) {
            cmt2.bind.bare %clk, @clock : !seq.clock
            cmt2.bind.value @read : () -> (i32) [ ready = @readReady, data = [@read]]
            cmt2.bind.method @write : (i32) -> () [
                enable = @writeEnable,
                ready = @writeReady,
                inputs = [@write],
                outputs = []
            ]
        } {
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        cmt2.module @gcd(%clk: !seq.clock, %rst: i1) {
            cmt2.instance @x = @reg (%clk) : !seq.clock
            cmt2.instance @y = @reg (%clk) : !seq.clock

            // Private value - will be inlined
            cmt2.value @doing() -> (i1) {} {
              %y = cmt2.call @y @read () : () -> (i32)
              %0 = hw.constant 0: i32
              %1 = comb.icmp ne %y, %0 : i32
              cmt2.return %1 : i1
            }

            // Rule: swap x and y when x < y and y != 0
            cmt2.rule @swap() -> i1 {
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)
                %2 = comb.icmp bin ult %0, %1 : i32
                %3 = cmt2.call @this @doing () : () -> (i1)
                %4 = comb.and %2, %3 : i1
                cmt2.return %4 : i1
            } {
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)

                cmt2.call @x @write (%1) : (i32) -> ()
                cmt2.call @y @write (%0) : (i32) -> ()
                cmt2.return
            }

            // Rule: subtract y from x when x >= y and y != 0
            cmt2.rule @sub() -> i1 {
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)
                %2 = comb.icmp bin uge %0, %1 : i32
                %3 = cmt2.call @this @doing () : () -> (i1)
                %4 = comb.and %2, %3 : i1
                cmt2.return %4 : i1
            } {
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)
                %2 = comb.sub %0, %1 : i32

                cmt2.call @x @write (%2) : (i32) -> ()
                cmt2.return
            }

            // Method: start GCD computation
            cmt2.method @start(%a: i32, %b: i32) -> () {
                %0 = cmt2.call @this @doing () : () -> (i1)
                %c1_i1 = hw.constant 1 : i1
                %2 = comb.xor %0, %c1_i1 : i1
                cmt2.return %2 : i1
            } {
                cmt2.call @x @write (%a) : (i32) -> ()
                cmt2.call @y @write (%b) : (i32) -> ()
                cmt2.return
            }

            // Value: get result when done
            cmt2.value @result() -> (i32) {
                %0 = cmt2.call @this @doing () : () -> (i1)
                %c1_i1 = hw.constant 1 : i1
                %2 = comb.xor %0, %c1_i1 : i1
                cmt2.return %2 : i1
            } {
                %x = cmt2.call @x @read () : () -> (i32)
                cmt2.return %x : i32
            }
        }
    }
}
