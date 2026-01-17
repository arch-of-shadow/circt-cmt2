// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s

//===----------------------------------------------------------------------===//
// Phase 7 C5: Automatic LS/LI Mode Inference
// Test that TDCC infers token mode from task body analysis
//===----------------------------------------------------------------------===//

// CHECK-LABEL: cmt2.circuit

cmt2.circuit {

  cmt2.module @ModeInferenceTest {
    cmt2.proc.static_step @compute<2> {}
    cmt2.proc.step @dynamic_step {}

    //===------------------------------------------------------------------===//
    // Test 1: Task with static control only -> LS mode (static_timing)
    //===------------------------------------------------------------------===//

    cmt2.proc.dataflow @static_pipeline(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @static_task()
      // CHECK-SAME: tdcc.static_timing
      // CHECK-NOT: tdcc.requires_li_mode
      %tok = cmt2.dataflow.task @static_task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @compute
          cmt2.proc.enable @compute
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
    // Test 2: Task with dynamic control (while loop) -> LI mode (requires_li_mode)
    //===------------------------------------------------------------------===//

    cmt2.proc.dataflow @dynamic_pipeline(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @dynamic_task()
      // CHECK-SAME: tdcc.requires_li_mode
      // CHECK-NOT: tdcc.static_timing
      %tok = cmt2.dataflow.task @dynamic_task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        cmt2.proc.while {
          %cnt = firrtl.constant 1 : !firrtl.uint<1>
          cmt2.proc.while_cond %cnt : !firrtl.uint<1>
        } do {
          cmt2.proc.enable @dynamic_step
          cmt2.proc.yield
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
    // Test 3: Task with static_repeat (static control) -> LS mode
    //===------------------------------------------------------------------===//

    cmt2.proc.dataflow @repeat_pipeline(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @repeat_task()
      // CHECK-SAME: tdcc.static_timing
      // CHECK-NOT: tdcc.requires_li_mode
      %tok = cmt2.dataflow.task @repeat_task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        cmt2.proc.static_repeat 4 {
          cmt2.proc.enable @compute
        }
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
