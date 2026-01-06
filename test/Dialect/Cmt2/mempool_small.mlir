// RUN: circt-opt %s | FileCheck %s

// CHECK-LABEL: cmt2.circuit
// CHECK: cmt2.module @ScratchpadMemoryPool

module {
  cmt2.circuit {
    cmt2.module @ScratchpadMemoryPool(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @inst_mem_a = @memory_mem_a(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.method @burst_read (%addr: !firrtl.uint<64>) -> (!firrtl.uint<64>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui64 = firrtl.constant 0 : !firrtl.uint<64>
        %c0_ui64_0 = firrtl.constant 0 : !firrtl.uint<64>
        %0 = firrtl.sub %addr, %c0_ui64_0 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %1 = firrtl.bits %0 63 to 0 : (!firrtl.uint<65>) -> !firrtl.uint<64>
        %c256_ui64 = firrtl.constant 256 : !firrtl.uint<64>
        %2 = firrtl.geq %addr, %c0_ui64_0 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %3 = firrtl.lt %addr, %c256_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %4 = firrtl.and %2, %3 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        %5 = cmt2.call @inst_mem_a @burst_read(%1) : (!firrtl.uint<64>) -> !firrtl.uint<64>
        %c0_ui64_1 = firrtl.constant 0 : !firrtl.uint<64>
        %6 = firrtl.mux(%4, %5, %c0_ui64_1) : (!firrtl.uint<1>, !firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %7 = firrtl.or %c0_ui64, %6 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        cmt2.return %7 : !firrtl.uint<64>
      }
      cmt2.method @burst_write (%addr: !firrtl.uint<64>, %data: !firrtl.uint<64>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui64 = firrtl.constant 0 : !firrtl.uint<64>
        %0 = firrtl.sub %addr, %c0_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %1 = firrtl.bits %0 63 to 0 : (!firrtl.uint<65>) -> !firrtl.uint<64>
        cmt2.call @inst_mem_a @burst_write(%1, %data) : (!firrtl.uint<64>, !firrtl.uint<64>) -> ()
        cmt2.return
      }
    }
    cmt2.module @memory_mem_a(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @bank_wrap_0 = @BankWrapper_mem_a_0(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.instance @bank_wrap_1 = @BankWrapper_mem_a_1(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.method @burst_read (%addr: !firrtl.uint<64>) -> (!firrtl.uint<64>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @bank_wrap_0 @burst_read(%addr) : (!firrtl.uint<64>) -> !firrtl.uint<64>
        %1 = cmt2.call @bank_wrap_1 @burst_read(%addr) : (!firrtl.uint<64>) -> !firrtl.uint<64>
        %2 = firrtl.or %0, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        cmt2.return %2 : !firrtl.uint<64>
      }
      cmt2.method @burst_write (%addr: !firrtl.uint<64>, %data: !firrtl.uint<64>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @bank_wrap_0 @burst_write(%addr, %data) : (!firrtl.uint<64>, !firrtl.uint<64>) -> ()
        cmt2.call @bank_wrap_1 @burst_write(%addr, %data) : (!firrtl.uint<64>, !firrtl.uint<64>) -> ()
        cmt2.return
      }
    }
    cmt2.module.extern.firrtl @Mem1r1w_w32_a8_d256 : @Mem1r1w_w32_a8_d256(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.method @rd0 : () -> ()[enable = "en", arguments = ["raddr"], results = []] {arg_attrs = [], res_attrs = []}
      cmt2.bind.value @rd1 : () -> ()[ready = "rd1_valid", arguments = [], results = ["rdata"]] {arg_attrs = [], res_attrs = []}
      cmt2.bind.method @write : () -> ()[enable = "wen", arguments = ["wdata", "waddr"], results = []] {arg_attrs = [], res_attrs = []}
    } {conflict = [[@write, @write], [@rd0, @rd0]]}
    cmt2.module @BankWrapper_mem_a_0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @mem_bank = @Mem1r1w_w32_a8_d256(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.instance @write_enable_wire = @WireDefault_enable_0 with []
      cmt2.instance @write_data_wire = @WireDefault_data_0 with []
      cmt2.instance @write_addr_wire = @WireDefault_addr_0 with []
      cmt2.method @burst_read (%addr: !firrtl.uint<64>) -> (!firrtl.uint<64>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c8_ui64 = firrtl.constant 8 : !firrtl.uint<64>
        %c2_ui64 = firrtl.constant 2 : !firrtl.uint<64>
        %c0_ui64 = firrtl.constant 0 : !firrtl.uint<64>
        %c2_ui64_0 = firrtl.constant 2 : !firrtl.uint<64>
        %0 = firrtl.div %addr, %c8_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %1 = firrtl.rem %0, %c2_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %2 = firrtl.sub %c0_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %3 = firrtl.add %2, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %4 = firrtl.rem %3, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %5 = firrtl.lt %4, %c2_ui64_0 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %6 = firrtl.sub %c0_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %7 = firrtl.add %6, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %8 = firrtl.rem %7, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %9 = firrtl.sub %0, %8 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %10 = firrtl.div %9, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %11 = firrtl.bits %10 7 to 0 : (!firrtl.uint<65>) -> !firrtl.uint<8>
        cmt2.call @mem_bank @rd0(%11) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        %12 = cmt2.call @mem_bank @rd1() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
        %13 = firrtl.cat %c0_ui32, %12 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
        %c0_ui32_1 = firrtl.constant 0 : !firrtl.uint<32>
        %14 = firrtl.cat %12, %c0_ui32_1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
        %c1_ui64 = firrtl.constant 1 : !firrtl.uint<64>
        %15 = firrtl.eq %4, %c1_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %16 = firrtl.mux(%15, %14, %13) : (!firrtl.uint<1>, !firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %c0_ui64_2 = firrtl.constant 0 : !firrtl.uint<64>
        %17 = firrtl.mux(%5, %16, %c0_ui64_2) : (!firrtl.uint<1>, !firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        cmt2.return %17 : !firrtl.uint<64>
      }
      cmt2.method @burst_write (%addr: !firrtl.uint<64>, %data: !firrtl.uint<64>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c8_ui64 = firrtl.constant 8 : !firrtl.uint<64>
        %c2_ui64 = firrtl.constant 2 : !firrtl.uint<64>
        %c0_ui64 = firrtl.constant 0 : !firrtl.uint<64>
        %c2_ui64_0 = firrtl.constant 2 : !firrtl.uint<64>
        %0 = firrtl.div %addr, %c8_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %1 = firrtl.rem %0, %c2_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %2 = firrtl.sub %c0_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %3 = firrtl.add %2, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %4 = firrtl.rem %3, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %5 = firrtl.lt %4, %c2_ui64_0 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %6 = firrtl.sub %c0_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %7 = firrtl.add %6, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %8 = firrtl.rem %7, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %9 = firrtl.sub %0, %8 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %10 = firrtl.div %9, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %11 = firrtl.bits %10 7 to 0 : (!firrtl.uint<65>) -> !firrtl.uint<8>
        %12 = firrtl.bits %data 31 to 0 : (!firrtl.uint<64>) -> !firrtl.uint<32>
        %13 = firrtl.bits %data 63 to 32 : (!firrtl.uint<64>) -> !firrtl.uint<32>
        %c1_ui64 = firrtl.constant 1 : !firrtl.uint<64>
        %14 = firrtl.eq %4, %c1_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %15 = firrtl.mux(%14, %13, %12) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
        %16 = firrtl.mux(%5, %c1_ui1, %c0_ui1) : (!firrtl.uint<1>, !firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        cmt2.call @write_enable_wire @write(%16) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.call @write_data_wire @write(%15) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.call @write_addr_wire @write(%11) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        cmt2.return
      }
      cmt2.rule @do_bank_write () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        %0 = cmt2.call @write_enable_wire @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        %1 = firrtl.eq %0, %c1_ui1 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        cmt2.return %1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @write_data_wire @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        %1 = cmt2.call @write_addr_wire @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<8>
        cmt2.call @mem_bank @write(%0, %1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>, !firrtl.uint<8>) -> ()
        cmt2.return
      }
    }
    cmt2.module @WireDefault_enable_0 {
      cmt2.instance @inner = @Wire_w1 with []
      cmt2.value @read () -> (!firrtl.uint<1>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @inner @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        cmt2.return %0 : !firrtl.uint<1>
      }
      cmt2.method @write (%in_: !firrtl.uint<1>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @inner @write(%in_) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.return
      }
      cmt2.rule @default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
        cmt2.call @inner @write(%c0_ui1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.return
      }
    } {precedence = [[@write, @default], [@default, @read]]}
    cmt2.module.extern.firrtl @Wire_w1 : @Wire_w1 {
      cmt2.bind.method @write : () -> ()[enable = "write_enable", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
      cmt2.bind.value @read : () -> ()[arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
    }
    cmt2.module @WireDefault_data_0 {
      cmt2.instance @inner = @Wire_w32 with []
      cmt2.value @read () -> (!firrtl.uint<32>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @inner @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        cmt2.return %0 : !firrtl.uint<32>
      }
      cmt2.method @write (%in_: !firrtl.uint<32>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @inner @write(%in_) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.return
      }
      cmt2.rule @default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
        cmt2.call @inner @write(%c0_ui32) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.return
      }
    } {precedence = [[@write, @default], [@default, @read]]}
    cmt2.module.extern.firrtl @Wire_w32 : @Wire_w32 {
      cmt2.bind.method @write : () -> ()[enable = "write_enable", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
      cmt2.bind.value @read : () -> ()[arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
    }
    cmt2.module @WireDefault_addr_0 {
      cmt2.instance @inner = @Wire_w8 with []
      cmt2.value @read () -> (!firrtl.uint<8>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @inner @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<8>
        cmt2.return %0 : !firrtl.uint<8>
      }
      cmt2.method @write (%in_: !firrtl.uint<8>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @inner @write(%in_) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        cmt2.return
      }
      cmt2.rule @default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui8 = firrtl.constant 0 : !firrtl.uint<8>
        cmt2.call @inner @write(%c0_ui8) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        cmt2.return
      }
    } {precedence = [[@write, @default], [@default, @read]]}
    cmt2.module.extern.firrtl @Wire_w8 : @Wire_w8 {
      cmt2.bind.method @write : () -> ()[enable = "write_enable", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
      cmt2.bind.value @read : () -> ()[arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
    }
    cmt2.module @BankWrapper_mem_a_1(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @mem_bank = @Mem1r1w_w32_a8_d256(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.instance @write_enable_wire = @WireDefault_enable_1 with []
      cmt2.instance @write_data_wire = @WireDefault_data_1 with []
      cmt2.instance @write_addr_wire = @WireDefault_addr_1 with []
      cmt2.method @burst_read (%addr: !firrtl.uint<64>) -> (!firrtl.uint<64>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c8_ui64 = firrtl.constant 8 : !firrtl.uint<64>
        %c2_ui64 = firrtl.constant 2 : !firrtl.uint<64>
        %c1_ui64 = firrtl.constant 1 : !firrtl.uint<64>
        %c2_ui64_0 = firrtl.constant 2 : !firrtl.uint<64>
        %0 = firrtl.div %addr, %c8_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %1 = firrtl.rem %0, %c2_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %2 = firrtl.sub %c1_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %3 = firrtl.add %2, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %4 = firrtl.rem %3, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %5 = firrtl.lt %4, %c2_ui64_0 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %6 = firrtl.sub %c1_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %7 = firrtl.add %6, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %8 = firrtl.rem %7, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %9 = firrtl.sub %0, %8 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %10 = firrtl.div %9, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %11 = firrtl.bits %10 7 to 0 : (!firrtl.uint<65>) -> !firrtl.uint<8>
        cmt2.call @mem_bank @rd0(%11) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        %12 = cmt2.call @mem_bank @rd1() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
        %13 = firrtl.cat %c0_ui32, %12 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
        %c0_ui32_1 = firrtl.constant 0 : !firrtl.uint<32>
        %14 = firrtl.cat %12, %c0_ui32_1 : (!firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<64>
        %c1_ui64_2 = firrtl.constant 1 : !firrtl.uint<64>
        %15 = firrtl.eq %4, %c1_ui64_2 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %16 = firrtl.mux(%15, %14, %13) : (!firrtl.uint<1>, !firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %c0_ui64 = firrtl.constant 0 : !firrtl.uint<64>
        %17 = firrtl.mux(%5, %16, %c0_ui64) : (!firrtl.uint<1>, !firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        cmt2.return %17 : !firrtl.uint<64>
      }
      cmt2.method @burst_write (%addr: !firrtl.uint<64>, %data: !firrtl.uint<64>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c8_ui64 = firrtl.constant 8 : !firrtl.uint<64>
        %c2_ui64 = firrtl.constant 2 : !firrtl.uint<64>
        %c1_ui64 = firrtl.constant 1 : !firrtl.uint<64>
        %c2_ui64_0 = firrtl.constant 2 : !firrtl.uint<64>
        %0 = firrtl.div %addr, %c8_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %1 = firrtl.rem %0, %c2_ui64 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %2 = firrtl.sub %c1_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %3 = firrtl.add %2, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %4 = firrtl.rem %3, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %5 = firrtl.lt %4, %c2_ui64_0 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %6 = firrtl.sub %c1_ui64, %1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %7 = firrtl.add %6, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<66>
        %8 = firrtl.rem %7, %c2_ui64 : (!firrtl.uint<66>, !firrtl.uint<64>) -> !firrtl.uint<64>
        %9 = firrtl.sub %0, %8 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %10 = firrtl.div %9, %c2_ui64 : (!firrtl.uint<65>, !firrtl.uint<64>) -> !firrtl.uint<65>
        %11 = firrtl.bits %10 7 to 0 : (!firrtl.uint<65>) -> !firrtl.uint<8>
        %12 = firrtl.bits %data 31 to 0 : (!firrtl.uint<64>) -> !firrtl.uint<32>
        %13 = firrtl.bits %data 63 to 32 : (!firrtl.uint<64>) -> !firrtl.uint<32>
        %c1_ui64_1 = firrtl.constant 1 : !firrtl.uint<64>
        %14 = firrtl.eq %4, %c1_ui64_1 : (!firrtl.uint<64>, !firrtl.uint<64>) -> !firrtl.uint<1>
        %15 = firrtl.mux(%14, %13, %12) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
        %16 = firrtl.mux(%5, %c1_ui1, %c0_ui1) : (!firrtl.uint<1>, !firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        cmt2.call @write_enable_wire @write(%16) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.call @write_data_wire @write(%15) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.call @write_addr_wire @write(%11) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        cmt2.return
      }
      cmt2.rule @do_bank_write () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        %0 = cmt2.call @write_enable_wire @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        %1 = firrtl.eq %0, %c1_ui1 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        cmt2.return %1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @write_data_wire @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        %1 = cmt2.call @write_addr_wire @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<8>
        cmt2.call @mem_bank @write(%0, %1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>, !firrtl.uint<8>) -> ()
        cmt2.return
      }
    }
    cmt2.module @WireDefault_enable_1 {
      cmt2.instance @inner = @Wire_w1 with []
      cmt2.value @read () -> (!firrtl.uint<1>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @inner @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        cmt2.return %0 : !firrtl.uint<1>
      }
      cmt2.method @write (%in_: !firrtl.uint<1>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @inner @write(%in_) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.return
      }
      cmt2.rule @default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
        cmt2.call @inner @write(%c0_ui1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.return
      }
    } {precedence = [[@write, @default], [@default, @read]]}
    cmt2.module @WireDefault_data_1 {
      cmt2.instance @inner = @Wire_w32 with []
      cmt2.value @read () -> (!firrtl.uint<32>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @inner @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        cmt2.return %0 : !firrtl.uint<32>
      }
      cmt2.method @write (%in_: !firrtl.uint<32>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @inner @write(%in_) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.return
      }
      cmt2.rule @default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
        cmt2.call @inner @write(%c0_ui32) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.return
      }
    } {precedence = [[@write, @default], [@default, @read]]}
    cmt2.module @WireDefault_addr_1 {
      cmt2.instance @inner = @Wire_w8 with []
      cmt2.value @read () -> (!firrtl.uint<8>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @inner @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<8>
        cmt2.return %0 : !firrtl.uint<8>
      }
      cmt2.method @write (%in_: !firrtl.uint<8>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @inner @write(%in_) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        cmt2.return
      }
      cmt2.rule @default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui8 = firrtl.constant 0 : !firrtl.uint<8>
        cmt2.call @inner @write(%c0_ui8) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> ()
        cmt2.return
      }
    } {precedence = [[@write, @default], [@default, @read]]}
  }
  firrtl.circuit "Mem1r1w_w32_a8_d256" {
    firrtl.module @Mem1r1w_w32_a8_d256(in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>, in %en: !firrtl.uint<1>, in %raddr: !firrtl.uint<8>, out %rd1_valid: !firrtl.uint<1>, out %rdata: !firrtl.uint<32>, in %wen: !firrtl.uint<1>, in %waddr: !firrtl.uint<8>, in %wdata: !firrtl.uint<32>) {
      %r = firrtl.reg %clock : !firrtl.clock, !firrtl.uint<1>
      firrtl.matchingconnect %r, %en : !firrtl.uint<1>
      firrtl.matchingconnect %rd1_valid, %r : !firrtl.uint<1>
      %mymemory_r, %mymemory_w = firrtl.mem  Undefined {depth = 256 : i64, name = "mymemory", portNames = ["r", "w"], readLatency = 1 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data flip: uint<32>>, !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data: uint<32>, mask: uint<1>>
      %0 = firrtl.subfield %mymemory_r[addr] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data flip: uint<32>>
      %1 = firrtl.subfield %mymemory_r[en] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data flip: uint<32>>
      %2 = firrtl.subfield %mymemory_r[clk] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data flip: uint<32>>
      %3 = firrtl.subfield %mymemory_r[data] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data flip: uint<32>>
      firrtl.matchingconnect %0, %raddr : !firrtl.uint<8>
      firrtl.matchingconnect %1, %en : !firrtl.uint<1>
      firrtl.matchingconnect %2, %clock : !firrtl.clock
      firrtl.matchingconnect %rdata, %3 : !firrtl.uint<32>
      %4 = firrtl.subfield %mymemory_w[addr] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data: uint<32>, mask: uint<1>>
      %5 = firrtl.subfield %mymemory_w[en] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data: uint<32>, mask: uint<1>>
      %6 = firrtl.subfield %mymemory_w[clk] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data: uint<32>, mask: uint<1>>
      %7 = firrtl.subfield %mymemory_w[data] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data: uint<32>, mask: uint<1>>
      %8 = firrtl.subfield %mymemory_w[mask] : !firrtl.bundle<addr: uint<8>, en: uint<1>, clk: clock, data: uint<32>, mask: uint<1>>
      firrtl.matchingconnect %4, %waddr : !firrtl.uint<8>
      firrtl.matchingconnect %5, %wen : !firrtl.uint<1>
      firrtl.matchingconnect %6, %clock : !firrtl.clock
      firrtl.matchingconnect %7, %wdata : !firrtl.uint<32>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.matchingconnect %8, %c1_ui1 : !firrtl.uint<1>
    }
    firrtl.module @Wire_w1(in %write_enable: !firrtl.uint<1>, out %write_ready: !firrtl.uint<1>, in %write_data: !firrtl.uint<1>, out %read_data: !firrtl.uint<1>, out %read_ready: !firrtl.uint<1>) {
      %wire_val = firrtl.wire : !firrtl.uint<1>
      %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      %0 = firrtl.mux(%write_enable, %write_data, %c0_ui1) : (!firrtl.uint<1>, !firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      firrtl.matchingconnect %wire_val, %0 : !firrtl.uint<1>
      firrtl.matchingconnect %read_data, %wire_val : !firrtl.uint<1>
      firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
    }
    firrtl.module @Wire_w32(in %write_enable: !firrtl.uint<1>, out %write_ready: !firrtl.uint<1>, in %write_data: !firrtl.uint<32>, out %read_data: !firrtl.uint<32>, out %read_ready: !firrtl.uint<1>) {
      %wire_val = firrtl.wire : !firrtl.uint<32>
      %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      %0 = firrtl.mux(%write_enable, %write_data, %c0_ui32) : (!firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>) -> !firrtl.uint<32>
      firrtl.matchingconnect %wire_val, %0 : !firrtl.uint<32>
      firrtl.matchingconnect %read_data, %wire_val : !firrtl.uint<32>
      firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
    }
    firrtl.module @Wire_w8(in %write_enable: !firrtl.uint<1>, out %write_ready: !firrtl.uint<1>, in %write_data: !firrtl.uint<8>, out %read_data: !firrtl.uint<8>, out %read_ready: !firrtl.uint<1>) {
      %wire_val = firrtl.wire : !firrtl.uint<8>
      %c0_ui8 = firrtl.constant 0 : !firrtl.uint<8>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      %0 = firrtl.mux(%write_enable, %write_data, %c0_ui8) : (!firrtl.uint<1>, !firrtl.uint<8>, !firrtl.uint<8>) -> !firrtl.uint<8>
      firrtl.matchingconnect %wire_val, %0 : !firrtl.uint<8>
      firrtl.matchingconnect %read_data, %wire_val : !firrtl.uint<8>
      firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
    }
  }
}
