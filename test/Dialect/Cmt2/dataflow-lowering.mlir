// RUN: circt-opt %s -cmt2-dataflow-lowering | FileCheck %s

// Test dataflow-to-rules lowering pass

// CHECK-LABEL: cmt2.circuit
cmt2.circuit {
  // CHECK-LABEL: cmt2.module @dataflow_module
  cmt2.module @dataflow_module {

    //===------------------------------------------------------------------===//
    // Test 1: Simple linear pipeline
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @simple_pipeline_stage0()
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK: cmt2.rule @simple_pipeline_stage1()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK: cmt2.rule @simple_pipeline_final()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<32>>)

    // Dataflow ops are erased after lowering (rules created above)
    // CHECK-NOT: cmt2.proc.dataflow @simple_pipeline
    cmt2.proc.dataflow @simple_pipeline(%input: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
      %tok0 = cmt2.dataflow.task @stage0() -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        %data = firrtl.constant 42 : !firrtl.uint<32>
        %tok = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      %tok1 = cmt2.dataflow.task @stage1() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<32>>) -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        %data = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        %one = firrtl.constant 1 : !firrtl.uint<32>
        %next = firrtl.add %data, %one : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
        %next_trunc = firrtl.bits %next 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
        %tok = cmt2.token.create %next_trunc : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok1: !cmt2.sync_token<data = !firrtl.uint<32>>) {
        %result = cmt2.token.data %tok1 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        cmt2.dataflow.return %result : !firrtl.uint<32>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 2: Fork-join pattern (multi-consumer token)
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @fork_join_source()
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<16>>)

    // CHECK: cmt2.rule @fork_join_branch_a()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<16>>)
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<16>>)

    // CHECK: cmt2.rule @fork_join_branch_b()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<16>>)
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<16>>)

    // Join task takes two token inputs
    // CHECK: cmt2.rule @fork_join_join()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<16>>, %{{.*}}: !cmt2.sync_token<data = !firrtl.uint<16>>)

    // Dataflow ops are erased after lowering
    // CHECK-NOT: cmt2.proc.dataflow @fork_join
    cmt2.proc.dataflow @fork_join(%x: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      %tok_src = cmt2.dataflow.task @source() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        %data = firrtl.constant 10 : !firrtl.uint<16>
        %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      // Fork: same token consumed by two tasks
      %tok_a = cmt2.dataflow.task @branch_a() tokens_in(%tok_src: !cmt2.sync_token<data = !firrtl.uint<16>>) -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        %data = cmt2.token.data %tok_src : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %one = firrtl.constant 1 : !firrtl.uint<16>
        %result = firrtl.add %data, %one : (!firrtl.uint<16>, !firrtl.uint<16>) -> !firrtl.uint<17>
        %result_trunc = firrtl.bits %result 15 to 0 : (!firrtl.uint<17>) -> !firrtl.uint<16>
        %tok = cmt2.token.create %result_trunc : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      %tok_b = cmt2.dataflow.task @branch_b() tokens_in(%tok_src: !cmt2.sync_token<data = !firrtl.uint<16>>) -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        %data = cmt2.token.data %tok_src : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %two = firrtl.constant 2 : !firrtl.uint<16>
        %result = firrtl.mul %data, %two : (!firrtl.uint<16>, !firrtl.uint<16>) -> !firrtl.uint<32>
        %result_trunc = firrtl.bits %result 15 to 0 : (!firrtl.uint<32>) -> !firrtl.uint<16>
        %tok = cmt2.token.create %result_trunc : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      // Join: final task waits for both branches
      cmt2.dataflow.task @join() tokens_in(%tok_a: !cmt2.sync_token<data = !firrtl.uint<16>>, %tok_b: !cmt2.sync_token<data = !firrtl.uint<16>>) {
        %a = cmt2.token.data %tok_a : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %b = cmt2.token.data %tok_b : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %result = firrtl.add %a, %b : (!firrtl.uint<16>, !firrtl.uint<16>) -> !firrtl.uint<17>
        %result_trunc = firrtl.bits %result 15 to 0 : (!firrtl.uint<17>) -> !firrtl.uint<16>
        cmt2.dataflow.return %result_trunc : !firrtl.uint<16>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 3: Timing attribute preservation
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @timed_s0()
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<8>>)
    // CHECK-SAME: timing = #cmt2.timing<[0, 1]>

    // CHECK: cmt2.rule @timed_s1()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<8>>)
    // CHECK-SAME: timing = #cmt2.timing<[1, 2]>

    // Dataflow ops are erased after lowering
    // CHECK-NOT: cmt2.proc.dataflow @timed
    cmt2.proc.dataflow @timed(%in: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok = cmt2.dataflow.task @s0() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) attributes {timing = #cmt2.timing<[0, 1]>} {
        %data = firrtl.constant 0 : !firrtl.uint<8>
        %t = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %t : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @s1() tokens_in(%tok: !cmt2.sync_token<data = !firrtl.uint<8>>) attributes {timing = #cmt2.timing<[1, 2]>} {
        %result = cmt2.token.data %tok : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }
  }
}
