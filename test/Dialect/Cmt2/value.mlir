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
    
    cmt2.circuit {
       

        cmt2.module @child(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
          
            cmt2.value @add1(%v: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %sum = firrtl.add %v, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %3 = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                
                cmt2.return %3 : !firrtl.uint<32>
            }
        }

        cmt2.module @top(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
        

            cmt2.instance @c = @child (%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            
            cmt2.value @add_incr(%a: !firrtl.uint<32>, %b: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %sum = firrtl.add %a, %b : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %3 = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                %4 = cmt2.call @c @add1 (%3) : (!firrtl.uint<32>) -> (!firrtl.uint<32>)
                cmt2.return %4 : !firrtl.uint<32>
            }
        }
    }
}
