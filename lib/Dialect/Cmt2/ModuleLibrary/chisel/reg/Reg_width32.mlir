module {
  firrtl.circuit "FIRRTLReg" {
    firrtl.module @FIRRTLReg(
      in %clock: !firrtl.clock,
      in %reset: !firrtl.uint<1>,
      in %write_enable: !firrtl.uint<1>,
      in %write_data: !firrtl.uint<32>,
      out %read_ready: !firrtl.uint<1>,
      out %read_data: !firrtl.uint<32>,
      out %write_ready: !firrtl.uint<1>
    ) {
      // Internal register
      %reg = firrtl.reg %clock : !firrtl.clock, !firrtl.uint<32>

      // Read is always ready, returns current register value
      %c1_ui1 = firrtl.constant 1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_ready, %c1_ui1 : !firrtl.uint<1>
      firrtl.matchingconnect %read_data, %reg : !firrtl.uint<32>

      // Write is always ready
      firrtl.matchingconnect %write_ready, %c1_ui1 : !firrtl.uint<1>

      // On write enable, update register
      firrtl.when %write_enable : !firrtl.uint<1> {
        firrtl.matchingconnect %reg, %write_data : !firrtl.uint<32>
      }
    }
  }
}
