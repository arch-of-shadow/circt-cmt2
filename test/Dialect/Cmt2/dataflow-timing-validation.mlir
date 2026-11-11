// RUN: circt-opt %s -cmt2-tdcc -cmt2-timing-validation 2>&1 | FileCheck %s

//===----------------------------------------------------------------------===//
// Phase 7 C3: Unified Timing Validation for Dataflow Tasks
// Test that TimingValidation cross-validates task timing with TDCC attributes
//===----------------------------------------------------------------------===//

// CHECK-NOT: error

cmt2.circuit {

  cmt2.module @TimingValidationTest {
    cmt2.proc.static_step @step_2cycle<2> {}
    cmt2.proc.static_step @step_3cycle<3> {}

    //===------------------------------------------------------------------===//
    // Test 1: Task with matching timing - should pass validation
    //===------------------------------------------------------------------===//

    // This task has seq{enable @step_2cycle, enable @step_2cycle}
    // TDCC computes: state 0 -> state 2 -> state 4 (done_state = 4)
    // Timing [0, 4] implies 4 cycles, which matches
    cmt2.proc.dataflow @matching_timing(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok = cmt2.dataflow.task @task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) attributes {timing = #cmt2.timing<[0, 4]>} {
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

    //===------------------------------------------------------------------===//
    // Test 2: Task without timing attribute - should pass (no cross-validation)
    //===------------------------------------------------------------------===//

    cmt2.proc.dataflow @no_timing(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok = cmt2.dataflow.task @task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @step_3cycle
        }
        %data = firrtl.constant 2 : !firrtl.uint<8>
        %t = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %t : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %result = cmt2.token.data %tok : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 3: Task without proc control - should pass (no TDCC attrs)
    //===------------------------------------------------------------------===//

    cmt2.proc.dataflow @no_proc_control(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok = cmt2.dataflow.task @task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) attributes {timing = #cmt2.timing<[0, 1]>} {
        %data = firrtl.constant 3 : !firrtl.uint<8>
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
