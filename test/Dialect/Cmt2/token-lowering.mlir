// RUN: circt-opt %s -cmt2-dataflow-lowering -cmt2-token-lowering | FileCheck %s

// Test token lowering pass: annotates tokens with implementation attributes

// CHECK-LABEL: cmt2.circuit
cmt2.circuit {
  // CHECK-LABEL: cmt2.module @token_lowering_module
  cmt2.module @token_lowering_module {

    //===------------------------------------------------------------------===//
    // Test 1: LS token -> shift register annotation
    //===------------------------------------------------------------------===//

    // After lowering, dataflow ops are erased and replaced with rules
    // CHECK: cmt2.rule @ls_pipeline_stage0()
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK: cmt2.rule @ls_pipeline_stage1()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK: cmt2.rule @ls_pipeline_final()
    // CHECK-SAME: tokens_in(%{{.*}}: !cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK-NOT: cmt2.proc.dataflow @ls_pipeline
    cmt2.proc.dataflow @ls_pipeline(%input: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
      // LS mode (default) tokens should get shiftreg implementation
      %tok0 = cmt2.dataflow.task @stage0() -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        %data = firrtl.constant 1 : !firrtl.uint<32>
        %tok = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      %tok1 = cmt2.dataflow.task @stage1() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<32>>) -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        %data = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        %tok = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok1: !cmt2.sync_token<data = !firrtl.uint<32>>) {
        %result = cmt2.token.data %tok1 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        cmt2.dataflow.return %result : !firrtl.uint<32>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 2: LI token -> FIFO annotation
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @li_pipeline_stage0()
    // CHECK: cmt2.rule @li_pipeline_final()
    // CHECK-NOT: cmt2.proc.dataflow @li_pipeline
    cmt2.proc.dataflow @li_pipeline(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      // LI mode tokens should get fifo implementation
      %tok0 = cmt2.dataflow.task @stage0() -> (!cmt2.sync_token<data = !firrtl.uint<16>, mode = li>) {
        %data = firrtl.constant 2 : !firrtl.uint<16>
        %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>, mode = li>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>, mode = li>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<16>, mode = li>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<16>, mode = li> -> !firrtl.uint<16>
        cmt2.dataflow.return %result : !firrtl.uint<16>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 3: Fork pattern - multi-consumer token
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @fork_pattern_source()
    // CHECK: cmt2.rule @fork_pattern_branch_a()
    // CHECK: cmt2.rule @fork_pattern_branch_b()
    // CHECK: cmt2.rule @fork_pattern_join()
    // CHECK-NOT: cmt2.proc.dataflow @fork_pattern
    cmt2.proc.dataflow @fork_pattern(%x: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // Token consumed by multiple tasks should get fanout/broadcast annotation
      %tok_src = cmt2.dataflow.task @source() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        %data = firrtl.constant 5 : !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      // Fork: same token consumed by two tasks
      %tok_a = cmt2.dataflow.task @branch_a() tokens_in(%tok_src: !cmt2.sync_token<data = !firrtl.uint<8>>) -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        %data = cmt2.token.data %tok_src : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      %tok_b = cmt2.dataflow.task @branch_b() tokens_in(%tok_src: !cmt2.sync_token<data = !firrtl.uint<8>>) -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        %data = cmt2.token.data %tok_src : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        %tok = cmt2.token.create %data : !firrtl.uint<8> -> !cmt2.sync_token<data = !firrtl.uint<8>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<8>>
      }

      cmt2.dataflow.task @join() tokens_in(%tok_a: !cmt2.sync_token<data = !firrtl.uint<8>>, %tok_b: !cmt2.sync_token<data = !firrtl.uint<8>>) {
        %a = cmt2.token.data %tok_a : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        %b = cmt2.token.data %tok_b : !cmt2.sync_token<data = !firrtl.uint<8>> -> !firrtl.uint<8>
        %result = firrtl.add %a, %b : (!firrtl.uint<8>, !firrtl.uint<8>) -> !firrtl.uint<9>
        %result_trunc = firrtl.bits %result 7 to 0 : (!firrtl.uint<9>) -> !firrtl.uint<8>
        cmt2.dataflow.return %result_trunc : !firrtl.uint<8>
      }
    }

    //===------------------------------------------------------------------===//
    // Test 4: Void tokens (synchronization only)
    //===------------------------------------------------------------------===//

    // CHECK: cmt2.rule @void_tokens_sync_point()
    // CHECK: cmt2.rule @void_tokens_after_sync()
    // CHECK-NOT: cmt2.proc.dataflow @void_tokens
    cmt2.proc.dataflow @void_tokens(%x: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      // Void tokens (no data) for pure synchronization
      %tok = cmt2.dataflow.task @sync_point() -> (!cmt2.sync_token) {
        %t = cmt2.token.create : !cmt2.sync_token
        cmt2.dataflow.yield %t : !cmt2.sync_token
      }

      cmt2.dataflow.task @after_sync() tokens_in(%tok: !cmt2.sync_token) {
        %result = firrtl.constant 42 : !firrtl.uint<8>
        cmt2.dataflow.return %result : !firrtl.uint<8>
      }
    }
  }
}
