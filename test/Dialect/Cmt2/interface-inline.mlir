// RUN: circt-opt %s | FileCheck %s
// This test demonstrates interface mechanism with module inlining

builtin.module {
  // Simple register module (external)
  "hw.module"() ({
    ^bb0(%write: i32, %writeEnable: i1, %clock: !seq.clock):
    %init = seq.initial () {
      %c0_i32 = "hw.constant"() {value = 0 : i32} : () -> i32
      seq.yield %c0_i32 : i32
    } : () -> !seq.immutable<i32>
    %read = seq.compreg %next, %clock initial %init : i32
    %next = "comb.mux"(%writeEnable, %write, %read) : (i1, i32, i32) -> i32
    %writeReady = "hw.constant"() {value = 1 : i1} : () -> i1
    %readReady = "hw.constant"() {value = 1 : i1} : () -> i1
    "hw.output"(%writeReady, %readReady, %read) : (i1, i1, i32) -> ()
  }) {
    argNames = ["write", "writeEnable", "clock"],
    resultNames = ["writeReady", "readReady", "data"],
    parameters = [],
    sym_name = "Reg32",
    module_type = !hw.modty<input write : i32, input writeEnable : i1, input clock : !seq.clock, output writeReady : i1, output readReady : i1, output data : i32>
  } : () -> ()

  cmt2.circuit {
    // External register module
    cmt2.module.extern.hw @reg : @Reg32(%clk: i1) {
      cmt2.bind.bare %clk, @clock : i1
      cmt2.bind.value @read : (i1) -> (i32) [ready = @readReady, data = [@data]]
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

    // Define an interface for reading data
    cmt2.interface @Reader {
      cmt2.value @getData() -> (i32) {}{}
    }

    // A child module that requires a Reader interface
    // This module will be inlined into the parent
    cmt2.module @child(%clk: i1) {
      // Declare that we need a Reader interface
      cmt2.interface.decl @reader : @Reader
      cmt2.instance @x = @reg(%clk) : i1

      // A method that calls through the interface
      cmt2.method @process(%x: i32) -> (i32) {
        cmt2.return
      } {
        // Call through the interface to get data
        %data = cmt2.call @reader @getData() : () -> (i32)
        // Add input to the data
        %result = comb.add %x, %data : i32
        cmt2.call @x @write(%result) : (i32) -> ()
        cmt2.return %result : i32
      }

      // A value that also uses the interface
      cmt2.value @doubleData() -> (i32) {
        cmt2.return
      } {
        %data = cmt2.call @reader @getData() : () -> (i32)
        %c2 = hw.constant 2 : i32
        %result = comb.mul %data, %c2 : i32
        cmt2.return %result : i32
      }
    }

    // Parent module that provides the interface and instantiates child
    cmt2.module @parent(%clk: i1) {
      // Create a register instance
      cmt2.instance @storage = @reg(%clk) : i1

      // Define the Reader interface by binding it to the register
      cmt2.interface.def @StorageReader : @Reader [
        [@storage, @read, @getData]
      ]

      // Instantiate the child module and bind the interface
      cmt2.instance @processor = @child(%clk) : i1 with [
        [@StorageReader, @reader]
      ]

      // A rule that uses the child's methods
      cmt2.rule @compute() -> i1 {
        cmt2.return
      } {
        %c10 = hw.constant 10 : i32
        // Call the child's method through the instance
        %result1 = cmt2.call @processor @process(%c10) : (i32) -> (i32)
        // Call the child's value
        %result2 = cmt2.call @processor @doubleData() : () -> (i32)
        // Write results back
        cmt2.call @storage @write(%result1) : (i32) -> ()
        cmt2.return
      }
    }
  }
}
