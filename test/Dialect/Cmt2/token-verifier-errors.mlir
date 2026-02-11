// RUN: circt-opt %s -split-input-file -verify-diagnostics

// Test verifier errors for token operations

// -----

// TokenDataOp: token must have data
func.func @token_data_no_data(%tok: !cmt2.sync_token) {
  // expected-error @+1 {{token must carry data}}
  %data = cmt2.token.data %tok : !cmt2.sync_token -> !firrtl.uint<32>
  return
}

// -----

// TokenDataOp: result type must match token data type
func.func @token_data_type_mismatch(%tok: !cmt2.sync_token<data = !firrtl.uint<32>>) {
  // expected-error @+1 {{result type '!firrtl.uint<16>' must match token data type '!firrtl.uint<32>'}}
  %data = cmt2.token.data %tok : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<16>
  return
}

// -----

// TokenCreateOp: token with data but no data operand
func.func @token_create_missing_data() {
  // expected-error @+1 {{token type has data but no data operand provided}}
  %tok = cmt2.token.create : !cmt2.sync_token<data = !firrtl.uint<32>>
  return
}

// -----

// TokenCreateOp: data type mismatch
func.func @token_create_type_mismatch(%data: !firrtl.uint<16>) {
  // expected-error @+1 {{data type '!firrtl.uint<16>' must match token data type '!firrtl.uint<32>'}}
  %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<32>>
  return
}

// -----

// TokenJoinOp: must have at least one input
func.func @token_join_empty() {
  // expected-error @+1 {{must have at least one input token}}
  %tok = cmt2.token.join : () -> !cmt2.sync_token
  return
}
