// RUN: circt-opt %s | circt-opt | FileCheck %s

//===----------------------------------------------------------------------===//
// Phase 7 C1: Dataflow-Proc Compatibility Tests
// Test that proc control operations can be nested inside DataflowTaskOp
//===----------------------------------------------------------------------===//

// CHECK-LABEL: cmt2.circuit
cmt2.circuit {

  //===--------------------------------------------------------------------===//
  // Test Module 1: Basic proc control operations in dataflow tasks
  //===--------------------------------------------------------------------===//

  // CHECK-LABEL: cmt2.module @BasicProcInDataflow
  cmt2.module @BasicProcInDataflow {
    // Define steps for proc.enable
    cmt2.proc.static_step @step_a <1> {}
    cmt2.proc.static_step @step_b <1> {}
    cmt2.proc.static_step @step_c <2> {}

    // Test 1: proc.seq in dataflow task
    // CHECK: cmt2.proc.dataflow @test_seq
    cmt2.proc.dataflow @test_seq(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok0 = cmt2.dataflow.task @task_seq() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        // CHECK: cmt2.proc.seq
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

    // Test 2: proc.par in dataflow task
    // CHECK: cmt2.proc.dataflow @test_par
    cmt2.proc.dataflow @test_par(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok0 = cmt2.dataflow.task @task_par() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        // CHECK: cmt2.proc.par
        cmt2.proc.par {
          cmt2.proc.enable @step_a
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

    // Test 3: proc.if in dataflow task
    // CHECK: cmt2.proc.dataflow @test_if
    cmt2.proc.dataflow @test_if(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok0 = cmt2.dataflow.task @task_if() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        %cond = firrtl.constant 1 : !firrtl.uint<1>
        // CHECK: cmt2.proc.if
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

    // Test 4: proc.static_repeat in dataflow task
    // CHECK: cmt2.proc.dataflow @test_static_repeat
    cmt2.proc.dataflow @test_static_repeat(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok0 = cmt2.dataflow.task @task_repeat() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        // CHECK: cmt2.proc.static_repeat 4
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

    // Test 5: proc.static_if in dataflow task
    // CHECK: cmt2.proc.dataflow @test_static_if
    cmt2.proc.dataflow @test_static_if(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok0 = cmt2.dataflow.task @task_static_if() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        %static_cond = firrtl.constant 1 : !firrtl.uint<1>
        // CHECK: cmt2.proc.static_if
        cmt2.proc.static_if %static_cond : !firrtl.uint<1> {
          cmt2.proc.enable @step_a
        } else {
          cmt2.proc.enable @step_b
        }
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

  //===--------------------------------------------------------------------===//
  // Test Module 2: Nested proc control in dataflow tasks
  //===--------------------------------------------------------------------===//

  // CHECK-LABEL: cmt2.module @NestedProcInDataflow
  cmt2.module @NestedProcInDataflow {
    cmt2.proc.static_step @op1 <1> {}
    cmt2.proc.static_step @op2 <1> {}
    cmt2.proc.static_step @op3 <1> {}

    // Test: seq containing par
    // CHECK: cmt2.proc.dataflow @test_seq_par
    cmt2.proc.dataflow @test_seq_par(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      %tok0 = cmt2.dataflow.task @task_nested() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        // CHECK: cmt2.proc.seq
        cmt2.proc.seq {
          cmt2.proc.enable @op1
          // CHECK: cmt2.proc.par
          cmt2.proc.par {
            cmt2.proc.enable @op2
            cmt2.proc.enable @op3
          }
        }
        %data = firrtl.constant 100 : !firrtl.uint<16>
        %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<16>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        cmt2.dataflow.return %result : !firrtl.uint<16>
      }
    }

    // Test: if containing seq
    // CHECK: cmt2.proc.dataflow @test_if_seq
    cmt2.proc.dataflow @test_if_seq(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      %tok0 = cmt2.dataflow.task @task_if_seq() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        %cond = firrtl.constant 1 : !firrtl.uint<1>
        // CHECK: cmt2.proc.if
        cmt2.proc.if %cond : !firrtl.uint<1> {
          // CHECK: cmt2.proc.seq
          cmt2.proc.seq {
            cmt2.proc.enable @op1
            cmt2.proc.enable @op2
          }
        } else {
          cmt2.proc.enable @op3
        }
        %data = firrtl.constant 200 : !firrtl.uint<16>
        %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<16>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        cmt2.dataflow.return %result : !firrtl.uint<16>
      }
    }

    // Test: static_repeat containing if
    // CHECK: cmt2.proc.dataflow @test_repeat_if
    cmt2.proc.dataflow @test_repeat_if(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      %tok0 = cmt2.dataflow.task @task_repeat_if() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        // CHECK: cmt2.proc.static_repeat 3
        cmt2.proc.static_repeat 3 {
          %cond = firrtl.constant 1 : !firrtl.uint<1>
          cmt2.proc.if %cond : !firrtl.uint<1> {
            cmt2.proc.enable @op1
          }
        }
        %data = firrtl.constant 300 : !firrtl.uint<16>
        %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      cmt2.dataflow.task @final() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<16>>) {
        %result = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        cmt2.dataflow.return %result : !firrtl.uint<16>
      }
    }
  }

  //===--------------------------------------------------------------------===//
  // Test Module 3: Multiple tasks with proc control (pipeline pattern)
  //===--------------------------------------------------------------------===//

  // CHECK-LABEL: cmt2.module @PipelineWithProcControl
  cmt2.module @PipelineWithProcControl {
    cmt2.proc.static_step @compute <2> {}
    cmt2.proc.static_step @validate <1> {}
    cmt2.proc.static_step @store <1> {}

    // Test: Linear pipeline with proc control in each stage
    // CHECK: cmt2.proc.dataflow @pipeline_with_control
    cmt2.proc.dataflow @pipeline_with_control(%input: !firrtl.uint<32>) -> (!firrtl.uint<32>) {
      // Stage 0: Initialization with sequential ops
      %tok0 = cmt2.dataflow.task @stage0() -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @compute
        }
        %data = firrtl.constant 0 : !firrtl.uint<32>
        %tok = cmt2.token.create %data : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      // Stage 1: Conditional processing
      %tok1 = cmt2.dataflow.task @stage1() tokens_in(%tok0: !cmt2.sync_token<data = !firrtl.uint<32>>) -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        %val = cmt2.token.data %tok0 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        %c0 = firrtl.constant 0 : !firrtl.uint<32>
        %eq = firrtl.eq %val, %c0 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<1>
        cmt2.proc.if %eq : !firrtl.uint<1> {
          cmt2.proc.enable @validate
        } else {
          cmt2.proc.seq {
            cmt2.proc.enable @compute
            cmt2.proc.enable @validate
          }
        }
        %tok = cmt2.token.create %val : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      // Stage 2: Iterative processing
      %tok2 = cmt2.dataflow.task @stage2() tokens_in(%tok1: !cmt2.sync_token<data = !firrtl.uint<32>>) -> (!cmt2.sync_token<data = !firrtl.uint<32>>) {
        cmt2.proc.static_repeat 2 {
          cmt2.proc.enable @compute
        }
        %val = cmt2.token.data %tok1 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        %tok = cmt2.token.create %val : !firrtl.uint<32> -> !cmt2.sync_token<data = !firrtl.uint<32>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<32>>
      }

      // Final stage
      cmt2.dataflow.task @final() tokens_in(%tok2: !cmt2.sync_token<data = !firrtl.uint<32>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @store
        }
        %result = cmt2.token.data %tok2 : !cmt2.sync_token<data = !firrtl.uint<32>> -> !firrtl.uint<32>
        cmt2.dataflow.return %result : !firrtl.uint<32>
      }
    }
  }

  //===--------------------------------------------------------------------===//
  // Test Module 4: Fork-join with proc control
  //===--------------------------------------------------------------------===//

  // CHECK-LABEL: cmt2.module @ForkJoinWithProcControl
  cmt2.module @ForkJoinWithProcControl {
    cmt2.proc.static_step @process_a <2> {}
    cmt2.proc.static_step @process_b <3> {}
    cmt2.proc.static_step @merge <1> {}

    // Test: Fork-join pattern with different proc control in each branch
    // CHECK: cmt2.proc.dataflow @fork_join_control
    cmt2.proc.dataflow @fork_join_control(%input: !firrtl.uint<16>) -> (!firrtl.uint<16>) {
      // Source task
      %tok_src = cmt2.dataflow.task @source() -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        %data = firrtl.constant 10 : !firrtl.uint<16>
        %tok = cmt2.token.create %data : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      // Branch A: Sequential processing
      %tok_a = cmt2.dataflow.task @branch_a() tokens_in(%tok_src: !cmt2.sync_token<data = !firrtl.uint<16>>) -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @process_a
          cmt2.proc.enable @process_a
        }
        %val = cmt2.token.data %tok_src : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %one = firrtl.constant 1 : !firrtl.uint<16>
        %result = firrtl.add %val, %one : (!firrtl.uint<16>, !firrtl.uint<16>) -> !firrtl.uint<17>
        %trunc = firrtl.bits %result 15 to 0 : (!firrtl.uint<17>) -> !firrtl.uint<16>
        %tok = cmt2.token.create %trunc : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      // Branch B: Iterative processing
      %tok_b = cmt2.dataflow.task @branch_b() tokens_in(%tok_src: !cmt2.sync_token<data = !firrtl.uint<16>>) -> (!cmt2.sync_token<data = !firrtl.uint<16>>) {
        cmt2.proc.static_repeat 3 {
          cmt2.proc.enable @process_b
        }
        %val = cmt2.token.data %tok_src : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %two = firrtl.constant 2 : !firrtl.uint<16>
        %result = firrtl.mul %val, %two : (!firrtl.uint<16>, !firrtl.uint<16>) -> !firrtl.uint<32>
        %trunc = firrtl.bits %result 15 to 0 : (!firrtl.uint<32>) -> !firrtl.uint<16>
        %tok = cmt2.token.create %trunc : !firrtl.uint<16> -> !cmt2.sync_token<data = !firrtl.uint<16>>
        cmt2.dataflow.yield %tok : !cmt2.sync_token<data = !firrtl.uint<16>>
      }

      // Join task
      cmt2.dataflow.task @join() tokens_in(%tok_a: !cmt2.sync_token<data = !firrtl.uint<16>>, %tok_b: !cmt2.sync_token<data = !firrtl.uint<16>>) {
        cmt2.proc.seq {
          cmt2.proc.enable @merge
        }
        %val_a = cmt2.token.data %tok_a : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %val_b = cmt2.token.data %tok_b : !cmt2.sync_token<data = !firrtl.uint<16>> -> !firrtl.uint<16>
        %sum = firrtl.add %val_a, %val_b : (!firrtl.uint<16>, !firrtl.uint<16>) -> !firrtl.uint<17>
        %result = firrtl.bits %sum 15 to 0 : (!firrtl.uint<17>) -> !firrtl.uint<16>
        cmt2.dataflow.return %result : !firrtl.uint<16>
      }
    }
  }

  //===--------------------------------------------------------------------===//
  // Test Module 5: Deeply nested proc control
  //===--------------------------------------------------------------------===//

  // CHECK-LABEL: cmt2.module @DeeplyNestedProcControl
  cmt2.module @DeeplyNestedProcControl {
    cmt2.proc.static_step @level1 <1> {}
    cmt2.proc.static_step @level2 <1> {}
    cmt2.proc.static_step @level3 <1> {}

    // Test: Deep nesting of proc control operations
    // CHECK: cmt2.proc.dataflow @deep_nesting
    cmt2.proc.dataflow @deep_nesting(%input: !firrtl.uint<8>) -> (!firrtl.uint<8>) {
      %tok0 = cmt2.dataflow.task @complex_task() -> (!cmt2.sync_token<data = !firrtl.uint<8>>) {
        // Level 1: seq
        cmt2.proc.seq {
          cmt2.proc.enable @level1
          // Level 2: par
          cmt2.proc.par {
            // Level 3a: static_repeat
            cmt2.proc.static_repeat 2 {
              cmt2.proc.enable @level2
            }
            // Level 3b: if
            %c = firrtl.constant 1 : !firrtl.uint<1>
            cmt2.proc.if %c : !firrtl.uint<1> {
              cmt2.proc.enable @level3
            }
          }
        }
        %data = firrtl.constant 42 : !firrtl.uint<8>
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
