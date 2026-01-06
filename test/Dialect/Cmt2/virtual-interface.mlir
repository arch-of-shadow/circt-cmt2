// RUN: circt-opt %s | FileCheck %s

// CHECK-LABEL: cmt2.circuit
// CHECK: cmt2.interface @OuterInterface
// CHECK: cmt2.module @ModuleB
// CHECK: cmt2.module @ModuleA

module {
  cmt2.circuit {
    cmt2.interface @OuterInterface {
      cmt2.method @get_outer_data () -> (!firrtl.uint<32>) attributes {arg_attrs = [], res_attrs = []} {
        cmt2.return
      } {
        cmt2.return
      }
    }
    cmt2.module.extern.firrtl @Reg_width32_init0 : @Reg_width32_init0(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.bind.bare %clk, @clock : !firrtl.clock
      cmt2.bind.bare %rst, @reset : !firrtl.uint<1>
      cmt2.bind.value @read : () -> !firrtl.uint<32>[ready = "read_ready", arguments = [], results = ["read_data"]] {arg_attrs = [], res_attrs = []}
      cmt2.bind.method @write : (!firrtl.uint<32>) -> ()[enable = "write_enable", ready = "write_ready", arguments = ["write_data"], results = []] {arg_attrs = [], res_attrs = []}
    }
    cmt2.module @ModuleB(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.interface.decl @outer_iface : @OuterInterface
      cmt2.instance @internal_data = @Reg_width32_init0(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with []
      cmt2.method @write_data () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @outer_iface @get_outer_data() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        cmt2.call @internal_data @write(%0) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<32>) -> ()
        cmt2.return
      }
      cmt2.method @read_by (%id: !firrtl.uint<8>) -> (!firrtl.uint<32>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @internal_data @read() {arg_attrs = [], res_attrs = []} : () -> !firrtl.uint<32>
        cmt2.return %0 : !firrtl.uint<32>
      }
    }
    cmt2.module @ModuleA(%clk: !firrtl.clock, %rst: !firrtl.uint<1>) {
      cmt2.interface.decl @outer_iface : @OuterInterface
      cmt2.instance @submodule_b = @ModuleB(%clk, %rst) : !firrtl.clock, !firrtl.uint<1> with [[@outer_iface, @outer_iface]]
      cmt2.method @test_write () -> () attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        cmt2.call @submodule_b @write_data() {arg_attrs = [], res_attrs = []} : () -> ()
        cmt2.return
      }
      cmt2.method @test_read (%id: !firrtl.uint<8>) -> (!firrtl.uint<32>) attributes {arg_attrs = [], res_attrs = []} {
        %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
        cmt2.return %c1_ui1 : !firrtl.uint<1>
      } {
        %0 = cmt2.call @submodule_b @read_by(%id) {arg_attrs = [], res_attrs = []} : (!firrtl.uint<8>) -> !firrtl.uint<32>
        cmt2.return %0 : !firrtl.uint<32>
      }
    }
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
  }
}