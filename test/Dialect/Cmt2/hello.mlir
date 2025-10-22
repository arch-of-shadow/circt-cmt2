// RUN: circt-opt %s | FileCheck %s --check-prefix=PARSE
// RUN: circt-opt %s --lower-cmt2-to-firrtl | FileCheck %s --check-prefix=FIRRTL
// RUN: circt-opt %s --lower-cmt2-to-firrtl | firtool --format=mlir --verilog | FileCheck %s --check-prefix=VERILOG

// This test demonstrates the complete Cmt2 → FIRRTL → Verilog pipeline with:
// 1. External FIRRTL modules from the module library
// 2. Interface mechanism (InterfaceDecl, InterfaceDef, interface_binds)
// 3. Hierarchical module composition with interface passing
// 4. Module library integration (using external @reg module)
//
// This can also be generated programmatically using the ECMT2 embedded DSL:
//   See examples/ECMT2/counter_example.cpp for similar usage patterns

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

            cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ ready = "readReady", arguments = [], results = ["read"]]

            cmt2.bind.method @write : (!firrtl.uint<1>, !firrtl.uint<32>) -> (!firrtl.uint<1>) [
                enable = "writeEnable",
                ready = "writeReady",
                arguments = ["write"],
                results = []
            ]
        } {
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        cmt2.interface @Reader {
            cmt2.value @getData() -> (!firrtl.uint<32>) {}{}
        }

        cmt2.interface @Writer {
            cmt2.method @store(%data: !firrtl.uint<32>) {}{}
        }

        cmt2.module @child(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.interface.decl @reader : @Reader
            cmt2.instance @r = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.method @set(%v: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %x = cmt2.call @reader @getData() : () -> (!firrtl.uint<32>)
                %old = cmt2.call @r @read () : () -> (!firrtl.uint<32>)
                %sum = firrtl.add %x, %v : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %3 = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                %new = firrtl.add %old, %3 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %4 = firrtl.bits %new 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @r @write (%4) : (!firrtl.uint<32>) -> ()
                cmt2.return %4 : !firrtl.uint<32>
            }
        }

        cmt2.module @hello(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            // Interface declaration for outward calls - creates module ports
            cmt2.interface.decl @writer : @Writer

            cmt2.instance @x = @reg (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.interface.def @ReadX : @Reader [
                [@x, @read, @getData]
            ]
            cmt2.instance @c = @child(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with [
                [@ReadX, @reader]
            ]

            cmt2.method @write(%v: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %old = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                cmt2.call @c @set(%v) : (!firrtl.uint<32>) -> ()
                cmt2.call @x @write(%v) : (!firrtl.uint<32>) -> ()
                cmt2.return %old : !firrtl.uint<32>
            }

            cmt2.rule @incr () -> () {
                cmt2.return
            } {
                %v = cmt2.call @x @read () : () -> (!firrtl.uint<32>)
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %sum = firrtl.add %v, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %3 = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @x @write(%3) : (!firrtl.uint<32>) -> ()
                // Call through interface declaration - becomes module output port
                cmt2.call @writer @store(%3) : (!firrtl.uint<32>) -> ()
                cmt2.return
            }
        } {
          precedence = [[@write, @incr]]
        }
    }
}

// PARSE-LABEL: cmt2.circuit
// PARSE: cmt2.interface @Reader
// PARSE: cmt2.interface @Writer
// PARSE: cmt2.module @child
// PARSE: cmt2.interface.decl @reader : @Reader
// PARSE: cmt2.module @hello
// PARSE: cmt2.interface.decl @writer : @Writer
// PARSE: cmt2.interface.def @ReadX : @Reader

// FIRRTL-LABEL: firrtl.circuit "hello"
// FIRRTL: firrtl.module @Reg32
// FIRRTL: firrtl.module @child
// FIRRTL-SAME: in %reader_getData_ready
// FIRRTL-SAME: out %reader_getData_enable
// FIRRTL-SAME: out %reader_getData_result
// FIRRTL: firrtl.module @hello
// FIRRTL-SAME: out %writer_store_enable
// FIRRTL-SAME: out %writer_store_data
// FIRRTL-SAME: in %writer_store_ready
// FIRRTL: firrtl.instance x @Reg32
// FIRRTL: firrtl.instance c @child

// VERILOG-LABEL: module hello
// VERILOG: output writer_store_enable
// VERILOG: output [31:0] writer_store_data
// VERILOG: input writer_store_ready
// VERILOG: Reg32 x
// VERILOG: child c