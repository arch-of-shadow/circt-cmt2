// RUN: circt-opt %s | circt-opt | FileCheck %s

// Test SyncToken type parsing and printing

// CHECK-LABEL: @sync_token_types
func.func @sync_token_types(
  // CHECK-SAME: %arg0: !cmt2.sync_token
  %arg0: !cmt2.sync_token,
  // CHECK-SAME: %arg1: !cmt2.sync_token<data = !firrtl.uint<32>>
  %arg1: !cmt2.sync_token<data = !firrtl.uint<32>>,
  // Default LS mode is elided in output
  // CHECK-SAME: %arg2: !cmt2.sync_token<data = !firrtl.uint<16>>
  %arg2: !cmt2.sync_token<data = !firrtl.uint<16>, mode = ls>,
  // CHECK-SAME: %arg3: !cmt2.sync_token<data = !firrtl.uint<8>, mode = li>
  %arg3: !cmt2.sync_token<data = !firrtl.uint<8>, mode = li>
) {
  return
}

// Test token operations

// CHECK-LABEL: @token_valid_op
func.func @token_valid_op(%tok: !cmt2.sync_token<data = !firrtl.uint<32>>) -> !firrtl.uint<1> {
  // CHECK: %[[VALID:.+]] = cmt2.token.valid %arg0 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<1>
  %valid = cmt2.token.valid %tok : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<1>
  // CHECK: return %[[VALID]]
  return %valid : !firrtl.uint<1>
}

// CHECK-LABEL: @token_data_op
func.func @token_data_op(%tok: !cmt2.sync_token<data = !firrtl.uint<32>>) -> !firrtl.uint<32> {
  // CHECK: %[[DATA:.+]] = cmt2.token.data %arg0 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
  %data = cmt2.token.data %tok : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
  // CHECK: return %[[DATA]]
  return %data : !firrtl.uint<32>
}

// CHECK-LABEL: @token_create_op
func.func @token_create_op(%data: !firrtl.uint<32>) -> (!cmt2.sync_token, !cmt2.sync_token<data = !firrtl.uint<32>>) {
  // CHECK: %[[TOK1:.+]] = cmt2.token.create : !cmt2.sync_token
  %tok1 = cmt2.token.create : !cmt2.sync_token
  // CHECK: %[[TOK2:.+]] = cmt2.token.create %arg0 : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
  %tok2 = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
  // CHECK: return %[[TOK1]], %[[TOK2]]
  return %tok1, %tok2 : !cmt2.sync_token, !cmt2.sync_token<data = !firrtl.uint<32>>
}

// CHECK-LABEL: @token_join_op
func.func @token_join_op(
  %tok_a: !cmt2.sync_token<data = !firrtl.uint<32>>,
  %tok_b: !cmt2.sync_token<data = !firrtl.uint<16>>
) -> !cmt2.sync_token {
  // CHECK: %[[JOINED:.+]] = cmt2.token.join %arg0, %arg1 : (!cmt2.sync_token<data = !firrtl.uint<32>>, !cmt2.sync_token<data = !firrtl.uint<16>>) -> !cmt2.sync_token
  %joined = cmt2.token.join %tok_a, %tok_b : (!cmt2.sync_token<data = !firrtl.uint<32>>, !cmt2.sync_token<data = !firrtl.uint<16>>) -> !cmt2.sync_token
  // CHECK: return %[[JOINED]]
  return %joined : !cmt2.sync_token
}

// Test RuleOp with token inputs and outputs

cmt2.circuit {
  // CHECK-LABEL: cmt2.module @token_rule_module
  cmt2.module @token_rule_module {
    // CHECK: cmt2.rule @basic_rule() -> ()
    cmt2.rule @basic_rule() -> () {
      cmt2.return
    } {
      cmt2.return
    }

    // CHECK: cmt2.rule @rule_with_tokens()
    // CHECK-SAME: tokens_in(%tok_a: !cmt2.sync_token<data = !firrtl.uint<32>>, %tok_b: !cmt2.sync_token)
    // CHECK-SAME: tokens_out(!cmt2.sync_token<data = !firrtl.uint<16>>)
    // CHECK-SAME: -> ()
    cmt2.rule @rule_with_tokens()
        tokens_in(%tok_a: !cmt2.sync_token<data = !firrtl.uint<32>>,
                  %tok_b: !cmt2.sync_token)
        tokens_out(!cmt2.sync_token<data = !firrtl.uint<16>>)
        -> () {
      // Guard region - check token validity
      %valid = cmt2.token.valid %tok_a : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<1>
      cmt2.return
    } {
      // Body region - use token data
      %data = cmt2.token.data %tok_a : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
      cmt2.return
    }

    // CHECK: cmt2.rule @rule_with_args_and_tokens(%data_in: !firrtl.uint<8>)
    // CHECK-SAME: tokens_in(%tok: !cmt2.sync_token<data = !firrtl.uint<32>>)
    // CHECK-SAME: -> (!firrtl.uint<8>)
    cmt2.rule @rule_with_args_and_tokens(%data_in: !firrtl.uint<8>)
        tokens_in(%tok: !cmt2.sync_token<data = !firrtl.uint<32>>)
        -> (!firrtl.uint<8>) {
      cmt2.return
    } {
      cmt2.return %data_in : !firrtl.uint<8>
    }
  }
}
