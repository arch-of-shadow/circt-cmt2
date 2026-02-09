// RUN: circt-opt %s | FileCheck %s
// RUN: circt-opt %s -cmt2-compile-static | FileCheck %s --check-prefix=COMPILE

// Systolic-style compute kernel example
// Inspired by Calyx's systolic array generator
//
// This example demonstrates:
// 1. A 2x2 PE (Processing Element) array
// 2. Static steps for PE operations (multiply-accumulate)
// 3. Static repeat for fixed iterations
// 4. Data movement between PEs (systolic flow)
// 5. Dynamic post-processing (collecting results)
//
// Pattern: Matrix-vector multiplication A * x = y
// where A is 2x2, x is 2x1, y is 2x1
//
// PE layout:
//   PE[0,0] PE[0,1]  <- row 0
//   PE[1,0] PE[1,1]  <- row 1
//      ^      ^
//     col0   col1
//
// Data flow:
// - Matrix elements flow left-to-right through PEs
// - Vector elements flow top-to-bottom through PEs
// - Each PE performs: accum += matrix_elem * vector_elem

builtin.module {
    firrtl.circuit "Reg_width32_init0" {
        // 32-bit register module for accumulators and data storage
        firrtl.module @Reg_width32_init0(in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>,
                                         in %write_enable: !firrtl.uint<1>, in %write_data: !firrtl.uint<32>,
                                         out %read_ready: !firrtl.uint<1>, out %read_data: !firrtl.uint<32>,
                                         out %write_ready: !firrtl.uint<1>) {
            %c0 = firrtl.constant 0 : !firrtl.uint<32>
            %reg = firrtl.regreset %clock, %reset, %c0 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>
            %c1 = firrtl.constant 1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_ready, %c1 : !firrtl.uint<1>
            firrtl.matchingconnect %read_data, %reg : !firrtl.uint<32>
            firrtl.matchingconnect %write_ready, %c1 : !firrtl.uint<1>
            firrtl.when %write_enable : !firrtl.uint<1> {
                firrtl.matchingconnect %reg, %write_data : !firrtl.uint<32>
            }
        }

        // Multiplier module (1-cycle static latency)
        firrtl.module @Multiplier(in %clock: !firrtl.clock,
                                  in %a: !firrtl.uint<32>, in %b: !firrtl.uint<32>,
                                  out %product: !firrtl.uint<32>) {
            %prod = firrtl.mul %a, %b : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
            %truncated = firrtl.tail %prod, 32 : (!firrtl.uint<64>) -> !firrtl.uint<32>
            firrtl.connect %product, %truncated : !firrtl.uint<32>, !firrtl.uint<32>
        }
    }

    cmt2.circuit {
        // Register module
        cmt2.module.extern.firrtl @Reg32 : @Reg_width32_init0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
            cmt2.bind.value @read : () -> !firrtl.uint<32> [ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
            cmt2.bind.method @write : (!firrtl.uint<32>) -> () [enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
        }

        // Multiplier module
        cmt2.module.extern.firrtl @Mult : @Multiplier(%clk: !firrtl.clock) {
            cmt2.bind.bare %clk, @clock : !firrtl.clock
            cmt2.bind.value @multiply : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32> [ready = "", arguments = ["a", "b"], results = ["product"]] {arg_attrs = [], res_attrs = []}
        }

        //===------------------------------------------------------------------===//
        // Processing Element (PE) Module
        // Performs multiply-accumulate: accum += a * b
        //===------------------------------------------------------------------===//

        // CHECK-LABEL: cmt2.module @PE
        cmt2.module @PE(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            // Accumulator register
            cmt2.instance @accum = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            // Multiplier
            cmt2.instance @mult = @Mult(%clk) : !firrtl.clock with []
            // Input registers for left (matrix) and top (vector) values
            cmt2.instance @left_in = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @top_in = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            // Output registers for passing data to next PE
            cmt2.instance @left_out = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @top_out = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []

            // Static step: Multiply-accumulate (1 cycle)
            // COMPILE: cmt2.proc.static_step @mac<1>
            // COMPILE: wrapper_generated
            cmt2.proc.static_step @mac <1> {
                // Read inputs
                %left = cmt2.call @left_in @read() : () -> !firrtl.uint<32>
                %top = cmt2.call @top_in @read() : () -> !firrtl.uint<32>
                // Multiply
                %prod = cmt2.call @mult @multiply(%left, %top) : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
                // Accumulate
                %old_accum = cmt2.call @accum @read() : () -> !firrtl.uint<32>
                %new_accum = firrtl.add %old_accum, %prod : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<33>
                %truncated = firrtl.tail %new_accum, 1 : (!firrtl.uint<33>) -> !firrtl.uint<32>
                cmt2.call @accum @write(%truncated) : (!firrtl.uint<32>) -> ()
            }
            // COMPILE: cmt2.instance @__fsm_mac
            // COMPILE: cmt2.rule @mac__tick
            // COMPILE: cmt2.value @mac__done
            // COMPILE: cmt2.rule @mac__start

            // Static step: Move data to next PE (1 cycle)
            // Left data moves right, top data moves down
            cmt2.proc.static_step @move_data <1> {
                %left = cmt2.call @left_in @read() : () -> !firrtl.uint<32>
                %top = cmt2.call @top_in @read() : () -> !firrtl.uint<32>
                cmt2.call @left_out @write(%left) : (!firrtl.uint<32>) -> ()
                cmt2.call @top_out @write(%top) : (!firrtl.uint<32>) -> ()
            }

            // Value to get result
            cmt2.value @get_result() -> (!firrtl.uint<32>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                %result = cmt2.call @accum @read() : () -> !firrtl.uint<32>
                cmt2.return %result : !firrtl.uint<32>
            }

            // Method to load left input
            cmt2.method @load_left(%val: !firrtl.uint<32>) -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                cmt2.call @left_in @write(%val) : (!firrtl.uint<32>) -> ()
                cmt2.return
            }

            // Method to load top input
            cmt2.method @load_top(%val: !firrtl.uint<32>) -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                cmt2.call @top_in @write(%val) : (!firrtl.uint<32>) -> ()
                cmt2.return
            }

            // PE execution rule: MAC then move data
            cmt2.proc.rule @pe_execute() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    cmt2.proc.enable @mac
                    cmt2.proc.enable @move_data
                }
            }
        }

        //===------------------------------------------------------------------===//
        // 2x2 Systolic Array
        // Computes A * x = y where A is 2x2, x is 2x1
        //===------------------------------------------------------------------===//

        // CHECK-LABEL: cmt2.module @SystolicArray2x2
        // COMPILE-LABEL: cmt2.module @SystolicArray2x2
        cmt2.module @SystolicArray2x2(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
            // 2x2 grid of PEs
            cmt2.instance @pe_0_0 = @PE(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @pe_0_1 = @PE(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @pe_1_0 = @PE(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @pe_1_1 = @PE(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []

            // Input data registers (simulating memory read)
            // Matrix A elements: a00, a01, a10, a11
            cmt2.instance @a00 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @a01 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @a10 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @a11 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            // Vector x elements: x0, x1
            cmt2.instance @x0 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @x1 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            // Result registers: y0, y1
            cmt2.instance @y0 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
            cmt2.instance @y1 = @Reg32(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []

            // Static step: Feed first row of matrix and first vector element
            // COMPILE: cmt2.proc.static_step @feed_cycle_1<1>
            cmt2.proc.static_step @feed_cycle_1 <1> {
                // Feed a00 to PE[0,0] from left
                %a00_val = cmt2.call @a00 @read() : () -> !firrtl.uint<32>
                cmt2.call @pe_0_0 @load_left(%a00_val) : (!firrtl.uint<32>) -> ()
                // Feed x0 to PE[0,0] from top
                %x0_val = cmt2.call @x0 @read() : () -> !firrtl.uint<32>
                cmt2.call @pe_0_0 @load_top(%x0_val) : (!firrtl.uint<32>) -> ()
            }

            // Static step: Feed second elements, execute PE[0,0]
            cmt2.proc.static_step @feed_cycle_2 <1> {
                // Feed a01 to PE[0,1]
                %a01_val = cmt2.call @a01 @read() : () -> !firrtl.uint<32>
                cmt2.call @pe_0_1 @load_left(%a01_val) : (!firrtl.uint<32>) -> ()
                // Feed x1 to PE[0,0] (for second iteration)
                %x1_val = cmt2.call @x1 @read() : () -> !firrtl.uint<32>
                cmt2.call @pe_0_0 @load_top(%x1_val) : (!firrtl.uint<32>) -> ()
                // Feed a10 to PE[1,0]
                %a10_val = cmt2.call @a10 @read() : () -> !firrtl.uint<32>
                cmt2.call @pe_1_0 @load_left(%a10_val) : (!firrtl.uint<32>) -> ()
            }

            // Static step: Continue feeding and propagation
            cmt2.proc.static_step @feed_cycle_3 <1> {
                // Feed a11 to PE[1,1]
                %a11_val = cmt2.call @a11 @read() : () -> !firrtl.uint<32>
                cmt2.call @pe_1_1 @load_left(%a11_val) : (!firrtl.uint<32>) -> ()
                // Feed x0 to PE[1,0] (propagated down)
                %x0_val = cmt2.call @x0 @read() : () -> !firrtl.uint<32>
                cmt2.call @pe_1_0 @load_top(%x0_val) : (!firrtl.uint<32>) -> ()
            }

            // Static step: Execute all PEs (MAC operation)
            // COMPILE: cmt2.proc.static_step @execute_pes<1>
            cmt2.proc.static_step @execute_pes <1> {
                // In a real systolic array, PEs execute in parallel
                // Here we call the mac step conceptually
                // The actual execution would be pipelined
                %c1 = firrtl.constant 1 : !firrtl.uint<32>
                cmt2.call @x0 @write(%c1) : (!firrtl.uint<32>) -> ()
            }

            // Dynamic step: Collect results after computation
            cmt2.proc.step @collect_results {
                // Read final accumulator values from diagonal PEs
                %r0 = cmt2.call @pe_0_1 @get_result() : () -> !firrtl.uint<32>
                %r1 = cmt2.call @pe_1_1 @get_result() : () -> !firrtl.uint<32>
                // Write to output registers
                cmt2.call @y0 @write(%r0) : (!firrtl.uint<32>) -> ()
                cmt2.call @y1 @write(%r1) : (!firrtl.uint<32>) -> ()
                %done = firrtl.constant 1 : !firrtl.uint<1>
            }

            // Main compute rule: Static kernel + dynamic post-processing
            // Pattern: static_repeat<N> { kernel } ; dynamic_post_process
            cmt2.proc.rule @compute() -> () {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } control {
                cmt2.proc.seq {
                    // Static feeding phase (3 cycles to fill pipeline)
                    cmt2.proc.enable @feed_cycle_1
                    cmt2.proc.enable @feed_cycle_2
                    cmt2.proc.enable @feed_cycle_3
                    // Static computation phase: 2 iterations for 2x2
                    // Each iteration is 1 cycle
                    cmt2.proc.static_repeat 2 {
                        cmt2.proc.enable @execute_pes
                    }
                    // Dynamic post-processing: collect results
                    cmt2.proc.enable @collect_results
                }
            }

            // Value to read output
            cmt2.value @get_y0() -> (!firrtl.uint<32>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                %val = cmt2.call @y0 @read() : () -> !firrtl.uint<32>
                cmt2.return %val : !firrtl.uint<32>
            }

            cmt2.value @get_y1() -> (!firrtl.uint<32>) {
                %c1 = firrtl.constant 1 : !firrtl.uint<1>
                cmt2.return %c1 : !firrtl.uint<1>
            } {
                %val = cmt2.call @y1 @read() : () -> !firrtl.uint<32>
                cmt2.return %val : !firrtl.uint<32>
            }
        }
    }
}
