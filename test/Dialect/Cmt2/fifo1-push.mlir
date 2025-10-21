module {
  firrtl.circuit "FIFO1_PUSH_w32" {
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
    firrtl.module @FIFO1_PUSH_w32(in %clk: !firrtl.clock, in %rst: !firrtl.uint<1>, out %full_ready: !firrtl.uint<1>, out %full_res0: !firrtl.uint<1>, in %deq_enable: !firrtl.uint<1>, out %deq_ready: !firrtl.uint<1>, out %deq_res0: !firrtl.uint<32>, in %enq_enable: !firrtl.uint<1>, out %enq_ready: !firrtl.uint<1>, in %enq_data: !firrtl.uint<32>) {
      %reg_data_clock, %reg_data_reset, %reg_data_write_enable, %reg_data_write_data, %reg_data_read_ready, %reg_data_read_data, %reg_data_write_ready = firrtl.instance reg_data @Reg_width32_init0(in clock: !firrtl.clock, in reset: !firrtl.uint<1>, in write_enable: !firrtl.uint<1>, in write_data: !firrtl.uint<32>, out read_ready: !firrtl.uint<1>, out read_data: !firrtl.uint<32>, out write_ready: !firrtl.uint<1>)
      firrtl.connect %reg_data_clock, %clk : !firrtl.clock
      firrtl.connect %reg_data_reset, %rst : !firrtl.uint<1>
      %c0_ui1 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %reg_data_write_enable, %c0_ui1 : !firrtl.uint<1>
      %c0_ui32 = firrtl.constant 0 : !firrtl.uint<32>
      firrtl.connect %reg_data_write_data, %c0_ui32 : !firrtl.uint<32>
      %full_reg_clock, %full_reg_reset, %full_reg_write_enable, %full_reg_write_data, %full_reg_read_ready, %full_reg_read_data, %full_reg_write_ready = firrtl.instance full_reg @Reg_width1_init0(in clock: !firrtl.clock, in reset: !firrtl.uint<1>, in write_enable: !firrtl.uint<1>, in write_data: !firrtl.uint<1>, out read_ready: !firrtl.uint<1>, out read_data: !firrtl.uint<1>, out write_ready: !firrtl.uint<1>)
      firrtl.connect %full_reg_clock, %clk : !firrtl.clock
      firrtl.connect %full_reg_reset, %rst : !firrtl.uint<1>
      %c0_ui1_0 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %full_reg_write_enable, %c0_ui1_0 : !firrtl.uint<1>
      %c0_ui1_1 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %full_reg_write_data, %c0_ui1_1 : !firrtl.uint<1>
      %deqed_write_enable, %deqed_write_ready, %deqed_write_data, %deqed_read_data, %deqed_read_ready = firrtl.instance deqed @Wire_w1(in write_enable: !firrtl.uint<1>, out write_ready: !firrtl.uint<1>, in write_data: !firrtl.uint<1>, out read_data: !firrtl.uint<1>, out read_ready: !firrtl.uint<1>)
      %c0_ui1_2 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %deqed_write_enable, %c0_ui1_2 : !firrtl.uint<1>
      %c0_ui1_3 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %deqed_write_data, %c0_ui1_3 : !firrtl.uint<1>
      %enqed_write_enable, %enqed_write_ready, %enqed_write_data, %enqed_read_data, %enqed_read_ready = firrtl.instance enqed @Wire_w1(in write_enable: !firrtl.uint<1>, out write_ready: !firrtl.uint<1>, in write_data: !firrtl.uint<1>, out read_data: !firrtl.uint<1>, out read_ready: !firrtl.uint<1>)
      %c0_ui1_4 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %enqed_write_enable, %c0_ui1_4 : !firrtl.uint<1>
      %c0_ui1_5 = firrtl.constant 0 : !firrtl.uint<1>
      firrtl.connect %enqed_write_data, %c0_ui1_5 : !firrtl.uint<1>
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      %0 = firrtl.and %c1_ui1, %full_reg_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %full_res0_6 = firrtl.wire {name = "full_res0"} : !firrtl.uint<1>
      %invalid_ui1 = firrtl.invalidvalue : !firrtl.uint<1>
      firrtl.connect %full_res0_6, %invalid_ui1 : !firrtl.uint<1>
      firrtl.when %0 : !firrtl.uint<1> {
        firrtl.connect %full_res0_6, %full_reg_read_data : !firrtl.uint<1>
      }
      %1 = firrtl.and %full_reg_read_data, %full_reg_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %2 = firrtl.and %1, %deqed_write_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %3 = firrtl.and %2, %reg_data_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %4 = firrtl.and %3, %deq_enable : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %deq_res0_7 = firrtl.wire {name = "deq_res0"} : !firrtl.uint<32>
      %invalid_ui32 = firrtl.invalidvalue : !firrtl.uint<32>
      firrtl.connect %deq_res0_7, %invalid_ui32 : !firrtl.uint<32>
      firrtl.when %4 : !firrtl.uint<1> {
        %c1_ui1_18 = firrtl.constant 1 : !firrtl.uint<1>
        %c1_ui1_19 = firrtl.constant 1 : !firrtl.uint<1>
        firrtl.connect %deqed_write_enable, %c1_ui1_19 : !firrtl.uint<1>
        firrtl.connect %deqed_write_data, %c1_ui1_18 : !firrtl.uint<1>
        firrtl.connect %deq_res0_7, %reg_data_read_data : !firrtl.uint<32>
      }
      %5 = firrtl.not %full_reg_read_data : (!firrtl.uint<1>) -> !firrtl.uint<1>
      %6 = firrtl.or %5, %deqed_read_data : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %7 = firrtl.and %6, %full_reg_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %8 = firrtl.and %7, %deqed_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %9 = firrtl.and %8, %enqed_write_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %10 = firrtl.and %9, %reg_data_write_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %c1_ui1_8 = firrtl.constant 1 : !firrtl.uint<1>
      %11 = firrtl.xor %4, %c1_ui1_8 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %12 = firrtl.and %10, %11 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %13 = firrtl.and %12, %enq_enable : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      firrtl.when %13 : !firrtl.uint<1> {
        %c1_ui1_18 = firrtl.constant 1 : !firrtl.uint<1>
        %c1_ui1_19 = firrtl.constant 1 : !firrtl.uint<1>
        firrtl.connect %enqed_write_enable, %c1_ui1_19 : !firrtl.uint<1>
        firrtl.connect %enqed_write_data, %c1_ui1_18 : !firrtl.uint<1>
        %c1_ui1_20 = firrtl.constant 1 : !firrtl.uint<1>
        firrtl.connect %reg_data_write_enable, %c1_ui1_20 : !firrtl.uint<1>
        firrtl.connect %reg_data_write_data, %enq_data : !firrtl.uint<32>
      }
      %c1_ui1_9 = firrtl.constant 1 : !firrtl.uint<1>
      %14 = firrtl.and %c1_ui1_9, %deqed_write_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %c1_ui1_10 = firrtl.constant 1 : !firrtl.uint<1>
      %15 = firrtl.xor %4, %c1_ui1_10 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %16 = firrtl.and %14, %15 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      firrtl.when %16 : !firrtl.uint<1> {
        %c0_ui1_18 = firrtl.constant 0 : !firrtl.uint<1>
        %c1_ui1_19 = firrtl.constant 1 : !firrtl.uint<1>
        firrtl.connect %deqed_write_enable, %c1_ui1_19 : !firrtl.uint<1>
        firrtl.connect %deqed_write_data, %c0_ui1_18 : !firrtl.uint<1>
      }
      %c1_ui1_11 = firrtl.constant 1 : !firrtl.uint<1>
      %17 = firrtl.and %c1_ui1_11, %enqed_write_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %c1_ui1_12 = firrtl.constant 1 : !firrtl.uint<1>
      %18 = firrtl.xor %13, %c1_ui1_12 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %19 = firrtl.and %17, %18 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      firrtl.when %19 : !firrtl.uint<1> {
        %c0_ui1_18 = firrtl.constant 0 : !firrtl.uint<1>
        %c1_ui1_19 = firrtl.constant 1 : !firrtl.uint<1>
        firrtl.connect %enqed_write_enable, %c1_ui1_19 : !firrtl.uint<1>
        firrtl.connect %enqed_write_data, %c0_ui1_18 : !firrtl.uint<1>
      }
      %c1_ui1_13 = firrtl.constant 1 : !firrtl.uint<1>
      %20 = firrtl.and %c1_ui1_13, %enqed_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %21 = firrtl.and %20, %deqed_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %22 = firrtl.and %21, %full_reg_read_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %23 = firrtl.and %22, %full_reg_write_ready : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %c1_ui1_14 = firrtl.constant 1 : !firrtl.uint<1>
      %24 = firrtl.xor %4, %c1_ui1_14 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %25 = firrtl.and %23, %24 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %c1_ui1_15 = firrtl.constant 1 : !firrtl.uint<1>
      %26 = firrtl.xor %13, %c1_ui1_15 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %27 = firrtl.and %25, %26 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %c1_ui1_16 = firrtl.constant 1 : !firrtl.uint<1>
      %28 = firrtl.xor %16, %c1_ui1_16 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %29 = firrtl.and %27, %28 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %c1_ui1_17 = firrtl.constant 1 : !firrtl.uint<1>
      %30 = firrtl.xor %19, %c1_ui1_17 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      %31 = firrtl.and %29, %30 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
      firrtl.when %31 : !firrtl.uint<1> {
        %32 = firrtl.not %deqed_read_data : (!firrtl.uint<1>) -> !firrtl.uint<1>
        %33 = firrtl.and %full_reg_read_data, %32 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        %34 = firrtl.or %enqed_read_data, %33 : (!firrtl.uint<1>, !firrtl.uint<1>) -> !firrtl.uint<1>
        %c1_ui1_18 = firrtl.constant 1 : !firrtl.uint<1>
        firrtl.connect %full_reg_write_enable, %c1_ui1_18 : !firrtl.uint<1>
        firrtl.connect %full_reg_write_data, %34 : !firrtl.uint<1>
      }
      firrtl.connect %full_ready, %0 : !firrtl.uint<1>
      firrtl.connect %full_res0, %full_res0_6 : !firrtl.uint<1>
      firrtl.connect %deq_ready, %3 : !firrtl.uint<1>
      firrtl.connect %deq_res0, %deq_res0_7 : !firrtl.uint<32>
      firrtl.connect %enq_ready, %12 : !firrtl.uint<1>
    }
  }
}

