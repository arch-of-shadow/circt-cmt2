// RUN: circt-opt %s -cmt2-tdcc -cmt2-dataflow-lowering -cmt2-proc-stmt-to-action | FileCheck %s

//===----------------------------------------------------------------------===//
// Phase 7 C2: FSM Generation for Dataflow Tasks with Proc Control
// Test that ProcStmtToAction generates FSM infrastructure for dataflow rules
//===----------------------------------------------------------------------===//

// CHECK-LABEL: cmt2.circuit

// Verify FSM register module is created (at circuit level)
// CHECK: cmt2.module.extern.firrtl @__FSMReg_3

// CHECK-LABEL: cmt2.module @DataflowProcFSMTest
cmt2.circuit {
  cmt2.module @DataflowProcFSMTest(%clock: !firrtl.clock, %reset: !firrtl.uint<1>) {
    cmt2.proc.static_step @compute<2> {}

    // Verify rule has FSM generation markers
    // CHECK: cmt2.rule @pipeline_process()
    // CHECK-SAME: dataflow.fsm_generated
    // CHECK-SAME: dataflow.fsm_inst = "__fsm_df_process"

    // Verify FSM instance is created
    // CHECK: cmt2.instance @__fsm_df_process = @__FSMReg_3

    // Verify tick rule for FSM state transitions
    // CHECK: cmt2.rule @pipeline_process_tick()

    // Verify enable rules for each step enable (2 enables of @compute)
    // CHECK-DAG: cmt2.rule @pipeline_process_enable_compute_s0(){{.*}}enables.step = "compute"
    // CHECK-DAG: cmt2.rule @pipeline_process_enable_compute_s2(){{.*}}enables.step = "compute"

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
  }
}
