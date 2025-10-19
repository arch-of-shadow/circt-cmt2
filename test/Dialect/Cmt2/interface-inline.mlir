// RUN: circt-opt %s | FileCheck %s
// This test demonstrates interface mechanism with module inlining

builtin.module {
  // Simple register module (external)
  "hw.module"() ({
    ^bb0(%write: !firrtl.uint<32>, %writeEnable: !firrtl.uint<1>, %clock: !seq.clock):
    %init = seq.initial () {
      %c0_i32 = "hw.constant"() {value = 0 : i32} : () -> i32
      seq.yield %c0_i32 : !firrtl.uint<32>
    } : () -> !seq.immutable<i32>
    %read = seq.compreg %next, %clock initial %init : !firrtl.uint<32>
    %next = "comb.mux"(%writeEnable, %write, %read) : (!firrtl.uint<1>, i32, !firrtl.uint<32>) -> i32
    %writeReady = "hw.constant"() {value = 1 : i1} : () -> i1
    %readReady = "hw.constant"() {value = 1 : i1} : () -> i1
    "hw.output"(%writeReady, %readReady, %read) : (!firrtl.uint<1>, i1, !firrtl.uint<32>) -> ()
  }) {
    argNames = ["write", "writeEnable", "clock"],
    resultNames = ["writeReady", "readReady", "data"],
    parameters = [],
    sym_name = "Reg32",
    module_type = !hw.modty<input write : !firrtl.uint<32>, input writeEnable : !firrtl.uint<1>, input clock : !seq.clock, output writeReady : !firrtl.uint<1>, output readReady : !firrtl.uint<1>, output data : i32>
  } : () -> ()

  cmt2.circuit {
    // External register module
    cmt2.module.extern.firrtl @reg : @Reg32(%clk: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.uint<1>
      cmt2.bind.value @read : (!firrtl.uint<1>) -> (!firrtl.uint<32>) [ready = @readReady, data = [@data]]
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

    // Define an interface for reading data
    cmt2.interface @Reader {
      cmt2.value @getData() -> (!firrtl.uint<32>) {}{}
    }

    // A child module that requires a Reader interface
    // This module will be inlined into the parent
    cmt2.module @child(%clk: !firrtl.uint<1>) {
      // Declare that we need a Reader interface
      cmt2.interface.decl @reader : @Reader
      cmt2.instance @x = @reg(%clk) : !firrtl.uint<1>

      // A method that calls through the interface
      cmt2.method @process(%x: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
        cmt2.return
      } {
        // Call through the interface to get data
        %data = cmt2.call @reader @getData() : () -> (!firrtl.uint<32>)
        // Add input to the data
        %result = comb.add %x, %data : !firrtl.uint<32>
        cmt2.call @x @write(%result) : (!firrtl.uint<32>) -> ()
        cmt2.return %result : !firrtl.uint<32>
      }

      // A value that also uses the interface
      cmt2.value @doubleData() -> (!firrtl.uint<32>) {
        cmt2.return
      } {
        %data = cmt2.call @reader @getData() : () -> (!firrtl.uint<32>)
        %c2 = hw.constant 2 : !firrtl.uint<32>
        %result = comb.mul %data, %c2 : !firrtl.uint<32>
        cmt2.return %result : !firrtl.uint<32>
      }
    }

    // Parent module that provides the interface and instantiates child
    cmt2.module @parent(%clk: !firrtl.uint<1>) {
      // Create a register instance
      cmt2.instance @storage = @reg(%clk) : !firrtl.uint<1>

      // Define the Reader interface by binding it to the register
      cmt2.interface.def @StorageReader : @Reader [
        [@storage, @read, @getData]
      ]

      // Instantiate the child module and bind the interface
      cmt2.instance @processor = @child(%clk) : i1 with [
        [@StorageReader, @reader]
      ]

      // A rule that uses the child's methods
      cmt2.rule @compute() -> !firrtl.uint<1> {
        cmt2.return
      } {
        %c10 = hw.constant 10 : !firrtl.uint<32>
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
