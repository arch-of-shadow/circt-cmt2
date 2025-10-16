// RUN: circt-opt %s | FileCheck %s
// RUN: circt-opt %s -cmt2-print-call-info | FileCheck %s --check-prefix=CALLINFO

// Test the interface mechanism with various operations

builtin.module {
    // FIRRTL module for storage
    firrtl.circuit "Storage" {
        firrtl.module @Storage(in %writeData: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
                               in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                               out %writeReady: !firrtl.uint<1>, out %readReady: !firrtl.uint<1>,
                               out %data: !firrtl.uint<32>) {
            %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
            %r = firrtl.regreset %clock, %reset, %c0_ui32 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>

            %next = firrtl.mux(%writeEnable, %writeData, %r) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
            firrtl.connect %r, %next : !firrtl.uint<32>, !firrtl.uint<32>

            %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.connect %writeReady, %c1_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
            firrtl.connect %readReady, %c1_ui1 : !firrtl.uint<1>, !firrtl.uint<1>
            firrtl.connect %data, %r : !firrtl.uint<32>, !firrtl.uint<32>
        }
    }

    cmt2.circuit {
        // Define an interface
        // CHECK: cmt2.interface @DataInterface
        cmt2.interface @DataInterface {
            cmt2.value @getData() -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                cmt2.return
            }
            cmt2.method @setData(%data: !firrtl.uint<32>) -> () {
                cmt2.return
            } {
                cmt2.return
            }
        }

        // External module for register
        cmt2.module.extern.firrtl @storage : @Storage(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : () -> (!firrtl.uint<32>) [
                ready = @readReady,
                data = [@data]
            ]
            cmt2.bind.method @write : (!firrtl.uint<32>) -> (!firrtl.uint<1>) [
                enable = @writeEnable,
                ready = @writeReady,
                inputs = [@writeData],
                outputs = []
            ]
        } {
            conflict = [[@write, @write]],
            conflictFree = [[@read, @read]],
            sequenceBefore = [[@read, @write]]
        }

        // Child module that uses an interface
        // CHECK: cmt2.module @consumer
        cmt2.module @consumer(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            // Declare that this module uses the DataInterface
            // CHECK: cmt2.interface.decl @dataSource : @DataInterface
            cmt2.interface.decl @dataSource : @DataInterface

            cmt2.instance @store = @storage(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Rule that calls through the interface
            cmt2.rule @processData() {
                cmt2.return
            } {
                // Call getData through the interface
                %data = cmt2.call @dataSource @getData() : () -> (!firrtl.uint<32>)

                // Use the data
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                %sum = firrtl.add %data, %c1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %result = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>

                // Write to storage
                cmt2.call @store @write(%result) : (!firrtl.uint<32>) -> ()
                cmt2.return
            }

            cmt2.method @updateData(%newData: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %oldData = cmt2.call @dataSource @getData() : () -> (!firrtl.uint<32>)
                // Call setData through the interface
                cmt2.call @dataSource @setData(%newData) : (!firrtl.uint<32>) -> ()
                cmt2.return %oldData : !firrtl.uint<32>
            }
        }

        // Parent module that provides the interface implementation
        // CHECK: cmt2.module @provider
        cmt2.module @provider(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.instance @reg1 = @storage(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
            cmt2.instance @reg2 = @storage(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

            // Define the interface implementation
            // CHECK: cmt2.interface.def @DataSource1 : @DataInterface
            cmt2.interface.def @DataSource1 : @DataInterface [
                [@reg1, @read, @getData],
                [@reg1, @write, @setData]
            ]

            // Instantiate consumer with interface binding
            // CHECK: cmt2.instance @cons = @consumer
            // CHECK-SAME: with {{\[}}[@DataSource1, @dataSource]]
            cmt2.instance @cons = @consumer(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with [
                [@DataSource1, @dataSource]
            ]

            cmt2.rule @produceData() {
                %r2 = cmt2.call @reg2 @read() : () -> (!firrtl.uint<32>)
                %c1_ui32 = firrtl.constant 1 : !firrtl.uint<32>
                %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
                %odd = firrtl.and %r2, %c1_ui32 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
                %isOdd = firrtl.neq %odd, %c0_ui32 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
                cmt2.return %isOdd : !firrtl.uint<1>
            } {
                %old = cmt2.call @reg2 @read() : () -> (!firrtl.uint<32>)
                %c42 = firrtl.constant 1 : !firrtl.uint<32>
                %new = firrtl.add %old, %c42 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %new 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                %prevData = cmt2.call @cons @updateData(%trunc) : (!firrtl.uint<32>) -> (!firrtl.uint<32>)
                cmt2.call @reg2 @write(%prevData) : (!firrtl.uint<32>) -> ()
                cmt2.return
            }

            cmt2.method @get() -> (!firrtl.uint<32>) {
                cmt2.return
            } {
                %0 = firrtl.constant 0 : !firrtl.uint<32>
                %r1 = cmt2.call @reg1 @read() : () -> (!firrtl.uint<32>)
                %r2 = cmt2.call @reg2 @read() : () -> (!firrtl.uint<32>)
                %sum = firrtl.add %r1, %r2 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %trunc = firrtl.bits %sum 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @reg2 @write(%0) : (!firrtl.uint<32>) -> ()
                cmt2.return %trunc : !firrtl.uint<32>
            }
        }
    }
}

// CALLINFO: CallInfoView:
// CALLINFO: Module: consumer
// CALLINFO: Entity: @processData
// CALLINFO: Entity: @updateData
// CALLINFO: Module: provider
// CALLINFO: Entity: @produceData
