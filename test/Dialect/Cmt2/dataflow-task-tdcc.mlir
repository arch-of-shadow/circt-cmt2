// RUN: circt-opt %s -cmt2-tdcc | FileCheck %s

//===----------------------------------------------------------------------===//
// Phase 7 C1: TDCC Attribute Verification for DataflowTaskOp
// Test that TDCC pass correctly adds FSM attributes to tasks with proc control
//===----------------------------------------------------------------------===//

// CHECK-LABEL: cmt2.circuit
cmt2.circuit {

  // CHECK-LABEL: cmt2.module @TDCCAttributeTest
  cmt2.module @TDCCAttributeTest {
    cmt2.proc.static_step @step_a<1> {}
    cmt2.proc.static_step @step_b<2> {}

    //===------------------------------------------------------------------===//
    // Test 1: Sequential task - verify FSM attributes are added
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.proc.dataflow @test_seq
    cmt2.proc.dataflow @test_seq(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @task_seq()
      // CHECK-SAME: tdcc.done_state
      // CHECK-SAME: tdcc.enables
      // CHECK-SAME: tdcc.fsm_width
      // CHECK-SAME: tdcc.has_proc_control
      // CHECK-SAME: tdcc.num_states
      // CHECK-SAME: tdcc.transitions
      %tok0 = cmt2.dataflow.task @task_seq() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @step_a
          cmt2.proc.enable @step_b
        }
        %data = firrtl.constant 1 : !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 2: Parallel task - verify in_simple_par attribute
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.proc.dataflow @test_par
    cmt2.proc.dataflow @test_par(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @task_par()
      // CHECK-SAME: tdcc.has_proc_control
      %tok0 = cmt2.dataflow.task @task_par() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        cmt2.proc.par {
          // CHECK: cmt2.proc.enable @step_a
          // CHECK-SAME: tdcc.in_simple_par = true
          cmt2.proc.enable @step_a
          // CHECK: cmt2.proc.enable @step_b
          // CHECK-SAME: tdcc.in_simple_par = true
          cmt2.proc.enable @step_b
        }
        %data = firrtl.constant 2 : !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 3: Conditional task - verify guarded transitions exist
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.proc.dataflow @test_if
    cmt2.proc.dataflow @test_if(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @task_if()
      // CHECK-SAME: tdcc.transitions
      // CHECK-SAME: guard_op_id
      %tok0 = cmt2.dataflow.task @task_if() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        %cond = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.proc.if %cond : !firrtl.uint<1> {
          cmt2.proc.enable @step_a
        } else {
          cmt2.proc.enable @step_b
        }
        %data = firrtl.constant 3 : !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 4: Static repeat - verify iteration attribute present
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.proc.dataflow @test_repeat
    cmt2.proc.dataflow @test_repeat(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @task_repeat()
      // CHECK-SAME: tdcc.enables
      // CHECK-DAG: {end_state = 2 : i64, is_static = true, iteration = 0 : i64, latency = 1 : i64, state = 1 : i64, step = @step_a}
      // CHECK-DAG: {end_state = 3 : i64, is_static = true, iteration = 1 : i64, latency = 1 : i64, state = 2 : i64, step = @step_a}
      // CHECK-DAG: {end_state = 4 : i64, is_static = true, iteration = 2 : i64, latency = 1 : i64, state = 3 : i64, step = @step_a}
      // CHECK-DAG: {end_state = 5 : i64, is_static = true, iteration = 3 : i64, latency = 1 : i64, state = 4 : i64, step = @step_a}
      %tok0 = cmt2.dataflow.task @task_repeat() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        cmt2.proc.static_repeat 4 {
          cmt2.proc.enable @step_a
        }
        %data = firrtl.constant 4 : !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 5: Task without proc control - should NOT have TDCC attributes
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.proc.dataflow @test_no_control
    cmt2.proc.dataflow @test_no_control(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // CHECK: cmt2.dataflow.task @task_simple()
      // CHECK-NOT: tdcc.has_proc_control
      // CHECK: cmt2.dataflow.yield
      %tok0 = cmt2.dataflow.task @task_simple() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        %data = firrtl.constant 5 : !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }
  }
}
