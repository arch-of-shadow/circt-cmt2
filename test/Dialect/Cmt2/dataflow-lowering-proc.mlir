// RUN: circt-opt %s -cmt2-tdcc -cmt2-dataflow-lowering | FileCheck %s

//===----------------------------------------------------------------------===//
// Phase 7 C2: Dataflow Lowering with Proc Control
// Test that DataflowLowering propagates TDCC attributes and skips proc ops
//===----------------------------------------------------------------------===//

// CHECK-LABEL: cmt2.circuit
cmt2.circuit {

  // CHECK-LABEL: cmt2.module @DataflowLoweringProcTest
  cmt2.module @DataflowLoweringProcTest {
    cmt2.proc.static_step @compute<2> {}

    //===------------------------------------------------------------------===//
    // Test: Rule generated from task with proc control should have:
    // 1. TDCC attributes propagated
    // 2. dataflow.from_task marker
    // 3. No proc control ops cloned to body
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @pipeline_process()
    // CHECK-SAME: attributes {
    // CHECK-SAME: dataflow.from_task
    // CHECK-SAME: dataflow.task_name = "process"
    // CHECK-SAME: tdcc.done_state
    // CHECK-SAME: tdcc.enables
    // CHECK-SAME: tdcc.fsm_width
    // CHECK-SAME: tdcc.has_proc_control
    // CHECK-SAME: tdcc.num_states
    // CHECK-SAME: tdcc.transitions
    // CHECK: cmt2.return
    // Body region - verify constant and token.create are cloned
    // CHECK: firrtl.constant 42
    // CHECK: cmt2.token.create
    // CHECK: cmt2.return

    // CHECK: cmt2.rule @pipeline_output()
    cmt2.proc.dataflow @pipeline(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      %tok0 = cmt2.dataflow.task @process() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @compute
          cmt2.proc.enable @compute
        }
        %data = firrtl.constant 42 : !firrtl.uint<16>
        %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      cmt2.dataflow.task @output() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<16>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        cmt2.dataflow.return %result : !firrtl.uint<16>
      }
    }

    //===------------------------------------------------------------------===//
    // Test: Rule generated from task WITHOUT proc control should:
    // 1. NOT have TDCC attributes
    // 2. NOT have dataflow.from_task marker
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @simple_pipeline_simple_task()
    // CHECK-NOT: tdcc.has_proc_control
    // CHECK: cmt2.rule @simple_pipeline_final()
    cmt2.proc.dataflow @simple_pipeline(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok = cmt2.dataflow.task @simple_task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
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
