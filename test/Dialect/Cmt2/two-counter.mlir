// RUN: circt-opt %s | FileCheck %s

builtin.module {
    firrtl.circuit "Reg32" {
        firrtl.module @Reg32(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
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
        cmt2.module.extern.firrtl @reg : @Reg32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            // out-of-method gaa.bind bind the scheduling unrelated IOs.
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>

            cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ ready = @readReady, data = [@read]]

            cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
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

        cmt2.module @twoCounter(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @x = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @y = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.method @incr(%a: !firrtl.uint<1>) -> (!firrtl.uint<32>) {
            } {
                %0 = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                %1 = cmt2.call @y @read () : () -> (!firrtl.uint<32>)

                %sum = firrtl.add %0, %1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %bits = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>

                %2 = cmt2.if %a : !firrtl.uint<1> -> !firrtl.uint<32> {
                    cmt2.call @x @write (%bits) : (!firrtl.uint<32>) -> ()
                    cmt2.yield %0 : !firrtl.uint<32>
                } else {
                    cmt2.call @y @write (%bits) : (!firrtl.uint<32>) -> ()
                    cmt2.yield %1 : !firrtl.uint<32>
                }
                cmt2.return %2 : !firrtl.uint<32>
            }

            cmt2.rule @incrementX() {} {
                %0 = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                %c1_i32 = firrtl.constant 1 : !firrtl.uint<32>
                %sum = firrtl.add %0, %c1_i32 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %bits = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @x @write (%bits) : (!firrtl.uint<32>) -> ()
            }   

        } {
            precedence = [[@incr, @incrementX]]
        }
    }
}