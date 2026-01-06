// RUN: circt-opt %s | FileCheck %s

// CHECK-LABEL: cmt2.circuit
// CHECK: cmt2.module @FIFO1_PUSH_w32

module {
  cmt2.circuit {
    cmt2.module @FIFO1_PUSH_w32(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.instance @reg_data = @Reg_width32_init0(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.instance @full_reg = @Reg_width1_init0(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.instance @deqed = @Wire_w1(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.instance @enqed = @Wire_w1(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.value @full () -> (!firrtl.uint<1>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @full_reg @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        cmt2.return %0 : !firrtl.uint<1>
      }
      cmt2.method @deq () -> (!firrtl.uint<32>) attributes {arg_attrs = [], res_attrs = []} {
        %0 = cmt2.call @full_reg @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        cmt2.return %0 : !firrtl.uint<1>
      } {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.call @deqed @write(%c1_ui1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        %0 = cmt2.call @reg_data @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        cmt2.return %0 : !firrtl.uint<32>
      }
      cmt2.method @enq (%data: !firrtl.uint<32>) -> () attributes {arg_attrs = [], res_attrs = []} {
        %0 = cmt2.call @full_reg @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        %1 = cmt2.call @deqed @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        %2 = firrtl.not %0 : (!firrtl.uint<1>) -> !firrtl.uint<1>
        %3 = firrtl.or %2, %1 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        cmt2.return %3 : !firrtl.uint<1>
      } {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.call @enqed @write(%c1_ui1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.call @reg_data @write(%data) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.return
      }
      cmt2.rule @deqed_default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
        cmt2.call @deqed @write(%c0_ui1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.return
      }
      cmt2.rule @enqed_default () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
        cmt2.call @enqed @write(%c0_ui1) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.return
      }
      cmt2.rule @next () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @enqed @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        %1 = cmt2.call @deqed @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        %2 = cmt2.call @full_reg @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<1>
        %3 = firrtl.not %1 : (!firrtl.uint<1>) -> !firrtl.uint<1>
        %4 = firrtl.and %2, %3 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        %5 = firrtl.or %0, %4 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        cmt2.call @full_reg @write(%5) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<1>) -> ()
        cmt2.return
      }
    } {precedence = [[@full, @deq], [@deq, @enq], [@enq, @deqed_default], [@deqed_default, @enqed_default], [@enqed_default, @next]]}
    cmt2.module.extern.firrtl @Reg_width32_init0 : @Reg_width32_init0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.value @read : () -> ()[ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
      cmt2.bind.method @write : () -> ()[enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
    }
    cmt2.module.extern.firrtl @Reg_width1_init0 : @Reg_width1_init0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.value @read : () -> ()[ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
      cmt2.bind.method @write : () -> ()[enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
    }
    cmt2.module.extern.firrtl @Wire_w1 : @Wire_w1 {
      cmt2.bind.method @write : () -> ()[enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
      cmt2.bind.value @read : () -> ()[ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
    } {conflict = [[@write, @write]], sequenceBefore = [[@write, @read]]}
  }
  firrtl.circuit "Reg_width32_init0" {
    firrtl.module @Reg_width32_init0(in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>, in %write_enable: !firrtl.uint<1>, in %write_data: !firrtl.uint<32>, out %read_ready: !firrtl.uint<1>, out %read_data: !firrtl.uint<32>, out %write_ready: !firrtl.uint<1>) {
      %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
      %reg = firrtl.regreset %clock, %reset, %c0_ui32 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<32>, !firrtl.uint<32>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_data, %reg : !firrtl.uint<32>
      firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.when %write_enable : !firrtl.uint<1> {
        firrtl.matchingconnect %reg, %write_data : !firrtl.uint<32>
      }
    }
    firrtl.module @Reg_width1_init0(in %clock: !firrtl.clock, in %reset: !firrtl.uint<1>, in %write_enable: !firrtl.uint<1>, in %write_data: !firrtl.uint<1>, out %read_ready: !firrtl.uint<1>, out %read_data: !firrtl.uint<1>, out %write_ready: !firrtl.uint<1>) {
      %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
      %reg = firrtl.regreset %clock, %reset, %c0_ui1 : !firrtl.clock, !firrtl.uint<1>, !firrtl.uint<1>, !firrtl.uint<1>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_data, %reg : !firrtl.uint<1>
      firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.when %write_enable : !firrtl.uint<1> {
        firrtl.matchingconnect %reg, %write_data : !firrtl.uint<1>
      }
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
  }
}