// RUN: circt-opt %s | FileCheck %s

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
        argNames = ["write", "writeEnable", "clock", "reset"],
        comment = "",
        parameters = [],
        resultNames = ["writeReady", "readReady", "ready"],
        sym_name = "Reg32",
        module_type = !hw.modty<input write : i32, input writeEnable : i1, input clock : !seq.clock, output writeReady : i1, output readReady : i1, output ready : i32>
    } : () -> ()

    cmt2.circuit {
        cmt2.module.extern.hw @reg : @Reg32 {
            ^bb0(%clk: i1, %rst: i1):
            // out-of-method gaa.bind bind the scheduling unrelated IOs.
            cmt2.bind.bare %clk, @clock : i1
            cmt2.bind.bare %rst, @reset : i1

            cmt2.bind.value @read : (i1) -> (i32) [ ready = @readReady, data = [@read]]

            cmt2.bind.method @write : (i1, i32) -> (i1) [
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
        
        // interface Read
        cmt2.interface @Read {
            cmt2.method @read: () -> (i32) {}{}
        }

        // a placeholder module to test interface
        cmt2.module @placeholder {
            cmt2.interface.decl @reader : @Read
        }

        cmt2.module @gcd {
            ^bb0(%clk: i1, %rst: i1):
            cmt2.interface.def @ReadX : @Read [
                [@x, @read, @read]
            ]

            // instance to test interface
            cmt2.instance @_ = @placeholder with [
                [@ReadX, @reader]
            ]

            cmt2.instance @x = @reg (%clk, %rst) : i1, i1
            cmt2.instance @y = @reg (%clk, %rst) : i1, i1


            cmt2.rule @swap {                
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)
                %2 = "comb.icmp"(%0, %1) {predicate = 8 : i64} : (i32, i32) -> i1
                %3 = "hw.constant"() {value = 0 : i32} : () -> i32
                // comb.icmp ne %0, %3 : i1
                %4 = "comb.icmp"(%1, %3) {predicate = 1 : i64} : (i32, i32) -> i1
                %5 = "comb.and"(%2, %4) : (i1, i1) -> i1
                cmt2.return %5 : i1
            } { 
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)

                cmt2.call @x @write (%1) : (i32) -> ()
                cmt2.call @y @write (%0) : (i32) -> ()
            }
            
            cmt2.rule @sub {
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)
                %2 = "comb.icmp"(%0, %1) {predicate = 2 : i64} : (i32, i32) -> i1
                %3 = "hw.constant"() {value = 0 : i32} : () -> i32
                // comb.icmp ne %0, %3 : i1
                %4 = "comb.icmp"(%1, %3) {predicate = 1 : i64} : (i32, i32) -> i1
                %5 = "comb.and"(%2, %4) : (i1, i1) -> i1
                cmt2.return %5 : i1
            } {
                %0 = cmt2.call @x @read () : () -> (i32)
                %1 = cmt2.call @y @read () : () -> (i32)
                %2 = "comb.sub"(%0, %1) : (i32, i32) -> i32

                cmt2.call @y @write (%2) : (i32) -> ()
            }

            cmt2.method @start : () -> () {
                %0 = "hw.constant"() {value = 0 : i32} : () -> i32
                %1 = cmt2.call @y @read () : () -> (i32)
                // comb.icmp eq %0, %1 : i1
                %2 = "comb.icmp"(%0, %1) {predicate = 0 : i64} : (i32, i32) -> i1
                cmt2.return %2 : i1
            } {
                ^bb0(%a: i32, %b: i32):
                cmt2.call @x @write (%a) : (i32) -> ()
                cmt2.call @y @write (%b) : (i32) -> ()
            }
            cmt2.value @result : () -> () {
                %y = cmt2.call @y @read () : () -> (i32)
                %0 = "hw.constant"() {value = 0 : i32} : () -> i32
                // comb.icmp eq %0, %1 : i1
                %1 = "comb.icmp"(%y, %0) {predicate = 0 : i64} : (i32, i32) -> i1
                cmt2.return %1 : i1
            } {
                %x = cmt2.call @x @read () : () -> (i32)
                cmt2.return %x : i32
            }
        }
    }
}