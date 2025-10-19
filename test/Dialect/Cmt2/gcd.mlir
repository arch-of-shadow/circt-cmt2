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

        cmt2.module @gcd(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @x = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @y = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            cmt2.value @doing() -> (!firrtl.uint<1>) {} {
              %y = cmt2.call @y @read () : () -> (!firrtl.uint<32>)
              %0 = firrtl.constant 0 : !firrtl.uint<32>
              %1 = firrtl.neq %y, %0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
              cmt2.return %1 : !firrtl.uint<1>
            }

            cmt2.rule @swap() {
                %0 = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                %1 = cmt2.call @y @read () : () -> (!firrtl.uint<32>)
                %2 = firrtl.gt %1, %0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                %3 = cmt2.call @this @doing () : () -> (!firrtl.uint<1>)
                %4 = firrtl.and %2, %3 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
                cmt2.return %4 : !firrtl.uint<1>
            } {
                %0 = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                %1 = cmt2.call @y @read () : () -> (!firrtl.uint<32>)

                cmt2.call @x @write (%1) : (!firrtl.uint<32>) -> ()
                cmt2.call @y @write (%0) : (!firrtl.uint<32>) -> ()
            }

            cmt2.rule @sub() {
                %0 = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                %1 = cmt2.call @y @read () : () -> (!firrtl.uint<32>)
                %2 = firrtl.leq %1, %0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                %3 = cmt2.call @this @doing () : () -> (!firrtl.uint<1>)
                %4 = firrtl.and %2, %3 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
                cmt2.return %4 : !firrtl.uint<1>
            } {
                %0 = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                %1 = cmt2.call @y @read () : () -> (!firrtl.uint<32>)
                %2 = firrtl.sub %0, %1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %3 = firrtl.bits %2 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>

                cmt2.call @y @write (%3) : (!firrtl.uint<32>) -> ()
            }

            cmt2.method @start(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> () {
                %0 = cmt2.call @this @doing () : () -> (!firrtl.uint<1>)
                %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
                %2 = firrtl.xor %0, %c1_i1 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
                cmt2.return %2 : !firrtl.uint<1>
            } {
                cmt2.call @x @write (%a) : (!firrtl.uint<32>) -> ()
                cmt2.call @y @write (%b) : (!firrtl.uint<32>) -> ()
            }
            cmt2.value @result() -> (!firrtl.uint<32>) {
                %0 = cmt2.call @this @doing () : () -> (!firrtl.uint<1>)
                %c1_i1 = firrtl.constant 1 : !firrtl.uint<1>
                %2 = firrtl.xor %0, %c1_i1 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
                cmt2.return %2 : !firrtl.uint<1>
            } {
                %x = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                cmt2.return %x : !firrtl.uint<32>
            }
        }
    }
}