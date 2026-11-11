// RUN: circt-opt %s --mlir-disable-threading --cmt2-print-call-info -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
// RUN: circt-opt %s --mlir-disable-threading --cmt2-print-conflict-matrix -o /dev/null 2>&1 | FileCheck %s --check-prefix=MATRIX
// RUN: circt-opt %s --mlir-disable-threading --cmt2-print-scheduler -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEDULE
// Interface aliases, repeated calls, @this, and independent instance identities.
cmt2.circuit {
  cmt2.interface @I {
    cmt2.method @put() -> () { cmt2.return } { cmt2.return }
  }
  cmt2.module @Cell(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.method @put() -> () { cmt2.return } { cmt2.return }
    cmt2.value @read() -> (!firrtl.uint<1>) { cmt2.return } {
      %one = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %one : !firrtl.uint<1>
    }
  }
  cmt2.module @Top(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.instance @a = @Cell(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.instance @b = @Cell(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.interface.def @alias : @I [[@a, @put, @put]]
    cmt2.value @local() -> (!firrtl.uint<1>) { cmt2.return } {
      %v = cmt2.call @a @read() : () -> (!firrtl.uint<1>)
      cmt2.return %v : !firrtl.uint<1>
    }
    cmt2.rule @first() {
      %v = cmt2.call @this @local() : () -> (!firrtl.uint<1>)
      cmt2.return %v : !firrtl.uint<1>
    } {
      cmt2.call @alias @put() : () -> ()
      cmt2.call @a @put() : () -> ()
      cmt2.return
    }
    cmt2.rule @shared() { cmt2.return } {
      cmt2.call @a @put() : () -> ()
      cmt2.return
    }
    cmt2.rule @independent() { cmt2.return } {
      cmt2.call @b @put() : () -> ()
      cmt2.return
    }
    cmt2.rule @explicitCF() { cmt2.return } {
      cmt2.call @a @put() : () -> ()
      cmt2.return
    }
  } { conflictFree = [[@first, @explicitCF]] }
}
// CALL: Entity: @first
// CALL-NEXT: Call: @this -> @local (type: Value)
// CALL-NEXT: Call: @a -> @put (type: Method)
// CALL-NEXT: Call: @a -> @put (type: Method)
// MATRIX-DAG: @first <> @shared
// MATRIX-DAG: @first / @independent
// MATRIX-DAG: @first / @explicitCF
// MATRIX-DAG: @first <> @first
// MATRIX-DAG: @local / @local
// SCHEDULE: Top

// Same module/instance/entity names in another circuit must not reuse symbols.
cmt2.circuit {
  cmt2.module @Cell(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.method @read() -> (!firrtl.uint<1>) { cmt2.return } {
      %one = firrtl.constant 1 : !firrtl.uint<1>
      cmt2.return %one : !firrtl.uint<1>
    }
  }
  cmt2.module @Top(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
    cmt2.instance @a = @Cell(%clk, %rst) : !firrtl.clock, !firrtl.uint<1>
    cmt2.rule @caller() { cmt2.return } {
      %v = cmt2.call @a @read() : () -> (!firrtl.uint<1>)
      cmt2.return
    }
  }
}
// CALL: Call: @a -> @read (type: Method)
