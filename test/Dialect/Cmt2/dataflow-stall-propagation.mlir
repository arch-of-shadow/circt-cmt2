// RUN: circt-opt %s -cmt2-tdcc -cmt2-dataflow-lowering -cmt2-proc-stmt-to-action | FileCheck %s

//===----------------------------------------------------------------------===//
// Phase 7 C4: Stall Propagation to Task FSMs
// Test that ProcStmtToAction marks FSM tick/enable rules for stall gating
// when the module has stall.controller attribute
//===----------------------------------------------------------------------===//

// CHECK-LABEL: cmt2.circuit

cmt2.circuit {
  // Module with stall.controller attribute (indicating mixed LS/LI region)
  // CHECK-LABEL: cmt2.module @StallGatedModule
  // The stall.controller attribute is normally set by StallControllerGen pass,
  // but we add it manually here for testing
  cmt2.module @StallGatedModule(%clock: !firrtl.clock, %reset: !firrtl.uint<1>) attributes {stall.controller} {
    cmt2.proc.static_step @compute<2> {}

    // The generated FSM tick rule should have stall.gated attribute
    // CHECK: cmt2.rule @pipeline_process_tick()
    // CHECK-SAME: dataflow.fsm_tick
    // CHECK-SAME: stall.gated

    // The generated enable rules should also have stall.gated attribute
    // CHECK: cmt2.rule @pipeline_process_enable_compute_s{{[0-9]+}}()
    // CHECK-SAME: stall.gated

    cmt2.proc.dataflow @pipeline(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      %tok0 = cmt2.dataflow.task @process() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        cmt2.proc.seq {
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

  // -----

  // Module WITHOUT stall.controller attribute
  // CHECK-LABEL: cmt2.module @NonStallGatedModule
  cmt2.module @NonStallGatedModule(%clock: !firrtl.clock, %reset: !firrtl.uint<1>) {
    cmt2.proc.static_step @compute<2> {}

    // The generated FSM tick rule should NOT have stall.gated attribute
    // CHECK: cmt2.rule @pipeline2_process_tick()
    // CHECK-SAME: dataflow.fsm_tick
    // CHECK-NOT: stall.gated

    cmt2.proc.dataflow @pipeline2(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      %tok0 = cmt2.dataflow.task @process() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        cmt2.proc.seq {
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
