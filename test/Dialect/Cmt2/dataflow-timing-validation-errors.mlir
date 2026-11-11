// RUN: circt-opt %s -cmt2-tdcc -cmt2-timing-validation -split-input-file -verify-diagnostics

//===----------------------------------------------------------------------===//
// Phase 7 C3: Unified Timing Validation Error Cases
// Test that TimingValidation detects timing mismatches
//===----------------------------------------------------------------------===//

cmt2.circuit {

  cmt2.module @TimingMismatchTest {
    cmt2.proc.static_step @step_2cycle<2> {}

    //===------------------------------------------------------------------===//
    // Test: Task with MISMATCHING timing - should FAIL validation
    // Control flow: seq{enable @step_2cycle, enable @step_2cycle} = 4 cycles
    // Declared timing: [0, 3] = 3 cycles
    // Mismatch: 4 != 3
    //===------------------------------------------------------------------===//

    cmt2.proc.dataflow @mismatching_timing(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // expected-error @+1 {{'cmt2.dataflow.task' op declared timing interval [0, 3] implies 3 cycles, but TDCC computed 4 cycles from control flow}}
      %tok = cmt2.dataflow.task @task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) attributes {timing = #cmt2.timing<[0, 3]>} {
        cmt2.proc.seq {
          cmt2.proc.enable @step_2cycle
          cmt2.proc.enable @step_2cycle
        }
        %data = firrtl.constant 1 : !firrtl.uint<8>
        %t = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %t : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %result = cmt2.token.data %tok : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }
  }
}
