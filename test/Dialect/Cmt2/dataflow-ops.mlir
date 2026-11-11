// RUN: circt-opt %s | circt-opt | FileCheck %s

// Test ProcDataflowOp parsing and printing

// CHECK-LABEL: cmt2.circuit
cmt2.circuit {
  // CHECK-LABEL: cmt2.module @dataflow_module
  cmt2.module @dataflow_module {

    // CHECK: cmt2.proc.dataflow @simple_pipeline(%input: !firrtl.uint<32>) -> (!firrtl.uint<32>)
    cmt2.proc.dataflow @simple_pipeline(%input: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
      // CHECK: %[[TOK0:.*]] = cmt2.dataflow.task @stage0()
      // CHECK-SAME: -> (!cmt2.sync_token<data = !firrtl.uint<32>>)
      %tok0 = cmt2.dataflow.task @stage0() -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        %data = firrtl.constant 42 : !firrtl.uint<32>
        %tok = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      // CHECK: %[[TOK1:.*]] = cmt2.dataflow.task @stage1()
      // CHECK-SAME: tokens_in(%[[TOK0]]: !cmt2.sync_token<data = !firrtl.uint<32>>)
      // CHECK-SAME: -> (!cmt2.sync_token<data = !firrtl.uint<32>>)
      %tok1 = cmt2.dataflow.task @stage1() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<32>>) -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        %data = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        %one = firrtl.constant 1 : !firrtl.uint<32>
        %next = firrtl.add %data, %one : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
        %next_trunc = firrtl.bits %next 31 to 0 : (!firrtl.uint<33>) -> !firrtl.uint<32>
        %tok = cmt2.token.create %next_trunc : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      // CHECK: cmt2.dataflow.task @final()
      // CHECK-SAME: tokens_in(%[[TOK1]]: !cmt2.sync_token<data = !firrtl.uint<32>>)
      cmt2.dataflow.task @final() tokens_in(%tok1: !cmt2.sync_token<data = !firrtl.uint<32>>) {
        %result = cmt2.token.data %tok1 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        cmt2.dataflow.return %result : !firrtl.uint<32>
      }
    }

    // CHECK: cmt2.proc.dataflow @fork_join_pattern(%x: !firrtl.uint<16>)
    cmt2.proc.dataflow @fork_join_pattern(%x: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      // Source task produces one token
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

    // CHECK: cmt2.proc.dataflow @with_timing(%in: !firrtl.uint<8>)
    // CHECK-SAME: attributes {interval = 2 : i64}
    cmt2.proc.dataflow @with_timing(%in: !firrtl.uint<8>) -> (!firrtl.uint<8>) attributes {interval = 2 : i64} {
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
