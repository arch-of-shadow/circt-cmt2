// RUN: circt-opt %s | FileCheck %s

// CHECK-LABEL: cmt2.circuit
// CHECK: cmt2.interface @Reader
// CHECK: cmt2.module @child
// CHECK: cmt2.interface.decl @reader
// CHECK: cmt2.module @parent
// CHECK: cmt2.interface.def @StorageReader

// This test demonstrates interface mechanism with module inlining

builtin.module {
  firrtl.circuit "Reg32" {
    firrtl.module @Reg32(in %write: !firrtl.uint<32>, in %writeEnable: !firrtl.uint<1>,
                         in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                         out %writeReady: !firrtl.uint<1>, out %readReady: !firrtl.uint<1>,
                         out %read: !firrtl.uint<32>) {
      %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
      %r = firrtl.regreset %clock, %reset, %c0_ui32 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>
      %next = firrtl.mux(%writeEnable, %write, %r) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
      firrtl.connect %r, %next : !firrtl.uint<32>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.connect %writeReady, %c1_ui1 : !firrtl.uint<1>
      firrtl.connect %readReady, %c1_ui1 : !firrtl.uint<1>
      firrtl.connect %read, %r : !firrtl.uint<32>
    }
  }

  cmt2.circuit {
    // External register module
    cmt2.module.extern.firrtl @reg : @Reg32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ready = "readReady", arguments = [], results = ["read"]]
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

    // Define an interface for reading data
    cmt2.interface @Reader {
      cmt2.value @getData() -> (!firrtl.uint<32>) {}{}
    }

    // A child module that requires a Reader interface
    cmt2.module @child(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      // Declare that we need a Reader interface
      cmt2.interface.decl @reader : @Reader
      cmt2.instance @x = @reg(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

      // A method that calls through the interface
      cmt2.method @process(%x: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
        cmt2.return
      } {
        // Call through the interface to get data
        %data = cmt2.call @reader @getData() : () -> (!firrtl.uint<32>)
        // Add input to the data
        %result = firrtl.add %x, %data : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
        %result32 = firrtl.bits %result 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
        cmt2.call @x @write(%result32) : (!firrtl.uint<32>) -> ()
        cmt2.return %result32 : !firrtl.uint<32>
      }

      // A value that also uses the interface
      cmt2.value @doubleData() -> (!firrtl.uint<32>) {
        cmt2.return
      } {
        %data = cmt2.call @reader @getData() : () -> (!firrtl.uint<32>)
        %c2 = firrtl.constant 2 : !firrtl.uint<32>
        %result = firrtl.mul %data, %c2 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
        %result32 = firrtl.bits %result 31 to 0 : (!firrtl.uint<64>) -> !firrtl.uint<32>
        cmt2.return %result32 : !firrtl.uint<32>
      }
    }

    // Parent module that provides the interface and instantiates child
    cmt2.module @parent(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      // Create a register instance
      cmt2.instance @storage = @reg(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>

      // Define the Reader interface by binding it to the register
      cmt2.interface.def @StorageReader : @Reader [
        [@storage, @read, @getData]
      ]

      // Instantiate the child module and bind the interface
      cmt2.instance @processor = @child(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with [
        [@StorageReader, @reader]
      ]

      // A rule that uses the child's methods
      cmt2.rule @compute () -> () {
        cmt2.return
      } {
        %c10 = firrtl.constant 10 : !firrtl.uint<32>
        // Call the child's method through the instance
        %result1 = cmt2.call @processor @process(%c10) : (!firrtl.uint<32>) -> (!firrtl.uint<32>)
        // Call the child's value
        %result2 = cmt2.call @processor @doubleData() : () -> (!firrtl.uint<32>)
        // Write results back
        cmt2.call @storage @write(%result1) : (!firrtl.uint<32>) -> ()
        cmt2.return
      }
    }
  }
}
