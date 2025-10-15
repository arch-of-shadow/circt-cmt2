// RUN: circt-opt -cmt2-inline-private-funcs --lower-cmt2-to-hw %s | FileCheck %s

// Simplified GCD test without interfaces
// Tests the full conversion pipeline on a realistic example

builtin.module {
    cmt2.circuit {
        // External register module with conflict matrix
        cmt2.module.extern.hw @reg : @Reg32(%clk: i1, %rst: i1) {
            cmt2.bind.value @read : () -> (i32) [ ready = @readReady, data = [@data]]
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

        // CHECK-LABEL: hw.module @gcd
        // CHECK-SAME: in %clk : i1, in %rst : i1
        // CHECK-SAME: in %start_enable : i1
        // CHECK-SAME: out start_ready : i1
        // CHECK-SAME: out result_ready : i1
        // Verify that private function @doing is inlined (calls to @y @read appear in guard regions)
        // CHECK: comb.icmp ne
        // Verify ready signal generation (AND chain with guard result and called ready signals)
        // CHECK: %{{.*}} = comb.and
        // CHECK: %swap_ready = sv.wire
        // CHECK: sv.assign %swap_ready
        // Verify fire signal generation
        // CHECK: %swap_fire = sv.wire
        // CHECK: sv.assign %swap_fire
        // Verify method enable signal assignment driven by fire signal
        // CHECK: sv.assign %{{.*}}_enable{{.*}}, %{{.*}} : i1
        // Verify NOT(preceding conflicts) in ready signal
        // CHECK: comb.xor
        // Verify output port connections
        // CHECK: hw.output
        cmt2.module @gcd(%clk: i1, %rst: i1) {
            cmt2.instance @x = @reg (%clk, %rst) : i1, i1
            cmt2.instance @y = @reg (%clk, %rst) : i1, i1

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
